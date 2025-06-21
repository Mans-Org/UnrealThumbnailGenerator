// Copyright Mans Isaksson. All Rights Reserved.

#include "ThumbnailGenerator.h"
#include "ThumbnailGeneratorModule.h"
#include "ThumbnailGeneratorInterfaces.h"
#include "ThumbnailGeneratorScript.h"
#include "ThumbnailScene/ThumbnailPreviewScene.h"
#include "ThumbnailScene/ThumbnailBackgroundScene.h"
#include "ThumbnailGeneratorCompatibilityLayer.h"
#include "CacheProvider.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/Platform.h"

#include "TimerManager.h"
#include "FXSystem.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "Slate/WidgetRenderer.h"
#include "Blueprint/UserWidget.h"
#include "GameDelegates.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/AppStyle.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "TextureResource.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace ThumbnailGenerator
{
	template <typename T>
	void FlipColorBufferVertically(void* ColorBuffer, int32 SizeX, int32 SizeY)
	{
		const auto Swap = [](T& A, T& B) { T Tmp = A; A = B; B = Tmp; };
		for (int32 x = 0; x < SizeX; x++)
		{
			for (int32 y = 0; y < SizeY / 2; y++)
			{
				Swap(
					(T&)(((uint8*)ColorBuffer)[(x + y * SizeX) * sizeof(T)]),
					(T&)(((uint8*)ColorBuffer)[(x + (SizeY - 1 - y) * SizeX) * sizeof(T)])
				);
			}
		}
	}

	template<typename T>
	static FORCEINLINE T MixAlpha(const T& A1, const T& A2, EThumbnailAlphaBlendMode BlendMode)
	{
		switch (BlendMode)
		{
		case EThumbnailAlphaBlendMode::EReplace:
			return A2;
		case EThumbnailAlphaBlendMode::EAdd:
			return A1 + A2;
		case EThumbnailAlphaBlendMode::EMultiply:
			return A1 * A2;
		case EThumbnailAlphaBlendMode::ESubtract:
			return A1 - A2;
		}
		return A2;
	}

	static FORCEINLINE bool IsValidPixelFormat(EPixelFormat PixelFormat) // Right now, we only support B8G8R8A8 and FloatRGBA
	{
		switch (PixelFormat)
		{
		case PF_B8G8R8A8: return true;
		case PF_FloatRGBA: return true;
		}
		return false;
	}

	static TArray<uint8> ExtractAlpha(UTextureRenderTarget2D* TextureTarget, bool bInverseAlpha)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_ExtractAlpha);

		TArray<uint8> OutAlpha;

		FRenderTarget* const TextureRenderTarget = TextureTarget->GameThread_GetRenderTargetResource();
		if (!TextureRenderTarget)
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("ThumbnailGenerator::ExtractAlpha - Invalid TextureTarget"));
			return OutAlpha;
		}

		const EPixelFormat PixelFormat = TextureTarget->GetFormat();
		if (!IsValidPixelFormat(PixelFormat))
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("ThumbnailGenerator::ExtractAlpha - Invalid Pixel Format"));
			return OutAlpha;
		}

		if (PixelFormat == PF_B8G8R8A8)
		{
			TArray<FColor> SurfData;
			TextureRenderTarget->ReadPixels(SurfData);

			OutAlpha.Reserve(SurfData.Num());

			for (const FColor &Data : SurfData)
				OutAlpha.Add(bInverseAlpha ? 255 - Data.A : Data.A);
		}
		else if (PixelFormat == PF_FloatRGBA)
		{
			TArray<FFloat16Color> SurfData;
			TextureRenderTarget->ReadFloat16Pixels(SurfData);
				
			OutAlpha.SetNumUninitialized(SurfData.Num() * 2);

			for (int32 i = 0; i < SurfData.Num(); i++)
			{
				const FFloat16 Alpha = bInverseAlpha ? FFloat16(1.f - (float)SurfData[i].A) : SurfData[i].A;
				FMemory::Memcpy(&OutAlpha[i * sizeof(FFloat16)], &Alpha, sizeof(FFloat16));
			}
		}

		return OutAlpha;
	}

	static UTexture2D* ConstructTransientTexture2D(UObject* Outer, const FString& NewTexName, uint32 SizeX, uint32 SizeY, EPixelFormat PixelFormat)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_ConstructTransientTexture2D);

		const bool bIsValidSize = (SizeX != 0 && SizeY != 0);
		if (!bIsValidSize)
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("ThumbnailGenerator::ConstructTransientTexture2D - Invalid Texture Size: %dx%d"), SizeX, SizeY)
			return nullptr;
		}

		if (!IsValidPixelFormat(PixelFormat))
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("ThumbnailGenerator::ConstructTransientTexture2D - Invalid Pixel Format"))
			return nullptr;
		}

		// create the 2d texture
		UTexture2D* Result = UTexture2D::CreateTransient(SizeX, SizeY, PixelFormat, *NewTexName);
		Result->NeverStream = true;
		Result->VirtualTextureStreaming = false;

		return Result;
	}

	static void FillTextureDataFromRenderTarget(UTexture2D* Texture2D, UTextureRenderTarget2D* TextureTarget, const TArray<uint8>& AlphaOverride, EThumbnailAlphaBlendMode AlphaBlendMode)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FillTextureDataFromRenderTarget);

		FRenderTarget* const TextureRenderTarget = TextureTarget->GameThread_GetRenderTargetResource();
		if (!TextureRenderTarget)
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("ThumbnailGenerator::FillTextureData - Invalid TextureTarget"));
			return;
		}

		const EPixelFormat PixelFormat = TextureTarget->GetFormat();
		if (!IsValidPixelFormat(PixelFormat))
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("ThumbnailGenerator::FillTextureData - Invalid Pixel Format"));
			return;
		}

		auto PlatformData = Texture2D->GetPlatformData();

		if (Texture2D->GetSizeX() != TextureTarget->SizeX || Texture2D->GetSizeY() != TextureTarget->SizeY || Texture2D->GetPixelFormat() != PixelFormat)
		{
			UE_LOG(LogThumbnailGenerator, Log, TEXT("Resize Texture2D %s to fit dimentions %dx%d"), *Texture2D->GetName(), TextureTarget->SizeX, TextureTarget->SizeY);

			Texture2D->ReleaseResource();

			if (!PlatformData)
			{
				PlatformData = new FTexturePlatformData();
				Texture2D->SetPlatformData(PlatformData);
			}

			if (PlatformData->Mips.Num() == 0)
			{
				PlatformData->Mips.Add(new FTexture2DMipMap());
			}

			FTexture2DMipMap& Mip = PlatformData->Mips[0];

			PlatformData->SizeX = TextureTarget->SizeX;
			PlatformData->SizeY = TextureTarget->SizeY;
			PlatformData->PixelFormat = TextureTarget->GetFormat();

			const auto BlockSize = PixelFormat == PF_B8G8R8A8 ? sizeof(FColor) : sizeof(FFloat16Color);
			Mip.SizeX = TextureTarget->SizeX;
			Mip.SizeY = TextureTarget->SizeY;
			Mip.BulkData.Lock(LOCK_READ_WRITE);
			Mip.BulkData.Realloc(TextureTarget->SizeX * TextureTarget->SizeY * BlockSize);
			Mip.BulkData.Unlock();
		}

		FTexture2DMipMap& mip = PlatformData->Mips[0];
		uint32* const TextureData = (uint32*)mip.BulkData.Lock(LOCK_READ_WRITE);
		const int32 TextureDataSize = mip.BulkData.GetBulkDataSize();

		if (PixelFormat == PF_B8G8R8A8)
		{
			TArray<FColor> SurfData;
			TextureRenderTarget->ReadPixels(SurfData);

			if (AlphaOverride.Num() > 0)
			{
				check(SurfData.Num() == AlphaOverride.Num());

				for (int32 Pixel = 0; Pixel < SurfData.Num(); Pixel++)
					SurfData[Pixel].A = ThumbnailGenerator::MixAlpha(SurfData[Pixel].A, AlphaOverride[Pixel], AlphaBlendMode);
			}
			else // On some platforms the default alpha is 0, not 255. Make sure to fix that here
			{
				for (int32 Pixel = 0; Pixel < SurfData.Num(); Pixel++)
					SurfData[Pixel].A = 255;
			}

			check(TextureDataSize == SurfData.Num() * sizeof(FColor));
			FMemory::Memcpy(TextureData, SurfData.GetData(), TextureDataSize);
		}
		else if (PixelFormat == PF_FloatRGBA)
		{
			TArray<FFloat16Color> SurfData;
			TextureRenderTarget->ReadFloat16Pixels(SurfData);

			if (AlphaOverride.Num() > 0)
			{
				check(SurfData.Num() * sizeof(FFloat16) == AlphaOverride.Num());

				for (int32 Pixel = 0; Pixel < SurfData.Num(); Pixel++)
				{
					FFloat16 NewAlpha;
					FMemory::Memcpy(&NewAlpha, &AlphaOverride[Pixel * sizeof(FFloat16)], sizeof(FFloat16));
					SurfData[Pixel].A = ThumbnailGenerator::MixAlpha(SurfData[Pixel].A, NewAlpha, AlphaBlendMode);
				}
			}
			else // On some platforms the default alpha is 0, not 1. Make sure to fix that here
			{
				const FFloat16 OpaqueAlpha = FFloat16(1.f);
				for (int32 Pixel = 0; Pixel < SurfData.Num(); Pixel++)
				{
					SurfData[Pixel].A = OpaqueAlpha;
				}
			}

			check(TextureDataSize == SurfData.Num() * sizeof(FFloat16Color));
			FMemory::Memcpy(TextureData, SurfData.GetData(), TextureDataSize);
		}

		mip.BulkData.Unlock();

		Texture2D->SRGB = PixelFormat == PF_B8G8R8A8;

		Texture2D->UpdateResource();
	}

	static UTextureRenderTarget2D* CreateTextureTarget(UObject* Outer, int32 Width, int32 Height, ETextureRenderTargetFormat Format, const FLinearColor &ClearColor)
	{
		if (Width > 0 && Height > 0)
		{
			static uint32 count = 0;
			UTextureRenderTarget2D* NewRenderTarget2D = NewObject<UTextureRenderTarget2D>(Outer, *FString::Printf(TEXT("ThumbnailRenderTarget_%d_%dx%d"), ++count, Width, Height), RF_Transient);
			NewRenderTarget2D->RenderTargetFormat = Format;
			NewRenderTarget2D->ClearColor = ClearColor;
			NewRenderTarget2D->InitAutoFormat(Width, Height);
			NewRenderTarget2D->UpdateResourceImmediate(true);

			return NewRenderTarget2D;
		}

		return nullptr;
	}

	struct FThumbnailGeneratorTaskQueue : public FTickableGameObject
	{
		TArray<TFunction<void()>> TaskQueue;

		virtual void Tick(float DeltaTime) override
		{
			if (TaskQueue.Num() > 0)
			{
				const auto Element = TaskQueue.Pop();
				Element();
			}
		}

		virtual bool IsTickableInEditor() const override { return true; }

		virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FThumbnailGeneratorTaskQueue, STATGROUP_Tickables); }

		static FThumbnailGeneratorTaskQueue& Get()
		{
			static TUniquePtr<FThumbnailGeneratorTaskQueue> ThumbnailGeneratorTaskQueue = nullptr;
			if (!ThumbnailGeneratorTaskQueue.IsValid())
			{
				ThumbnailGeneratorTaskQueue = MakeUnique<FThumbnailGeneratorTaskQueue>();
			}

			return *ThumbnailGeneratorTaskQueue;
		}
	};
};

