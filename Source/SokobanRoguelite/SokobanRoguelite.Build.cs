using UnrealBuildTool;
public class SokobanRoguelite : ModuleRules
{
    public SokobanRoguelite(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "ProceduralMeshComponent" });
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "SokobanRules", "DeveloperSettings" });
    }
}
