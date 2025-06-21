// Copyright Mans Isaksson. All Rights Reserved.

#include "ThumbnailBackgroundScene.h"
#include "ThumbnailGeneratorModule.h"
#include "ThumbnailPreviewScene.h"
#include "ThumbnailGeneratorInterfaces.h"
#include "ThumbnailGeneratorCompatibilityLayer.h"

#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureCube.h"
#include "Engine/LevelStreaming.h"
#include "Engine/WorldComposition.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Level.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "ShaderCompiler.h"

namespace InstanceWorldHelpers
{
	const FString InstancePrefix     = TEXT("INST");
	
	/** Package names currently being duplicated, needed by FixupForInstance */
	TSet<FName> InstancePackageNames = {};

	static bool HasInstancePrefix(const FString& PackageName)
	{
		const FString ShortPrefixedName = FPackageName::GetLongPackageAssetName(PackageName);
		return ShortPrefixedName.StartsWith(FString::Printf(TEXT("%s_"), *InstancePrefix), ESearchCase::CaseSensitive);
	}

	static FString BuildInstancePackagePrefix(int32 InstanceID)
	{
		return FString::Printf(TEXT("%s_%d_"), *InstancePrefix, InstanceID);
	}

	static void AddInstancePackageName(FName NewPIEPackageName)
	{
		InstancePackageNames.Add(NewPIEPackageName);
	}

	static UObject* FindObjectInPackage(UPackage* Package, UClass* Class, const FSoftObjectPath* ObjectPath)
	{
		FString ObjectName;
		if (ObjectPath)
		{
			const FString ShortPackageOuterAndName = FPackageName::GetLongPackageAssetName(ObjectPath->ToString());
			ShortPackageOuterAndName.Split(".", nullptr, &ObjectName);
		}

		TArray<UObject*> Objects;
		GetObjectsWithOuter(Package, Objects, false);
		for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
		{
			if (Class && (*ObjIt)->IsA(Class))
			{
				return (*ObjIt);
			}
			else if ((*ObjIt)->GetPathName(Package) == ObjectName)
			{
				return *ObjIt;
			}
		}

		return nullptr;
	}

	static FString ConvertToInstancePackageName(const FString& PackageName, int32 InstanceID)
	{
		const FString PackageAssetName = FPackageName::GetLongPackageAssetName(PackageName);

		if (HasInstancePrefix(PackageAssetName))
		{
			return PackageName;
		}
		else
		{
			check(InstanceID != -1);
			const FString PackageAssetPath = FPackageName::GetLongPackagePath(PackageName);
			const FString PackagePrefix = BuildInstancePackagePrefix(InstanceID);
			return FString::Printf(TEXT("%s/%s%s"), *PackageAssetPath, *PackagePrefix, *PackageAssetName);
		}
	}

	static FString StripPrefixFromPackageName(const FString& PrefixedName, const FString& Prefix)
	{
		FString ResultName;
		if (HasInstancePrefix(PrefixedName))
		{
			const FString ShortPrefixedName = FPackageName::GetLongPackageAssetName(PrefixedName);
			const FString NamePath = FPackageName::GetLongPackagePath(PrefixedName);
			ResultName = NamePath + "/" + ShortPrefixedName.RightChop(Prefix.Len());
		}
		else
		{
			ResultName = PrefixedName;
		}

		return ResultName;
	}

	static void ReinitializeWorldCompositionForInstance(UWorldComposition* WorldComposition, int32 InstanceID)
	{
		UWorldComposition::FTilesList& Tiles = WorldComposition->GetTilesList();
		for (int32 TileIdx = 0; TileIdx < Tiles.Num(); ++TileIdx)
		{
			FWorldCompositionTile& Tile = Tiles[TileIdx];

			FString InstancePackageName = InstanceWorldHelpers::ConvertToInstancePackageName(Tile.PackageName.ToString(), InstanceID);
			Tile.PackageName = FName(*InstancePackageName);
			InstanceWorldHelpers::AddInstancePackageName(Tile.PackageName);
			for (FName& LODPackageName : Tile.LODPackageNames)
			{
				FString InstanceLODPackageName = InstanceWorldHelpers::ConvertToInstancePackageName(LODPackageName.ToString(), InstanceID);
				LODPackageName = FName(*InstanceLODPackageName);
				InstanceWorldHelpers::AddInstancePackageName(LODPackageName);
			}
		}
	}

