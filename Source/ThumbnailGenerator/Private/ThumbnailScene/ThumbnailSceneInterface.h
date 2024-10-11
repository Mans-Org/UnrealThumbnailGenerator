// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "UObject/ObjectPtr.h"

class AActor;

class FThumbnailSceneInterface
{
public:
	virtual void UpdateScene(const struct FThumbnailSettings& ThumbnailSettings, bool bForceUpdate = false) = 0;

	virtual class UWorld* GetThumbnailWorld() const = 0;

	virtual TSet<TObjectPtr<AActor>> GetPersistentActors() const { return TSet<TObjectPtr<AActor>>(); }

	virtual FString GetDebugName() const = 0;
};