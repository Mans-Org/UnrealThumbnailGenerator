// Copyright Mans Isaksson. All Rights Reserved.

#include "ThumbnailGeneratorSettings.h"
#include "ThumbnailGeneratorModule.h"
#include "ThumbnailGeneratorScript.h"
#include "ThumbnailGenerator.h"

#include "UObject/ConstructorHelpers.h"
#include "Components/SkinnedMeshComponent.h"
#include "Particles/ParticleSystemComponent.h"

FThumbnailSettings::FThumbnailSettings()
{
	// to set all bOverride_.. by default to false
	FMemory::Memzero(this, sizeof(FThumbnailSettings));

	ThumbnailTextureWidth = 512;
	ThumbnailTextureHeight = 512;

	bCaptureAlpha = false;
	AlphaBlendMode = EThumbnailAlphaBlendMode::EReplace;
	ThumbnailUI = nullptr;

	ProjectionType = ECameraProjectionMode::Perspective;
	CameraFOV = 45.f;
	CameraOrbitRotation = FRotator(-18.f, -22.f, 0.f);
	CameraFitMode = EThumbnailCameraFitMode::EFit;
	CameraDistanceOffset = -20.f;
	CameraDistanceOverride = 0.f;
	OrthoWidthOffset = 0.f;
	OrthoWidthOverride = 0.f;
	CameraPositionOffset = FVector::ZeroVector;
	CameraRotationOffset = FRotator::ZeroRotator;

	CustomCameraLocation = FVector::ZeroVector;
	CustomCameraRotation = FRotator::ZeroRotator;
	CustomOrthoWidth = 0.f;
	CustomActorBounds = FBox(EForceInit::ForceInit);
	
	SimulationMode = EThumbnailSceneSimulationMode::ESpecifiedComponents;
	SimulateSceneTime = 0.01f;
	SimulateSceneFramerate = 15.f;
	ComponentsToSimulate = { USkinnedMeshComponent::StaticClass(), UParticleSystemComponent::StaticClass() };

	CustomActorTransform = FTransform::Identity;
	bSnapToFloor = false;
	ComponentBoundsBlacklist = { UParticleSystemComponent::StaticClass() };
	bIncludeHiddenComponentsInBounds = false;

	DirectionalLightRotation = FRotator(-45.f, 30.f, 0.f);
	DirectionalLightIntensity = 1.0f;
	DirectionalLightColor = FLinearColor(1.f, 1.f, 1.f);

	DirectionalFillLightRotation = FRotator(-45.f, -160.f, 0.f);
	DirectionalFillLightIntensity = 0.75f;
	DirectionalFillLightColor = FLinearColor(1.f, 1.f, 1.f);

	SkyLightIntensity = 0.8f;
	SkyLightColor = FLinearColor(1.f, 1.f, 1.f);

	bShowEnvironment = true;
	bEnvironmentAffectLighting = true;
	EnvironmentColor = FLinearColor(1.f, 1.f, 1.f);
	EnvironmentCubeMap = FSoftObjectPath(ThumbnailAssetPaths::CubeMap);
	EnvironmentRotation = 0.f;

	PostProcessingSettings = FPostProcessSettings();

	ThumbnailSkySphere = FSoftClassPath(ThumbnailAssetPaths::SkySphere);
	ThumbnailGeneratorScripts = { };

	bDebugBounds = false;
}

