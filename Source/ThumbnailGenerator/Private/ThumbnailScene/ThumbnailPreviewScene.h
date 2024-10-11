// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "ThumbnailScene/ThumbnailSceneInterface.h"
#include "PreviewScene.h"

class FThumbnailPreviewScene : public FPreviewScene, public FThumbnailSceneInterface
{
private:
	TObjectPtr<class AActor>                     SkySphereActor       = nullptr;
	TObjectPtr<class UDirectionalLightComponent> DirectionalFillLight = nullptr;

	FLinearColor LastEnvironmentColor = FLinearColor::White;

public:

	FThumbnailPreviewScene();

	/* returns true if the sky light has changed */
	static bool UpdateLightSources(const FThumbnailSettings& ThumbnailSettings, class UDirectionalLightComponent* DirectionalLight, 
		class UDirectionalLightComponent* DirectionalFillLight, USkyLightComponent* SkyLight, bool bForceUpdate);

	static bool UpdateSkySphere(const FThumbnailSettings& ThumbnailSettings, UWorld* World, TObjectPtr<AActor>* SkySphereActorPtr, bool bForceUpdate);

	/** Begin FThumbnailSceneInterface */
	virtual void UpdateScene(const FThumbnailSettings& ThumbnailSettings, bool bForceUpdate = false) override;

	virtual class UWorld* GetThumbnailWorld() const override { return GetWorld(); };

	virtual TSet<TObjectPtr<AActor>> GetPersistentActors() const override { return TSet<TObjectPtr<AActor>>({ SkySphereActor }); }

	virtual FString GetDebugName() const override;
	/** End FThumbnailSceneInterface*/

	/** Begin FPreviewScene */
	virtual FLinearColor GetBackgroundColor() const override {return FColor(0, 0, 0, 0); }
	/** End FPreviewScene */

	/** Begin FGCObject */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	/** End FGCObject */
};