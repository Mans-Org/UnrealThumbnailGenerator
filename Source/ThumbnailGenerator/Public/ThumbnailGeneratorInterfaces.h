// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ThumbnailGeneratorSettings.h"
#include "ThumbnailGeneratorInterfaces.generated.h"


UINTERFACE(MinimalAPI)
class UThumbnailSceneInterface : public UInterface
{
	GENERATED_BODY()
};

class THUMBNAILGENERATOR_API IThumbnailSceneInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Thumbnail Generator")
	void OnUpdateThumbnailScene(const FThumbnailSettings& ThumbnailSettings);

};


UINTERFACE(MinimalAPI)
class UThumbnailActorInterface : public UInterface
{
	GENERATED_BODY()
};

class THUMBNAILGENERATOR_API IThumbnailActorInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Thumbnail Actor")
	void PreCaptureActorThumbnail();

	UFUNCTION(BlueprintNativeEvent, Category = "Thumbnail Actor")
	FTransform GetThumbnailTransform() const;

};