struct FHashableRenderTargetInfo
{
	uint16 Width  = 0;
	uint16 Height = 0;
	EThumbnailBitDepth BitDepth = EThumbnailBitDepth::E8;
	friend inline uint32 GetTypeHash(const FHashableRenderTargetInfo& O) 
	{
		return (uint32(O.Width) << 16) // first 16 bits
			| (uint32(O.Height) & 0xfffffffe) // 17-31
			| (O.BitDepth == EThumbnailBitDepth::E8 ? 0 : 1); // 32:nd bit
	}
	friend inline bool operator==(const FHashableRenderTargetInfo& A, const FHashableRenderTargetInfo& B) { return GetTypeHash(A) == GetTypeHash(B); }
};

struct FRenderTargetCache : public TCacheProvider<FHashableRenderTargetInfo, UTextureRenderTarget2D>
{
	virtual int32 MaxCacheSize() override { return UThumbnailGeneratorSettings::Get()->MaxRenderTargetCacheSize * 1000 * 1000; }
	virtual int32 GetItemDataFootprint(UTextureRenderTarget2D* InRenderTarget) override 
	{ 
		const uint32 BytesPerPixel = [&]()->uint32
		{
			switch (InRenderTarget->GetFormat())
			{
			case PF_B8G8R8A8: return sizeof(FColor);
			case PF_FloatRGBA: return sizeof(FLinearColor);
			}
			return 4; // Unsuported format, default to 1 byte per component
		}();
		return InRenderTarget->SizeX * InRenderTarget->SizeY * BytesPerPixel;
	}
	virtual FString DebugCacheName() const override { return TEXT("Render Target Cache"); }

	virtual void OnItemRemovedFromCache(UTextureRenderTarget2D* InRenderTarget) { InRenderTarget->MarkAsGarbage(); }
};

FThumbnailGenerator::FThumbnailGenerator(bool bInvalidateOnPIEEnd)
	: FThumbnailGenerator()
{
#if WITH_EDITOR
	if (bInvalidateOnPIEEnd)
	{
		EndPIEDelegateHandle = FEditorDelegates::EndPIE.AddLambda([this](bool)
		{
			InvalidateThumbnailWorld();
		});
	}
#endif
}

FThumbnailGenerator::~FThumbnailGenerator()
{
#if WITH_EDITOR
	if (EndPIEDelegateHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPIEDelegateHandle);
	}
#endif

	if (RenderTargetCache.IsValid())
		RenderTargetCache->ClearCache();

	for (UThumbnailGeneratorScript* ThumbnailGeneratorScript : ThumbnailGeneratorScripts)
	{
		if (IsValid(ThumbnailGeneratorScript))
		{
			ThumbnailGeneratorScript->MarkAsGarbage();
		}
	}
}

UTexture2D* FThumbnailGenerator::GenerateActorThumbnail(TSubclassOf<AActor> ActorClass, const FThumbnailSettings& ThumbnailSettings, UTexture2D* ResourceObject, const TMap<FString, FString>& Properties)
{
	return FinishGenerateActorThumbnail(BeginGenerateActorThumbnail(ActorClass, ThumbnailSettings, Properties), ThumbnailSettings, ResourceObject);
}