	static void RenameSteamingLevelForInstance(ULevelStreaming* StreamingLevel, int32 InstanceID)
	{
		// Apply instance prefix to this level references
		if (StreamingLevel->PackageNameToLoad == NAME_None)
		{
			// TODO: We always load a fresh level, should not need to Strip the Prefix
			FString NonPrefixedName = InstanceWorldHelpers::StripPrefixFromPackageName(StreamingLevel->GetWorldAssetPackageName(), InstanceWorldHelpers::BuildInstancePackagePrefix(InstanceID));
			StreamingLevel->PackageNameToLoad = FName(*NonPrefixedName);
		}

		FName PlayWorldStreamingPackageName = FName(*InstanceWorldHelpers::ConvertToInstancePackageName(StreamingLevel->GetWorldAssetPackageName(), InstanceID));
		InstanceWorldHelpers::AddInstancePackageName(PlayWorldStreamingPackageName);
		StreamingLevel->SetWorldAssetByPackageName(PlayWorldStreamingPackageName);

		// Rename LOD levels if any
		if (StreamingLevel->LODPackageNames.Num() > 0)
		{
			StreamingLevel->LODPackageNamesToLoad.Reset(StreamingLevel->LODPackageNames.Num());
			for (FName& LODPackageName : StreamingLevel->LODPackageNames)
			{
				// Store LOD level original package name
				StreamingLevel->LODPackageNamesToLoad.Add(LODPackageName);

				// Apply Instance prefix to package name			
				const FName NonPrefixedLODPackageName = LODPackageName;
				LODPackageName = FName(*InstanceWorldHelpers::ConvertToInstancePackageName(LODPackageName.ToString(), InstanceID));
				InstanceWorldHelpers::AddInstancePackageName(LODPackageName);
			}
		}
	}

	static void RedirectObjectSoftReferencesToInstance(UObject* Object, int32 InstanceID)
	{
		struct FSoftPathInstanceFixupSerializer : public FArchiveUObject
		{
			int32 InstanceID;

			FSoftPathInstanceFixupSerializer(int32 InInstanceID)
				: InstanceID(InInstanceID)
			{
				this->SetIsSaving(true);
			}

			static void FixupForInstance(FSoftObjectPath& SoftPath, int32 InstanceID)
			{
				if (InstanceID != INDEX_NONE && !SoftPath.IsNull())
				{
					const FString Path = SoftPath.ToString();

					// Determine if this reference has already been fixed up for instance
					if (!HasInstancePrefix(Path))
					{
						// Name of the ULevel subobject of UWorld, set in InitializeNewWorld
						const bool bIsChildOfLevel = SoftPath.GetSubPathString().StartsWith(TEXT("PersistentLevel."));

						const FString ShortPackageOuterAndName = FPackageName::GetLongPackageAssetName(Path);

						FString InstancePath;
						if (GEngine->IsEditor())
						{
							// TODO: For some reason, we get the INST_X prefix on both the package and the object in the editor so we need to some some extra fiddling with the path.
							FString PackageName;
							FString ObjectName;
							ShortPackageOuterAndName.Split(".", &PackageName, &ObjectName);
							const FString PrefixedPackageName = FString::Printf(TEXT("%s%s"), *InstanceWorldHelpers::BuildInstancePackagePrefix(InstanceID), *PackageName);
							const FString PrefixedObjectName = FString::Printf(TEXT("%s%s"), *InstanceWorldHelpers::BuildInstancePackagePrefix(InstanceID), *ObjectName);
							const FString PrefixedShortPackageOuterAndName = FString::Printf(TEXT("%s.%s"), *PrefixedPackageName, *PrefixedObjectName);
							InstancePath = FString::Printf(TEXT("%s/%s"), *FPackageName::GetLongPackagePath(Path), *PrefixedShortPackageOuterAndName);
						}
						else
						{
							InstancePath = FString::Printf(TEXT("%s/%s%s"), *FPackageName::GetLongPackagePath(Path), *InstanceWorldHelpers::BuildInstancePackagePrefix(InstanceID), *ShortPackageOuterAndName);
						}

						const FName InstancePackage = (!bIsChildOfLevel ? FName(*FPackageName::ObjectPathToPackageName(InstancePath)) : NAME_None);

						// Duplicate if this an already registered Instance package or this looks like a level subobject reference
						if (bIsChildOfLevel || InstancePackageNames.Contains(InstancePackage))
						{
							// Need to prepend Instance prefix, as we're in a Instance world and this refers to an object in an Instance package
							SoftPath.SetPath(MoveTemp(InstancePath));
						}
					}
				}
			};

			FArchive& operator<<(FSoftObjectPath& Value)
			{
				FixupForInstance(Value, InstanceID);
				return *this;
			}
		};

		/*
			TODO: Do we want to recursively fix up all sub-objects?
		*/

		FSoftPathInstanceFixupSerializer FixupSerializer(InstanceID);

		TArray<UObject*> SubObjects;
		GetObjectsWithOuter(Object, SubObjects);
		for (UObject* SubObject : SubObjects)
			SubObject->Serialize(FixupSerializer);

		Object->Serialize(FixupSerializer);
	}