FThumbnailSettings FThumbnailSettings::MergeThumbnailSettings(const FThumbnailSettings& DefaultSettings, const FThumbnailSettings& OverrideSettings)
{
	FThumbnailSettings OutSettings;

	struct FPropertyMemberAddr
	{
		FBoolProperty* OverrideBoolProperty;
		FProperty* Property;
	};
	static TArray<FPropertyMemberAddr, TInlineAllocator<32>> OverrideAndPropertyMemberValueAddr;
	static bool bAreValueAddrInitialized = false;
	static FCriticalSection CriticalSection;

	if (!bAreValueAddrInitialized)
	{
		FScopeLock Lock(&CriticalSection);
		if (!bAreValueAddrInitialized)
		{
			// Save property pointer locations on first merge for major performance improvements
			if (OverrideAndPropertyMemberValueAddr.Num() == 0)
			{
				TMap<FName, FProperty*> OverrideProperties;
				OverrideProperties.Reserve(64);
				for (FProperty* Property = FThumbnailSettings::StaticStruct()->PropertyLink; Property; Property = Property->PropertyLinkNext)
				{
					const FString PropertyName = Property->GetName();
					if (PropertyName.StartsWith("bOverride_"))
					{
						OverrideProperties.Add(*PropertyName, Property);
					}
					else if (FProperty** OverrideProperty = OverrideProperties.Find(*FString::Printf(TEXT("bOverride_%s"), *PropertyName)))
					{
						OverrideAndPropertyMemberValueAddr.Add(FPropertyMemberAddr { CastFieldChecked<FBoolProperty>(*OverrideProperty), Property });
					}
				}
			}

			bAreValueAddrInitialized = true;
		}
	}

	for (const FPropertyMemberAddr& It : OverrideAndPropertyMemberValueAddr)
	{
		if (It.OverrideBoolProperty->GetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>(&OverrideSettings))) // Is bOverride_ set in OverrideSettings
		{
			It.OverrideBoolProperty->SetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>((void*)&OutSettings), true);
			It.Property->CopyCompleteValue(
				It.Property->ContainerPtrToValuePtr<void>((void*)&OutSettings), 
				It.Property->ContainerPtrToValuePtr<void>(&OverrideSettings)
			);
		}
		else if (It.OverrideBoolProperty->GetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>(&DefaultSettings))) // Is bOverride_ set in DefaultSettings
		{
			It.OverrideBoolProperty->SetPropertyValue(It.OverrideBoolProperty->ContainerPtrToValuePtr<void>((void*)&OutSettings), true);
			It.Property->CopyCompleteValue(
				It.Property->ContainerPtrToValuePtr<void>((void*)&OutSettings), 
				It.Property->ContainerPtrToValuePtr<void>(&DefaultSettings)
			);
		}
	}

	// Disable auto exposure as it doesn't work well in a thumbnail scenario
	OutSettings.PostProcessingSettings.bOverride_AutoExposureMinBrightness = true;
	OutSettings.PostProcessingSettings.AutoExposureMinBrightness = 1.f;
	OutSettings.PostProcessingSettings.bOverride_AutoExposureMaxBrightness = true;
	OutSettings.PostProcessingSettings.AutoExposureMaxBrightness = 1.f;

	return OutSettings;
}

UThumbnailGeneratorSettings::UThumbnailGeneratorSettings()
{
	// Force reference some assets to make sure they get cooked
	ConstructorHelpers::FClassFinder<AActor> SkySphereClassFinder(ThumbnailAssetPaths::SkySphere);
	AssetRefs.Add(SkySphereClassFinder.Class);

	ConstructorHelpers::FClassFinder<UObject> ThumbnailScriptFinder(ThumbnailAssetPaths::CustomDepthScript);
	AssetRefs.Add(ThumbnailScriptFinder.Class);

	ConstructorHelpers::FObjectFinder<UMaterialInterface> ThumbnailOutlineMat1(ThumbnailAssetPaths::OutlinePostProcessMaterial_NoAlpha);
	AssetRefs.Add(ThumbnailOutlineMat1.Object);

	ConstructorHelpers::FObjectFinder<UMaterialInterface> ThumbnailOutlineMat2(ThumbnailAssetPaths::OutlinePostProcessMaterial_WithAlpha);
	AssetRefs.Add(ThumbnailOutlineMat2.Object);
}

UThumbnailGeneratorSettings* UThumbnailGeneratorSettings::Get()
{
	return GetMutableDefault<UThumbnailGeneratorSettings>();
}

const TArray<FName> &UThumbnailGeneratorSettings::GetPresetList()
{
	static TArray<FName> PresetList = 
	{
		TEXT("Default"),
		TEXT("Default, No Background"),
		TEXT("Outline"),
		TEXT("Outline, No Background"),
		TEXT("Silhuett"),
		TEXT("Silhuett, No Background")
	};

	return PresetList;
}