AActor* FThumbnailGenerator::BeginGenerateActorThumbnail(TSubclassOf<AActor> ActorClass, const FThumbnailSettings& ThumbnailSettings, const TMap<FString, FString>& Properties, bool bFinishSpawningActor)
{
	const auto EjectWithError = [&](const FString &Error)->AActor*
	{
		const static FString FuncName = TEXT("FThumbnailGenerator::BeginGenerateActorThumbnail");
		UE_LOG(LogThumbnailGenerator, Error, TEXT("%s - %s"), *FuncName , *Error);

		CleanupThumbnailCapture();

		return nullptr;
	};

	if (bIsCapturingThumbnail)
	{
		return EjectWithError("Called without first calling FinishGenerateActorThumbnail");
	}

	UClass* const ClassPtr = ActorClass.Get();

	if (!IsValid(ClassPtr))
		return EjectWithError("Invalid Actor Class");

	if (ThumbnailSettings.ThumbnailTextureWidth <= 0 || ThumbnailSettings.ThumbnailTextureHeight <= 0)
		return EjectWithError(FString::Printf(TEXT("Invalid Texture Size (%dx%d)"), ThumbnailSettings.ThumbnailTextureWidth, ThumbnailSettings.ThumbnailTextureHeight));

	if (!ThumbnailScene.IsValid())
		InitializeThumbnailWorld(UThumbnailGeneratorSettings::Get()->BackgroundSceneSettings);

	UWorld* const ThumbnailWorld = ThumbnailScene->GetThumbnailWorld();
	if (!IsValid(ThumbnailWorld))
		return EjectWithError("Invalid Preview World");

	// Update scripts
	{
		const auto AreScriptsDifferent = [](const TArray<UThumbnailGeneratorScript*> &ExistingScripts, const TArray<TSubclassOf<UThumbnailGeneratorScript>> &NewScripts)->bool
		{
			if (ExistingScripts.Num() != NewScripts.Num())
				return true;

			// Make order matter since we expect this array not to change much. It is therefore 
			// more important that the compare function is fast, rather than we avoid re-creating the ThumbnailGeneratorScripts
			for (int32 i = 0; i < NewScripts.Num(); i++)
			{
				if (!IsValid(ExistingScripts[i]) || NewScripts[i].Get() != ExistingScripts[i]->GetClass())
					return true;
			}

			return false;
		};

		if (AreScriptsDifferent(ThumbnailGeneratorScripts, ThumbnailSettings.ThumbnailGeneratorScripts))
		{
			for (UThumbnailGeneratorScript* ThumbnailGeneratorScript : ThumbnailGeneratorScripts)
			{
				if (IsValid(ThumbnailGeneratorScript))
				{
					ThumbnailGeneratorScript->MarkAsGarbage();
				}
			}

			ThumbnailGeneratorScripts.Empty();

			for (const TSubclassOf<UThumbnailGeneratorScript>& ThumbnailGeneratorScript : ThumbnailSettings.ThumbnailGeneratorScripts)
			{
				if (ThumbnailGeneratorScript.Get())
					ThumbnailGeneratorScripts.Add(NewObject<UThumbnailGeneratorScript>(GetThumbnailWorld(), ThumbnailGeneratorScript.Get()));
			}
		}
	}

	ThumbnailScene->UpdateScene(ThumbnailSettings);

	PrepareThumbnailCapture();

	FActorSpawnParameters SpawnParams;
	SpawnParams.bNoFail                        = true;
	SpawnParams.bDeferConstruction             = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* const SpawnedActor = ThumbnailWorld->SpawnActor<AActor>(ActorClass.Get(), SpawnParams);

	if (!IsValid(SpawnedActor))
		return EjectWithError("Failed to spawn thumbnail actor");

	UClass* SpawnedActorClass = SpawnedActor->GetClass();
	for (const TPair<FString, FString>& SerializedProperty : Properties)
	{
		if (FProperty* Property = FindFProperty<FProperty>(SpawnedActorClass, *SerializedProperty.Key))
			Property->ImportText_Direct(*SerializedProperty.Value, Property->ContainerPtrToValuePtr<void>(SpawnedActor), SpawnedActor, 0);
	}

	if (bFinishSpawningActor)
	{
		SpawnedActor->FinishSpawning(FTransform::Identity);
	}

	return SpawnedActor;
}

UTexture2D* FThumbnailGenerator::FinishGenerateActorThumbnail(AActor* Actor, const FThumbnailSettings& ThumbnailSettings, UTexture2D* ResourceObject, bool bFinishSpawningActor)
{
	const auto EjectWithError = [&](const FString &Error)->UTexture2D*
	{
		if (IsValid(Actor))
			Actor->Destroy();

		CleanupThumbnailCapture();

		const static FString FuncName = TEXT("FThumbnailGenerator::FinishGenerateActorThumbnail");
		UE_LOG(LogThumbnailGenerator, Error, TEXT("%s - %s"), *FuncName , *Error);
		return nullptr;
	};

	if (!bIsCapturingThumbnail)
	{
		return EjectWithError("Called without first calling BeginGenerateActorThumbnail");
	}

	if (!IsValid(Actor))
		return EjectWithError(TEXT("Invalid actor"));

	if (bFinishSpawningActor)
	{
		Actor->FinishSpawning(FTransform::Identity);
	}

	if (Actor->Implements<UThumbnailActorInterface>())
	{
		const FTransform ThumbnailActorTransform = IThumbnailActorInterface::Execute_GetThumbnailTransform(Actor);
		if (!IsValid(Actor))
			return EjectWithError("IThumbnailActorInterface::GetThumbnailTransform has destroyed the thumbnail actor");

		Actor->SetActorTransform(ThumbnailActorTransform);

		IThumbnailActorInterface::Execute_PreCaptureActorThumbnail(Actor);
		if (!IsValid(Actor)) 
			return EjectWithError("IThumbnailActorInterface::PreCaptureActorThumbnail has destroyed the thumbnail actor");
	}

	if (ThumbnailSettings.bOverride_CustomActorTransform)
		Actor->SetActorTransform(ThumbnailSettings.CustomActorTransform);

	for (UThumbnailGeneratorScript* ThumbnailGeneratorScript : ThumbnailGeneratorScripts)
	{
		ThumbnailGeneratorScript->PreCaptureActorThumbnail(Actor);
		if (!IsValid(Actor)) 
			return EjectWithError("UThumbnailGeneratorScript::PreCaptureActorThumbnail has destroyed the thumbnail actor");
	}

	// Simulate scene
	{
		const auto GetActorComponents = [&]()
		{
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components, true);
			return Components;
		};

		const auto SimulatedTick = [&ThumbnailSettings](const TFunction<void(float)>& TickCallback)
		{
			const auto StepSize = 1.f / ThumbnailSettings.SimulateSceneFramerate;
			for (float time = ThumbnailSettings.SimulateSceneTime; time > 0.f; time -= StepSize)
			{
				const auto dt = StepSize + FMath::Min(0.f, time - StepSize);
				TickCallback(dt);
			}
		};

		const auto DispatchComponentsBeginPlay = [](const TArray<UActorComponent*> &Components)
		{
			for (UActorComponent* Component : Components)
			{
				if (Component->IsRegistered() && !Component->HasBegunPlay())
				{
					Component->RegisterAllComponentTickFunctions(true);
					Component->BeginPlay();

					if (UParticleSystemComponent* ParticleSystemComponent = Cast<UParticleSystemComponent>(Component))
						ParticleSystemComponent->bWarmingUp = true; // To prevent async updates
				}
			}
		};

		switch (ThumbnailSettings.SimulationMode)
		{
		case EThumbnailSceneSimulationMode::EActor:
		{
			const auto SpawnedComponents = GetActorComponents();

			// Do the component dispatch ourselves as we need to set some custom settings anyway
			DispatchComponentsBeginPlay(SpawnedComponents);

			Actor->DispatchBeginPlay();

			SimulatedTick([&](float DeltaTime)
			{
				for (UActorComponent* Component : SpawnedComponents)
				{
					if (Component->IsRegistered() && Component->HasBegunPlay())
						Component->TickComponent(DeltaTime, LEVELTICK_All, &Component->PrimaryComponentTick);
				}

				Actor->TickActor(DeltaTime, LEVELTICK_All, Actor->PrimaryActorTick);
			});
		}
		break;

		case EThumbnailSceneSimulationMode::EAllComponents:
		{
			const auto SpawnedComponents = GetActorComponents();
			DispatchComponentsBeginPlay(SpawnedComponents);

			SimulatedTick([&](float DeltaTime)
			{
				for (UActorComponent* Component : SpawnedComponents)
				{
					if (Component->IsRegistered() && Component->HasBegunPlay())
						Component->TickComponent(DeltaTime, LEVELTICK_All, &Component->PrimaryComponentTick);
				}
			});
		}
		break;

		case EThumbnailSceneSimulationMode::ESpecifiedComponents:
		{
			const auto IsTickable = [&](UActorComponent* Component)
			{
				for (const auto& ComponentClass : ThumbnailSettings.ComponentsToSimulate)
				{
					if (Component->IsA(ComponentClass.Get()))
						return true;
				}
				return false;
			};

			const auto SpawnedComponents = GetActorComponents().FilterByPredicate([&](auto* Component) { return IsTickable(Component); });
			DispatchComponentsBeginPlay(SpawnedComponents);

			SimulatedTick([&](float DeltaTime)
			{
				for (UActorComponent* Component : SpawnedComponents)
				{
					if (Component->IsRegistered() && Component->HasBegunPlay())
						Component->TickComponent(DeltaTime, LEVELTICK_All, &Component->PrimaryComponentTick);
				}
			});
		}
		break;

		case EThumbnailSceneSimulationMode::ENone:
		default:
			break;
		}
	}

	const auto RenderTargetWidth  = uint16(ThumbnailSettings.ThumbnailTextureWidth);
	const auto RenderTargetHeight = uint16(ThumbnailSettings.ThumbnailTextureHeight);
	const auto RenderBitDepth     = ThumbnailSettings.ThumbnailBitDepth;
	const auto RenderTargetInfo   = FHashableRenderTargetInfo{ RenderTargetWidth, RenderTargetHeight, RenderBitDepth };
	UTextureRenderTarget2D* RenderTarget = RenderTargetCache->GetCachedItem(RenderTargetInfo);
	if (!RenderTarget)
	{
		RenderTarget = ThumbnailGenerator::CreateTextureTarget(
			GetTransientPackage(),
			RenderTargetWidth,
			RenderTargetHeight,
			RenderBitDepth == EThumbnailBitDepth::E8 ? ETextureRenderTargetFormat::RTF_RGBA8_SRGB : ETextureRenderTargetFormat::RTF_RGBA16f,
			FLinearColor(0.f, 0.f, 0.f, 1.f) // Important: When rendering with MSAA the alpha will no be touched, setting it as 0 would leave the whole image fully transparent
		);

		if (!ensure(RenderTarget))
		{
			return EjectWithError("Could not create a render target for thumbnail capture");
		}

		RenderTargetCache->CacheItem(RenderTargetInfo, RenderTarget);
	}

	UTexture2D* const Thumbnail = CaptureThumbnail(ThumbnailSettings, RenderTarget, Actor, ResourceObject);
	if (!Thumbnail)
	{
		return EjectWithError("Failed to generate thumbnail texture");
	}

	CleanupThumbnailCapture();

	return Thumbnail;
}