	static void RenameToInstanceWorld(UWorld* World, int32 InstanceID)
	{
		if (World->WorldComposition)
		{
			InstanceWorldHelpers::ReinitializeWorldCompositionForInstance(World->WorldComposition, InstanceID);
		}

		for (ULevelStreaming* LevelStreaming : World->GetStreamingLevels())
		{
			RenameSteamingLevelForInstance(LevelStreaming, InstanceID);
		}

		InstanceWorldHelpers::RedirectObjectSoftReferencesToInstance(World->PersistentLevel, InstanceID);
	}

	static UWorld* CreateInstanceWorldByLoadingFromPackage(FSoftObjectPath WorldAsset, int32 InstanceID)
	{
		const FString WorldPackageName = UWorld::RemovePIEPrefix(FPackageName::ObjectPathToPackageName(WorldAsset.ToString()));
		const FString InstancePackageName = InstanceWorldHelpers::ConvertToInstancePackageName(WorldPackageName, InstanceID);

		// Set the world type in the static map, so that UWorld::PostLoad can set the world type
		UWorld::WorldTypePreLoadMap.FindOrAdd(*InstancePackageName) = EWorldType::GamePreview;
		InstanceWorldHelpers::AddInstancePackageName(*InstancePackageName);

		// Loads the contents of "WorldPackageName" into a new package "NewWorldPackage"
		UPackage* InstancePackage = CreatePackage(*InstancePackageName);
		InstancePackage->SetFlags(EObjectFlags::RF_Transient);
		UPackage* NewWorldPackage = LoadPackage(InstancePackage, *WorldPackageName, LOAD_None);

		// Clean up the world type list now that PostLoad has occurred
		UWorld::WorldTypePreLoadMap.Remove(*InstancePackageName);

		if (NewWorldPackage == nullptr)
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("Failed to load world package %s"), *WorldPackageName);
			return nullptr;
		}

		UWorld* NewWorld = UWorld::FindWorldInPackage(NewWorldPackage);

		// If the world was not found, follow a redirector if there is one.
		if (!NewWorld)
		{
			NewWorld = UWorld::FollowWorldRedirectorInPackage(NewWorldPackage);
			if (NewWorld)
				NewWorldPackage = NewWorld->GetOutermost();
		}

		if (!NewWorld)
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("Could not find a world in package %s"), *InstancePackageName);
			return nullptr;
		}

		InstanceWorldHelpers::RenameToInstanceWorld(NewWorld, InstanceID);

		return NewWorld;
	}
};