void UThumbnailGeneratorSettings::ApplyPreset(FName Preset)
{
	static TMap<FName, TFunction<void()>> PresetMap =
	{
		{ TEXT("Default"), &UThumbnailGeneratorSettings::ApplyDefaultPreset },
		{ TEXT("Default, No Background"), &UThumbnailGeneratorSettings::ApplyDefaultNoBackgroundPreset },
		{ TEXT("Outline"), &UThumbnailGeneratorSettings::ApplyOutlinePreset },
		{ TEXT("Outline, No Background"), &UThumbnailGeneratorSettings::ApplyOutlineNoBackgroundPreset },
		{ TEXT("Silhuett"), &UThumbnailGeneratorSettings::ApplySilhuettPreset },
		{ TEXT("Silhuett, No Background"), &UThumbnailGeneratorSettings::ApplySilhuettNoBackgroundPreset }
	};

	if (auto* PresetFunc = PresetMap.Find(Preset))
		(*PresetFunc)();
}

void UThumbnailGeneratorSettings::ApplyDefaultPreset()
{
	UThumbnailGeneratorSettings* Settings = UThumbnailGeneratorSettings::Get();

	// Don't reset some settigs, as that would be pretty annoying
	const FThumbnailSettings OldSettings = Settings->DefaultThumbnailSettings;

	Settings->DefaultThumbnailSettings = FThumbnailSettings();

	// Restore some old settings
	Settings->DefaultThumbnailSettings.bOverride_ThumbnailTextureWidth = OldSettings.bOverride_ThumbnailTextureWidth;
	Settings->DefaultThumbnailSettings.ThumbnailTextureWidth = OldSettings.ThumbnailTextureWidth;
	
	Settings->DefaultThumbnailSettings.bOverride_ThumbnailTextureHeight = OldSettings.bOverride_ThumbnailTextureHeight;
	Settings->DefaultThumbnailSettings.ThumbnailTextureHeight = OldSettings.ThumbnailTextureHeight;

	Settings->DefaultThumbnailSettings.bOverride_ThumbnailBitDepth = OldSettings.bOverride_ThumbnailBitDepth;
	Settings->DefaultThumbnailSettings.ThumbnailBitDepth = OldSettings.ThumbnailBitDepth;

	Settings->TryUpdateDefaultConfigFile();
}

void UThumbnailGeneratorSettings::ApplyDefaultNoBackgroundPreset()
{
	ApplyDefaultPreset();

	UThumbnailGeneratorSettings* Settings = UThumbnailGeneratorSettings::Get();

	// Enable Alpha Capture
	Settings->DefaultThumbnailSettings.bOverride_bCaptureAlpha = true;
	Settings->DefaultThumbnailSettings.bCaptureAlpha = true;

	// Set AlphaBlendMode to Replace
	Settings->DefaultThumbnailSettings.bOverride_AlphaBlendMode = true;
	Settings->DefaultThumbnailSettings.AlphaBlendMode = EThumbnailAlphaBlendMode::EReplace;

	// Disable the Environment
	Settings->DefaultThumbnailSettings.bOverride_bShowEnvironment = true;
	Settings->DefaultThumbnailSettings.bShowEnvironment = false;

	Settings->TryUpdateDefaultConfigFile();
}

void UThumbnailGeneratorSettings::ApplyOutlinePreset()
{
	ApplyDefaultPreset();

	UThumbnailGeneratorSettings* Settings = UThumbnailGeneratorSettings::Get();

	// Apply script which turns on custom depth on all primitives
	Settings->DefaultThumbnailSettings.bOverride_ThumbnailGeneratorScripts = true;
	Settings->DefaultThumbnailSettings.ThumbnailGeneratorScripts = { FSoftClassPath(ThumbnailAssetPaths::CustomDepthScript).TryLoadClass<UThumbnailGeneratorScript>() };
	
	// Enable post processing override
	Settings->DefaultThumbnailSettings.bOverride_PostProcessingSettings = true;
	
	// Apply the Outline Material
	FPostProcessSettings& PostProcessingSettings = Settings->DefaultThumbnailSettings.PostProcessingSettings;
	PostProcessingSettings.WeightedBlendables.Array = { FWeightedBlendable{ 1.f, FSoftObjectPath(ThumbnailAssetPaths::OutlinePostProcessMaterial_NoAlpha).TryLoad() } };

	Settings->TryUpdateDefaultConfigFile();
}

