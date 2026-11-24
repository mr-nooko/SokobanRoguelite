#include "Modules/ModuleManager.h"
#include "GridLevelAsset.h"
#include "GridProjectSettings.h"
#include "ISettingsModule.h"
#include "GridCampaignSequence.h"
#include "GridGameMode.h"
#include "GameMapsSettings.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Containers/Ticker.h"
#include "GridLevelBrowser.h"
#include "GridStructureAsset.h"
#include "ObjectTools.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "GridVisualProfile.h"
#include "PropertyCustomizationHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "GridScene.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "EditorUndoClient.h"
#include "Editor/TransBuffer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Factories/DataAssetFactory.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SVirtualWindow.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Misc/PackageName.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Widgets/SBoxPanel.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SokobanWorkbench"

class FGridWorkbenchTabTest;

namespace
{
const FName TabId(TEXT("SokobanGridEditor"));
enum class EStructureTool : uint8 { None, Capture, Place, Paste };

FIntRect GridSelectionRect(FIntPoint Start, FIntPoint End)
{
    return FIntRect(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y),
        FMath::Max(Start.X, End.X) + 1, FMath::Max(Start.Y, End.Y) + 1);
}

void SelectCells(TSet<FIntPoint>& Cells, FIntRect Rect, bool bAdd)
{
    if (!bAdd) { Cells.Reset(); }
    for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
    {
        for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X) { Cells.Add(FIntPoint(X, Y)); }
    }
}
const TCHAR* WorkbenchMap = TEXT("/Game/Maps/GridWorkbench");

FString GetLaunchMap()
{
    return FPackageName::ObjectPathToPackageName(UGameMapsSettings::GetGameDefaultMap());
}

bool OpenEditorMap(const FString& PackageName)
{
    if (!GEditor || GEditor->PlayWorld) { return false; }
    const auto* World = GEditor->GetEditorWorldContext().World();
    if (World && World->GetOutermost()->GetName() == PackageName) { return true; }
    FString Filename;
    if (!FPackageName::DoesPackageExist(PackageName, &Filename) ||
        !FEditorFileUtils::SaveDirtyPackages(true, true, true))
    {
        return false;
    }
    return FEditorFileUtils::LoadMap(Filename);
}

void PlayLaunchMap()
{
    if (!GEditor || GEditor->PlayWorld) { return; }
    FRequestPlaySessionParams Params;
    Params.GlobalMapOverride = GetLaunchMap();
    GEditor->RequestPlaySession(Params);
}

void OpenWorkbench()
{
    if (GEditor && !GEditor->PlayWorld)
    {
        const auto* World = GEditor->GetEditorWorldContext().World();
        if (World && World->GetOutermost()->GetName() == GetLaunchMap() && !OpenEditorMap(WorkbenchMap)) { return; }
    }
    FGlobalTabmanager::Get()->TryInvokeTab(TabId);
}

enum class EBrush : uint8 { Start, Goal, Wall, Rubble, Box, Spike, Blast, Eject, Enemy, Laser, Rotate, Erase, CutCell, RestoreCell };
enum class EMovePiece : uint8 { Start, Goal, Wall, Rubble, Box, Enemy };

struct FComponentRow
{
    FIntPoint Cell;
    FString Label;
};

struct FGridMovePiece
{
    EMovePiece Kind;
    FIntPoint Cell;
    FIntPoint Spike = FIntPoint::ZeroValue;
    int32 Index = INDEX_NONE;

    void ForEachCell(TFunctionRef<void(FIntPoint)> Visit) const
    {
        Visit(Cell);
        if (Spike != FIntPoint::ZeroValue) { Visit(Cell + Spike); }
    }
};

void ForEachMovePiece(const UGridLevelAsset& Draft, TFunctionRef<void(FGridMovePiece)> Visit)
{
    Visit({EMovePiece::Start, Draft.Start});
    Visit({EMovePiece::Goal, Draft.Goal});
    for (const FIntPoint Cell : Draft.Walls) { Visit({EMovePiece::Wall, Cell}); }
    for (const FIntPoint Cell : Draft.Rubble) { Visit({EMovePiece::Rubble, Cell}); }
    for (int32 Index = 0; Index < Draft.Boxes.Num(); ++Index)
    {
        const auto& Box = Draft.Boxes[Index];
        Visit({EMovePiece::Box, Box.Cell, Box.Spike, Index});
    }
    for (int32 Index = 0; Index < Draft.Enemies.Num(); ++Index)
    {
        Visit({EMovePiece::Enemy, Draft.Enemies[Index].Cell, FIntPoint::ZeroValue, Index});
    }
}

TOptional<FGridMovePiece> FindMovePiece(const UGridLevelAsset& Draft, FIntPoint Cell)
{
    TOptional<FGridMovePiece> Piece;
    int32 Matches = 0;
    ForEachMovePiece(Draft, [&](FGridMovePiece Candidate) {
        if (Cell != Candidate.Cell && Cell != Candidate.Cell + Candidate.Spike) { return; }
        Piece = Candidate;
        ++Matches;
    });
    return Matches == 1 ? Piece : TOptional<FGridMovePiece>();
}

enum class EBrushGroup { Terrain, Entities, Editing, BoardShape };

struct FBrushEntry
{
    EBrush Brush;
    EBrushGroup Group;
    const TCHAR* Label;
};

const FBrushEntry Brushes[] = {
    {EBrush::Start, EBrushGroup::Terrain, TEXT("起点")},
    {EBrush::Goal, EBrushGroup::Terrain, TEXT("终点")},
    {EBrush::Wall, EBrushGroup::Terrain, TEXT("墙")},
    {EBrush::Rubble, EBrushGroup::Terrain, TEXT("碎墙")},
    {EBrush::Box, EBrushGroup::Entities, TEXT("普通箱")},
    {EBrush::Spike, EBrushGroup::Entities, TEXT("尖刺箱")},
    {EBrush::Blast, EBrushGroup::Entities, TEXT("爆炸箱")},
    {EBrush::Eject, EBrushGroup::Entities, TEXT("弹射箱")},
    {EBrush::Enemy, EBrushGroup::Entities, TEXT("接触敌人")},
    {EBrush::Laser, EBrushGroup::Entities, TEXT("射线敌人")},
    {EBrush::Rotate, EBrushGroup::Editing, TEXT("旋转朝向")},
    {EBrush::Erase, EBrushGroup::Editing, TEXT("擦除")},
    {EBrush::CutCell, EBrushGroup::BoardShape, TEXT("挖空")},
    {EBrush::RestoreCell, EBrushGroup::BoardShape, TEXT("补地板")}
};

FIntPoint TurnRight(FIntPoint Direction)
{
    return {-Direction.Y, Direction.X};
}

const TCHAR* Arrow(FIntPoint Direction)
{
    if (Direction == FIntPoint(1, 0)) { return TEXT("→"); }
    if (Direction == FIntPoint(0, 1)) { return TEXT("↓"); }
    if (Direction == FIntPoint(-1, 0)) { return TEXT("←"); }
    if (Direction == FIntPoint(0, -1)) { return TEXT("↑"); }
    return TEXT("?");
}

FButtonStyle MakeAccentButtonStyle(FLinearColor Color)
{
    return FButtonStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
        .SetNormal(FSlateRoundedBoxBrush(Color, 4.f))
        .SetHovered(FSlateRoundedBoxBrush(FMath::Lerp(Color, FLinearColor::White, .10f), 4.f))
        .SetPressed(FSlateRoundedBoxBrush(FMath::Lerp(Color, FLinearColor::Black, .18f), 4.f));
}

class SGridWorkbench final : public SCompoundWidget, public FEditorUndoClient
{
public:
    friend class ::FGridWorkbenchTabTest;
    SLATE_BEGIN_ARGS(SGridWorkbench) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        if (GEditor) { GEditor->RegisterForUndo(this); }
        static const FButtonStyle SaveStyle = MakeAccentButtonStyle({.025f, .19f, .085f});
        ChildSlot
        [
            SNew(SBorder).Padding(FMargin(14, 12))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                            .Text(LOCTEXT("Title", "关卡编辑器")) ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
                        [ SNew(STextBlock).AutoWrapText(true).Text(this, &SGridWorkbench::GetAssetLabel) ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("Load", "读取预览")).OnClicked(this, &SGridWorkbench::Reload) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
                    [
                        SNew(SButton).Text(LOCTEXT("OpenWorkbenchMap", "打开编辑地图"))
                        .Visibility_Lambda([this] { return ActiveScene() ? EVisibility::Collapsed : EVisibility::Visible; })
                        .OnClicked_Lambda([this] {
                            if (OpenEditorMap(WorkbenchMap)) { Reload(); }
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("New", "新建草稿")).OnClicked(this, &SGridWorkbench::NewDraft)
                        .IsEnabled_Lambda([this] { return ActiveScene() != nullptr; }) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
                    [ SNew(SButton).ButtonStyle(&SaveStyle).Text(LOCTEXT("Save", "保存草稿")).OnClicked(this, &SGridWorkbench::SaveDraft)
                        .IsEnabled_Lambda([this] { return Asset() != nullptr; }) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                    [ SNew(SButton).ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
                        .Text(LOCTEXT("PlayDraft", "试玩关卡")).OnClicked(this, &SGridWorkbench::PlayDraft)
                        .IsEnabled_Lambda([this] { return Asset() != nullptr; }) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("PlayCampaign", "开始界面试玩"))
                        .IsEnabled_Lambda([this] { return CurrentWorld() != nullptr; })
                        .OnClicked(this, &SGridWorkbench::PlayCampaign) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("EditCampaign", "编辑关卡序列"))
                        .OnClicked(this, &SGridWorkbench::EditCampaign) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
                    [
                        SNew(SButton).Text(LOCTEXT("ReturnLaunchMap", "返回启动关卡"))
                        .OnClicked_Lambda([] { OpenEditorMap(GetLaunchMap()); return FReply::Handled(); })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                    [ SNew(SButton).Text(LOCTEXT("BrowseDrafts", "关卡浏览器"))
                        .OnClicked(this, &SGridWorkbench::OpenLevelBrowser) ]
                    + SHorizontalBox::Slot().FillWidth(.65f).Padding(0, 0, 6, 0)
                    [
                        SNew(SSearchBox)
                        .HintText(LOCTEXT("SearchDrafts", "搜索草稿名称或路径"))
                        .OnTextChanged_Lambda([this](const FText& Text) {
                            DraftSearch = Text.ToString().TrimStartAndEnd();
                            FilterLibrary();
                        })
                        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit) {
                            if (Commit == ETextCommit::OnEnter && SavedDrafts.Num() == 1)
                            {
                                LibraryCombo->SetSelectedItem(SavedDrafts[0]);
                                BindSelected();
                            }
                        })
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SAssignNew(LibraryCombo, SComboBox<TSharedPtr<FAssetData>>)
                        .OptionsSource(&SavedDrafts)
                        .OnComboBoxOpening(this, &SGridWorkbench::RefreshLibrary)
                        .OnGenerateWidget_Lambda([](TSharedPtr<FAssetData> Entry) {
                            return SNew(STextBlock).Text(FText::FromName(Entry->AssetName)).ToolTipText(FText::FromName(Entry->PackageName));
                        })
                        .OnSelectionChanged(this, &SGridWorkbench::SelectDraft)
                        [ SNew(STextBlock).Text(this, &SGridWorkbench::GetLibraryLabel) ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("BindDraft", "载入所选")).OnClicked(this, &SGridWorkbench::BindSelected)
                        .IsEnabled_Lambda([this] { return CurrentWorld() && SelectedDraft.IsValid(); }) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("Locate", "定位所选")).OnClicked(this, &SGridWorkbench::LocateDraft)
                        .IsEnabled_Lambda([this] { return SelectedDraft.IsValid(); }) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("CopyDraft", "复制当前")).OnClicked(this, &SGridWorkbench::CopyDraft)
                        .IsEnabled_Lambda([this] { return Asset() != nullptr; }) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("RefreshDrafts", "刷新")).OnClicked_Lambda([this] {
                        RefreshLibrary();
                        return FReply::Handled();
                    }) ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
                    [
                        SNew(SBox).WidthOverride(210)
                        [
                            SNew(SBorder).Padding(10)
                            [
                                SNew(SScrollBox)
                                + SScrollBox::Slot()
                                [
                                    SNew(SVerticalBox)
                                    + SVerticalBox::Slot().AutoHeight()
                                    [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                                        .Text(LOCTEXT("CreatePanel", "创建组件")) ]
                                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                                    [ MakePalette(LOCTEXT("TerrainTools", "起终点与地形"), EBrushGroup::Terrain) ]
                                    + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                                    [ MakePalette(LOCTEXT("EntityTools", "箱子与敌人"), EBrushGroup::Entities) ]
                                    + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                                    [ MakePalette(LOCTEXT("EditTools", "编辑操作"), EBrushGroup::Editing) ]
                                    + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                                    [ MakePalette(LOCTEXT("ShapeTools", "棋盘形状"), EBrushGroup::BoardShape) ]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                                    [ MakeBatchPanel() ]
                                ]
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(SBorder).Padding(10)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                                [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                                    .Text(this, &SGridWorkbench::GetBoardTitle) ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                                [ MakeHistoryButton(false) ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 10, 0)
                                [ MakeHistoryButton(true) ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                                [ MakeZoomButton(LOCTEXT("Compact", "紧凑"), 0.70f) ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                                [ MakeZoomButton(LOCTEXT("Normal", "标准"), 1.f) ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                                [ MakeZoomButton(LOCTEXT("Large", "放大"), 1.25f) ]
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                                [ SNew(SButton).Text(LOCTEXT("CopyPieces", "复制组件"))
                                    .ToolTipText(LOCTEXT("CopyPiecesHelp", "复制选中的墙、碎墙、箱子和敌人（Ctrl+C）。"))
                                    .IsEnabled_Lambda([this] { return Asset() && !GestureLevel.IsValid(); })
                                    .OnClicked(this, &SGridWorkbench::CopyPieces) ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                                [ SNew(SButton).Text(LOCTEXT("PastePieces", "粘贴组件"))
                                    .IsEnabled_Lambda([this] { return Clipboard.IsValid() && Asset() && !GestureLevel.IsValid(); })
                                    .OnClicked(this, &SGridWorkbench::StartPasting) ]
                                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                                [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("BoardSelectionHelp",
                                    "右键从空格拖框，Ctrl+右键补选，右键拖动选中组件。")) ]
                            ]
                            + SVerticalBox::Slot().FillHeight(1.f)
                            [
                                SAssignNew(BoardVertical, SScrollBox)
                                + SScrollBox::Slot()
                                [
                                    SAssignNew(BoardHorizontal, SScrollBox).Orientation(Orient_Horizontal)
                                    + SScrollBox::Slot() [ SAssignNew(GridHost, SBox) ]
                                ]
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                    [ SNew(SBox).WidthOverride(278) [ MakeInspector() ] ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                [
                    SNew(SBorder).Padding(9)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                            [ SNew(STextBlock).Text(LOCTEXT("StatusTitle", "检查结果")) ]
                            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                            [
                                SNew(SButton).Text(LOCTEXT("ValidationSettings", "关卡合法性校验设置"))
                                .OnClicked_Lambda([] {
                                    const auto* Settings = GetDefault<UGridProjectSettings>();
                                    auto& Module = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
                                    Module.ShowViewer(Settings->GetContainerName(), Settings->GetCategoryName(), Settings->GetSectionName());
                                    return FReply::Handled();
                                })
                            ]
                            + SHorizontalBox::Slot().AutoWidth()
                            [ SNew(SButton).ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
                                .Text(LOCTEXT("Validate", "验证关卡")).OnClicked(this, &SGridWorkbench::ValidateDraft)
                                .IsEnabled_Lambda([this] { return Asset() != nullptr; }) ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
                            [ SNew(STextBlock).Text(LOCTEXT("Resources", "验证用背包 · 道具数量")) ]
                            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 7, 0)
                            [ MakeResourceInput(LOCTEXT("Strength", "大力士药丸"), &ActualStrength, &Sokoban::FInventory::Strength) ]
                            + SHorizontalBox::Slot().AutoWidth()
                            [ MakeResourceInput(LOCTEXT("Undo", "时空回溯药丸"), &ActualUndo, &Sokoban::FInventory::Undo) ]
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SBox).MaxDesiredHeight(94)
                            [
                                SNew(SScrollBox)
                                + SScrollBox::Slot()
                                [ SNew(STextBlock).AutoWrapText(true).Text(this, &SGridWorkbench::GetStatus) ]
                            ]
                        ]
                    ]
                ]
            ]
        ];
        Reload();
        RefreshLibrary();
        ArtChangeHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SGridWorkbench::OnAssetPropertyChanged);
        MapOpenedHandle = FEditorDelegates::OnMapOpened.AddSP(this, &SGridWorkbench::OnMapOpened);
    }

    ~SGridWorkbench() override
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ArtChangeHandle);
        FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
        if (GEditor) { GEditor->UnregisterForUndo(this); }
    }

    void PostUndo(bool bSuccess) override { if (bSuccess) { MoveSelection.Reset(); Refresh(); } }
    void PostRedo(bool bSuccess) override { if (bSuccess) { MoveSelection.Reset(); Refresh(); } }
    bool SupportsKeyboardFocus() const override { return true; }

    FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (Event.IsControlDown() && !Event.IsAltDown())
        {
            if (Event.GetKey() == EKeys::Z) { return ApplyHistory(Event.IsShiftDown()); }
            if (Event.GetKey() == EKeys::Y) { return ApplyHistory(true); }
            if (Event.GetKey() == EKeys::C) { return CopyPieces(); }
            if (Event.GetKey() == EKeys::V) { return StartPasting(); }
        }
        if (Event.GetKey() == EKeys::Escape && !GestureLevel.IsValid())
        {
            if (StructureTool == EStructureTool::Paste)
            {
                SetStructureTool(EStructureTool::None);
                return FReply::Handled();
            }
            if (!MoveSelection.IsEmpty())
            {
                MoveSelection.Reset();
                MoveSelectionFootprint.Reset();
                return FReply::Handled();
            }
        }
        return SCompoundWidget::OnKeyDown(Geometry, Event);
    }