void FThumbnailGenerator::InitializeThumbnailWorld(const FThumbnailBackgroundSceneSettings &BackgroundSceneSettings)
{
	if (ThumbnailScene.IsValid())
		ThumbnailScene.Reset();

	UWorld* ThumbnailWorld = nullptr;

	if (BackgroundSceneSettings.BackgroundWorld.ToSoftObjectPath().IsValid())
	{
		ThumbnailScene = MakeShareable(new FThumbnailBackgroundScene(BackgroundSceneSettings));

		ThumbnailWorld = ThumbnailScene->GetThumbnailWorld();
		checkf(ThumbnailWorld, TEXT("Could not create thumbnail background world"));
	}
	else
	{
		ThumbnailScene = MakeShareable(new FThumbnailPreviewScene);
		ThumbnailWorld = ThumbnailScene->GetThumbnailWorld();
	}
	
	CaptureComponent = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
	CaptureComponent->bCaptureEveryFrame           = false;
	CaptureComponent->bCaptureOnMovement           = false;
	CaptureComponent->PostProcessBlendWeight       = 1.f;
	CaptureComponent->PrimitiveRenderMode          = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	CaptureComponent->CompositeMode                = ESceneCaptureCompositeMode::SCCM_Overwrite;
	CaptureComponent->CaptureSource                = GetCaptureSource();
#if ENGINE_VERSION_LESS_THAN(5, 1)
	CaptureComponent->bDisableFlipCopyGLES         = true;
#endif
	CaptureComponent->bAlwaysPersistRenderingState = true;
	CaptureComponent->TextureTarget                = nullptr;
	CaptureComponent->bConsiderUnrenderedOpaquePixelAsFullyTranslucent = true;

	CaptureComponent->RegisterComponentWithWorld(ThumbnailWorld);

	if (!RenderTargetCache.IsValid())
		RenderTargetCache = MakeShareable(new FRenderTargetCache);

	if (!WidgetRenderer.IsValid())
		WidgetRenderer = MakeShareable(new FWidgetRenderer(false, false));
}

void FThumbnailGenerator::InvalidateThumbnailWorld()
{
	if (IsValid(CaptureComponent))
	{
		CaptureComponent->DestroyComponent();
		CaptureComponent = nullptr;
	}

	if (ThumbnailScene.IsValid())
		ThumbnailScene.Reset();

	bIsCapturingThumbnail = false;
	ThumbnailSceneActors.Empty();
}

UWorld* FThumbnailGenerator::GetThumbnailWorld() const
{
	return ThumbnailScene.IsValid() ? ThumbnailScene->GetThumbnailWorld() : nullptr;
}

ESceneCaptureSource FThumbnailGenerator::GetCaptureSource() const
{
	return IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5) ? ESceneCaptureSource::SCS_FinalColorHDR : ESceneCaptureSource::SCS_FinalColorLDR;
}

