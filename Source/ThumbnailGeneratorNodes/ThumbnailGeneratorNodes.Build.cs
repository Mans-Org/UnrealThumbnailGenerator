// Copyright Mans Isaksson. All Rights Reserved.

using UnrealBuildTool;

public class ThumbnailGeneratorNodes : ModuleRules
{
    public ThumbnailGeneratorNodes(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "ThumbnailGenerator"
        });

        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(new string[] 
            {
                "UnrealEd",
                "BlueprintGraph",
                "KismetCompiler",
                "Kismet"
            });
        }
    }
}