void UThumbnailGeneratorSettings::ApplyOutlineNoBackgroundPreset()
{
	ApplyDefaultPreset();

	UThumbnailGeneratorSettings* Settings = UThumbnailGeneratorSettings::Get();

	// Enable Alpha Capture
	Settings->DefaultThumbnailSettings.bOverride_bCaptureAlpha = true;
	Settings->DefaultThumbnailSettings.bCaptureAlpha = true;

	// Set AlphaBlendMode to Add
	Settings->DefaultThumbnailSettings.bOverride_AlphaBlendMode = true;
	Settings->DefaultThumbnailSettings.AlphaBlendMode = EThumbnailAlphaBlendMode::EAdd;

	// Disable the Environment
	Settings->DefaultThumbnailSettings.bOverride_bShowEnvironment = true;
	Settings->DefaultThumbnailSettings.bShowEnvironment = false;

	// Apply script which turns on custom depth on all primitives
	Settings->DefaultThumbnailSettings.bOverride_ThumbnailGeneratorScripts = true;
	Settings->DefaultThumbnailSettings.ThumbnailGeneratorScripts = { FSoftClassPath(ThumbnailAssetPaths::CustomDepthScript).TryLoadClass<UThumbnailGeneratorScript>() };

	// Enable post processing override
	Settings->DefaultThumbnailSettings.bOverride_PostProcessingSettings = true;

	// Apply the Outline Material
	FPostProcessSettings& PostProcessingSettings = Settings->DefaultThumbnailSettings.PostProcessingSettings;
	PostProcessingSettings.WeightedBlendables.Array = { FWeightedBlendable{ 1.f, FSoftObjectPath(ThumbnailAssetPaths::OutlinePostProcessMaterial_WithAlpha).TryLoad() } };

	Settings->TryUpdateDefaultConfigFile();
}

void UThumbnailGeneratorSettings::ApplySilhuettPreset()
{
	ApplyDefaultPreset();

	UThumbnailGeneratorSettings* Settings = UThumbnailGeneratorSettings::Get();

	// Apply script which turns on custom depth on all primitives
	Settings->DefaultThumbnailSettings.bOverride_ThumbnailGeneratorScripts = true;
	Settings->DefaultThumbnailSettings.ThumbnailGeneratorScripts = { FSoftClassPath(ThumbnailAssetPaths::CustomDepthScript).TryLoadClass<UThumbnailGeneratorScript>() };

	// Reset Camera
	Settings->DefaultThumbnailSettings.bOverride_ProjectionType = true;
	Settings->DefaultThumbnailSettings.ProjectionType = ECameraProjectionMode::Orthographic;

	Settings->DefaultThumbnailSettings.bOverride_CameraOrbitRotation = true;
	Settings->DefaultThumbnailSettings.CameraOrbitRotation = FRotator(0.f, 90.f, 0.f);

	// Enable post processing override
	Settings->DefaultThumbnailSettings.bOverride_PostProcessingSettings = true;

	// Apply the Outline Material
	FPostProcessSettings& PostProcessingSettings = Settings->DefaultThumbnailSettings.PostProcessingSettings;
	PostProcessingSettings.WeightedBlendables.Array = { FWeightedBlendable{ 1.f, FSoftObjectPath(ThumbnailAssetPaths::SilhuettPostProcessMaterial).TryLoad() } };

	Settings->TryUpdateDefaultConfigFile();
}

void UThumbnailGeneratorSettings::ApplySilhuettNoBackgroundPreset()
{
	ApplySilhuettPreset();

	UThumbnailGeneratorSettings* Settings = UThumbnailGeneratorSettings::Get();

	// Enable Alpha Capture
	Settings->DefaultThumbnailSettings.bOverride_bCaptureAlpha = true;
	Settings->DefaultThumbnailSettings.bCaptureAlpha = true;

	// Set AlphaBlendMode to Replace
	Settings->DefaultThumbnailSettings.bOverride_AlphaBlendMode = true;
	Settings->DefaultThumbnailSettings.AlphaBlendMode = EThumbnailAlphaBlendMode::EReplace;

	// Disable the Environment
	Settings->DefaultThumbnailSettings.bOverride_bShowEnvironment = true;
	Settings->DefaultThumbnailSettings.bShowEnvironment = false;

	Settings->TryUpdateDefaultConfigFile();
}

#if WITH_EDITOR
void UThumbnailGeneratorSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty ? (PropertyChangedEvent.MemberProperty->GetFName()) : NAME_None;
	
	if (UThumbnailGeneratorSettings::Get() == this)
	{
		// Due to a bug in unreal the DefaultConfig will not be saved when we modify InlineEditConditionToggle booleans.
		// To fix this we detect changes here and save the config.
		TryUpdateDefaultConfigFile();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