void UThumbnailBackgroundLevelStreamingFixer::SetStreamingLevel(ULevelStreaming* InLevelStreaming, int32 InInstanceID)
{
	if (!IsValid(InLevelStreaming))
	{
		UE_LOG(LogThumbnailGenerator, Error, TEXT("UThumbnailBackgroundLevelStreamingFixer::SetStreamingLevel, received invalid LevelStreaming"));
		return;
	}

	if (InInstanceID == INDEX_NONE)
	{
		UE_LOG(LogThumbnailGenerator, Error, TEXT("UThumbnailBackgroundLevelStreamingFixer::SetStreamingLevel, received invalid instance world index"));
		return;
	}

	LevelStreaming = InLevelStreaming;
	InstanceID = InInstanceID;
	LevelStreaming->OnLevelShown.AddDynamic(this, &UThumbnailBackgroundLevelStreamingFixer::OnLevelShown);
}

void UThumbnailBackgroundLevelStreamingFixer::OnLevelShown()
{
	if (!IsValid(LevelStreaming))
	{
		UE_LOG(LogThumbnailGenerator, Error, TEXT("UThumbnailBackgroundLevelStreamingFixer::OnLevelShown, LevelStreaming has been destroyed"));
		return;
	}

	InstanceWorldHelpers::RedirectObjectSoftReferencesToInstance(LevelStreaming->GetLoadedLevel(), InstanceID);
}

