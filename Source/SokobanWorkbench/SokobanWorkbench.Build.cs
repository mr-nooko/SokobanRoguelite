using UnrealBuildTool;

public class SokobanWorkbench : ModuleRules
{
    public SokobanWorkbench(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "InputCore", "UnrealEd", "LevelEditor", "EngineSettings",
            "Json", "JsonUtilities", "ToolMenus", "AssetTools", "AssetRegistry", "ContentBrowser", "ContentBrowserData", "PropertyEditor", "Settings", "SokobanRoguelite", "SokobanRules"
        });
    }
}