UTexture2D* FThumbnailGenerator::CaptureThumbnail(const FThumbnailSettings& ThumbnailSettings, UTextureRenderTarget2D* RenderTarget, AActor* Actor, UTexture2D* ResourceObject)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CaptureThumbnail);

	const bool bIsPerspective = ThumbnailSettings.ProjectionType == ECameraProjectionMode::Perspective;
	const bool bAutoFrameCamera = !(ThumbnailSettings.bOverride_CustomCameraLocation ||
									ThumbnailSettings.bOverride_CustomCameraRotation ||
									(!bIsPerspective && ThumbnailSettings.bOverride_CustomOrthoWidth));

	FMinimalViewInfo CaptureComponentView;
	CaptureComponentView.ProjectionMode = ThumbnailSettings.ProjectionType;

	if (bAutoFrameCamera)
	{
		const auto CameraRotation = ThumbnailSettings.CameraRotationOffset.Quaternion() * ThumbnailSettings.CameraOrbitRotation.Quaternion();

		const float AspectRatio = ThumbnailSettings.ThumbnailTextureWidth > 0 && ThumbnailSettings.ThumbnailTextureHeight > 0 
			? (float)ThumbnailSettings.ThumbnailTextureWidth / (float)ThumbnailSettings.ThumbnailTextureHeight
			: 1.f;

		const static auto CalcActorLocalThumbnailBounds = [](AActor* InActor, const FThumbnailSettings& InThumbnailSettings, bool bDrawDebug)->FBox
		{
			const static TFunction<bool(UActorComponent*, const TSet<UClass*>&)> IsBlacklisted = [](UActorComponent* InComponent, const TSet<UClass*>& Blacklist)->bool
			{
				for (UClass* BlacklistedClass : Blacklist)
				{
					if (InComponent->IsA(BlacklistedClass))
						return true;
				}

				// Check if our owner is blacklisted, useful for components that are auto-generated such as the Text3DComponent
				if (UActorComponent* PrimitiveParentComponent = Cast<UActorComponent>(InComponent->GetOuter()))
				{
					return IsBlacklisted(PrimitiveParentComponent, Blacklist);
				}

				return false;
			};

			const static auto CalcPrimitiveBounds = [](UPrimitiveComponent* InPrimitiveComponent)->FBox
			{
				FBox OutBounds(EForceInit::ForceInit);
				if (InPrimitiveComponent->bUseAttachParentBound && InPrimitiveComponent->GetAttachParent() != nullptr)
					return OutBounds;

				const static auto CalcSkinnedMeshLocalBounds = [](USkinnedMeshComponent* SkinnedMeshComponent)->FBox
				{
					const auto LODIndex = 0;

					const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SkinnedMeshComponent->GetSkinnedAsset());
					if (!IsValid(SkeletalMesh) 
						|| !SkeletalMesh->GetResourceForRendering()
						|| !SkeletalMesh->GetResourceForRendering()->LODRenderData.IsValidIndex(LODIndex))
						return SkinnedMeshComponent->CalcBounds(FTransform::Identity).GetBox();

					const FSkeletalMeshLODRenderData& SkelMeshLODData = SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];
					const FSkinWeightVertexBuffer* SkinWeightBuffer = SkinnedMeshComponent->GetSkinWeightBuffer(LODIndex);
					if (!SkinWeightBuffer)
						return SkinnedMeshComponent->CalcBounds(FTransform::Identity).GetBox();

					TArray<FVector3f> VertexPositions;
					TArray<FMatrix44f> CachedRefToLocals;
					SkinnedMeshComponent->CacheRefToLocalMatrices(CachedRefToLocals);
					USkinnedMeshComponent::ComputeSkinnedPositions(SkinnedMeshComponent, VertexPositions, CachedRefToLocals, SkelMeshLODData, *SkinWeightBuffer);

					FBox Bounds(EForceInit::ForceInit);
					for (const FVector3f& Position : VertexPositions)
						Bounds += FVector(Position.X, Position.Y, Position.Z);

					return Bounds;
				};

				if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkeletalMeshComponent>(InPrimitiveComponent))
					OutBounds = CalcSkinnedMeshLocalBounds(SkinnedMeshComponent);
				else
					OutBounds = InPrimitiveComponent->CalcBounds(FTransform::Identity).GetBox();

				const FTransform& ActorTransform = InPrimitiveComponent->GetOwner()->GetActorTransform();
				const FTransform& ComponentTransform = InPrimitiveComponent->GetComponentTransform();
				const FTransform ComponentActorSpaceTransform = ComponentTransform.GetRelativeTransform(ActorTransform);

				return OutBounds.TransformBy(ComponentActorSpaceTransform);
			};

			FBox Box(EForceInit::ForceInit);
			for (UActorComponent* ActorComponent : InActor->GetComponents())
			{
				UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComponent);
				if (PrimComp && PrimComp->IsRegistered()
					&& (PrimComp->IsVisible() || InThumbnailSettings.bIncludeHiddenComponentsInBounds)
					&& !IsBlacklisted(PrimComp, InThumbnailSettings.ComponentBoundsBlacklist))
				{
					const FBox PrimitiveBounds = CalcPrimitiveBounds(PrimComp);
					Box += PrimitiveBounds;

					if (bDrawDebug)
					{
						const FTransform& ActorTransform = InActor->GetActorTransform();
						DrawDebugBox(
							PrimComp->GetWorld(), 
							ActorTransform.TransformPosition(PrimitiveBounds.GetCenter()), 
							PrimitiveBounds.GetExtent() * ActorTransform.GetScale3D(), 
							ActorTransform.GetRotation(), 
							FColor::Red, 
							true,
							-1.f, 
							-1
						);
					}
				}
			}

			return Box;
		};

		const FTransform& ActorTransform = Actor->GetActorTransform();

		const FBox    LocalBoundingBox  = ThumbnailSettings.bOverride_CustomActorBounds ? ThumbnailSettings.CustomActorBounds : CalcActorLocalThumbnailBounds(Actor, ThumbnailSettings, ThumbnailSettings.bDebugBounds);
		const FVector LocalBoundsExtent = LocalBoundingBox.GetExtent();
		const FVector LocalBoundsOrigin = LocalBoundingBox.GetCenter();
		const FVector LocalBoundsMin    = LocalBoundsOrigin - LocalBoundsExtent;
		const FVector LocalBoundsMax    = LocalBoundsOrigin + LocalBoundsExtent;

		typedef TArray<FVector, TInlineAllocator<8>> FBoundsVertices;

		FBoundsVertices BoundsVertices =
		{
			ActorTransform.TransformPosition({ LocalBoundsMin.X, LocalBoundsMin.Y, LocalBoundsMin.Z }),
			ActorTransform.TransformPosition({ LocalBoundsMin.X, LocalBoundsMax.Y, LocalBoundsMin.Z }),
			ActorTransform.TransformPosition({ LocalBoundsMax.X, LocalBoundsMax.Y, LocalBoundsMin.Z }),
			ActorTransform.TransformPosition({ LocalBoundsMax.X, LocalBoundsMin.Y, LocalBoundsMin.Z }),
			ActorTransform.TransformPosition({ LocalBoundsMin.X, LocalBoundsMin.Y, LocalBoundsMax.Z }),
			ActorTransform.TransformPosition({ LocalBoundsMin.X, LocalBoundsMax.Y, LocalBoundsMax.Z }),
			ActorTransform.TransformPosition({ LocalBoundsMax.X, LocalBoundsMax.Y, LocalBoundsMax.Z }),
			ActorTransform.TransformPosition({ LocalBoundsMax.X, LocalBoundsMin.Y, LocalBoundsMax.Z }),
		};

		if (ThumbnailSettings.bSnapToFloor)
		{
			float MinZLocation = BIG_NUMBER;
			for (const FVector& Vertex : BoundsVertices)
			{
				if (Vertex.Z < MinZLocation)
					MinZLocation = Vertex.Z;
			}

			const FVector ActorLocaton = ActorTransform.GetLocation();
			Actor->SetActorLocation(FVector(ActorLocaton.X, ActorLocaton.Y, ActorLocaton.Z - MinZLocation));

			for (FVector& Vertex : BoundsVertices)
			{
				Vertex.Z -= MinZLocation;
			}
		}

		if (ThumbnailSettings.bDebugBounds)
		{
			const TArray<TPair<const FVector&, const FVector&>> BoundingBoxEdges =
			{
				TPair<const FVector&, const FVector&>(BoundsVertices[0], BoundsVertices[1]),
				TPair<const FVector&, const FVector&>(BoundsVertices[1], BoundsVertices[2]),
				TPair<const FVector&, const FVector&>(BoundsVertices[2], BoundsVertices[3]),
				TPair<const FVector&, const FVector&>(BoundsVertices[3], BoundsVertices[0]),

				TPair<const FVector&, const FVector&>(BoundsVertices[4], BoundsVertices[5]),
				TPair<const FVector&, const FVector&>(BoundsVertices[5], BoundsVertices[6]),
				TPair<const FVector&, const FVector&>(BoundsVertices[6], BoundsVertices[7]),
				TPair<const FVector&, const FVector&>(BoundsVertices[7], BoundsVertices[4]),

				TPair<const FVector&, const FVector&>(BoundsVertices[0], BoundsVertices[4]),
				TPair<const FVector&, const FVector&>(BoundsVertices[1], BoundsVertices[5]),
				TPair<const FVector&, const FVector&>(BoundsVertices[2], BoundsVertices[6]),
				TPair<const FVector&, const FVector&>(BoundsVertices[3], BoundsVertices[7]),
			};

			for (const auto& Edge : BoundingBoxEdges)
			{
				DrawDebugLine(ThumbnailScene->GetThumbnailWorld(), Edge.Key, Edge.Value, FColor::Blue, false, -1, -1);
			}
		}

		const FBoundsVertices BoundsVerticesInCameraSpace =
		{
			CameraRotation.UnrotateVector(BoundsVertices[0]),
			CameraRotation.UnrotateVector(BoundsVertices[1]),
			CameraRotation.UnrotateVector(BoundsVertices[2]),
			CameraRotation.UnrotateVector(BoundsVertices[3]),
			CameraRotation.UnrotateVector(BoundsVertices[4]),
			CameraRotation.UnrotateVector(BoundsVertices[5]),
			CameraRotation.UnrotateVector(BoundsVertices[6]),
			CameraRotation.UnrotateVector(BoundsVertices[7]),
		};

		if (bIsPerspective)
		{
			const static auto CalculatePerspectiveViewLocation = [](float InAspectRatio, const FThumbnailSettings& InThumbnailSettings, const FBoundsVertices& InCameraSpaceBoundsVertices)->FVector
			{
				/*
				* Algorighm for calculating the perspective camera location
				* 
				*    Camera
				*      []
				*     /  \
				*    /    \
				*   /      \
				*  c1      c2
				* 
				* p1 *-----* p1
				*    |     |
				* p3 *-----* p4
				* 
				* This algorithm works by building a set of all possible combination of points e.g. [(p1,p2),(p1,p3) ..., (p3, p4)].
				* We then loop through all pair of points and calculate the requred position of the camera to frame those two points. We 
				* then pick the point that moves the camera furthest back.
				* We do this for both the horizontal and vertical component and then merge the results into a final camera location.
				*/

				const FVector LeftCameraFrustrumDir   = FVector::ForwardVector.RotateAngleAxis(InThumbnailSettings.CameraFOV * 0.5f, -FVector::UpVector);
				const FVector RightCameraFrustrumDir  = LeftCameraFrustrumDir * FVector(1, -1, 1);
				const FVector TopCameraFrustrumDir    = FVector::ForwardVector.RotateAngleAxis((InThumbnailSettings.CameraFOV * 0.5f) / InAspectRatio, -FVector::RightVector);
				const FVector BottomCameraFrustrumDir = TopCameraFrustrumDir * FVector(1, 1, -1);

				const static auto FrameCamera2D = [](const FVector2D& Point1, const FVector2D& Point2, 
					const FVector2D& LeftFrustrumEdgeDir, const FVector2D& RightFrustrumEdgeDir)->FVector2D
				{
					const FVector2D& LeftPoint = Point1.X < Point2.X ? Point1 : Point2;
					const FVector2D& RightPoint = Point1.X > Point2.X ? Point1 : Point2;

					// Intersection of 2D lines: https://stackoverflow.com/questions/4543506/algorithm-for-intersection-of-2-lines

					const float A1 = -LeftFrustrumEdgeDir.Y;
					const float B1 = LeftFrustrumEdgeDir.X;
					const float C1 = A1 * Point1.X + B1 * Point1.Y;

					const float A2 = -RightFrustrumEdgeDir.Y;
					const float B2 = RightFrustrumEdgeDir.X;
					const float C2 = A2 * Point2.X + B2 * Point2.Y;

					const float Determinant = A1 * B2 - A2 * B1;
					const FVector2D IntersectLocation = FMath::IsNearlyZero(Determinant) 
						? FVector2D::ZeroVector 
						: FVector2D((B2 * C1 - B1 * C2) / Determinant, (A1 * C2 - A2 * C1) / Determinant);

					// The points are too close together so the "optimal" location is behind the first point.
					// For now we return an "invalid location" since we are guaranteed to find a better location
					// later due to us checking a box (There will be a line parallel to this one which works and
					// will allow the camera to see both of these points).
					if (IntersectLocation.Y > FMath::Min(LeftPoint.Y, RightPoint.Y))
						return FVector2D(BIG_NUMBER);

					return IntersectLocation;
				};

				TPair<FVector, FVector> BestHorizontalPair = TPair<FVector, FVector>(FVector::ZeroVector, FVector::ZeroVector);
				TPair<FVector, FVector> BestVerticalPair   = TPair<FVector, FVector>(FVector::ZeroVector, FVector::ZeroVector);
				FVector2D BestHorizontalIntersectLocation  = FVector2D(BIG_NUMBER);
				FVector2D BestVerticalIntersectLocation    = FVector2D(BIG_NUMBER);
				for (int32 i = 0; i < 8; i++)
				{
					for (int32 j = i + 1; j < 8; j++)
					{
						const FVector& Point1 = InCameraSpaceBoundsVertices[i];
						const FVector& Point2 = InCameraSpaceBoundsVertices[j];

						// Horizontal Intersection
						{
							const FVector& LeftPoint = Point1.Y > Point2.Y ? Point2 : Point1;
							const FVector& RightPoint = Point1.Y > Point2.Y ? Point1 : Point2;

							const FVector2D HorizontalLocation = FrameCamera2D(FVector2D(LeftPoint.Y, LeftPoint.X), FVector2D(RightPoint.Y, RightPoint.X),
								FVector2D(LeftCameraFrustrumDir.Y, LeftCameraFrustrumDir.X), FVector2D(RightCameraFrustrumDir.Y, RightCameraFrustrumDir.X));

							if (HorizontalLocation.Y < BestHorizontalIntersectLocation.Y)
							{
								BestHorizontalIntersectLocation = HorizontalLocation;
								BestHorizontalPair = TPair<FVector, FVector>(Point1, Point2);
							}
						}

						// Vertical Intersection
						{
							const FVector& TopPoint = Point1.Z > Point2.Z ? Point2 : Point1;
							const FVector& BottomPoint = Point1.Z > Point2.Z ? Point1 : Point2;

							const FVector2D VerticalLocation = FrameCamera2D(FVector2D(TopPoint.Z, TopPoint.X), FVector2D(BottomPoint.Z, BottomPoint.X),
								FVector2D(BottomCameraFrustrumDir.Z, BottomCameraFrustrumDir.X), FVector2D(TopCameraFrustrumDir.Z, TopCameraFrustrumDir.X));
					
							if (VerticalLocation.Y < BestVerticalIntersectLocation.Y)
							{
								BestVerticalIntersectLocation = VerticalLocation;
								BestVerticalPair = TPair<FVector, FVector>(Point1, Point2);
							}
						}
					}
				}

				const FVector HorizontalCameraLocation = FVector(BestHorizontalIntersectLocation.Y, BestHorizontalIntersectLocation.X, 0.f);
				const FVector VerticalCameraLocation = FVector(BestVerticalIntersectLocation.Y, 0.f, BestVerticalIntersectLocation.X);
				const FVector NewCameraLocation = [&]() 
				{
					switch (InThumbnailSettings.CameraFitMode)
					{
					case EThumbnailCameraFitMode::EFill:
						return FVector(FMath::Max(HorizontalCameraLocation.X, VerticalCameraLocation.X), HorizontalCameraLocation.Y, VerticalCameraLocation.Z);
					case EThumbnailCameraFitMode::EFit:
						return FVector(FMath::Min(HorizontalCameraLocation.X, VerticalCameraLocation.X), HorizontalCameraLocation.Y, VerticalCameraLocation.Z);
					case EThumbnailCameraFitMode::EFitX:
						return FVector(HorizontalCameraLocation.X, HorizontalCameraLocation.Y, VerticalCameraLocation.Z);
					case EThumbnailCameraFitMode::EFitY:
						return FVector(VerticalCameraLocation.X, HorizontalCameraLocation.Y, VerticalCameraLocation.Z);
					}
					return FVector::ZeroVector;
				}();
		
#if 0 // DEBUG DRAWING FOR FRAMING
				{
					UWorld* World = GEngine->GetWorldContextFromPIEInstanceChecked(0).World();

					// Draw box in camera space
					{
						struct FVertexPair { FVector A; FVector B; };
						const TArray<FVertexPair> VertexPairs =
						{
							{ InCameraSpaceBoundsVertices[0], InCameraSpaceBoundsVertices[1] },
							{ InCameraSpaceBoundsVertices[1], InCameraSpaceBoundsVertices[2] },
							{ InCameraSpaceBoundsVertices[2], InCameraSpaceBoundsVertices[3] },
							{ InCameraSpaceBoundsVertices[3], InCameraSpaceBoundsVertices[0] },

							{ InCameraSpaceBoundsVertices[4], InCameraSpaceBoundsVertices[5] },
							{ InCameraSpaceBoundsVertices[5], InCameraSpaceBoundsVertices[6] },
							{ InCameraSpaceBoundsVertices[6], InCameraSpaceBoundsVertices[7] },
							{ InCameraSpaceBoundsVertices[7], InCameraSpaceBoundsVertices[4] },

							{ InCameraSpaceBoundsVertices[0], InCameraSpaceBoundsVertices[4] },
							{ InCameraSpaceBoundsVertices[1], InCameraSpaceBoundsVertices[5] },
							{ InCameraSpaceBoundsVertices[2], InCameraSpaceBoundsVertices[6] },
							{ InCameraSpaceBoundsVertices[3], InCameraSpaceBoundsVertices[7] },
						};

						for (const FVertexPair& VertexPair : VertexPairs)
							DrawDebugLine(World, VertexPair.A, VertexPair.B, FColor::Purple, true, -1, -1, 1.f);
					}

					const auto DrawCamera = [&](const FVector &Location, const FColor& Color)
					{
						DrawDebugCamera(World, Location, FRotator::ZeroRotator, InThumbnailSettings.CameraFOV, 1.f, Color, true, -1.f, -1);

						DrawDebugLine(World, Location, Location + LeftCameraFrustrumDir   * 600.f, Color, true, -1.f, -1, 1);
						DrawDebugLine(World, Location, Location + RightCameraFrustrumDir  * 600.f, Color, true, -1.f, -1, 1);
						DrawDebugLine(World, Location, Location + TopCameraFrustrumDir    * 600.f, Color, true, -1.f, -1, 1);
						DrawDebugLine(World, Location, Location + BottomCameraFrustrumDir * 600.f, Color, true, -1.f, -1, 1);
					};

					DrawCamera(HorizontalCameraLocation, FColor::Blue);
					DrawDebugLine(World, BestHorizontalPair.Key, BestHorizontalPair.Value, FColor::Blue, true, -1, -1, 1);
					
					DrawCamera(VerticalCameraLocation, FColor::Red);
					DrawDebugLine(World, BestVerticalPair.Key, BestVerticalPair.Value, FColor::Red, true, -1, -1, 1);

					DrawCamera(NewCameraLocation, FColor::Green);
				}
#endif

				return NewCameraLocation;
			};

			FVector AutoLocation = CalculatePerspectiveViewLocation(AspectRatio, ThumbnailSettings, BoundsVerticesInCameraSpace);
			AutoLocation.X = ThumbnailSettings.bOverride_CameraDistanceOverride 
				? ThumbnailSettings.CameraDistanceOverride
				: AutoLocation.X + ThumbnailSettings.CameraDistanceOffset;

			CaptureComponentView.Location = CameraRotation.RotateVector(AutoLocation);
			CaptureComponentView.FOV      = ThumbnailSettings.CameraFOV;
		}
		else
		{
			struct FOrthographicView
			{
				float OrthoWidth;
				FVector CameraLocation;
			};
			const static auto CalculateOrthographicView = [](float InAspectRatio, const FThumbnailSettings& InThumbnailSettings, const FBoundsVertices& InCameraSpaceBoundsVertices)->FOrthographicView
			{
				struct FBounds2D { FVector2D Min; FVector2D Max; };
				const auto ProjectedBounds2D = [&]()->FBounds2D
				{
					FVector2D OutMin(EForceInit::ForceInit);
					FVector2D OutMax(EForceInit::ForceInit);
					for (const auto &Vertex : InCameraSpaceBoundsVertices)
					{
						if (Vertex.Y < OutMin.X)
							OutMin.X = Vertex.Y;
						if (Vertex.Y > OutMax.X)
							OutMax.X = Vertex.Y;

						if (Vertex.Z < OutMin.Y)
							OutMin.Y = Vertex.Z;
						if (Vertex.Z > OutMax.Y)
							OutMax.Y = Vertex.Z;
					}

					return { OutMin, OutMax };
				}();

				const auto Bounds2DDimentions = FVector2D(FMath::Abs(ProjectedBounds2D.Max.X - ProjectedBounds2D.Min.X), FMath::Abs(ProjectedBounds2D.Max.Y - ProjectedBounds2D.Min.Y));

				const float OrthoWidth = [&]() -> float
				{
					switch (InThumbnailSettings.CameraFitMode)
					{
					case EThumbnailCameraFitMode::EFill:
						return FMath::Min(Bounds2DDimentions.X, Bounds2DDimentions.Y * InAspectRatio);
					case EThumbnailCameraFitMode::EFit:
						return FMath::Max(Bounds2DDimentions.X, Bounds2DDimentions.Y * InAspectRatio);
					case EThumbnailCameraFitMode::EFitX:
						return Bounds2DDimentions.X;
					case EThumbnailCameraFitMode::EFitY:
						return Bounds2DDimentions.Y * InAspectRatio;
					}
					return 0.f;
				}();

				const FVector CameraLocation = FVector(-1000.f, // Make sure we're not clipping by moving it back an additional 1000cm
					(ProjectedBounds2D.Max.X + ProjectedBounds2D.Min.X) * 0.5f, 
					(ProjectedBounds2D.Max.Y + ProjectedBounds2D.Min.Y) * 0.5f
				);

				return { OrthoWidth, CameraLocation };
			};

			const FOrthographicView OrthographicView = CalculateOrthographicView(AspectRatio, ThumbnailSettings, BoundsVerticesInCameraSpace);
			CaptureComponentView.OrthoWidth = ThumbnailSettings.bOverride_OrthoWidthOverride 
				? ThumbnailSettings.OrthoWidthOverride
				: OrthographicView.OrthoWidth + ThumbnailSettings.OrthoWidthOffset;
			CaptureComponentView.Location   = CameraRotation.RotateVector(OrthographicView.CameraLocation);
		}

		CaptureComponentView.Location += CameraRotation.RotateVector(ThumbnailSettings.CameraPositionOffset);
		CaptureComponentView.Rotation = CameraRotation.Rotator();
	}
	else
	{
		CaptureComponentView.Location = ThumbnailSettings.CustomCameraLocation;
		CaptureComponentView.Rotation = ThumbnailSettings.CustomCameraRotation;
		CaptureComponentView.OrthoWidth = ThumbnailSettings.CustomOrthoWidth;
	}
	
	CaptureComponentView.PostProcessBlendWeight = 1.f;
	CaptureComponentView.PostProcessSettings    = ThumbnailSettings.PostProcessingSettings;

	// The Vignette effect is current broken on mobile so make sure to disable it
	#if (PLATFORM_ANDROID || PLATFORM_IOS)
	CaptureComponentView.PostProcessSettings.bOverride_VignetteIntensity = true;
	CaptureComponentView.PostProcessSettings.VignetteIntensity           = 0.f;
	#endif

	CaptureComponent->SetCameraView(CaptureComponentView);
	CaptureComponent->PostProcessSettings    = CaptureComponentView.PostProcessSettings;
	CaptureComponent->PostProcessBlendWeight = CaptureComponentView.PostProcessBlendWeight;
	CaptureComponent->bCameraCutThisFrame    = true; // Reset view each capture
	CaptureComponent->TextureTarget          = RenderTarget;

	TArray<uint8> AlphaOverride;
	if (ThumbnailSettings.bCaptureAlpha)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_CaptureAlpha);
		// I haven't been able to find a way of extracting the Alpha when capturing SCS_FinalColorHDR/SCS_FinalColorLDR.
		// This is a bit of an ugly hack where we capture the scene again using SCS_SceneColorHDR
		// and copy the alpha results into our main capture.
		CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR; 
		CaptureComponent->CaptureScene();
		CaptureComponent->CaptureSource = GetCaptureSource();

		AlphaOverride = ThumbnailGenerator::ExtractAlpha(RenderTarget, true);
	}

	CaptureComponent->CaptureScene();

	// Clear any debug lines drawn by our thumbnail actor
	if (ThumbnailScene->GetThumbnailWorld()->GetLineBatcher(UWorld::ELineBatcherType::World))
		ThumbnailScene->GetThumbnailWorld()->GetLineBatcher(UWorld::ELineBatcherType::World)->Flush();
	if (ThumbnailScene->GetThumbnailWorld()->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
		ThumbnailScene->GetThumbnailWorld()->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent)->Flush();
	if (ThumbnailScene->GetThumbnailWorld()->GetLineBatcher(UWorld::ELineBatcherType::Foreground))
		ThumbnailScene->GetThumbnailWorld()->GetLineBatcher(UWorld::ELineBatcherType::Foreground)->Flush();

	CaptureComponent->TextureTarget = nullptr;

	// Render UI if specified
	if (ThumbnailSettings.ThumbnailUI.Get() != nullptr)
	{
		if (UUserWidget* UserWidget = CreateWidget(CaptureComponent->GetWorld(), ThumbnailSettings.ThumbnailUI))
		{
			WidgetRenderer->DrawWidget(RenderTarget, UserWidget->TakeWidget(), FVector2D(RenderTarget->SizeX, RenderTarget->SizeY), 0.f, false);
			UserWidget->MarkAsGarbage();
		}
	}

	UTexture2D* ThumbnailTexture = IsValid(ResourceObject) 
		? ResourceObject
		: ThumbnailGenerator::ConstructTransientTexture2D(
			GetTransientPackage(), 
			FString::Printf(TEXT("%s_Thumbnail"), *Actor->GetName()), 
			RenderTarget->SizeX, 
			RenderTarget->SizeY,
			RenderTarget->GetFormat()
		);

	if (!ThumbnailTexture)
	{
		UE_LOG(LogThumbnailGenerator, Error, TEXT("CaptureThumbnail - Failed to construct Texture2D object"));
		return nullptr;
	}

	ThumbnailGenerator::FillTextureDataFromRenderTarget(ThumbnailTexture, RenderTarget, AlphaOverride, ThumbnailSettings.AlphaBlendMode);

	ThumbnailTexture->SRGB = true;
	ThumbnailTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
	ThumbnailTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;

	return ThumbnailTexture;
}

