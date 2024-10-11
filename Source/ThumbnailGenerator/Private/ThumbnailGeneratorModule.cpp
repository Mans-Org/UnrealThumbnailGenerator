// Copyright Mans Isaksson. All Rights Reserved.

#include "ThumbnailGeneratorModule.h"
#include "ThumbnailGenerator.h"

#include "Misc/CoreDelegates.h"

DEFINE_LOG_CATEGORY(LogThumbnailGenerator);

#if WITH_EDITOR
FThumbnailGeneratorModule::FSaveThumbnailDelegate FThumbnailGeneratorModule::SaveThumbnailDelegate;
#endif

FThumbnailGenerator* GThumbnailGenerator = nullptr;

namespace ThumbnailAssetPaths
{
	const TCHAR* CubeMap                              = TEXT("TextureCube'/ThumbnailGenerator/SkySphere/T_Thumbnail_CubeMap.T_Thumbnail_CubeMap'");
	const TCHAR* SkySphere                            = TEXT("/ThumbnailGenerator/SkySphere/BP_ThumbnailGenerator_SkySphere.BP_ThumbnailGenerator_SkySphere_C");
	const TCHAR* CustomDepthScript                    = TEXT("/ThumbnailGenerator/BP_Thumbnail_CustomDepth_Script.BP_Thumbnail_CustomDepth_Script_C");
	const TCHAR* OutlinePostProcessMaterial_NoAlpha   = TEXT("/ThumbnailGenerator/ThumbnailOutline/PP_Thumbnail_Outliner_NoAlpha");
	const TCHAR* OutlinePostProcessMaterial_WithAlpha = TEXT("/ThumbnailGenerator/ThumbnailOutline/PP_Thumbnail_Outliner_WithAlpha");
	const TCHAR* SilhuettPostProcessMaterial          = TEXT("/ThumbnailGenerator/Silhuett/PP_Thumbnail_Silhuett");
};

void FThumbnailGeneratorModule::StartupModule()
{
	FCoreDelegates::OnPreExit.AddRaw(this, &FThumbnailGeneratorModule::Cleanup);

	if (GThumbnailGenerator == nullptr)
	{
		GThumbnailGenerator = new FThumbnailGenerator(true);
	}
}

void FThumbnailGeneratorModule::ShutdownModule()
{
	FCoreDelegates::OnPreExit.RemoveAll(this);
	Cleanup();
}

void FThumbnailGeneratorModule::Cleanup()
{
	if (GThumbnailGenerator != nullptr)
	{
		delete GThumbnailGenerator;
		GThumbnailGenerator = nullptr;
	}
}

IMPLEMENT_MODULE(FThumbnailGeneratorModule, ThumbnailGenerator)