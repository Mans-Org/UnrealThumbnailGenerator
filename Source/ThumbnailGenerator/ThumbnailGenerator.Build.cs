// Copyright Mans Isaksson. All Rights Reserved.

using UnrealBuildTool;

public class ThumbnailGenerator : ModuleRules
{
    public ThumbnailGenerator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
			"Slate",
            "RHI",
            "UMG",
            "RenderCore"
        });

        if (Target.bBuildEditor == true)
        {
            PublicDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd"
            });
        }
    }
}
