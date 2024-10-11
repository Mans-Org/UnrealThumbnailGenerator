// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Delegates/DelegateCombinations.h"
#include "Logging/LogMacros.h"

class UTexture2D;

DECLARE_LOG_CATEGORY_EXTERN(LogThumbnailGenerator, Log, All);

namespace ThumbnailAssetPaths
{
	extern const TCHAR* CubeMap;
	extern const TCHAR* SkySphere; 
	extern const TCHAR* CustomDepthScript;
	extern const TCHAR* OutlinePostProcessMaterial_NoAlpha;
	extern const TCHAR* OutlinePostProcessMaterial_WithAlpha;
	extern const TCHAR* SilhuettPostProcessMaterial;
};

class FThumbnailGeneratorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if WITH_EDITOR
	DECLARE_DELEGATE_RetVal_ThreeParams(UTexture2D*, FSaveThumbnailDelegate, UTexture2D*, const FString&, const FString&);
	THUMBNAILGENERATOR_API static FSaveThumbnailDelegate SaveThumbnailDelegate;
#endif

private:
	void Cleanup();
};
