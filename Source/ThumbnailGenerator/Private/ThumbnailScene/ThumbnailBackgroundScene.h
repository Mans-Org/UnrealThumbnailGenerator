// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "ThumbnailSceneInterface.h"
#include "ThumbnailGeneratorSettings.h"
#include "UObject/GCObject.h"
#include "ThumbnailBackgroundScene.generated.h"

UCLASS()
class UThumbnailBackgroundLevelStreamingFixer : public UObject
{
	GENERATED_BODY()
private:
	
	UPROPERTY()
	class ULevelStreaming* LevelStreaming;

	int32 InstanceID;

public:
	void SetStreamingLevel(ULevelStreaming* InLevelStreaming, int32 InInstanceID);

	UFUNCTION()
	void OnLevelShown();
};

class FThumbnailBackgroundScene : public FThumbnailSceneInterface, public FGCObject
{
private:
	TObjectPtr<class UDirectionalLightComponent> DirectionalLight     = nullptr;
	TObjectPtr<class UDirectionalLightComponent> DirectionalFillLight = nullptr;
	TObjectPtr<class USkyLightComponent>         SkyLight             = nullptr;
	TObjectPtr<class AActor>                     SkySphereActor       = nullptr;

	TObjectPtr<class UWorld> BackgroundWorld = nullptr;

	FLinearColor LastEnvironmentColor = FLinearColor::White;

	FThumbnailBackgroundSceneSettings SceneSettings;

	struct FInstanceID // Simple class for generating IDs with a preference for lower values
	{
	private:
		static TSet<int32>& TakenIDs() { static TSet<int32> IDs; return IDs; }
		int32 UniqueID;

	public:
		FInstanceID()
		{
			int32 ID = 0;
			while (true)
			{
				if (!TakenIDs().Contains(ID))
				{
					UniqueID = ID;
					TakenIDs().Add(UniqueID);
					break;
				}
				ID++;
			}
		}

		~FInstanceID() { TakenIDs().Remove(UniqueID); }

		FORCEINLINE int32 GetID() const { return UniqueID; }
	} InstanceID;

public:

	FThumbnailBackgroundScene(const FThumbnailBackgroundSceneSettings &BackgroundSceneSettings);
	virtual ~FThumbnailBackgroundScene();
	
	FWorldContext* GetWorldContext() const;

	/** Begin FThumbnailSceneInterface */
	virtual void UpdateScene(const FThumbnailSettings& ThumbnailSettings, bool bForceUpdate = false) override;
	virtual class UWorld* GetThumbnailWorld() const override { return BackgroundWorld; };
	virtual FString GetDebugName() const;
	/** End FThumbnailSceneInterface*/

	// ~Begin: FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	// ~End: FGCObject Interface
};