private:
    bool CanApplyHistory(bool bRedo) const
    {
        return Asset() && !GestureLevel.IsValid() && GEditor && GEditor->Trans &&
            (bRedo ? GEditor->Trans->CanRedo() : GEditor->Trans->CanUndo());
    }

    FReply ApplyHistory(bool bRedo)
    {
        if (CanApplyHistory(bRedo))
        {
            const bool bApplied = bRedo ? GEditor->RedoTransaction() : GEditor->UndoTransaction();
            if (bApplied)
            {
                Message = bRedo ? LOCTEXT("DidRedo", "已重做。") : LOCTEXT("DidUndo", "已撤销。");
            }
        }
        return FReply::Handled();
    }

    TSharedRef<SWidget> MakeHistoryButton(bool bRedo)
    {
        static const FButtonStyle HistoryStyle = MakeAccentButtonStyle({.24f, .10f, .025f});
        return SNew(SButton)
            .ButtonStyle(bRedo ? &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button") : &HistoryStyle)
            .Text(bRedo ? LOCTEXT("RedoEdit", "重做") : LOCTEXT("UndoEdit", "撤销"))
            .ToolTipText(bRedo ? LOCTEXT("RedoEditTip", "重做 UE 编辑器里最近撤销的操作（Ctrl+Y 或 Ctrl+Shift+Z）")
                : LOCTEXT("UndoEditTip", "撤销 UE 编辑器里最近的操作（Ctrl+Z）"))
            .IsEnabled_Lambda([this, bRedo] { return CanApplyHistory(bRedo); })
            .OnClicked_Lambda([this, bRedo] { return ApplyHistory(bRedo); });
    }

    TSharedRef<SWidget> MakePalette(const FText& Title, EBrushGroup Group)
    {
        const auto Panel = SNew(SGridPanel);
        Panel->SetColumnFill(0, 1.f);
        Panel->SetColumnFill(1, 1.f);
        int32 Index = 0;
        for (const FBrushEntry Entry : Brushes)
        {
            if (Entry.Group != Group) { continue; }
            Panel->AddSlot(Index % 2, Index / 2).Padding(2)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .ButtonColorAndOpacity_Lambda([this, Entry] {
                    return StructureTool == EStructureTool::None && Brush == Entry.Brush ? FLinearColor(.20f, .70f, .68f) : FLinearColor::White;
                })
                .Text(FText::FromString(Entry.Label))
                .OnClicked_Lambda([this, Entry] { Brush = Entry.Brush; SetStructureTool(EStructureTool::None); return FReply::Handled(); })
            ];
            ++Index;
        }
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 3)
            [ SNew(STextBlock).Text(Title) ]
            + SVerticalBox::Slot().AutoHeight() [ Panel ];
    }

    FText GetBoardTitle() const
    {
        if (StructureTool == EStructureTool::Capture) { return FText::Format(LOCTEXT("DragSelectionTitle", "棋盘 · 记录结构 · 已选 {0} 格"), SelectedCells.Num()); }
        if (StructureTool == EStructureTool::Place) { return LOCTEXT("PlaceTitle", "棋盘 · 摆放结构：选择左上角"); }
        if (StructureTool == EStructureTool::Paste) { return LOCTEXT("PasteTitle", "棋盘 · 左键选择粘贴位置 · Esc 退出"); }
        if (!MoveSelection.IsEmpty())
        {
            return FText::Format(LOCTEXT("MoveSelectionTitle", "棋盘 · 已选 {0} 个组件"), MoveSelection.Num());
        }
        for (const auto& Entry : Brushes)
        {
            if (Entry.Brush == Brush)
            {
                const FIntPoint Extent = BrushExtent();
                return FText::Format(LOCTEXT("BoardBrushSize", "棋盘 · {0} · {1} × {2}"),
                    FText::FromString(Entry.Label), Extent.X, Extent.Y);
            }
        }
        return LOCTEXT("Board", "棋盘");
    }

    TSharedRef<SWidget> MakeZoomButton(const FText& Name, float Zoom)
    {
        return SNew(SButton).Text(Name)
            .ButtonColorAndOpacity_Lambda([this, Zoom] {
                return GridZoom == Zoom ? FLinearColor(.20f, .70f, .68f) : FLinearColor::White;
            })
            .OnClicked_Lambda([this, Zoom] {
                if (GridZoom != Zoom) { GridZoom = Zoom; RebuildGrid(); }
                return FReply::Handled();
            });
    }

    TSharedRef<SWidget> MakeDimension(const FText& Title, int32* Value)
    {
        return SNew(SNumericEntryBox<int32>)
            .Label()[ SNew(STextBlock).Text(Title) ]
            .AllowSpin(false).MinValue(1).MaxValue(Sokoban::FLevelGate::MaxBoardSide)
            .IsEnabled_Lambda([this] { return Asset() != nullptr; })
            .Value_Lambda([this, Value]() -> TOptional<int32> {
                return Asset() ? TOptional<int32>(*Value) : TOptional<int32>();
            })
            .OnValueCommitted_Lambda([Value](int32 Number, ETextCommit::Type Commit) {
                if (Commit != ETextCommit::OnCleared) { *Value = FMath::Clamp(Number, 1, Sokoban::FLevelGate::MaxBoardSide); }
            });
    }

    FIntPoint BrushExtent() const
    {
        return Brush == EBrush::Start || Brush == EBrush::Goal || Brush == EBrush::Rotate
            ? FIntPoint(1, 1) : BatchSize;
    }

    TSharedRef<SWidget> MakeBatchPanel()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
            [ SNew(STextBlock).Text(LOCTEXT("BatchTitle", "批量摆放与结构库")) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
                [ MakeDimension(LOCTEXT("BrushWidth", "宽"), &BatchSize.X) ]
                + SHorizontalBox::Slot().FillWidth(1)
                [ MakeDimension(LOCTEXT("BrushHeight", "高"), &BatchSize.Y) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("BatchHelp", "按住左键拖动可连续摆放，宽 × 高从经过的格子向右下展开。右键拖动已有组件。起点、终点和旋转仍为单格。")) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SAssignNew(StructureName, SEditableTextBox).HintText(LOCTEXT("StructureName", "结构名（可选）")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(UGridStructureAsset::StaticClass())
                .ObjectPath_Lambda([this] { return Structure.IsValid() ? Structure->GetPathName() : FString(); })
                .OnObjectChanged_Lambda([this](const FAssetData& Entry) {
                    Structure.Reset(Entry.IsValid() ? Cast<UGridStructureAsset>(Entry.GetAsset()) : nullptr);
                    if (!Structure.IsValid() && StructureTool == EStructureTool::Place) { StructureTool = EStructureTool::None; }
                })
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
                [
                    SNew(SButton).Text(LOCTEXT("CaptureStructure", "记录结构"))
                    .ButtonColorAndOpacity_Lambda([this] {
                        return StructureTool == EStructureTool::Capture ? FLinearColor(.20f, .70f, .68f) : FLinearColor::White;
                    })
                    .ToolTipText(LOCTEXT("CaptureHelp", "在棋盘按住左键拖框，按住 Ctrl 补选，完成后点击「保存选区」。"))
                    .IsEnabled_Lambda([this] { return Asset() != nullptr; })
                    .OnClicked_Lambda([this] {
                        SetStructureTool(EStructureTool::Capture);
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().FillWidth(1)
                [
                    SNew(SButton).Text(LOCTEXT("PlaceStructure", "摆放结构"))
                    .ButtonColorAndOpacity_Lambda([this] {
                        return StructureTool == EStructureTool::Place ? FLinearColor(.20f, .70f, .68f) : FLinearColor::White;
                    })
                    .ToolTipText(LOCTEXT("PlaceHelp", "选中结构后点击棋盘左上格，可连续摆放。切换画笔退出。"))
                    .IsEnabled_Lambda([this] { return Asset() && Structure.IsValid(); })
                    .OnClicked_Lambda([this] {
                        SetStructureTool(EStructureTool::Place);
                        return FReply::Handled();
                    })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
            [
                SNew(SVerticalBox)
                .Visibility_Lambda([this] {
                    return StructureTool == EStructureTool::Capture ? EVisibility::Visible : EVisibility::Collapsed;
                })
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("DragHelp", "左键拖框选格，按住 Ctrl 继续补选。")) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
                    [
                        SNew(SButton).Text(LOCTEXT("SaveSelection", "保存选区"))
                        .IsEnabled_Lambda([this] { return Asset() && !SelectedCells.IsEmpty() && !DragRect.IsSet(); })
                        .OnClicked(this, &SGridWorkbench::CaptureStructure)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton).Text(LOCTEXT("CancelSelection", "取消"))
                        .OnClicked_Lambda([this] {
                            SetStructureTool(EStructureTool::None);
                            return FReply::Handled();
                        })
                    ]
                ]
            ];
    }

    void SetStructureTool(EStructureTool Tool)
    {
        StructureTool = Tool;
        SelectedCells.Reset();
        ClearGridDrag();
        HoveredCell.Reset();
        SelectionLevel = Asset();
        SelectionBoardSize = SelectionLevel.IsValid() ? SelectionLevel->Size : FIntPoint::ZeroValue;
        Message = FText::GetEmpty();
    }

    FReply CopyPieces()
    {
        const auto* Draft = Asset();
        if (!Draft || GestureLevel.IsValid()) { return FReply::Handled(); }
        TSet<FIntPoint> Cells;
        FIntPoint Anchor = Draft->Size;
        int32 Count = 0;
        ForEachMovePiece(*Draft, [&](FGridMovePiece Piece) {
            const bool bSelected = MoveSelection.IsEmpty()
                ? SelectedComponent.IsSet() && SelectedComponent.GetValue() == Piece.Cell
                : MoveSelection.Contains(Piece.Cell);
            if (!bSelected || Piece.Kind == EMovePiece::Start || Piece.Kind == EMovePiece::Goal) { return; }
            ++Count;
            Piece.ForEachCell([&](FIntPoint Cell) {
                Cells.Add(Cell);
                Anchor = FIntPoint(FMath::Min(Anchor.X, Cell.X), FMath::Min(Anchor.Y, Cell.Y));
            });
        });
        if (Cells.IsEmpty())
        {
            Message = LOCTEXT("NothingToCopy", "请先选中墙、碎墙、箱子或敌人。起点和终点各只有一个。");
            return FReply::Handled();
        }
        TStrongObjectPtr<UGridStructureAsset> Copy(NewObject<UGridStructureAsset>());
        if (!Copy->Capture(*Draft, Cells, Message)) { return FReply::Handled(); }
        Clipboard = MoveTemp(Copy);
        ClipboardCells.Reset();
        for (FIntPoint Cell : Cells) { ClipboardCells.Add(Cell - Anchor); }
        Message = FText::Format(LOCTEXT("CopiedPieces", "已复制 {0} 个组件。按 Ctrl+V 或点击「粘贴组件」。"), Count);
        return FReply::Handled();
    }

    FReply StartPasting()
    {
        if (Asset() && Clipboard.IsValid() && !GestureLevel.IsValid())
        {
            SetStructureTool(EStructureTool::Paste);
            return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::SetDirectly);
        }
        return FReply::Handled();
    }

    FReply CaptureStructure()
    {
        const auto* Draft = Asset();
        if (!Draft || Draft != SelectionLevel.Get()) { return FReply::Handled(); }
        TStrongObjectPtr<UGridStructureAsset> Capture(NewObject<UGridStructureAsset>());
        if (!Capture->Capture(*Draft, SelectedCells, Message)) { return FReply::Handled(); }

        FString Name = StructureName->GetText().ToString().TrimStartAndEnd();
        if (Name.IsEmpty()) { Name = TEXT("DA_GridStructure"); }
        const FString BasePath = TEXT("/Game/Maps/Structures/") + Name;
        if (Name != ObjectTools::SanitizeObjectName(Name) || !FPackageName::IsValidLongPackageName(BasePath))
        {
            Message = LOCTEXT("InvalidStructureName", "结构名称含不可用字符，请使用中文、字母、数字或下划线。");
            return FReply::Handled();
        }

        auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
        FString PackageName, AssetName;
        Tools.CreateUniqueAssetName(BasePath, TEXT(""), PackageName, AssetName);
        auto* Factory = NewObject<UDataAssetFactory>();
        Factory->DataAssetClass = UGridStructureAsset::StaticClass();
        auto* Saved = Cast<UGridStructureAsset>(Tools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName),
            UGridStructureAsset::StaticClass(), Factory));
        if (!Saved)
        {
            Message = LOCTEXT("StructureCreateFailed", "无法创建结构资产。");
            return FReply::Handled();
        }

        Saved->Size = Capture->Size;
        Saved->VoidCells = MoveTemp(Capture->VoidCells);
        Saved->Walls = MoveTemp(Capture->Walls);
        Saved->Rubble = MoveTemp(Capture->Rubble);
        Saved->Boxes = MoveTemp(Capture->Boxes);
        Saved->Enemies = MoveTemp(Capture->Enemies);
        Saved->MarkPackageDirty();
        Structure.Reset(Saved);
        SetStructureTool(EStructureTool::None);
        const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({Saved->GetOutermost()}, false);
        Message = FText::Format(bSaved ? LOCTEXT("StructureSaved", "已保存结构 {0}。")
            : LOCTEXT("StructureUnsaved", "已创建结构 {0}，写盘失败。请在内容浏览器重试保存。"),
            FText::FromString(Saved->GetName()));
        return FReply::Handled();
    }

    TSharedRef<SWidget> MakeLimitInput(const FText& Label, const FText& Tip, int32 UGridLevelAsset::* Field)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
            [ SNew(STextBlock).Text(Label).ToolTipText(Tip) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SNumericEntryBox<int32>)
                .AllowSpin(false).MinValue(1)
                .IsEnabled_Lambda([this] { return Asset() != nullptr; })
                .Value_Lambda([this, Field]() -> TOptional<int32> {
                    if (const auto* Draft = Asset()) { return Draft->*Field; }
                    return {};
                })
                .OnValueCommitted_Lambda([this, Field, Label](int32 Value, ETextCommit::Type Commit) {
                    auto* Draft = Asset();
                    if (!Draft || Commit == ETextCommit::OnCleared || Value < 1 || Draft->*Field == Value) { return; }
                    const FScopedTransaction Transaction(FText::Format(LOCTEXT("ChangeLimit", "修改{0}"), Label));
                    Draft->SetFlags(RF_Transactional);
                    Draft->Modify();
                    Draft->*Field = Value;
                    Draft->MarkPackageDirty();
                    Refresh();
                })
                .ToolTipText(Tip)
            ];
    }

    TSharedRef<SWidget> MakeBeamOption(const FText& Label, bool UGridLevelAsset::* Field, bool bNeedsPolicy)
    {
        return SNew(SCheckBox)
            .IsEnabled_Lambda([this, bNeedsPolicy] {
                const auto* Draft = Asset();
                return Draft && (!bNeedsPolicy || Draft->bConfigureBeamTerrain);
            })
            .IsChecked_Lambda([this, Field] {
                const auto* Draft = Asset();
                return Draft && Draft->*Field ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this, Field](ECheckBoxState State) {
                auto* Draft = Asset();
                const bool bChecked = State == ECheckBoxState::Checked;
                if (!Draft || Draft->*Field == bChecked) { return; }
                const FScopedTransaction Transaction(LOCTEXT("ChangeBeamPolicy", "修改射线地形设置"));
                Draft->SetFlags(RF_Transactional);
                Draft->Modify();
                Draft->*Field = bChecked;
                Draft->MarkPackageDirty();
                Refresh();
            })
            [ SNew(STextBlock).Text(Label) ];
    }

    TSharedRef<SWidget> MakeLevelSettings()
    {
        return SNew(SBorder).Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                .Text(LOCTEXT("LevelSettings", "关卡设置")) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
                [ MakeDimension(LOCTEXT("Width", "宽"), &PendingSize.X) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeDimension(LOCTEXT("Height", "高"), &PendingSize.Y) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 10)
            [ SNew(SButton).Text(LOCTEXT("ApplySize", "应用棋盘尺寸"))
                .IsEnabled_Lambda([this] { return Asset() != nullptr; })
                .OnClicked(this, &SGridWorkbench::ApplySize) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [ MakeLimitInput(LOCTEXT("StrengthLimit", "大力士推箱上限"),
                LOCTEXT("StrengthLimitTip", "使用一次大力士后，单次推动最多能移动的箱子数。"),
                &UGridLevelAsset::StrengthLimit) ]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeBeamOption(LOCTEXT("ConfigureBeam", "配置射线地形遮挡"),
                &UGridLevelAsset::bConfigureBeamTerrain, false) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 10)
            [ MakeBeamOption(LOCTEXT("BeamStops", "地形阻挡射线"),
                &UGridLevelAsset::bBeamStopsOnTerrain, true) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton).Text(LOCTEXT("AssetDetails", "打开草稿属性"))
                .IsEnabled_Lambda([this] { return Asset() != nullptr; })
                .OnClicked_Lambda([this] {
                    if (auto* Draft = Asset())
                    {
                        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Draft);
                    }
                    return FReply::Handled();
                }) ]
        ];
    }

    static bool InsideRectangle(FIntPoint Cell, FIntPoint Size)
    {
        return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Size.X && Cell.Y < Size.Y;
    }

    static TOptional<FIntPoint> FindCroppedObject(const UGridLevelAsset& Draft, FIntPoint Size)
    {
        const auto Outside = [Size](FIntPoint Cell) { return !InsideRectangle(Cell, Size); };
        if (Outside(Draft.Start)) { return Draft.Start; }
        if (Outside(Draft.Goal)) { return Draft.Goal; }
        for (const auto Cell : Draft.Walls) { if (Outside(Cell)) { return Cell; } }
        for (const auto Cell : Draft.Rubble) { if (Outside(Cell)) { return Cell; } }
        for (const auto& Box : Draft.Boxes)
        {
            if (Outside(Box.Cell)) { return Box.Cell; }
            if (Box.Spike != FIntPoint::ZeroValue && Outside(Box.Cell + Box.Spike)) { return Box.Cell + Box.Spike; }
        }
        for (const auto& Enemy : Draft.Enemies) { if (Outside(Enemy.Cell)) { return Enemy.Cell; } }
        return {};
    }

    FReply ApplySize()
    {
        auto* Draft = Asset();
        if (!Draft || Draft->Size == PendingSize || !Sokoban::FLevelGate::SupportsBoardSize(PendingSize))
        {
            return FReply::Handled();
        }

        if (const auto Outside = FindCroppedObject(*Draft, PendingSize); Outside.IsSet())
        {
            ValidationMessage = FText::GetEmpty();
            Message = FText::Format(LOCTEXT("CroppedObject", "格子（列 {0}，行 {1}）仍有物件或起终点，移动后再缩小。"),
                Outside->X, Outside->Y);
            return FReply::Handled();
        }

        const FScopedTransaction Transaction(LOCTEXT("ResizeBoard", "调整棋盘尺寸"));
        Draft->SetFlags(RF_Transactional);
        Draft->Modify();
        Draft->Size = PendingSize;
        for (auto It = Draft->VoidCells.CreateIterator(); It; ++It)
        {
            if (!InsideRectangle(*It, PendingSize)) { It.RemoveCurrent(); }
        }
        Draft->MarkPackageDirty();
        Refresh();
        return FReply::Handled();
    }

    TSharedRef<SWidget> MakeArtPanel()
    {
        return SNew(SBorder).Padding(8)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                .Text(LOCTEXT("ArtSettings", "美术表现")) ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(UGridVisualProfile::StaticClass())
                .ObjectPath_Lambda([this] {
                    const auto* Draft = Asset();
                    return Draft && Draft->VisualProfile ? Draft->VisualProfile->GetPathName() : FString();
                })
                .OnObjectChanged(this, &SGridWorkbench::SelectArtProfile)
                .IsEnabled_Lambda([this] { return Asset() != nullptr; })
                .AllowClear(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
                [ SNew(SButton).Text(LOCTEXT("NewArt", "新建配置")).OnClicked(this, &SGridWorkbench::NewArtProfile)
                    .IsEnabled_Lambda([this] { return Asset() != nullptr; }) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ SNew(SButton).Text(LOCTEXT("OpenArt", "打开配置")).OnClicked(this, &SGridWorkbench::OpenArtProfile)
                    .IsEnabled_Lambda([this] { return ArtProfile() != nullptr; }) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
            [ SNew(SButton).Text(LOCTEXT("LocateArt", "在内容浏览器定位"))
                .OnClicked(this, &SGridWorkbench::LocateArtProfile)
                .IsEnabled_Lambda([this] { return ArtProfile() != nullptr; }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
            [
                SNew(SButton).Text(LOCTEXT("OpenDefaultArt", "打开项目默认配置"))
                .IsEnabled_Lambda([] { return AGridScene::GetDefaultArtProfile() != nullptr; })
                .OnClicked_Lambda([] {
                    if (auto* Profile = AGridScene::GetDefaultArtProfile())
                    {
                        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Profile);
                    }
                    return FReply::Handled();
                })
            ]
        ];
    }

    UGridVisualProfile* ArtProfile() const
    {
        const auto* Draft = Asset();
        return Draft && Draft->VisualProfile ? Draft->VisualProfile.Get() : AGridScene::GetDefaultArtProfile();
    }

    void SelectArtProfile(const FAssetData& Entry)
    {
        auto* Profile = Entry.IsValid() ? Cast<UGridVisualProfile>(Entry.GetAsset()) : nullptr;
        if (!Entry.IsValid() || Profile) { AssignArtProfile(Profile); }
    }

    void AssignArtProfile(UGridVisualProfile* Profile)
    {
        auto* Draft = Asset();
        if (!Draft || Draft->VisualProfile == Profile) { return; }
        const FScopedTransaction Transaction(LOCTEXT("AssignArt", "绑定关卡美术配置"));
        Draft->SetFlags(RF_Transactional);
        Draft->Modify();
        Draft->VisualProfile = Profile;
        Draft->MarkPackageDirty();
        RefreshArtPreview();
        Message = LOCTEXT("ArtChanged", "美术配置已更新。保存草稿可保留绑定。");
    }

    FReply NewArtProfile()
    {
        if (!Asset()) { return FReply::Handled(); }
        auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
        FString PackageName, AssetName;
        Tools.CreateUniqueAssetName(TEXT("/Game/Art/Profiles/DA_GridArt"), TEXT(""), PackageName, AssetName);
        auto* Factory = NewObject<UDataAssetFactory>();
        Factory->DataAssetClass = UGridVisualProfile::StaticClass();
        if (auto* Profile = Cast<UGridVisualProfile>(Tools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName),
            UGridVisualProfile::StaticClass(), Factory)))
        {
            AssignArtProfile(Profile);
            return OpenArtProfile();
        }
        Message = LOCTEXT("ArtCreateFailed", "无法创建美术配置。");
        return FReply::Handled();
    }

    FReply OpenArtProfile()
    {
        if (auto* Profile = ArtProfile())
        {
            GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Profile);
        }
        return FReply::Handled();
    }

    FReply LocateArtProfile()
    {
        if (auto* Profile = ArtProfile())
        {
            auto& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
            Browser.Get().SyncBrowserToAssets({FAssetData(Profile)});
        }
        return FReply::Handled();
    }

    void OnAssetPropertyChanged(UObject* ChangedObject, FPropertyChangedEvent&)
    {
        if (!ChangedObject) { return; }
        if (ChangedObject == Asset() || ChangedObject == GetDefault<UGridProjectSettings>())
        {
            Refresh();
        }
        else if (ChangedObject == ArtProfile() || ChangedObject == AGridScene::GetDefaultArtProfile())
        {
            RefreshArtPreview();
        }
    }

    void RefreshArtPreview()
    {
        if (auto* Active = ActiveScene())
        {
            Active->RefreshEditorPreview();
            GEditor->RedrawLevelEditingViewports();
        }
    }

    UWorld* CurrentWorld() const
    {
        return GEditor && !GEditor->PlayWorld ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    AGridScene* FindScene() const
    {
        UWorld* World = CurrentWorld();
        if (!World) { return nullptr; }
        if (USelection* Selected = GEditor->GetSelectedActors())
        {
            AGridScene* SelectedScene = nullptr;
            for (FSelectionIterator It(*Selected); It; ++It)
            {
                auto* Candidate = Cast<AGridScene>(*It);
                if (!Candidate || Candidate->GetWorld() != World) { continue; }
                if (SelectedScene) { return nullptr; }
                SelectedScene = Candidate;
            }
            if (SelectedScene) { return SelectedScene; }
        }
        AGridScene* Only = nullptr;
        for (TActorIterator<AGridScene> It(World); It; ++It)
        {
            if (Only) { return nullptr; }
            Only = *It;
        }
        return Only;
    }

    AGridScene* ActiveScene() const
    {
        auto* Candidate = Scene.Get();
        return Candidate && Candidate->GetWorld() == CurrentWorld() ? Candidate : nullptr;
    }

    UGridLevelAsset* Asset() const
    {
        auto* Active = ActiveScene();
        return Active ? Active->DraftLevel.Get() : nullptr;
    }

    FText GetAssetLabel() const
    {
        const auto* Draft = Asset();
        return Draft ? FText::FromString(FString::Printf(TEXT("关卡 %s   ·   草稿 %s   ·   %d × %d 格%s"),
            *Draft->RoomId.ToString(), *Draft->GetName(), Draft->Size.X, Draft->Size.Y,
            Draft->GetOutermost()->IsDirty() ? TEXT("   ·   未保存") : TEXT("")))
            : LOCTEXT("NoDraft", "未绑定草稿");
    }

    FText GetStatus() const
    {
        if (!CurrentWorld()) { return LOCTEXT("NeedsEditor", "停止试玩后可编辑关卡。"); }
        if (!ActiveScene()) { return LOCTEXT("Select", "先打开关卡编辑地图，再选择关卡预览对象。"); }

        const auto* Draft = Asset();
        if (!Draft) { return Message.IsEmpty() ? LOCTEXT("Create", "新建草稿，或在关卡预览对象的「关卡草稿」中指定已有资产。") : Message; }
        const FText Detail = ValidationMessage.IsEmpty() ? Message : ValidationMessage;
        const auto& Errors = LayoutWarnings;
        if (!Errors.IsEmpty())
        {
            return FText::Format(LOCTEXT("Warnings", "{2}布局警告（{0}）\n{1}"),
                FText::AsNumber(Errors.Num()), FText::FromString(FString::Join(Errors, TEXT("\n"))),
                FText::FromString(Detail.IsEmpty() ? FString() : Detail.ToString() + TEXT("\n")));
        }
        if (!ChainAdvisory.IsEmpty())
        {
            return Detail.IsEmpty() ? ChainAdvisory : FText::Format(LOCTEXT("DetailAndAdvisory", "{0}\n{1}"), Detail, ChainAdvisory);
        }
        return !Detail.IsEmpty() ? Detail : LOCTEXT("Ready", "布局检查通过。放置后即时刷新 3D 预览，Ctrl+Z / Ctrl+Y 撤销或重做。");
    }

    FReply EditCampaign()
    {
        auto* Sequence = GetDefault<UGridProjectSettings>()->CampaignSequence.LoadSynchronous();
        if (Sequence && GEditor)
        {
            GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Sequence);
        }
        else
        {
            Message = LOCTEXT("MissingCampaign", "请先在项目设置的「推箱子关卡」中设置默认关卡序列。");
        }
        return FReply::Handled();
    }

    FReply PlayCampaign()
    {
        PlayLaunchMap();
        return FReply::Handled();
    }

    FReply PlayDraft()
    {
        const auto* Active = ActiveScene();
        if (!Active || !Active->DraftLevel) { return FReply::Handled(); }
        for (TActorIterator<AGridScene> It(Active->GetWorld()); It; ++It)
        {
            if ((*It)->bUseForPlaytest != (*It == Active))
            {
                Message = LOCTEXT("SelectPlaytestScene", "请只为当前关卡预览对象开启「用于试玩」。");
                return FReply::Handled();
            }
        }
        if (!Active->DraftLevel->GetLayoutWarnings().IsEmpty())
        {
            Message = LOCTEXT("InvalidPlaytestLayout", "请先修正布局警告，再开始试玩。");
            return FReply::Handled();
        }
        FRequestPlaySessionParams Params;
        Params.GameModeOverride = AGridDraftPlaytestMode::StaticClass();
        GEditor->RequestPlaySession(Params);
        return FReply::Handled();
    }

    void OnMapOpened(const FString&, bool) { Reload(); }

    FReply Reload()
    {
        Scene = FindScene();
        Refresh();
        return FReply::Handled();
    }

    void RefreshLibrary()
    {
        auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        FARFilter Filter;
        Filter.ClassPaths.Add(UGridLevelAsset::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        Filter.bIncludeOnlyOnDiskAssets = true;
        LibraryEntries.Reset();
        Registry.GetAssets(Filter, LibraryEntries);
        LibraryEntries.Sort([](const FAssetData& Left, const FAssetData& Right) {
            return Left.PackageName.LexicalLess(Right.PackageName);
        });
        FilterLibrary();
    }

    void FilterLibrary()
    {
        if (!LibraryCombo) { return; }
        const FName Previous = SelectedDraft ? SelectedDraft->PackageName : NAME_None;
        SavedDrafts.Reset();
        TSharedPtr<FAssetData> Restored;
        for (const auto& Entry : LibraryEntries)
        {
            if (!DraftSearch.IsEmpty() && !Entry.PackageName.ToString().Contains(DraftSearch)) { continue; }
            auto Item = MakeShared<FAssetData>(Entry);
            SavedDrafts.Add(Item);
            if (Entry.PackageName == Previous) { Restored = Item; }
        }
        SelectedDraft = Restored;
        LibraryCombo->RefreshOptions();
        LibraryCombo->SetSelectedItem(Restored);
    }

    FText GetLibraryLabel() const
    {
        return SelectedDraft ? FText::FromName(SelectedDraft->AssetName) :
            SavedDrafts.IsEmpty() && !DraftSearch.IsEmpty() ? LOCTEXT("NoMatchingDrafts", "没有符合条件的草稿") :
            FText::Format(LOCTEXT("SelectDraftCount", "选择已保存的草稿（{0}）"), SavedDrafts.Num());
    }

    void SelectDraft(TSharedPtr<FAssetData> Entry, ESelectInfo::Type)
    {
        SelectedDraft = Entry;
    }

    FReply OpenLevelBrowser()
    {
        if (const auto Existing = BrowserWindow.Pin())
        {
            Existing->BringToFront(true);
            return FReply::Handled();
        }
        RefreshLibrary();
        const auto Window = SNew(SWindow)
            .Title(LOCTEXT("BrowserTitle", "推箱子 · 关卡浏览器"))
            .ClientSize(FVector2D(940.f, 570.f))
            .MinWidth(650.f).MinHeight(410.f)
            .SupportsMinimize(false);
        BrowserWindow = Window;
        Window->SetContent(SNew(SGridLevelBrowser)
            .Entries(LibraryEntries)
            .CanOpenDraft_Lambda([Weak = TWeakPtr<SGridWorkbench>(SharedThis(this))] {
                const auto Workbench = Weak.Pin();
                return Workbench && Workbench->CurrentWorld();
            })
            .OnOpenDraft(this, &SGridWorkbench::BindDraftEntry)
            .OnCreateDraft(this, &SGridWorkbench::CreateDraftInBrowserFolder)
            .OnDraftDeleted_Lambda([Weak = TWeakPtr<SGridWorkbench>(SharedThis(this))] {
                if (const auto Workbench = Weak.Pin()) { Workbench->Refresh(); Workbench->RefreshLibrary(); }
            })
            .OnClose_Lambda([Weak = TWeakPtr<SWindow>(Window)] {
                if (const auto Browser = Weak.Pin()) { Browser->RequestDestroyWindow(); }
                return FReply::Handled();
            }));
        FSlateApplication::Get().AddWindow(Window);
        return FReply::Handled();
    }

    AGridScene* EnsurePreviewScene(FText& Error)
    {
        auto* World = CurrentWorld();
        if (!World)
        {
            Error = LOCTEXT("StopPlayToLoad", "请先结束试玩，再载入关卡。");
            return nullptr;
        }
        if (auto* Active = ActiveScene()) { return Active; }
        if (auto* Preview = FindScene())
        {
            Scene = Preview;
            return Preview;
        }
        if (World->GetOutermost()->GetName() != WorkbenchMap)
        {
            if (!OpenEditorMap(WorkbenchMap))
            {
                Error = LOCTEXT("CouldNotOpenWorkbench", "未能打开编辑地图。请保存或处理当前地图的改动后重试。");
                return nullptr;
            }
            Reload();
            if (auto* Active = ActiveScene()) { return Active; }
        }
        Error = LOCTEXT("ChoosePreview", "找不到唯一的关卡预览对象。请在场景大纲选择一个关卡预览对象后重试。");
        return nullptr;
    }

    bool BindDraftEntry(const FAssetData& Entry, FText& Error)
    {
        TStrongObjectPtr<UGridLevelAsset> Draft(Cast<UGridLevelAsset>(Entry.GetAsset()));
        if (!Draft.IsValid())
        {
            Error = LOCTEXT("LoadFailed", "草稿无法读取，可能已被移动或删除。请重新打开关卡浏览器。");
            return false;
        }
        auto* Active = EnsurePreviewScene(Error);
        if (!Active) { return false; }
        BindDraft(*Active, *Draft);
        Message = LOCTEXT("LoadedDraft", "已载入所选草稿。保存地图可保留绑定。");
        return true;
    }

    FReply BindSelected()
    {
        FText Error;
        if (SelectedDraft && !BindDraftEntry(*SelectedDraft, Error)) { Message = Error; }
        return FReply::Handled();
    }

    FReply LocateDraft()
    {
        if (SelectedDraft)
        {
            auto& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
            Browser.Get().SyncBrowserToAssets({*SelectedDraft});
        }
        return FReply::Handled();
    }

    FReply NewDraft() { return CreateDraft(nullptr); }

    bool CreateDraftInBrowserFolder(const FString& Folder, FText& Error)
    {
        if (!EnsurePreviewScene(Error)) { return false; }
        const auto* Previous = Asset();
        CreateDraft(nullptr, Folder);
        if (Asset() && Asset() != Previous) { return true; }
        Error = Message;
        return false;
    }

    FReply CopyDraft()
    {
        if (auto* Draft = Asset()) { return CreateDraft(Draft); }
        return FReply::Handled();
    }

    FReply CreateDraft(UGridLevelAsset* Source, const FString& Folder = SGridLevelBrowser::DraftRoot)
    {
        if (auto* Active = ActiveScene())
        {
            int32 Serial = 1;
            TArray<FAssetData> DraftAssets;
            auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
            Registry.GetAssetsByClass(UGridLevelAsset::StaticClass()->GetClassPathName(), DraftAssets, true);
            TSet<FName> UsedNames;
            TSet<FName> UsedRoomIds;
            for (const auto& Entry : DraftAssets)
            {
                UsedNames.Add(Entry.AssetName);
                if (const auto* Existing = Cast<UGridLevelAsset>(Entry.GetAsset())) { UsedRoomIds.Add(Existing->RoomId); }
            }
            FString Name;
            FName RoomId;
            do
            {
                Name = FString::Printf(TEXT("DA_Grid_%03d"), Serial);
                RoomId = FName(*FString::Printf(TEXT("GRID_%03d"), Serial++));
            } while (UsedNames.Contains(FName(*Name)) || UsedRoomIds.Contains(RoomId) ||
                FPackageName::DoesPackageExist(Folder / Name) || FindPackage(nullptr, *(Folder / Name)));

            const auto Dialog = SNew(SWindow)
                .Title(LOCTEXT("DraftNameTitle", "填写关卡草稿名称"))
                .ClientSize(FVector2D(450, 155)).SupportsMinimize(false).SupportsMaximize(false);
            TSharedPtr<SEditableTextBox> Input;
            TSharedPtr<STextBlock> Feedback;
            bool bConfirmed = false;
            const auto Confirm = [&] {
                const FString Value = Input->GetText().ToString().TrimStartAndEnd();
                if (Value.IsEmpty() || Value != ObjectTools::SanitizeObjectName(Value) ||
                    !FPackageName::IsValidLongPackageName(Folder / Value))
                {
                    Feedback->SetText(LOCTEXT("BadDraftName", "请输入有效的关卡名称，可使用中文、字母、数字和下划线。"));
                    return FReply::Handled();
                }
                if (FPackageName::DoesPackageExist(Folder / Value) || FindPackage(nullptr, *(Folder / Value)))
                {
                    Feedback->SetText(LOCTEXT("DraftNameTaken", "当前文件夹已有同名资产，请换一个名称。"));
                    return FReply::Handled();
                }
                Name = Value;
                bConfirmed = true;
                Dialog->RequestDestroyWindow();
                return FReply::Handled();
            };
            Dialog->SetContent(SNew(SBorder).Padding(12)
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SAssignNew(Input, SEditableTextBox).Text(FText::FromString(Name)).SelectAllTextWhenFocused(true)
                        .OnTextCommitted_Lambda([&](const FText&, ETextCommit::Type Commit) {
                            if (Commit == ETextCommit::OnEnter) { Confirm(); }
                        }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
                    [ SAssignNew(Feedback, STextBlock).AutoWrapText(true)
                        .Text(LOCTEXT("DraftNameHelp", "序列和浏览器会显示这个资产名称。")) ]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                    [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(4)
                        [ SNew(SButton).Text(LOCTEXT("ConfirmName", "创建")).OnClicked_Lambda(Confirm) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(4)
                        [ SNew(SButton).Text(LOCTEXT("CancelName", "取消")).OnClicked_Lambda([&] {
                            Dialog->RequestDestroyWindow(); return FReply::Handled();
                        }) ] ] ]);
            Dialog->SetWidgetToFocusOnActivate(Input);
            FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().FindWidgetWindow(AsShared()));
            if (!bConfirmed)
            {
                Message = LOCTEXT("DraftNameCancelled", "已取消创建草稿。");
                return FReply::Handled();
            }

            auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
            UGridLevelAsset* Draft = nullptr;
            if (Source)
            {
                Draft = Cast<UGridLevelAsset>(Tools.DuplicateAsset(Name, Folder, Source));
            }
            else
            {
                auto* Factory = NewObject<UDataAssetFactory>();
                Factory->DataAssetClass = UGridLevelAsset::StaticClass();
                Draft = Cast<UGridLevelAsset>(Tools.CreateAsset(Name, Folder, UGridLevelAsset::StaticClass(), Factory));
            }

            if (Draft)
            {
                Draft->SetFlags(RF_Transactional);
                Draft->RoomId = RoomId;
                Draft->MarkPackageDirty();
                BindDraft(*Active, *Draft);
                Message = LOCTEXT("NewCreated", "草稿已绑定。保存草稿后会进入列表，再保存地图以保留绑定。");
            }
            else { Message = LOCTEXT("CreateFailed", "创建草稿失败。"); }
        }
        return FReply::Handled();
    }

    void BindDraft(AGridScene& Active, UGridLevelAsset& Draft)
    {
        const FScopedTransaction Transaction(LOCTEXT("Bind", "绑定关卡草稿"));
        Active.Modify();
        Active.DraftLevel = &Draft;
        Active.MarkPackageDirty();
        Refresh();
    }

    TSharedRef<SWidget> MakeResourceInput(const FText& Title, TOptional<int32>* Value,
                                         int32 Sokoban::FInventory::* InventoryField)
    {
        return SNew(SBox).WidthOverride(118)
        [
            SNew(SNumericEntryBox<int32>)
            .Label()[SNew(STextBlock).Text(Title)]
            .MinValue(0)
            .Value_Lambda([Value, InventoryField] {
                return TOptional<int32>(Value->Get(GetDefault<UGridProjectSettings>()->StartingInventory().*InventoryField));
            })
            .OnValueCommitted_Lambda([this, Value](int32 NewValue, ETextCommit::Type Commit) {
                if (Commit == ETextCommit::OnCleared) { Value->Reset(); }
                else { *Value = FMath::Max(0, NewValue); }
                ValidationMessage = FText::GetEmpty();
            })
        ];
    }

    FReply ValidateDraft()
    {
        if (const auto* Draft = Asset())
        {
            Message = FText::GetEmpty();
            const auto* Settings = GetDefault<UGridProjectSettings>();
            const Sokoban::FInventory Start = Settings->StartingInventory();
            auto Limits = Settings->SearchLimits();
            Limits.WitnessLimit = FMath::Max(Limits.MinimumSolutions, Sokoban::FSearchLimits::EasyStart);
            const auto Resources = Settings->MakeResourceContext({ActualStrength.Get(Start.Strength), ActualUndo.Get(Start.Undo)});
            const Sokoban::FBoundedLevelSolver Solver;
            const auto Report = Sokoban::FLevelGate::Validate(Draft->ToLevel(), Resources, Limits, Solver);
            LayoutWarnings = Report.Errors;
            if (!LayoutWarnings.IsEmpty())
            {
                ValidationMessage = LOCTEXT("LayoutRejected", "布局不合法，未开始求解。");
                return FReply::Handled();
            }
            const auto StatusText = [](Sokoban::ECheck Status) {
                if (Status == Sokoban::ECheck::Pass) { return LOCTEXT("Pass", "通过"); }
                if (Status == Sokoban::ECheck::Fail) { return LOCTEXT("Fail", "未通过"); }
                return LOCTEXT("Unknown", "未确定");
            };
            const auto Stage = [&StatusText](const FText& Name, const Sokoban::FSolutionReport& Check, Sokoban::FInventory Stock) {
                return FText::Format(LOCTEXT("Stage", "{0}（大力士药丸 {1}，时空回溯药丸 {2}） · 可通关 {3} · 合法解法数 {4} · {5}{6} 解"),
                    Name, Stock.Strength, Stock.Undo, StatusText(Check.Solvable), StatusText(Check.MeetsMinimum),
                    Check.bComplete ? FText::GetEmpty() : LOCTEXT("LowerBound", "至少 "), Check.CountLowerBound);
            };
            const FText Difficulty = Report.Actual.Difficulty == Sokoban::EDifficulty::High ? LOCTEXT("Hard", "困难") :
                Report.Actual.Difficulty == Sokoban::EDifficulty::Medium ? LOCTEXT("Medium", "中等") :
                Report.Actual.Difficulty == Sokoban::EDifficulty::Low ? LOCTEXT("Easy", "简单") : LOCTEXT("Unclassified", "未分档");
            ValidationMessage = FText::Format(
                LOCTEXT("ValidationReport", "正式准入模拟 {0} · 两阶段分别至少 {1} 解 · 难度 {6}\n步上限 {2}，每阶段节点预算 {3}\n{4}\n{5}"),
                StatusText(Report.Status), Limits.MinimumSolutions, Limits.MaxActions, Limits.NodeBudget,
                Stage(LOCTEXT("Capability", "能力阶段"), Report.Capability, Resources.Filter(Resources.CapabilityBudget)),
                Stage(LOCTEXT("Actual", "真实资源阶段"), Report.Actual, Resources.Filter(Resources.ActualInventory)), Difficulty);
        }
        return FReply::Handled();
    }

    FReply SaveDraft()
    {
        if (auto* Draft = Asset())
        {
            const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({Draft->GetOutermost()}, false);
            Message = bSaved ? LOCTEXT("Saved", "草稿已保存。") : LOCTEXT("SaveFailed", "草稿保存失败。");
            if (bSaved) { RefreshLibrary(); }
        }
        return FReply::Handled();
    }

    TSharedRef<SWidget> MakeInspector()
    {
        return SNew(SBorder).Padding(10)
        [
            SNew(SSplitter).Orientation(Orient_Vertical)
            + SSplitter::Slot().Value(.48f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                    .Text(LOCTEXT("ComponentList", "组件列表")) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
                [
                    SNew(SSearchBox).HintText(LOCTEXT("SearchComponents", "搜索类型、编号或坐标"))
                    .OnTextChanged_Lambda([this](const FText& Text) {
                        ComponentSearch = Text.ToString().TrimStartAndEnd();
                        RebuildComponents();
                    })
                ]
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    SAssignNew(ComponentList, SListView<TSharedPtr<FComponentRow>>)
                    .ListItemsSource(&ComponentRows)
                    .SelectionMode(ESelectionMode::Single)
                    .OnGenerateRow_Lambda([](TSharedPtr<FComponentRow> Row, const TSharedRef<STableViewBase>& Owner) {
                        return SNew(STableRow<TSharedPtr<FComponentRow>>, Owner)
                            [ SNew(STextBlock).Text(FText::FromString(Row->Label)) ];
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FComponentRow> Row, ESelectInfo::Type) {
                        if (Row && !bSyncingComponents) { SelectComponent(Row->Cell, true); }
                    })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 7)
                [ SNew(STextBlock).Text_Lambda([this] {
                    return FText::Format(LOCTEXT("VisibleComponents", "{0} 项 · Ctrl+左键查看棋盘组件"), ComponentRows.Num());
                }) ]
            ]
            + SSplitter::Slot().Value(.52f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                    [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                        .Text(LOCTEXT("Properties", "属性")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [ SAssignNew(ComponentDetails, SBox) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ MakeLevelSettings() ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                    [ MakeArtPanel() ]
                ]
            ]
        ];
    }

    void SyncComponentSelection()
    {
        if (!ComponentList) { return; }
        const TGuardValue<bool> Updating(bSyncingComponents, true);
        if (SelectedComponent.IsSet())
        {
            if (const auto* Row = ComponentRows.FindByPredicate([this](const auto& Entry) {
                return Entry->Cell == SelectedComponent.GetValue();
            }))
            {
                ComponentList->SetSelection(*Row);
                return;
            }
        }
        ComponentList->ClearSelection();
    }

    void SelectComponent(FIntPoint Cell, bool bLocate)
    {
        const auto* Draft = Asset();
        if (!Draft) { return; }
        if (const auto Piece = FindMovePiece(*Draft, Cell); Piece.IsSet()) { Cell = Piece->Cell; }
        SelectedComponent = Cell;
        SyncComponentSelection();
        RefreshComponentDetails();
        if (bLocate)
        {
            if (const auto* CellWidget = ComponentWidgets.Find(Cell))
            {
                BoardVertical->ScrollDescendantIntoView(CellWidget->Pin(), false);
                BoardHorizontal->ScrollDescendantIntoView(CellWidget->Pin(), false);
            }
        }
    }

    void RebuildComponents()
    {
        if (!ComponentList) { return; }
        const auto* Draft = Asset();
        if (Draft != ComponentDraft.Get()) { SelectedComponent.Reset(); }
        ComponentDraft = Asset();
        ComponentRows.Reset();
        if (Draft)
        {
            const auto Add = [&](FIntPoint Cell, const FString& Kind, FName Id = NAME_None) {
                const FString Name = Id.IsNone() ? Kind : FString::Printf(TEXT("%s · %s"), *Kind, *Id.ToString());
                const FString Label = FString::Printf(TEXT("%s   (%d, %d)"), *Name, Cell.X, Cell.Y);
                if (ComponentSearch.IsEmpty() || Label.Contains(ComponentSearch))
                {
                    ComponentRows.Add(MakeShared<FComponentRow>(FComponentRow{Cell, Label}));
                }
            };
            Add(Draft->Start, TEXT("起点"));
            Add(Draft->Goal, TEXT("终点"));
            for (FIntPoint Cell : Draft->Walls) { Add(Cell, TEXT("墙")); }
            for (FIntPoint Cell : Draft->Rubble) { Add(Cell, TEXT("碎墙")); }
            for (FIntPoint Cell : Draft->VoidCells) { Add(Cell, TEXT("挖空")); }
            for (const auto& Box : Draft->Boxes)
            {
                Add(Box.Cell, GetGridBoxVisualLabel(Box.Spike, Box.bExplodes, Box.bEject).ToString(), Box.Id);
            }
            for (const auto& Enemy : Draft->Enemies)
            {
                Add(Enemy.Cell, Enemy.BeamDirection != FIntPoint::ZeroValue ? TEXT("射线敌人") : TEXT("接触敌人"), Enemy.Id);
            }
            ComponentRows.Sort([](const auto& A, const auto& B) {
                if (A->Cell.Y != B->Cell.Y) { return A->Cell.Y < B->Cell.Y; }
                if (A->Cell.X != B->Cell.X) { return A->Cell.X < B->Cell.X; }
                return A->Label < B->Label;
            });
        }
        ComponentList->RequestListRefresh();
        SyncComponentSelection();
        RefreshComponentDetails();
    }

    void EditSelectedComponent(FIntPoint ExpectedCell, TFunctionRef<void(UGridLevelAsset&, const FGridMovePiece&)> Edit)
    {
        auto* Draft = Asset();
        if (!Draft || !SelectedComponent.IsSet() || SelectedComponent.GetValue() != ExpectedCell) { return; }
        const auto Piece = FindMovePiece(*Draft, SelectedComponent.GetValue());
        if (!Piece.IsSet()) { return; }
        const FScopedTransaction Transaction(LOCTEXT("EditComponent", "修改组件属性"));
        Draft->SetFlags(RF_Transactional);
        Draft->Modify();
        Edit(*Draft, Piece.GetValue());
        Draft->MarkPackageDirty();
        Refresh();
    }

    TSharedRef<SWidget> MakeBoxToggle(const FText& Name, bool bChecked, bool FGridBoxDraft::* Field)
    {
        return SNew(SCheckBox).IsChecked(bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([this, Field, Cell = SelectedComponent.GetValue()](ECheckBoxState State) {
                EditSelectedComponent(Cell, [&](UGridLevelAsset& Draft, const FGridMovePiece& Piece) {
                    if (Piece.Kind != EMovePiece::Box) { return; }
                    auto& Box = Draft.Boxes[Piece.Index];
                    const bool bEnabled = State == ECheckBoxState::Checked;
                    if (Field == &FGridBoxDraft::bExplodes) { Box.SetExplosionEnabled(bEnabled); }
                    else { Box.*Field = bEnabled; }
                });
            })
            [ SNew(STextBlock).Text(Name) ];
    }

    void RefreshComponentDetails()
    {
        if (!ComponentDetails) { return; }
        const auto* Draft = Asset();
        if (!Draft || !SelectedComponent.IsSet())
        {
            ComponentDetails->SetContent(SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("ChooseComponent", "选择组件以查看和编辑属性。")));
            return;
        }

        const FIntPoint Cell = SelectedComponent.GetValue();
        const auto Display = DescribeCell(*Draft, Cell);
        const auto Details = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::Format(LOCTEXT("ComponentLocation", "{0} · 列 {1}，行 {2}"),
                FText::FromString(Display.Details), Cell.X, Cell.Y)) ];
        ComponentDetails->SetContent(Details);
        const auto Piece = FindMovePiece(*Draft, Cell);
        if (!Piece.IsSet()) { return; }

        const FGridBoxDraft* Box = Piece->Kind == EMovePiece::Box ? &Draft->Boxes[Piece->Index] : nullptr;
        const FGridEnemyDraft* Enemy = Piece->Kind == EMovePiece::Enemy ? &Draft->Enemies[Piece->Index] : nullptr;
        if (Box || Enemy)
        {
            Details->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
            [ SNew(STextBlock).Text(FText::Format(LOCTEXT("ComponentId", "编号 {0}"),
                FText::FromName(Box ? Box->Id : Enemy->Id))) ];
        }
        if ((Box && Box->Spike != FIntPoint::ZeroValue) || (Enemy && Enemy->BeamDirection != FIntPoint::ZeroValue))
        {
            Details->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SButton).Text(LOCTEXT("RotateSelected", "顺时针旋转"))
                .OnClicked_Lambda([this, Cell] {
                    EditSelectedComponent(Cell, [](UGridLevelAsset& Level, const FGridMovePiece& Selected) {
                        if (Selected.Kind == EMovePiece::Box)
                        {
                            auto& Direction = Level.Boxes[Selected.Index].Spike;
                            Direction = TurnRight(Direction);
                        }
                        else if (Selected.Kind == EMovePiece::Enemy)
                        {
                            auto& Direction = Level.Enemies[Selected.Index].BeamDirection;
                            Direction = TurnRight(Direction);
                        }
                    });
                    return FReply::Handled();
                })
            ];
        }
        if (!Box) { return; }

        Details->AddSlot().AutoHeight()
        [ MakeBoxToggle(LOCTEXT("Eject", "沿推动方向弹射"), Box->bEject, &FGridBoxDraft::bEject) ];
        Details->AddSlot().AutoHeight()
        [ MakeBoxToggle(LOCTEXT("Explodes", "启用爆炸"), Box->bExplodes, &FGridBoxDraft::bExplodes) ];
        if (Box->bExplodes)
        {
            Details->AddSlot().AutoHeight()
            [ MakeBoxToggle(LOCTEXT("EjectBlockedExplodes", "弹射起步受阻时爆炸"), Box->bExplodeWhenEjectBlocked,
                &FGridBoxDraft::bExplodeWhenEjectBlocked) ];
        }
        else if (Box->bExplodeWhenEjectBlocked)
        {
            Details->AddSlot().AutoHeight()
            [ SNew(SButton).Text(LOCTEXT("ClearExplosionOptions", "清除残留爆炸选项"))
                .OnClicked_Lambda([this, Cell] {
                    EditSelectedComponent(Cell, [](UGridLevelAsset& Draft, const FGridMovePiece& Piece) {
                        if (Piece.Kind == EMovePiece::Box) { Draft.Boxes[Piece.Index].SetExplosionEnabled(false); }
                    });
                    return FReply::Handled();
                }) ];
        }
    }

    struct FCellDisplay
    {
        FString Label;
        FString Details;
        FLinearColor Fill = FLinearColor(.048f, .061f, .079f);
    };

    static FCellDisplay DescribeCell(const UGridLevelAsset& Draft, FIntPoint Cell)
    {
        FCellDisplay Display;
        TArray<FString> Labels;
        const auto Add = [&](FString Name, FLinearColor Fill) {
            Labels.Add(MoveTemp(Name));
            Display.Fill = Fill;
        };
        if (Draft.VoidCells.Contains(Cell))
        {
            Add(TEXT("挖空"), FLinearColor(.012f, .016f, .023f));
        }
        if (Draft.Start == Cell) { Add(TEXT("起点"), FLinearColor(.025f, .22f, .31f)); }
        if (Draft.Goal == Cell) { Add(TEXT("终点"), FLinearColor(.035f, .25f, .13f)); }
        if (Draft.Walls.Contains(Cell)) { Add(TEXT("墙"), FLinearColor(.21f, .24f, .27f)); }
        if (Draft.Rubble.Contains(Cell)) { Add(TEXT("碎墙"), FLinearColor(.27f, .16f, .087f)); }
        for (const auto& Box : Draft.Boxes)
        {
            if (Box.Cell == Cell)
            {
                const auto Role = GetGridBoxVisualRole(Box.Spike, Box.bExplodes, Box.bEject);
                const auto Color = Role == EGridVisualRole::ExplosiveCrate ? FLinearColor(.37f, .12f, .038f) :
                    Role == EGridVisualRole::SpikeCrate ? FLinearColor(.25f, .11f, .31f) :
                    Role == EGridVisualRole::EjectCrate ? FLinearColor(.06f, .23f, .46f) : FLinearColor(.26f, .18f, .052f);
                FString Label = GetGridBoxVisualLabel(Box.Spike, Box.bExplodes, Box.bEject).ToString();
                if (Box.Spike != FIntPoint::ZeroValue) { Label += FString(TEXT(" ")) + Arrow(Box.Spike); }
                Add(Label, Color);
            }
            if (Box.Spike != FIntPoint::ZeroValue && Box.Cell + Box.Spike == Cell)
            {
                Add(TEXT("尖刺"), FLinearColor(.16f, .08f, .23f));
            }
        }
        for (const auto& Enemy : Draft.Enemies)
        {
            if (Enemy.Cell == Cell)
            {
                Add(Enemy.BeamDirection == FIntPoint::ZeroValue ? FString(TEXT("接触敌人")) :
                    FString(TEXT("射线敌人 ")) + Arrow(Enemy.BeamDirection), FLinearColor(.33f, .071f, .086f));
            }
        }
        if (Labels.Num() > 1)
        {
            Display.Label = TEXT("重叠");
            Display.Fill = FLinearColor(.43f, .12f, .05f);
        }
        else if (!Labels.IsEmpty()) { Display.Label = Labels[0]; }
        Display.Details = Labels.IsEmpty() ? TEXT("空格") : FString::Join(Labels, TEXT("、"));
        return Display;
    }

    void Refresh()
    {
        Message = FText::GetEmpty();
        ValidationMessage = FText::GetEmpty();
        if (auto* Active = ActiveScene()) { Active->RefreshEditorPreview(); }
        if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
        if (const auto* Draft = Asset()) { PendingSize = Draft->Size; }
        RebuildGrid();
    }

    FIntPoint PointerCell(const FPointerEvent& Event, FIntPoint BoardSize, bool bClamp) const
    {
        // Use geometry from the hit cell's pointer event, including its current zoom and scroll transform.
        const FVector2D Position = GestureGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        const FVector2D Pitch = FVector2D(GestureGeometry.GetLocalSize()) + FVector2D(2.0, 2.0);
        const FIntPoint Cell = GestureAnchor + FIntPoint(FMath::FloorToInt((Position.X + 1.0) / Pitch.X),
                                                        FMath::FloorToInt((Position.Y + 1.0) / Pitch.Y));
        return bClamp ? FIntPoint(FMath::Clamp(Cell.X, 0, BoardSize.X - 1),
                                 FMath::Clamp(Cell.Y, 0, BoardSize.Y - 1)) : Cell;
    }

    bool CanDragBrush() const
    {
        return StructureTool == EStructureTool::None &&
            Brush != EBrush::Start && Brush != EBrush::Goal && Brush != EBrush::Rotate;
    }

    void ExtendBrushStroke(const UGridLevelAsset& Draft, FIntPoint Cell)
    {
        const FIntPoint Delta = Cell - StrokeEnd;
        const int32 Distance = FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
        for (int32 Step = 0; Step <= Distance; ++Step)
        {
            const double Alpha = Distance > 0 ? static_cast<double>(Step) / Distance : 0.0;
            const FIntPoint Anchor = StrokeEnd + FIntPoint(
                FMath::RoundToInt(Alpha * Delta.X), FMath::RoundToInt(Alpha * Delta.Y));
            if (StrokeAnchors.Contains(Anchor)) { continue; }
            StrokeAnchors.Add(Anchor);
            CollectBrushCells(Draft, Anchor, StrokeCells, StrokeError);
        }
        StrokeEnd = Cell;
    }

    void RefreshMoveSelection(const UGridLevelAsset& Draft)
    {
        MoveSelectionFootprint.Reset();
        TSet<FIntPoint> Present;
        ForEachMovePiece(Draft, [&](FGridMovePiece Piece) {
            if (!MoveSelection.Contains(Piece.Cell)) { return; }
            Present.Add(Piece.Cell);
            Piece.ForEachCell([&](FIntPoint Cell) { MoveSelectionFootprint.Add(Cell); });
        });
        MoveSelection = MoveTemp(Present);
    }

    void SelectMoveRect(const UGridLevelAsset& Draft, const FIntRect& Rect, bool bAdd)
    {
        if (!bAdd) { MoveSelection.Reset(); }
        ForEachMovePiece(Draft, [&](FGridMovePiece Piece) {
            if (Rect.Contains(Piece.Cell) || Rect.Contains(Piece.Cell + Piece.Spike))
            {
                MoveSelection.Add(Piece.Cell);
            }
        });
        RefreshMoveSelection(Draft);
    }

    FReply BeginGridGesture(FIntPoint Cell, const FGeometry& Geometry, const FPointerEvent& Event)
    {
        const FKey Button = Event.GetEffectingButton();
        if (GestureLevel.IsValid() || (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton))
        {
            return FReply::Unhandled();
        }
        auto* Draft = Asset();
        if (!Draft) { return FReply::Unhandled(); }

        ClearGridDrag();
        GestureLevel = Draft;
        GestureButton = Button;
        GestureGeometry = Geometry;
        GestureAnchor = Cell;
        bInspectGesture = Button == EKeys::LeftMouseButton && Event.IsControlDown() &&
            StructureTool != EStructureTool::Capture;
        if (Button == EKeys::RightMouseButton)
        {
            const auto Piece = FindMovePiece(*Draft, Cell);
            if (Event.IsControlDown() || !Piece.IsSet())
            {
                DragRect = GridSelectionRect(Cell, Cell);
                bAddSelection = Event.IsControlDown();
            }
            else
            {
                if (!MoveSelection.Contains(Piece->Cell))
                {
                    MoveSelection.Reset();
                    MoveSelection.Add(Piece->Cell);
                    RefreshMoveSelection(*Draft);
                }
                MoveGrabbedCell = Piece->Cell;
                ForEachMovePiece(*Draft, [&](FGridMovePiece Candidate) {
                    if (MoveSelection.Contains(Candidate.Cell)) { MovePieces.Add(Candidate); }
                    else { Candidate.ForEachCell([&](FIntPoint Occupied) { MoveReservedCells.Add(Occupied); }); }
                });
            }
        }
        else if (bInspectGesture)
        {
            SelectComponent(Cell, false);
        }
        else if (StructureTool == EStructureTool::Capture)
        {
            DragRect = GridSelectionRect(Cell, Cell);
            bAddSelection = Event.IsControlDown();
        }
        else if (CanDragBrush())
        {
            StrokeEnd = Cell;
            ExtendBrushStroke(*Draft, Cell);
        }
        return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
    }

    void UpdateGridGesture(const UGridLevelAsset& Draft, const FPointerEvent& Event)
    {
        if (bInspectGesture) { return; }
        if (!MovePieces.IsEmpty())
        {
            MoveOffset = PointerCell(Event, Draft.Size, false) - GestureAnchor;
            MovePreviewCells.Reset();
            bCanDropPieces = true;
            for (const auto& Piece : MovePieces)
            {
                Piece.ForEachCell([&](FIntPoint Source) {
                    const FIntPoint Destination = Source + MoveOffset;
                    if (!UGridStructureAsset::Fits(Draft.Size, Destination, {1, 1}) ||
                        Draft.VoidCells.Contains(Destination) || MoveReservedCells.Contains(Destination) ||
                        MovePreviewCells.Contains(Destination))
                    {
                        bCanDropPieces = false;
                    }
                    MovePreviewCells.Add(Destination);
                });
            }
            return;
        }
        const FIntPoint Cell = PointerCell(Event, Draft.Size, true);
        if (DragRect.IsSet()) { DragRect = GridSelectionRect(GestureAnchor, Cell); }
        else if (CanDragBrush()) { ExtendBrushStroke(Draft, Cell); }
    }

    FReply OnMouseMove(const FGeometry&, const FPointerEvent& Event) override
    {
        const auto* Draft = GestureLevel.Get();
        if (!Draft || Draft != Asset()) { return FReply::Unhandled(); }
        UpdateGridGesture(*Draft, Event);
        return FReply::Handled();
    }

    FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (!GestureLevel.IsValid() || Event.GetEffectingButton() != GestureButton)
        {
            return FReply::Unhandled();
        }
        if (auto* Draft = GestureLevel.Get(); Draft == Asset())
        {
            UpdateGridGesture(*Draft, Event);
            if (!MovePieces.IsEmpty())
            {
                if (MoveOffset == FIntPoint::ZeroValue) { SelectComponent(MoveGrabbedCell, false); }
                else if (bCanDropPieces) { CommitMove(*Draft); }
                else
                {
                    Message = LOCTEXT("CannotDropPiece", "落点有占用、挖空或越界，所选组件都留在原位。");
                }
            }
            else if (DragRect.IsSet())
            {
                if (GestureButton == EKeys::RightMouseButton) { SelectMoveRect(*Draft, DragRect.GetValue(), bAddSelection); }
                else { SelectCells(SelectedCells, DragRect.GetValue(), bAddSelection); }
            }
            else if (!bInspectGesture && CanDragBrush())
            {
                const FText Error = StrokeError;
                const bool bPlaced = !StrokeCells.IsEmpty();
                CommitBrushCells(*Draft, StrokeCells);
                if (!Error.IsEmpty())
                {
                    Message = bPlaced ? FText::Format(LOCTEXT("StrokeSkipped", "部分落点已跳过。{0}"), Error) : Error;
                }
            }
            else if (!bInspectGesture && PointerCell(Event, Draft->Size, false) == GestureAnchor)
            {
                Paint(GestureAnchor);
            }
        }
        ClearGridDrag();
        return FReply::Handled().ReleaseMouseCapture();
    }

    void CommitMove(UGridLevelAsset& Draft)
    {
        const FScopedTransaction Transaction(LOCTEXT("MovePieceTransaction", "移动所选关卡组件"));
        Draft.SetFlags(RF_Transactional);
        Draft.Modify();
        // Remove all terrain sources first. A destination may reuse another selected source cell.
        for (const auto& Piece : MovePieces)
        {
            if (Piece.Kind == EMovePiece::Wall) { Draft.Walls.Remove(Piece.Cell); }
            if (Piece.Kind == EMovePiece::Rubble) { Draft.Rubble.Remove(Piece.Cell); }
        }
        MoveSelection.Reset();
        for (const auto& Piece : MovePieces)
        {
            const FIntPoint Destination = Piece.Cell + MoveOffset;
            MoveSelection.Add(Destination);
            switch (Piece.Kind)
            {
                case EMovePiece::Start: Draft.Start = Destination; break;
                case EMovePiece::Goal: Draft.Goal = Destination; break;
                case EMovePiece::Wall: Draft.Walls.Add(Destination); break;
                case EMovePiece::Rubble: Draft.Rubble.Add(Destination); break;
                case EMovePiece::Box: Draft.Boxes[Piece.Index].Cell = Destination; break;
                case EMovePiece::Enemy: Draft.Enemies[Piece.Index].Cell = Destination; break;
            }
        }
        SelectedComponent = MoveGrabbedCell + MoveOffset;
        Draft.MarkPackageDirty();
        Refresh();
    }

    void ClearGridDrag()
    {
        DragRect.Reset();
        MovePieces.Reset();
        MoveReservedCells.Reset();
        MovePreviewCells.Reset();
        MoveOffset = FIntPoint::ZeroValue;
        bInspectGesture = false;
        bCanDropPieces = false;
        GestureLevel.Reset();
        StrokeAnchors.Reset();
        StrokeCells.Reset();
        StrokeError = FText::GetEmpty();
    }

    void OnMouseCaptureLost(const FCaptureLostEvent&) override { ClearGridDrag(); }

    void RebuildGrid()
    {
        ClearGridDrag();
        HoveredCell.Reset();
        ComponentWidgets.Reset();
        RebuildComponents();
        auto Panel = SNew(SGridPanel);
        GridHost->SetContent(SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
            .Padding(0)
            .OnMouseMove_Lambda([this](const FGeometry&, const FPointerEvent&) {
                HoveredCell.Reset();
                return FReply::Unhandled();
            })
            [ Panel ]);
        auto* Draft = Asset();
        if (Draft != MoveSelectionLevel.Get())
        {
            MoveSelection.Reset();
            MoveSelectionFootprint.Reset();
            MoveSelectionLevel = Draft;
        }
        if (Draft) { RefreshMoveSelection(*Draft); }
        if (StructureTool == EStructureTool::Capture &&
            (Draft != SelectionLevel.Get() || !Draft || Draft->Size != SelectionBoardSize))
        {
            SetStructureTool(EStructureTool::None);
        }
        LayoutWarnings.Reset();
        ChainAdvisory = FText::GetEmpty();
        if (!Draft) { return; }
        LayoutWarnings = Draft->GetLayoutWarnings();
        ChainAdvisory = FText::FromString(Sokoban::FLevelGate::ChainModuleAdvisory(Draft->ToLevel()));
        if (!Sokoban::FLevelGate::SupportsBoardSize(Draft->Size)) { return; }
        const auto AxisLabel = [](int32 Number)
        {
            return SNew(STextBlock).Text(FText::AsNumber(Number)).Justification(ETextJustify::Center);
        };
        for (int32 X = 0; X < Draft->Size.X; ++X)
        {
            Panel->AddSlot(X + 1, 0).HAlign(HAlign_Center).Padding(1)[AxisLabel(X)];
        }
        for (int32 Y = 0; Y < Draft->Size.Y; ++Y)
        {
            Panel->AddSlot(0, Y + 1).VAlign(VAlign_Center).Padding(4, 0)[AxisLabel(Y)];
        }
        for (int32 Y = 0; Y < Draft->Size.Y; ++Y)
        {
            for (int32 X = 0; X < Draft->Size.X; ++X)
            {
                const FIntPoint Cell(X, Y);
                const FCellDisplay Display = DescribeCell(*Draft, Cell);
                const FText Tooltip = FText::FromString(FString::Printf(TEXT("列 %d，行 %d · %s"), X, Y, *Display.Details));
                TSharedPtr<SBox> CellWidget;
                Panel->AddSlot(X + 1, Y + 1).Padding(1)
                [
                    SAssignNew(CellWidget, SBox).WidthOverride(88 * GridZoom).HeightOverride(52 * GridZoom)
                    [
                        SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor_Lambda([this, Cell, Fill = Display.Fill] {
                            if (!MovePieces.IsEmpty() && MoveOffset != FIntPoint::ZeroValue &&
                                MovePreviewCells.Contains(Cell))
                            {
                                const FLinearColor Color = bCanDropPieces ? FLinearColor(.10f, .58f, .56f) : FLinearColor(.72f, .25f, .19f);
                                return FMath::Lerp(Fill, Color, .70f);
                            }
                            if (GestureLevel.IsValid() && GestureButton == EKeys::RightMouseButton && DragRect.IsSet())
                            {
                                const bool bSelected = DragRect->Contains(Cell) ||
                                    (bAddSelection && MoveSelectionFootprint.Contains(Cell));
                                return bSelected ? FMath::Lerp(Fill, FLinearColor(.10f, .48f, .95f), .67f) : Fill;
                            }
                            if (StructureTool == EStructureTool::Capture)
                            {
                                const bool bSelected = (DragRect.IsSet() && DragRect->Contains(Cell)) ||
                                    ((!DragRect.IsSet() || bAddSelection) && SelectedCells.Contains(Cell));
                                return bSelected ? FMath::Lerp(Fill, FLinearColor(.10f, .48f, .95f), .67f) : Fill;
                            }
                            if (StructureTool == EStructureTool::Paste)
                            {
                                if (!HoveredCell.IsSet() || !ClipboardCells.Contains(Cell - HoveredCell.GetValue())) { return Fill; }
                                const FLinearColor Color = bPasteCanPlace ? FLinearColor(.10f, .58f, .56f) : FLinearColor(.72f, .25f, .19f);
                                return FMath::Lerp(Fill, Color, .70f);
                            }
                            if (MoveSelectionFootprint.Contains(Cell))
                            {
                                return FMath::Lerp(Fill, FLinearColor(.12f, .38f, .77f), .67f);
                            }
                            if (GestureLevel.IsValid() && CanDragBrush())
                            {
                                return StrokeCells.Contains(Cell) ? FMath::Lerp(Fill, FLinearColor(.10f, .58f, .56f), .70f) : Fill;
                            }
                            const FIntPoint Extent = StructureTool == EStructureTool::Place && Structure.IsValid()
                                ? Structure->Size : BrushExtent();
                            if (HoveredCell.IsSet() && UGridStructureAsset::Fits(Extent, Cell - HoveredCell.GetValue(), FIntPoint(1, 1)))
                            {
                                const auto* Level = Asset();
                                const bool bFits = Level && UGridStructureAsset::Fits(Level->Size, HoveredCell.GetValue(), Extent);
                                return FMath::Lerp(Fill, bFits ? FLinearColor(.10f, .58f, .56f) : FLinearColor(.72f, .25f, .19f), .55f);
                            }
                            if (SelectedComponent.IsSet() && SelectedComponent.GetValue() == Cell)
                            {
                                return FMath::Lerp(Fill, FLinearColor(.12f, .38f, .77f), .62f);
                            }
                            return Fill;
                        })
                        .Padding(2)
                        .HAlign(HAlign_Center)
                        .VAlign(VAlign_Center)
                        .ToolTipText(Tooltip)
                        .OnMouseButtonDown_Lambda([this, Cell](const FGeometry& Geometry, const FPointerEvent& Event) {
                            return BeginGridGesture(Cell, Geometry, Event);
                        })
                        .OnMouseDoubleClick_Lambda([this, Cell](const FGeometry& Geometry, const FPointerEvent& Event) {
                            return BeginGridGesture(Cell, Geometry, Event);
                        })
                        .OnMouseMove_Lambda([this, Cell](const FGeometry&, const FPointerEvent&) {
                            if (StructureTool == EStructureTool::Paste &&
                                (!HoveredCell.IsSet() || HoveredCell.GetValue() != Cell))
                            {
                                const auto* Level = Asset();
                                FText Error;
                                bPasteCanPlace = Clipboard.IsValid() && Level && Clipboard->CanPlace(*Level, Cell, Error);
                            }
                            HoveredCell = Cell;
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(Display.Label))
                            .Visibility(EVisibility::HitTestInvisible)
                            .ColorAndOpacity(FSlateColor(FLinearColor::White))
                            .Justification(ETextJustify::Center)
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt(13 * GridZoom)))
                        ]
                    ]
                ];
                ComponentWidgets.Add(Cell, CellWidget);
            }
        }
    }

    FReply Paint(FIntPoint Anchor)
    {
        auto* Draft = Asset();
        if (!Draft) { return FReply::Handled(); }
        if (StructureTool == EStructureTool::Capture) { return FReply::Handled(); }
        if (StructureTool == EStructureTool::Place || StructureTool == EStructureTool::Paste)
        {
            const bool bPaste = StructureTool == EStructureTool::Paste;
            const auto* Stamp = bPaste ? Clipboard.Get() : Structure.Get();
            if (!Stamp || !Stamp->CanPlace(*Draft, Anchor, Message)) { return FReply::Handled(); }
            const FScopedTransaction Transaction(bPaste ? LOCTEXT("PastePiecesTransaction", "粘贴关卡组件")
                : LOCTEXT("StampStructure", "摆放关卡结构"));
            Draft->SetFlags(RF_Transactional);
            Draft->Modify();
            Stamp->Place(*Draft, Anchor);
            if (bPaste)
            {
                MoveSelection.Reset();
                ForEachMovePiece(*Draft, [&](FGridMovePiece Piece) {
                    if (ClipboardCells.Contains(Piece.Cell - Anchor)) { MoveSelection.Add(Piece.Cell); }
                });
            }
            Draft->MarkPackageDirty();
            Refresh();
            return FReply::Handled();
        }

        PaintDraft(*Draft, Anchor);
        return FReply::Handled();
    }

    void PaintDraft(UGridLevelAsset& Draft, FIntPoint Anchor)
    {
        TSet<FIntPoint> Cells;
        FText Error;
        CollectBrushCells(Draft, Anchor, Cells, Error);
        if (!Error.IsEmpty()) { Message = Error; }
        CommitBrushCells(Draft, Cells);
    }

    void CollectBrushCells(const UGridLevelAsset& Draft, FIntPoint Anchor,
                           TSet<FIntPoint>& Result, FText& Error) const
    {
        const FIntPoint Extent = BrushExtent();
        if (!UGridStructureAsset::Fits(Draft.Size, Anchor, Extent))
        {
            Error = LOCTEXT("BatchOutside", "画笔范围超出棋盘。");
            return;
        }
        TArray<FIntPoint> Cells;
        const bool bCut = Brush == EBrush::CutCell;
        const bool bRestore = Brush == EBrush::RestoreCell;
        for (int32 Y = Anchor.Y; Y < Anchor.Y + Extent.Y; ++Y)
        {
            for (int32 X = Anchor.X; X < Anchor.X + Extent.X; ++X)
            {
                const FIntPoint Cell(X, Y);
                const bool bVoid = Draft.VoidCells.Contains(Cell);
                if (bCut && !bVoid && UGridStructureAsset::IsOccupied(Draft, Cell))
                {
                    Error = LOCTEXT("CutOccupiedBatch", "范围内有物件或起终点，无法挖空。");
                    return;
                }
                if (!bCut && !bRestore && bVoid && Brush != EBrush::Erase)
                {
                    Error = LOCTEXT("VoidBatch", "范围内有挖空格，请先补地板。");
                    return;
                }
                if ((bCut && bVoid) || (bRestore && !bVoid) || (Brush == EBrush::Erase && bVoid)) { continue; }
                if (Brush == EBrush::Rotate || Brush == EBrush::Erase)
                {
                    const bool bRotate = Brush == EBrush::Rotate;
                    const bool bHasEntity = Draft.Boxes.ContainsByPredicate([Cell, bRotate](const FGridBoxDraft& Box) {
                        return Box.Cell == Cell && (!bRotate || Box.Spike != FIntPoint::ZeroValue);
                    }) || Draft.Enemies.ContainsByPredicate([Cell, bRotate](const FGridEnemyDraft& Enemy) {
                        return Enemy.Cell == Cell && (!bRotate || Enemy.BeamDirection != FIntPoint::ZeroValue);
                    });
                    if (!bHasEntity && (bRotate || (!Draft.Walls.Contains(Cell) && !Draft.Rubble.Contains(Cell)))) { continue; }
                }
                Cells.Add(Cell);
            }
        }
        for (const FIntPoint Cell : Cells) { Result.Add(Cell); }
    }

    void CommitBrushCells(UGridLevelAsset& Draft, const TSet<FIntPoint>& Cells)
    {
        if (Cells.IsEmpty()) { return; }
        const FScopedTransaction Transaction(LOCTEXT("BatchPaint", "编辑关卡格子"));
        Draft.SetFlags(RF_Transactional);
        Draft.Modify();
        for (const FIntPoint Cell : Cells) { PaintCell(&Draft, Cell); }
        Draft.MarkPackageDirty();
        Refresh();
    }

    void PaintCell(UGridLevelAsset* Draft, FIntPoint Cell)
    {
        if (Brush == EBrush::CutCell) { Draft->VoidCells.Add(Cell); return; }
        if (Brush == EBrush::RestoreCell) { Draft->VoidCells.Remove(Cell); return; }
        auto* Box = Draft->Boxes.FindByPredicate([Cell](const FGridBoxDraft& Entry) { return Entry.Cell == Cell; });
        auto* Enemy = Draft->Enemies.FindByPredicate([Cell](const FGridEnemyDraft& Entry) { return Entry.Cell == Cell; });
        FIntPoint* Facing = Box && Box->Spike != FIntPoint::ZeroValue ? &Box->Spike :
            Enemy && Enemy->BeamDirection != FIntPoint::ZeroValue ? &Enemy->BeamDirection : nullptr;
        if (Brush == EBrush::Rotate)
        {
            if (Facing) { *Facing = TurnRight(*Facing); }
        }
        else
        {
            Draft->Walls.Remove(Cell);
            Draft->Rubble.Remove(Cell);
            Draft->Boxes.RemoveAll([Cell](const FGridBoxDraft& Entry) { return Entry.Cell == Cell; });
            Draft->Enemies.RemoveAll([Cell](const FGridEnemyDraft& Entry) { return Entry.Cell == Cell; });
            switch (Brush)
            {
                case EBrush::Start: Draft->Start = Cell; break;
                case EBrush::Goal: Draft->Goal = Cell; break;
                case EBrush::Wall: Draft->Walls.Add(Cell); break;
                case EBrush::Rubble: Draft->Rubble.Add(Cell); break;
                case EBrush::Box:
                case EBrush::Spike:
                case EBrush::Blast:
                case EBrush::Eject:
                {
                    auto& Item = Draft->Boxes.AddDefaulted_GetRef();
                    Item.Id = Draft->AllocateEntityId(TEXT("B"), Cell);
                    Item.Cell = Cell;
                    Item.Spike = Brush == EBrush::Spike ? FIntPoint(1, 0) : FIntPoint::ZeroValue;
                    Item.bExplodes = Brush == EBrush::Blast;
                    Item.bEject = Brush == EBrush::Eject;
                    break;
                }
                case EBrush::Enemy:
                case EBrush::Laser:
                {
                    auto& Item = Draft->Enemies.AddDefaulted_GetRef();
                    Item.Id = Draft->AllocateEntityId(TEXT("E"), Cell);
                    Item.Cell = Cell;
                    Item.BeamDirection = Brush == EBrush::Laser ? FIntPoint(1, 0) : FIntPoint::ZeroValue;
                    break;
                }
                default: break;
            }
        }
    }

    TWeakObjectPtr<AGridScene> Scene;
    TSharedPtr<SBox> GridHost;
    TSharedPtr<SScrollBox> BoardVertical, BoardHorizontal;
    TMap<FIntPoint, TWeakPtr<SWidget>> ComponentWidgets;
    TSharedPtr<SListView<TSharedPtr<FComponentRow>>> ComponentList;
    TSharedPtr<SBox> ComponentDetails;
    bool bSyncingComponents = false;
    TArray<TSharedPtr<FComponentRow>> ComponentRows;
    TOptional<FIntPoint> SelectedComponent;
    TWeakObjectPtr<UGridLevelAsset> ComponentDraft;
    FString ComponentSearch;
    TWeakPtr<SWindow> BrowserWindow;
    TArray<FAssetData> LibraryEntries;
    FString DraftSearch;
    bool bInspectGesture = false;
    EBrush Brush = EBrush::Wall;
    EStructureTool StructureTool = EStructureTool::None;
    FIntPoint BatchSize = FIntPoint(1, 1);
    TOptional<FIntPoint> HoveredCell;
    TWeakObjectPtr<UGridLevelAsset> SelectionLevel;
    FIntPoint SelectionBoardSize = FIntPoint::ZeroValue;
    TSet<FIntPoint> SelectedCells;
    TWeakObjectPtr<UGridLevelAsset> MoveSelectionLevel;
    TSet<FIntPoint> MoveSelection;
    TSet<FIntPoint> MoveSelectionFootprint;
    TWeakObjectPtr<UGridLevelAsset> GestureLevel;
    FKey GestureButton;
    FGeometry GestureGeometry;
    TArray<FGridMovePiece> MovePieces;
    TSet<FIntPoint> MoveReservedCells;
    TSet<FIntPoint> MovePreviewCells;
    FIntPoint MoveGrabbedCell = FIntPoint::ZeroValue;
    FIntPoint MoveOffset = FIntPoint::ZeroValue;
    bool bCanDropPieces = false;
    FIntPoint GestureAnchor = FIntPoint::ZeroValue;
    FIntPoint StrokeEnd = FIntPoint::ZeroValue;
    TSet<FIntPoint> StrokeAnchors;
    TSet<FIntPoint> StrokeCells;
    FText StrokeError;
    TOptional<FIntRect> DragRect;
    bool bAddSelection = false;
    TSharedPtr<SEditableTextBox> StructureName;
    TStrongObjectPtr<UGridStructureAsset> Structure;
    TStrongObjectPtr<UGridStructureAsset> Clipboard;
    TSet<FIntPoint> ClipboardCells;
    bool bPasteCanPlace = false;
    FDelegateHandle ArtChangeHandle;
    FDelegateHandle MapOpenedHandle;
    FText Message;
    FText ValidationMessage;
    TArray<TSharedPtr<FAssetData>> SavedDrafts;
    TSharedPtr<FAssetData> SelectedDraft;
    TSharedPtr<SComboBox<TSharedPtr<FAssetData>>> LibraryCombo;
    TOptional<int32> ActualStrength;
    TOptional<int32> ActualUndo;
    FIntPoint PendingSize = FIntPoint(8, 6);
    float GridZoom = 1.f;
    TArray<FString> LayoutWarnings;
    FText ChainAdvisory;
};

class FSokobanWorkbenchModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateRaw(this, &FSokobanWorkbenchModule::Spawn))
            .SetDisplayName(LOCTEXT("Tab", "关卡编辑器"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        if (!IsRunningCommandlet())
        {
            ViewportTicker = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateRaw(this, &FSokobanWorkbenchModule::UpdateViewport), .25f);
        }
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSokobanWorkbenchModule::RegisterMenu));
    }

    void ShutdownModule() override
    {
        if (ViewportTicker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(ViewportTicker); }
        DetachLaunchOverlay();
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
    }

private:
    TSharedRef<SDockTab> Spawn(const FSpawnTabArgs&)
    {
        auto Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SGridWorkbench)];
        WorkbenchTab = Tab;
        return Tab;
    }

    TSharedRef<SWidget> MakeLaunchOverlay()
    {
        return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.022f, .038f, .058f))
            .HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(20)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(LOCTEXT("LaunchTitle", "推箱子战斗"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 18)
                [
                    SNew(STextBlock).Text(LOCTEXT("LaunchLevel", "启动关卡"))
                    .ColorAndOpacity(FLinearColor(.62f, .80f, .87f))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [
                    SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("LaunchPlay", "播放游戏"))
                    .OnClicked_Lambda([] { PlayLaunchMap(); return FReply::Handled(); })
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("LaunchEdit", "打开关卡编辑器"))
                    .OnClicked_Lambda([] { OpenWorkbench(); return FReply::Handled(); })
                ]
            ];
    }

    void DetachLaunchOverlay()
    {
        const auto Viewport = OverlayViewport.Pin();
        if (Viewport && LaunchOverlay) { Viewport->RemoveOverlayWidget(LaunchOverlay.ToSharedRef()); }
        OverlayViewport.Reset();
        LaunchOverlay.Reset();
    }

    bool UpdateViewport(float)
    {
        const auto* World = GEditor && !GEditor->PlayWorld ? GEditor->GetEditorWorldContext().World() : nullptr;
        const FString Map = World ? World->GetOutermost()->GetName() : FString();
        if (Map == WorkbenchMap)
        {
            for (TActorIterator<AGridScene> It(World); It; ++It)
            {
                const bool bHide = !WorkbenchTab.IsValid();
                if (It->IsTemporarilyHiddenInEditor() != bHide) { It->SetIsTemporarilyHiddenInEditor(bHide); }
            }
        }

        auto* Editor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
        const auto Viewport = Editor && Map == GetLaunchMap() ? Editor->GetFirstActiveViewport() : nullptr;
        if (Viewport != OverlayViewport.Pin() || (!Viewport && LaunchOverlay))
        {
            DetachLaunchOverlay();
            if (Viewport)
            {
                LaunchOverlay = MakeLaunchOverlay();
                OverlayViewport = Viewport;
                Viewport->AddOverlayWidget(LaunchOverlay.ToSharedRef(), 10);
            }
        }
        return true;
    }

    FTSTicker::FDelegateHandle ViewportTicker;
    TWeakPtr<IAssetViewport> OverlayViewport;
    TSharedPtr<SWidget> LaunchOverlay;
    TWeakPtr<SDockTab> WorkbenchTab;

    void RegisterMenu()
    {
        FToolMenuOwnerScoped Owner(this);
        auto& Section = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window")->FindOrAddSection("Sokoban", LOCTEXT("MenuSection", "推箱子工具"));
        Section.AddMenuEntry(TabId, LOCTEXT("Menu", "推箱子关卡编辑器"), LOCTEXT("Tip", "在网格上编辑当前关卡草稿。"),
            FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&OpenWorkbench)));
    }
};
}
IMPLEMENT_MODULE(FSokobanWorkbenchModule, SokobanWorkbench)

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridStructureTest, "Sokoban.Workbench.ReusableStructure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridStructureTest::RunTest(const FString&)
{
    TestTrue(TEXT("A 1x4 brush fits at the bottom edge"), UGridStructureAsset::Fits({8, 6}, {7, 2}, {1, 4}));
    TestFalse(TEXT("A 1x4 brush is never clipped"), UGridStructureAsset::Fits({8, 6}, {7, 3}, {1, 4}));

    const auto Source = NewObject<UGridLevelAsset>();
    Source->Walls.Add({0, 0});
    Source->Rubble.Add({0, 1});
    Source->VoidCells.Add({0, 2});
    auto& Box = Source->Boxes.AddDefaulted_GetRef();
    Box.Cell = {1, 2};
    Box.Spike = {1, 0};
    Box.bEject = true;
    auto& Enemy = Source->Enemies.AddDefaulted_GetRef();
    Enemy.Cell = {2, 1};
    Enemy.BeamDirection = {-1, 0};

    FText Error;
    const auto Structure = NewObject<UGridStructureAsset>();
    Source->Walls.Add({1, 1});
    TSet<FIntPoint> Selection;
    SelectCells(Selection, GridSelectionRect({2, 2}, {0, 2}), false);
    TestEqual(TEXT("Backward drag selects both ends"), Selection.Num(), 3);
    SelectCells(Selection, GridSelectionRect({0, 0}, {0, 2}), true);
    TestEqual(TEXT("Supplemental selection merges overlapping cells"), Selection.Num(), 5);
    SelectCells(Selection, GridSelectionRect({1, 2}, {1, 2}), false);
    TestEqual(TEXT("A plain drag replaces the selection"), Selection.Num(), 1);
    TestFalse(TEXT("Cannot crop a spike"), Structure->Capture(*Source, Selection, Error));
    SelectCells(Selection, GridSelectionRect({0, 0}, {0, 2}), true);
    SelectCells(Selection, GridSelectionRect({2, 1}, {2, 2}), true);
    if (!TestTrue(TEXT("Capture disconnected selections"), Structure->Capture(*Source, Selection, Error)))
    {
        return false;
    }

    TestFalse(TEXT("Unselected cell inside bounds is not captured"), Structure->Walls.Contains({1, 1}));
    const auto Target = NewObject<UGridLevelAsset>();
    Target->Start = {0, 0};
    Target->Goal = {7, 5};
    Target->Walls.Add({5, 3});
    TestFalse(TEXT("Existing item in the destination spike cell blocks the whole stamp"),
        Structure->CanPlace(*Target, {3, 1}, Error));
    TestFalse(TEXT("A stamp cannot exceed the board"), Structure->CanPlace(*Target, {7, 1}, Error));
    TestFalse(TEXT("A stamp cannot overwrite start"), Structure->CanPlace(*Target, {0, 0}, Error));
    Target->Walls.Remove({5, 3});
    Target->Rubble.Add({4, 2});
    Structure->Walls.Add({1, 2});
    TestFalse(TEXT("An internal wall over the box is rejected on an empty destination"),
        Structure->CanPlace(*Target, {3, 1}, Error));
    Structure->Walls.Remove({1, 2});
    Source->Walls.Add({2, 2});
    TestFalse(TEXT("An internal wall over the spike cannot be captured"),
        Structure->Capture(*Source, Selection, Error));
    TestFalse(TEXT("Failed capture keeps the previous structure"), Structure->Walls.Contains({2, 2}));
    Source->Walls.Remove({2, 2});
    if (!TestTrue(TEXT("Blanks leave existing cells untouched"), Structure->CanPlace(*Target, {3, 1}, Error)))
    {
        return false;
    }
    Structure->Place(*Target, {3, 1});
    TestTrue(TEXT("Walls, rubble, and voids use target coordinates"), Target->Walls.Contains({3, 1}) &&
        Target->Rubble.Contains({3, 2}) && Target->Rubble.Contains({4, 2}) && Target->VoidCells.Contains({3, 3}));
    TestTrue(TEXT("Box retains behavior and receives a level-local ID"), Target->Boxes.Num() == 1 &&
        Target->Boxes[0].Cell == FIntPoint(4, 3) && Target->Boxes[0].Spike == FIntPoint(1, 0) &&
        Target->Boxes[0].bEject && Target->Boxes[0].Id == FName(TEXT("B_04_03")));
    TestTrue(TEXT("Enemy keeps its facing"), Target->Enemies.Num() == 1 &&
        Target->Enemies[0].Cell == FIntPoint(5, 2) && Target->Enemies[0].BeamDirection == FIntPoint(-1, 0));
    TestFalse(TEXT("A second stamp cannot replace placed elements"), Structure->CanPlace(*Target, {3, 1}, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridWorkbenchTabTest, "Sokoban.Workbench.OpensGridEditor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridWorkbenchTabTest::RunTest(const FString&)
{
    TestTrue(TEXT("Grid editor tab is registered"), FGlobalTabmanager::Get()->HasTabSpawner(TabId));
    const auto Panel = SNew(SGridWorkbench);
    TestTrue(TEXT("Grid editor constructs Slate content"), Panel->GetChildren()->Num() > 0);
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    Registry.WaitForCompletion();
    Panel->RefreshLibrary();
    TestTrue(TEXT("Saved workbench draft is listed"), Panel->SavedDrafts.ContainsByPredicate(
        [](const TSharedPtr<FAssetData>& Entry) {
            return Entry && Entry->PackageName == FName(TEXT("/Game/Maps/DA_Workbench"));
        }));
    Panel->DraftSearch = TEXT("No_Matching_Draft_87642");
    Panel->FilterLibrary();
    TestTrue(TEXT("Draft library filters names and paths"), Panel->SavedDrafts.IsEmpty());
    Panel->DraftSearch.Reset();
    Panel->FilterLibrary();
    TestFalse(TEXT("Clearing the search restores draft results"), Panel->SavedDrafts.IsEmpty());
    auto* Draft = NewObject<UGridLevelAsset>();
    Draft->Size = {8, 6};
    Draft->Start = {1, 1};
    Draft->Goal = {4, 3};
    TestFalse(TEXT("Shrink keeps placed cells"), Panel->FindCroppedObject(*Draft, {5, 4}).IsSet());
    TestTrue(TEXT("Shrink protects goal"), Panel->FindCroppedObject(*Draft, {4, 4}).IsSet());
    auto& Box = Draft->Boxes.AddDefaulted_GetRef();
    Box.Cell = {4, 1};
    Box.Spike = {1, 0};
    const FName BoxId(TEXT("MoveTestBox"));
    Box.Id = BoxId;
    const auto Cropped = Panel->FindCroppedObject(*Draft, {5, 4});
    TestTrue(TEXT("Shrink protects a spike even when its box still fits"), Cropped.IsSet() && *Cropped == FIntPoint(5, 1));

    Panel->BatchSize = {1, 4};
    Panel->Brush = EBrush::Wall;
    Draft->VoidCells.Add({6, 4});
    Panel->PaintDraft(*Draft, {6, 2});
    TestTrue(TEXT("A void rejects the entire batch"), Draft->Walls.IsEmpty());
    Draft->VoidCells.Reset();
    Panel->PaintDraft(*Draft, {6, 3});
    TestTrue(TEXT("An out-of-bounds batch changes nothing"), Draft->Walls.IsEmpty());
    Panel->PaintDraft(*Draft, {6, 2});
    TestTrue(TEXT("Batch paint places all four cells"), Draft->Walls.Num() == 4 && Draft->Walls.Contains({6, 2}) &&
        Draft->Walls.Contains({6, 3}) && Draft->Walls.Contains({6, 4}) && Draft->Walls.Contains({6, 5}));
    Panel->Brush = EBrush::Erase;
    Panel->PaintDraft(*Draft, {6, 2});
    TestTrue(TEXT("Batch erase removes all four cells"), Draft->Walls.IsEmpty());
    Panel->Brush = EBrush::Wall;
    Panel->BatchSize = {1, 2};
    Panel->StrokeEnd = {0, 4};
    Panel->ExtendBrushStroke(*Draft, {3, 4});
    Panel->ExtendBrushStroke(*Draft, {0, 4});
    TestEqual(TEXT("Fast drag fills intermediate anchors once"), Panel->StrokeCells.Num(), 8);
    Panel->ExtendBrushStroke(*Draft, {0, 5});
    TestFalse(TEXT("Invalid footprint is skipped"), Panel->StrokeError.IsEmpty());
    TestEqual(TEXT("Valid part of stroke is retained"), Panel->StrokeCells.Num(), 8);
    Panel->CommitBrushCells(*Draft, Panel->StrokeCells);
    TestEqual(TEXT("Stroke is painted as one batch"), Draft->Walls.Num(), 8);

    auto* World = Panel->CurrentWorld();
    if (!TestNotNull(TEXT("Editor world for routed input test"), World)) { return false; }
    FActorSpawnParameters Params;
    Params.ObjectFlags = RF_Transient;
    Params.bTemporaryEditorActor = true;
    auto* Scene = World->SpawnActor<AGridScene>(Params);
    if (!TestNotNull(TEXT("Temporary workbench preview"), Scene)) { return false; }
    Scene->DraftLevel = Draft;
    Panel->Scene = Scene;
    Draft->Walls.Reset();
    Panel->BatchSize = {1, 1};
    Panel->RebuildGrid();

    auto Window = SNew(SVirtualWindow).Size(FVector2D(1350, 920));
    Window->SetContent(Panel);
    auto& Slate = FSlateApplication::Get();
    Slate.RegisterVirtualWindow(Window);
    const auto PathToCell = [&](FIntPoint Cell, FWidgetPath& Path) {
        Window->SlatePrepass();
        const auto Border = Panel->GridHost->GetChildren()->GetChildAt(0);
        const auto Grid = Border->GetChildren()->GetChildAt(0);
        const int32 Index = Draft->Size.X + Draft->Size.Y + Cell.Y * Draft->Size.X + Cell.X;
        const auto Button = Grid->GetChildren()->GetChildAt(Index)->GetChildren()->GetChildAt(0);
        if (!Slate.GeneratePathToWidgetUnchecked(Button, Path, EVisibility::All)) { return false; }
        TArray<FWidgetAndPointer> PointerPath;
        for (int32 WidgetIndex = 0; WidgetIndex < Path.Widgets.Num(); ++WidgetIndex)
        {
            PointerPath.Emplace(Path.Widgets[WidgetIndex]);
        }
        Path = FWidgetPath(PointerPath);
        return true;
    };
    const auto Center = [](const FWidgetPath& Path) {
        const auto& Geometry = Path.Widgets[Path.Widgets.Num() - 1].Geometry;
        return FVector2D(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5f));
    };
    const TSet<FKey> Released;
    const FModifierKeysState Ctrl(false, false, true, false, false, false, false, false, false);
    const auto SendGesture = [&](FIntPoint First, FIntPoint Last, FKey Button = EKeys::LeftMouseButton, bool bCtrl = false) {
        const FModifierKeysState Keys = bCtrl ? Ctrl : FModifierKeysState();
        const TSet<FKey> Pressed{Button};
        FWidgetPath StartPath, EndPath;
        if (!PathToCell(First, StartPath) || !PathToCell(Last, EndPath)) { return false; }
        const FVector2D Start = Center(StartPath), End = Center(EndPath);
        const auto Down = Slate.RoutePointerDownEvent(StartPath,
            FPointerEvent(FSlateApplication::CursorPointerIndex, Start, Start, Pressed, Button, 0.f, Keys));
        if (First != Last)
        {
            Slate.RoutePointerMoveEvent(EndPath,
                FPointerEvent(FSlateApplication::CursorPointerIndex, End, Start, Pressed, EKeys::Invalid, 0.f, Keys), false);
        }
        const auto Up = Slate.RoutePointerUpEvent(EndPath,
            FPointerEvent(FSlateApplication::CursorPointerIndex, End, Start, Released, Button, 0.f, Keys));
        return Down.IsEventHandled() && Up.IsEventHandled();
    };
    TestTrue(TEXT("Slate routes a click through the grid"), SendGesture({0, 0}, {0, 0}));
    TestTrue(TEXT("A mouse click places a new component"), Draft->Walls.Contains({0, 0}));
    TestTrue(TEXT("Slate routes a drag through mouse capture"), SendGesture({1, 0}, {4, 0}));
    TestTrue(TEXT("Mouse drag paints through all intermediate cells"), Draft->Walls.Num() == 5 &&
        Draft->Walls.Contains({2, 0}) && Draft->Walls.Contains({3, 0}) && Draft->Walls.Contains({4, 0}));
    Panel->SetStructureTool(EStructureTool::Capture);
    TestTrue(TEXT("Slate routes a structure selection drag"), SendGesture({1, 0}, {3, 1}));
    TestEqual(TEXT("Structure selection captures all cells in the rectangle"), Panel->SelectedCells.Num(), 6);
    Panel->SetStructureTool(EStructureTool::None);
    TestTrue(TEXT("Right drag moves an existing wall"), SendGesture({0, 0}, {0, 1}, EKeys::RightMouseButton));
    TestTrue(TEXT("Wall leaves its original cell"), !Draft->Walls.Contains({0, 0}) && Draft->Walls.Contains({0, 1}));
    TestTrue(TEXT("Undo is available after moving a component"), Panel->CanApplyHistory(false));
    Panel->ApplyHistory(false);
    TestTrue(TEXT("Workbench undo restores the original cell"), Draft->Walls.Contains({0, 0}) && !Draft->Walls.Contains({0, 1}));
    TestTrue(TEXT("Redo is available after undo"), Panel->CanApplyHistory(true));
    Panel->ApplyHistory(true);
    TestTrue(TEXT("Workbench redo restores the move"), !Draft->Walls.Contains({0, 0}) && Draft->Walls.Contains({0, 1}));
    TestTrue(TEXT("Right drag handles an occupied destination"), SendGesture({0, 1}, {1, 0}, EKeys::RightMouseButton));
    TestTrue(TEXT("An occupied destination preserves both walls"), Draft->Walls.Contains({0, 1}) && Draft->Walls.Contains({1, 0}));

    Draft->Boxes[0].bEject = true;
    TestTrue(TEXT("The spike tip can be used to move its box"), SendGesture({5, 1}, {5, 2}, EKeys::RightMouseButton));
    TestTrue(TEXT("Moved box keeps its ID, facing and modules"), Draft->Boxes[0].Cell == FIntPoint(4, 2) &&
        Draft->Boxes[0].Spike == FIntPoint(1, 0) && Draft->Boxes[0].bEject && Draft->Boxes[0].Id == BoxId);
    Draft->Goal = {7, 5};
    Draft->VoidCells.Add({5, 3});
    TestTrue(TEXT("Right drag handles an invalid spike destination"), SendGesture({4, 2}, {4, 3}, EKeys::RightMouseButton));
    TestTrue(TEXT("Invalid spike destination keeps the box in place"), Draft->Boxes[0].Cell == FIntPoint(4, 2));
    Panel->ComponentSearch = BoxId.ToString();
    Panel->RebuildComponents();
    TestTrue(TEXT("Component list filters by ID and carries the updated coordinate"), Panel->ComponentRows.Num() == 1 &&
        Panel->ComponentRows[0]->Cell == FIntPoint(4, 2));
    Panel->SelectComponent({5, 2}, false);
    TestTrue(TEXT("Selecting a spike highlights its box"), Panel->SelectedComponent.IsSet() &&
        Panel->SelectedComponent.GetValue() == FIntPoint(4, 2));
    Panel->EditSelectedComponent({4, 2}, [](UGridLevelAsset& Level, const FGridMovePiece& Piece) {
        Level.Boxes[Piece.Index].bEject = false;
    });
    TestFalse(TEXT("Inspector edits the selected box"), Draft->Boxes[0].bEject);
    Panel->ApplyHistory(false);
    TestTrue(TEXT("Inspector change is undoable"), Draft->Boxes[0].bEject);

    Draft->RoomId = TEXT("WorkbenchRegression");
    Panel->Brush = EBrush::Box;
    TestTrue(TEXT("Place a box at its original coordinate"), SendGesture({3, 4}, {3, 4}));
    const FName PaintedId = Draft->Boxes.Last().Id;
    TestTrue(TEXT("Move the newly placed box"), SendGesture({3, 4}, {3, 5}, EKeys::RightMouseButton));
    Draft->Enemies.Add({TEXT("B_03_04_1"), {1, 5}});
    TestTrue(TEXT("Place another box in the vacated cell"), SendGesture({3, 4}, {3, 4}));
    TestTrue(TEXT("Moving keeps the ID and painting skips IDs held by both boxes and enemies"),
        Draft->Boxes.Num() == 3 && Draft->Boxes[1].Id == PaintedId && Draft->Boxes[1].Cell == FIntPoint(3, 5) &&
        Draft->Boxes[2].Id == FName(TEXT("B_03_04_2")) && Draft->GetLayoutWarnings().IsEmpty());

    Panel->SelectComponent({4, 2}, false);
    Panel->EditSelectedComponent({4, 2}, [](UGridLevelAsset& Level, const FGridMovePiece& Piece) {
        auto& Selected = Level.Boxes[Piece.Index];
        Selected.bExplodes = true;
        Selected.bExplodeWhenEjectBlocked = true;
        Selected.SetExplosionEnabled(false);
    });
    TestTrue(TEXT("Turning off explosion clears dependent options and remains legal"),
        !Draft->Boxes[0].bExplodes && !Draft->Boxes[0].bExplodeWhenEjectBlocked &&
        Draft->GetLayoutWarnings().IsEmpty());

    Draft->Start = {0, 5};
    Draft->Walls = {{1, 1}, {2, 1}};
    Draft->VoidCells = {{5, 3}};
    Draft->Boxes.SetNum(1);
    Draft->Boxes[0].Cell = {3, 2};
    Draft->Enemies = {{TEXT("MoveAndCopyLaser"), {2, 3}, {0, -1}}};
    Draft->bConfigureBeamTerrain = true;
    Panel->Refresh();
    const FKey Right = EKeys::RightMouseButton;
    TestTrue(TEXT("Right drag on empty floor selects a rectangle"), SendGesture({0, 0}, {2, 1}, Right));
    TestTrue(TEXT("Control right click adds a spike box by its tip"), SendGesture({4, 2}, {4, 2}, Right, true));
    TestTrue(TEXT("Control right drag adds a second rectangle"), SendGesture({1, 3}, {2, 3}, Right, true));
    TestEqual(TEXT("The cumulative selection contains both walls, box, and laser"), Panel->MoveSelection.Num(), 4);
    TestTrue(TEXT("Right drag on a selected wall translates the group"), SendGesture({1, 1}, {2, 1}, Right));
    TestTrue(TEXT("The group reuses its vacated source cell and keeps entity directions"),
        Draft->Walls.Num() == 2 && Draft->Walls.Contains({2, 1}) && Draft->Walls.Contains({3, 1}) &&
        Draft->Boxes[0].Cell == FIntPoint(4, 2) && Draft->Boxes[0].Spike == FIntPoint(1, 0) &&
        Draft->Enemies[0].Cell == FIntPoint(3, 3) && Draft->Enemies[0].BeamDirection == FIntPoint(0, -1));
    TestTrue(TEXT("The group drag handles a blocked spike destination"), SendGesture({2, 1}, {2, 2}, Right));
    TestTrue(TEXT("One invalid destination leaves the entire group in place"),
        Draft->Walls.Contains({2, 1}) && Draft->Walls.Contains({3, 1}) &&
        Draft->Boxes[0].Cell == FIntPoint(4, 2) && Draft->Enemies[0].Cell == FIntPoint(3, 3));
    Panel->ApplyHistory(false);
    TestTrue(TEXT("One undo restores the entire move"), Draft->Walls.Contains({1, 1}) &&
        !Draft->Walls.Contains({3, 1}) && Draft->Boxes[0].Cell == FIntPoint(3, 2) &&
        Draft->Enemies[0].Cell == FIntPoint(2, 3));

    TestTrue(TEXT("Select the spike box for copying"), SendGesture({4, 2}, {4, 2}, Right));
    TestTrue(TEXT("Add the laser and start marker to the copy selection"),
        SendGesture({2, 3}, {2, 3}, Right, true) && SendGesture({0, 5}, {0, 5}, Right, true));
    Panel->OnKeyDown(FGeometry(), FKeyEvent(EKeys::C, Ctrl, 0, false, 0, 0));
    Panel->OnKeyDown(FGeometry(), FKeyEvent(EKeys::V, Ctrl, 0, false, 0, 0));
    TestTrue(TEXT("Paste mode rejects an occupied destination"), SendGesture({3, 1}, {3, 1}));
    TestTrue(TEXT("A blocked paste makes no partial changes"), Draft->Boxes.Num() == 1 && Draft->Enemies.Num() == 1);
    TestTrue(TEXT("Place the copied selection at a new anchor"), SendGesture({4, 0}, {4, 0}));
    TestTrue(TEXT("Paste preserves facing, relative positions and modules and allocates independent IDs"),
        Draft->Boxes.Num() == 2 && Draft->Enemies.Num() == 2 &&
        Draft->Boxes[1].Cell == FIntPoint(5, 0) && Draft->Boxes[1].Spike == FIntPoint(1, 0) &&
        Draft->Boxes[1].bEject && Draft->Boxes[1].Id != Draft->Boxes[0].Id &&
        Draft->Enemies[1].Cell == FIntPoint(4, 1) && Draft->Enemies[1].BeamDirection == FIntPoint(0, -1) &&
        Draft->Enemies[1].Id != Draft->Enemies[0].Id && Draft->Start == FIntPoint(0, 5));
    Panel->ApplyHistory(false);
    TestTrue(TEXT("One undo removes the full paste without touching the source"),
        Draft->Boxes.Num() == 1 && Draft->Boxes[0].Cell == FIntPoint(3, 2) &&
        Draft->Enemies.Num() == 1 && Draft->Enemies[0].Cell == FIntPoint(2, 3));

    FPropertyChangedEvent SizeChanged(FindFProperty<FProperty>(UGridLevelAsset::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UGridLevelAsset, Size)), EPropertyChangeType::ValueSet);
    Draft->Size.X = Sokoban::FLevelGate::MaxBoardSide + 1;
    Draft->PostEditChangeProperty(SizeChanged);
    TestTrue(TEXT("Native property editing refreshes shared layout warnings and the input dimensions"),
        Panel->LayoutWarnings.Contains(Sokoban::FLevelGate::BoardSizeError()) && Panel->PendingSize == Draft->Size);
    Draft->Size.X = 8;
    Draft->PostEditChangeProperty(SizeChanged);
    TestTrue(TEXT("Correcting the size immediately clears the workbench warning"), Panel->LayoutWarnings.IsEmpty());

    Slate.UnregisterVirtualWindow(Window);
    World->DestroyActor(Scene, false, false);

    // Exercise the startup-map route in the headless editor without changing any saved assets.
    if (!FApp::CanEverRender() && World->GetOutermost()->GetName() == GetLaunchMap() &&
        !World->GetOutermost()->IsDirty())
    {
        auto* Fixture = LoadObject<UGridLevelAsset>(nullptr, TEXT("/Game/Maps/Drafts/DA_Grid_001.DA_Grid_001"));
        if (!TestNotNull(TEXT("Saved draft for startup map load"), Fixture)) { return false; }
        TestNull(TEXT("No preview scene is bound on the startup map"), Panel->ActiveScene());
        FText OpenError;
        const bool bLoaded = Panel->BindDraftEntry(FAssetData(Fixture), OpenError);
        auto* EditingWorld = Panel->CurrentWorld();
        TestTrue(FString::Printf(TEXT("Loading from the startup map opens the workbench (%s)"), *OpenError.ToString()),
            bLoaded && EditingWorld && EditingWorld->GetOutermost()->GetName() == WorkbenchMap);
        TestTrue(TEXT("The selected saved draft is now bound"), Panel->Asset() == Fixture);
        if (bLoaded) { AddInfo(TEXT("Startup map load route exercised")); }
        if (EditingWorld && EditingWorld->GetOutermost()->GetName() == WorkbenchMap)
        {
            EditingWorld->GetOutermost()->SetDirtyFlag(false);
            TestTrue(TEXT("Return to the startup map after testing"), OpenEditorMap(GetLaunchMap()));
        }
    }
    return true;
}
#endif
#undef LOCTEXT_NAMESPACE