FThumbnailBackgroundScene::FThumbnailBackgroundScene(const FThumbnailBackgroundSceneSettings &BackgroundSceneSettings)
	: SceneSettings(BackgroundSceneSettings)
{
	const FThumbnailSettings& DefaultSettings = UThumbnailGeneratorSettings::Get()->DefaultThumbnailSettings;

	const FSoftObjectPath BackgroundWorldPath = SceneSettings.BackgroundWorld.ToSoftObjectPath();
	
	// Create the world
	{
		const auto CreateInstancedWorld = [&]()->UWorld*
		{
			UWorld* World = InstanceWorldHelpers::CreateInstanceWorldByLoadingFromPackage(BackgroundWorldPath, InstanceID.GetID());
			checkf(World, TEXT("Unable to load UWorld asset %s"), *BackgroundWorldPath.ToString());

			World->AddToRoot();

			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::GamePreview);
			WorldContext.SetCurrentWorld(World);

			// TODO: I do not believe that sharing the GameInstance with the main world would work, need to investigate this
			//~ World->SetGameInstance(WorldContext.OwningGameInstance);

			FURL URL;
			URL.Map = World->GetOutermost()->GetName();
			WorldContext.LastURL = URL;

			// Register LevelStreamingFixers (Will fixup the sub-level references one the levels are loaded/shown)
			for (ULevelStreaming* LevelStreaming : World->GetStreamingLevels())
			{
				if (UThumbnailBackgroundLevelStreamingFixer* LevelStreamingFixer = NewObject<UThumbnailBackgroundLevelStreamingFixer>(LevelStreaming))
					LevelStreamingFixer->SetStreamingLevel(LevelStreaming, InstanceID.GetID());
			}

			return World;
		};

		const auto InitWorld = [&](UWorld* World)
		{
			const FWorldContext& WorldContext = *GEngine->GetWorldContextFromWorld(World);
			const FURL& URL = WorldContext.LastURL;

			UWorld::InitializationValues IVS = UWorld::InitializationValues()
				.InitializeScenes(true)
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.SetTransactional(true)
				.CreateFXSystem(true);

			World->InitWorld(IVS);

			// Process global shader results before we try to render anything
			// Do this before we register components, as USkinnedMeshComponents require the GPU skin cache global shaders when creating render state.
			if (GShaderCompilingManager)
			{
				GShaderCompilingManager->ProcessAsyncResults(false, true);
			}

			// load any per-map packages
			check(World->PersistentLevel);
			GEngine->LoadPackagesFully(World, FULLYLOAD_Map, World->PersistentLevel->GetOutermost()->GetName());

			// Make sure "always loaded" sub-levels are fully loaded
			World->FlushLevelStreaming(EFlushLevelStreamingType::Visibility);

			if (!GIsEditor && !IsRunningDedicatedServer())
			{
				// If requested, duplicate dynamic levels here after the source levels are created.
				World->DuplicateRequestedLevels(FName(*URL.Map));
			}

			World->InitializeActorsForPlay(URL);

			FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);

			World->bWorldWasLoadedThisTick = true;

			// We want to update streaming immediately so that there's no tick prior to processing any levels that should be initially visible
			// that requires calculating the scene, so redraw everything now to take care of it all though don't present the frame.
			GEngine->RedrawViewports(false);

			// RedrawViewports() may have added a dummy playerstart location. Remove all views to start from fresh the next Tick().
			//~ IStreamingManager::Get().RemoveStreamingViews(RemoveStreamingViews_All); // RemoveStreamingViews is not tagged with ENGINE_API, causing unresolved external symbols

			World->UpdateAllSkyCaptures();
		};

		double StartTime = FPlatformTime::Seconds();
		BackgroundWorld = CreateInstancedWorld();
		if (!BackgroundWorld)
		{
			UE_LOG(LogThumbnailGenerator, Error, TEXT("Failed to create instanced world from %s"), *BackgroundWorldPath.ToString());
			return;
		}

		InitWorld(BackgroundWorld);

		double StopTime = FPlatformTime::Seconds();
		UE_LOG(LogThumbnailGenerator, Verbose, TEXT("Took %f seconds to LoadMap(%s)"), StopTime - StartTime, *BackgroundWorld->GetName());

		// Disable ticking by the engine since we manage ticking on our own
		BackgroundWorld->SetShouldTick(false);
	}

	// Initialize scene lighting
	{
		const auto InitSkyLight = [&](USkyLightComponent* InSkyLight)
		{
			if ((SkyLight = InSkyLight) == nullptr)
				return;

			if (!SkyLight->IsRegistered()) 
				SkyLight->RegisterComponentWithWorld(BackgroundWorld);

			SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
			SkyLight->Intensity  = DefaultSettings.SkyLightIntensity;
			SkyLight->LightColor = DefaultSettings.SkyLightColor.ToFColor(true);
			SkyLight->Mobility   = EComponentMobility::Movable;
			SkyLight->Cubemap    = DefaultSettings.EnvironmentCubeMap.LoadSynchronous();
			SkyLight->MarkRenderStateDirty();
			SkyLight->SetCaptureIsDirty();
		};

		const auto InitDirectionalLight = [&](UDirectionalLightComponent* InDirectionalLight)
		{
			if ((DirectionalLight = InDirectionalLight) == nullptr)
				return;

			if (!DirectionalLight->IsRegistered()) 
				DirectionalLight->RegisterComponentWithWorld(BackgroundWorld);

			DirectionalLight->LightColor = DefaultSettings.DirectionalLightColor.ToFColor(true);
			DirectionalLight->Intensity  = DefaultSettings.DirectionalLightIntensity;
			DirectionalLight->Mobility   = EComponentMobility::Movable;
			DirectionalLight->SetAbsolute(true, true, true);
			DirectionalLight->SetRelativeRotation(DefaultSettings.DirectionalLightRotation);
			DirectionalLight->UpdateColorAndBrightness();
			DirectionalLight->MarkRenderStateDirty();
		};

		const auto InitDirectionalFillLight = [&](UDirectionalLightComponent* InDirectionalFillLight)
		{
			if ((DirectionalFillLight = InDirectionalFillLight) == nullptr)
				return;

			if (!DirectionalFillLight->IsRegistered()) 
				DirectionalFillLight->RegisterComponentWithWorld(BackgroundWorld);

			DirectionalFillLight->LightColor = DefaultSettings.DirectionalFillLightColor.ToFColor(true);
			DirectionalFillLight->Intensity  = DefaultSettings.DirectionalFillLightIntensity;
			DirectionalFillLight->Mobility   = EComponentMobility::Movable;
			DirectionalFillLight->SetAbsolute(true, true, true);
			DirectionalFillLight->SetRelativeRotation(DefaultSettings.DirectionalFillLightRotation);
			DirectionalFillLight->UpdateColorAndBrightness();
			DirectionalFillLight->MarkRenderStateDirty();
		};

		const auto SourceSkyLight = [&]()
		{
			USkyLightComponent* SkyLightComponent = nullptr;
			ForEachObjectOfClass(USkyLightComponent::StaticClass(), [&](UObject* Object)
			{
				if (Object->GetWorld() != BackgroundWorld)
					return;

				if (!SkyLightComponent)
					SkyLightComponent = Cast<USkyLightComponent>(Object);

			}, true, RF_ClassDefaultObject, EInternalObjectFlags::Garbage);

			return SkyLight;
		};

		const auto SourceDirectionalLights = [&]()->TArray<UDirectionalLightComponent*>
		{
			TArray<UDirectionalLightComponent*> OutDirectionalLights;
			ForEachObjectOfClass(UDirectionalLightComponent::StaticClass(), [&](UObject* Object)
			{
				if (Object->GetWorld() != BackgroundWorld)
					return;

				if (UDirectionalLightComponent* DirectionalLightComponent = Cast<UDirectionalLightComponent>(Object))
					OutDirectionalLights.Add(DirectionalLightComponent);

			}, true, RF_ClassDefaultObject, EInternalObjectFlags::Garbage);
			return OutDirectionalLights;
		};

		switch (SceneSettings.SpawnLightsMode)
		{
		case EBackgroundWorldLightMode::ESpawnLights:
		{
			InitSkyLight(NewObject<USkyLightComponent>(GetTransientPackage(), NAME_None, RF_Transient));
			InitDirectionalLight(NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient));
			InitDirectionalFillLight(NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		}
		case EBackgroundWorldLightMode::ESourceFromWorld:
		{
			InitSkyLight(SourceSkyLight());
			const auto DirectionalLights = SourceDirectionalLights();
			InitDirectionalLight(DirectionalLights.Num() > 0 ? DirectionalLights[0] : nullptr);
			InitDirectionalFillLight(DirectionalLights.Num() > 1 ? DirectionalLights[1] : nullptr);
		}
		case EBackgroundWorldLightMode::ESourceSkyLight:
		{
			InitSkyLight(SourceSkyLight());
		}
		case EBackgroundWorldLightMode::ESourceDirectionalLights:
		{
			const auto DirectionalLights = SourceDirectionalLights();
			InitDirectionalLight(DirectionalLights.Num() > 0 ? DirectionalLights[0] : nullptr);
			InitDirectionalFillLight(DirectionalLights.Num() > 1 ? DirectionalLights[1] : nullptr);
		}
		case EBackgroundWorldLightMode::ESourceAvailableSpawnRest:
		{
			USkyLightComponent* SourcedSkyLightComponent = SourceSkyLight();
			InitSkyLight(SourcedSkyLightComponent ? SourcedSkyLightComponent : NewObject<USkyLightComponent>(GetTransientPackage(), NAME_None, RF_Transient));

			const auto DirectionalLights = SourceDirectionalLights();

			UDirectionalLightComponent* SourcedDirectionalLight = DirectionalLights.Num() > 0 ? DirectionalLights[0] : nullptr;
			InitDirectionalLight(SourcedDirectionalLight ? SourcedDirectionalLight : NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient));

			UDirectionalLightComponent* SourcedDirectionalFillLight = DirectionalLights.Num() > 1 ? DirectionalLights[1] : nullptr;
			InitDirectionalFillLight(SourcedDirectionalFillLight ? SourcedDirectionalFillLight : NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		}
		case EBackgroundWorldLightMode::EIgnoreLights:
		default:
			break;
		}
	}

	UpdateScene(UThumbnailGeneratorSettings::Get()->DefaultThumbnailSettings, true);
}