void FThumbnailGenerator::PrepareThumbnailCapture()
{
	UWorld* World = GetThumbnailWorld();
	check(World != nullptr);

	ThumbnailSceneActors.Reset();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		ThumbnailSceneActors.Add(*It);
	}

	ThumbnailSceneActors.Append(ThumbnailScene->GetPersistentActors());

	bIsCapturingThumbnail = true;
}

void FThumbnailGenerator::CleanupThumbnailCapture()
{
	if (!bIsCapturingThumbnail)
	{
		return;
	}

	UWorld* World = GetThumbnailWorld();
	check(World != nullptr);

	TSet<TObjectPtr<AActor>> AllActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AllActors.Add(*It);
	}

	TSet<TObjectPtr<AActor>> NewActors = AllActors.Difference(ThumbnailSceneActors);
	for (TObjectPtr<AActor> Actor : NewActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}

	ThumbnailSceneActors.Reset();

	bIsCapturingThumbnail = false;
}

void FThumbnailGenerator::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CaptureComponent);
	Collector.AddReferencedObjects(ThumbnailGeneratorScripts);
	Collector.AddReferencedObjects(ThumbnailSceneActors);
}

FString FThumbnailGenerator::GetReferencerName() const
{
	return FString::Printf(TEXT("ThumbnailGenerator_%s"), ThumbnailScene.IsValid() ? *ThumbnailScene->GetDebugName() : TEXT("Empty"));
}


