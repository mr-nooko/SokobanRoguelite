using UnrealBuildTool;
public class SokobanRules : ModuleRules
{
    public SokobanRules(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core" });
    }
}