FThumbnailBackgroundScene::~FThumbnailBackgroundScene()
{
	// World cleanup, based on UEngine::LoadMap
	if (GEngine && BackgroundWorld)
	{
		BackgroundWorld->bIsLevelStreamingFrozen = false;

		// clean up any per-map loaded packages for the map we are leaving
		if (BackgroundWorld && BackgroundWorld->PersistentLevel)
		{
			GEngine->CleanupPackagesToFullyLoad(*GetWorldContext(), FULLYLOAD_Map, BackgroundWorld->PersistentLevel->GetOutermost()->GetName());
		}

		BackgroundWorld->bIsTearingDown = true;

		// TODO: Do we want to call end-play on the actors?
		//for (FActorIterator ActorIt(BackgroundWorld); ActorIt; ++ActorIt)
		//{
		//	if (!ActorIt->IsUnreachable())
		//		ActorIt->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
		//}

		BackgroundWorld->CleanupWorld();

		GEngine->WorldDestroyed(BackgroundWorld);

		BackgroundWorld->RemoveFromRoot();

		// Mark everything else contained in the world to be deleted
		for (auto LevelIt(BackgroundWorld->GetLevelIterator()); LevelIt; ++LevelIt)
		{
			if (const ULevel* Level = *LevelIt)
			{
				CastChecked<UWorld>(Level->GetOuter())->MarkObjectsPendingKill();
			}
		}

		for (ULevelStreaming* LevelStreaming : BackgroundWorld->GetStreamingLevels())
		{
			// If an unloaded levelstreaming still has a loaded level we need to mark its objects to be deleted as well
			if (LevelStreaming->GetLoadedLevel() && (!LevelStreaming->ShouldBeLoaded() || !LevelStreaming->ShouldBeVisible()))
			{
				CastChecked<UWorld>(LevelStreaming->GetLoadedLevel()->GetOuter())->MarkObjectsPendingKill();
			}
		}

		GEngine->DestroyWorldContext(BackgroundWorld);

		// trim memory to clear up allocations from the previous level (also flushes rendering)
		//GEngine->TrimMemory(); // TODO: Causes assert, how important is this?
	}
}