/*
* UThumbnailGeneration 
*/

UTexture2D* UThumbnailGeneration::GenerateThumbnail(TSubclassOf<AActor> ActorClass, const FThumbnailSettings& ThumbnailSettings, 
	UTexture2D* ResourceObject, const TMap<FString, FString>& Properties)
{
	const FThumbnailSettings MergedThumbnailSettings = FThumbnailSettings::MergeThumbnailSettings(UThumbnailGeneratorSettings::Get()->DefaultThumbnailSettings, ThumbnailSettings);
	return GThumbnailGenerator->GenerateActorThumbnail(ActorClass, MergedThumbnailSettings, ResourceObject, Properties);
}

void UThumbnailGeneration::GenerateThumbnailAsync(UClass* ActorClass, const FGenerateThumbnailCallbackNative& Callback, const FThumbnailSettings& ThumbnailSettings,
	const FPreCaptureThumbnailNative& PreCaptureThumbnail, UTexture2D* ResourceObject, const TMap<FString, FString>& Properties)
{
	TStrongObjectPtr<UClass> StrongClassPtr(ActorClass);
	TStrongObjectPtr<UTexture2D> StrongResourceObject(ResourceObject);
	ThumbnailGenerator::FThumbnailGeneratorTaskQueue::Get().TaskQueue.Add([StrongClassPtr, ThumbnailSettings, StrongResourceObject, Properties, Callback, PreCaptureThumbnail]()
	{
		const FThumbnailSettings MergedThumbnailSettings = FThumbnailSettings::MergeThumbnailSettings(UThumbnailGeneratorSettings::Get()->DefaultThumbnailSettings, ThumbnailSettings);
		AActor* ThumbnailActor = GThumbnailGenerator->BeginGenerateActorThumbnail(StrongClassPtr.Get(), MergedThumbnailSettings, Properties);
		PreCaptureThumbnail.ExecuteIfBound(ThumbnailActor);

		UTexture2D* Thumbnail = GThumbnailGenerator->FinishGenerateActorThumbnail(ThumbnailActor, MergedThumbnailSettings, StrongResourceObject.Get());
		Callback.ExecuteIfBound(Thumbnail);
	});
}

