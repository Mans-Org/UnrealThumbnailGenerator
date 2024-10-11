// Copyright Mans Isaksson. All Rights Reserved.

using UnrealBuildTool;

public class ThumbnailGeneratorEditor : ModuleRules
{
    public ThumbnailGeneratorEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
		});

        PrivateDependencyModuleNames.AddRange(new string[]
        {
			"ThumbnailGenerator",
			"PropertyEditor",
            "InputCore",
			"Slate",
			"SlateCore",

            "GraphEditor",     // For the SNameComboBox
            "UnrealEd",        // For Save Thumbnail
            "DesktopPlatform", // For Export Thumbnail
            "ImageWriteQueue", // For Export Thumbnail (PNG/EXR)
            "LevelEditor",     // For level viewport right-click, generate thumbnail
            "ContentBrowser",  // For content browser right-click, generate thumbnail
            "EditorWidgets",   // For the SThumbnailGeneratorEditor
			"EditorStyle",     // For the SThumbnailGeneratorEditor
            "WorkspaceMenuStructure",
        });
    }
}