FWorldContext* FThumbnailBackgroundScene::GetWorldContext() const
{
	return GEngine->GetWorldContextFromWorld(BackgroundWorld);
}

void FThumbnailBackgroundScene::UpdateScene(const FThumbnailSettings& ThumbnailSettings, bool bForceUpdate)
{
	bool bSkyChanged = false;

	bSkyChanged |= FThumbnailPreviewScene::UpdateLightSources(ThumbnailSettings, DirectionalLight, DirectionalFillLight, SkyLight, bForceUpdate);

	if (SceneSettings.bSpawnSkySphere)
	{
		// Check if environment color has changed
		if (bForceUpdate || !LastEnvironmentColor.Equals(ThumbnailSettings.EnvironmentColor))
		{
			bSkyChanged = true;
			LastEnvironmentColor = ThumbnailSettings.EnvironmentColor;
		}

		bSkyChanged |= FThumbnailPreviewScene::UpdateSkySphere(ThumbnailSettings, GetThumbnailWorld(), &SkySphereActor, bForceUpdate || bSkyChanged);
	}

	if (bSkyChanged)
	{
		SkyLight->SetCaptureIsDirty();
		SkyLight->MarkRenderStateDirty();
		SkyLight->UpdateSkyCaptureContents(BackgroundWorld);
	}
}

FString FThumbnailBackgroundScene::GetDebugName() const
{
	UWorld* ThumbnailWorld = GetThumbnailWorld();
	return ThumbnailWorld ? *ThumbnailWorld->GetOutermost()->GetName() : *FString::Printf(TEXT("EmptyBackgroundScene_%d"), InstanceID.GetID());
}

void FThumbnailBackgroundScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DirectionalLight);
	Collector.AddReferencedObject(DirectionalFillLight);
	Collector.AddReferencedObject(SkyLight);
	Collector.AddReferencedObject(SkySphereActor);
	Collector.AddReferencedObject(BackgroundWorld);
}

FString FThumbnailBackgroundScene::GetReferencerName() const
{
	return FString::Printf(TEXT("ThumbnailBackgroundScene_%s"), *GetDebugName());
}