UWorld* UThumbnailGeneration::GetThumbnailWorld()
{
	return GThumbnailGenerator->GetThumbnailWorld();
}

USceneCaptureComponent2D* UThumbnailGeneration::GetThumbnailCaptureComponent()
{
	return GThumbnailGenerator->GetThumbnailCaptureComponent();
}

void UThumbnailGeneration::InitializeThumbnailWorld(FThumbnailBackgroundSceneSettings BackgroundSceneSettings)
{
	GThumbnailGenerator->InitializeThumbnailWorld(BackgroundSceneSettings);
}

UTexture2D* UThumbnailGeneration::SaveThumbnail(UTexture2D* Thumbnail, const FDirectoryPath& OutputDirectory, FString OutputName)
{
#if WITH_EDITOR
	if (FThumbnailGeneratorModule::SaveThumbnailDelegate.IsBound())
		return FThumbnailGeneratorModule::SaveThumbnailDelegate.Execute(Thumbnail, OutputDirectory.Path, OutputName);
#endif
	return nullptr;
}

AActor* UThumbnailGeneration::K2_BeginGenerateThumbnail(UClass* ActorClass, const FThumbnailSettings& ThumbnailSettings)
{
	return GThumbnailGenerator->BeginGenerateActorThumbnail(ActorClass, ThumbnailSettings, TMap<FString, FString>(), false);
}

UTexture2D* UThumbnailGeneration::K2_FinishGenerateThumbnail(AActor* Actor, const FThumbnailSettings& ThumbnailSettings)
{
	return GThumbnailGenerator->FinishGenerateActorThumbnail(Actor, ThumbnailSettings, nullptr, false);
}

void UThumbnailGeneration::K2_GenerateThumbnailAsync(UClass* ActorClass, FThumbnailSettings ThumbnailSettings, 
	TMap<FString, FString> Properties, FGenerateThumbnailCallback Callback, FPreCaptureThumbnail PreCaptureThumbnail)
{
	GenerateThumbnailAsync(
		ActorClass,
		FGenerateThumbnailCallbackNative::CreateUFunction(Callback.GetUObject(), Callback.GetFunctionName()),
		ThumbnailSettings,
		FPreCaptureThumbnailNative::CreateUFunction(PreCaptureThumbnail.GetUObject(), PreCaptureThumbnail.GetFunctionName()),
		nullptr,
		Properties
	);
}

void UThumbnailGeneration::K2_FinishSpawningThumbnailActor(AActor* Actor)
{
	if (IsValid(Actor))
	{
		Actor->FinishSpawning(FTransform::Identity);
	}
}

FThumbnailSettings UThumbnailGeneration::K2_FinalizeThumbnailSettings(FThumbnailSettings ThumbnailSettings)
{
	return FThumbnailSettings::MergeThumbnailSettings(UThumbnailGeneratorSettings::Get()->DefaultThumbnailSettings, ThumbnailSettings);
}

DEFINE_FUNCTION(UThumbnailGeneration::execK2_ExportPropertyText)
{
	Stack.StepCompiledIn<FProperty>(NULL);

	auto* Property        = Stack.MostRecentProperty;
	auto* PropertyValAddr = (void*)Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
	FString PropertyTextValue;
	Property->ExportTextItem_Direct(PropertyTextValue, PropertyValAddr, nullptr, nullptr, 0, nullptr);
	*(FString*)RESULT_PARAM = PropertyTextValue;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UThumbnailGeneration::execK2_ExportArrayPropertyText)
{
	Stack.StepCompiledIn<FArrayProperty>(NULL);

	auto* Property        = Stack.MostRecentProperty;
	auto* PropertyValAddr = (void*)Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
	FString PropertyTextValue;
	Property->ExportTextItem_Direct(PropertyTextValue, PropertyValAddr, nullptr, nullptr, 0, nullptr);
	*(FString*)RESULT_PARAM = PropertyTextValue;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UThumbnailGeneration::execK2_ExportMapPropertyText)
{
	Stack.StepCompiledIn<FMapProperty>(NULL);

	auto* Property        = Stack.MostRecentProperty;
	auto* PropertyValAddr = (void*)Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
	FString PropertyTextValue;
	Property->ExportTextItem_Direct(PropertyTextValue, PropertyValAddr, nullptr, nullptr, 0, nullptr);
	*(FString*)RESULT_PARAM = PropertyTextValue;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UThumbnailGeneration::execK2_ExportSetPropertyText)
{
	Stack.StepCompiledIn<FSetProperty>(NULL);

	auto* Property        = Stack.MostRecentProperty;
	auto* PropertyValAddr = (void*)Stack.MostRecentPropertyAddress;

	P_FINISH;

	P_NATIVE_BEGIN;
	FString PropertyTextValue;
	Property->ExportTextItem_Direct(PropertyTextValue, PropertyValAddr, nullptr, nullptr, 0, nullptr);
	*(FString*)RESULT_PARAM = PropertyTextValue;
	P_NATIVE_END;
}