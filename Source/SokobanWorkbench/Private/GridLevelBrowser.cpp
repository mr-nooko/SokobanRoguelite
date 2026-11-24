#include "GridLevelBrowser.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "GridLevelAsset.h"
#include "GridCampaignSequence.h"
#include "ObjectTools.h"
#include "Misc/MessageDialog.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STileView.h"

#define LOCTEXT_NAMESPACE "SokobanLevelBrowser"

namespace
{
const FLinearColor FloorColor(.085f, .12f, .17f);
const FLinearColor WallColor(.38f, .44f, .53f);
const FLinearColor RubbleColor(.49f, .25f, .12f);
const FLinearColor StartColor(.10f, .61f, .83f);
const FLinearColor GoalColor(.18f, .71f, .34f);
const FLinearColor BoxColor(.78f, .51f, .095f);
const FLinearColor SpikeColor(.58f, .28f, .77f);
const FLinearColor BlastColor(.84f, .31f, .09f);
const FLinearColor EnemyColor(.77f, .12f, .26f);

TArray<FString> FindSequenceReferences(const UGridLevelAsset& Draft)
{
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    Registry.WaitForCompletion();
    TArray<FAssetData> SequenceAssets;
    Registry.GetAssetsByClass(UGridCampaignSequence::StaticClass()->GetClassPathName(), SequenceAssets, true);
    TArray<FString> References;
    for (const auto& SequenceAsset : SequenceAssets)
    {
        const auto* Sequence = Cast<UGridCampaignSequence>(SequenceAsset.GetAsset());
        if (!Sequence) { continue; }
        const FString SequenceName = SequenceAsset.PackageName.ToString();
        for (const auto& Entry : Sequence->PlacedRooms)
        {
            if (Entry.Level == &Draft) { References.Add(FString::Printf(TEXT("%s · 第 %d 关"), *SequenceName, Entry.Position)); }
        }
        for (int32 Index = 0; Index < Sequence->Slots.Num(); ++Index)
        {
            const auto& Slot = Sequence->Slots[Index];
            if (Slot.Level == &Draft || Slot.Fallback == &Draft)
            {
                References.Add(FString::Printf(TEXT("%s · 旧版槽位 %d"), *SequenceName, Index + 1));
            }
        }
        if (Sequence->AutoRooms.Fallback == &Draft) { References.Add(SequenceName + TEXT(" · 自定义后备关")); }
    }

    return References;
}

class SGridDraftPreview final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SGridDraftPreview) {}
        SLATE_ARGUMENT(const UGridLevelAsset*, Draft)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        const auto* Draft = Args._Draft;
        if (!Draft || !Sokoban::FLevelGate::SupportsBoardSize(Draft->Size)) { return; }
        Size = Draft->Size;
        Cells.Init(FloorColor, Size.X * Size.Y);
        const auto Set = [this](FIntPoint Cell, FLinearColor Color) {
            if (Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Size.X && Cell.Y < Size.Y)
            {
                Cells[Cell.Y * Size.X + Cell.X] = Color;
            }
        };
        for (FIntPoint Cell : Draft->Walls) { Set(Cell, WallColor); }
        for (FIntPoint Cell : Draft->Rubble) { Set(Cell, RubbleColor); }
        for (const auto& Box : Draft->Boxes)
        {
            const auto Role = GetGridBoxVisualRole(Box.Spike, Box.bExplodes, Box.bEject);
            Set(Box.Cell, Role == EGridVisualRole::ExplosiveCrate ? BlastColor : Role == EGridVisualRole::SpikeCrate ? SpikeColor :
                Role == EGridVisualRole::EjectCrate ? StartColor * .7f : BoxColor);
            if (Box.Spike != FIntPoint::ZeroValue) { Set(Box.Cell + Box.Spike, SpikeColor * .65f); }
        }
        for (const auto& Enemy : Draft->Enemies) { Set(Enemy.Cell, EnemyColor); }
        Set(Draft->Start, StartColor);
        Set(Draft->Goal, GoalColor);
        for (FIntPoint Cell : Draft->VoidCells) { Set(Cell, FLinearColor::Transparent); }
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D(140.f, 96.f); }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const FVector2f Available = Geometry.GetLocalSize();
        if (Cells.IsEmpty() || Available.X < 6.f || Available.Y < 6.f) { return Layer; }
        const float Scale = FMath::Min((Available.X - 4.f) / Size.X, (Available.Y - 4.f) / Size.Y);
        const float Gap = Scale >= 5.f ? 1.f : 0.f;
        const FVector2f Offset = (Available - FVector2f(Size.X * Scale, Size.Y * Scale)) * .5f;
        const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        for (int32 Y = 0; Y < Size.Y; ++Y)
        {
            for (int32 X = 0; X < Size.X; ++X)
            {
                const FLinearColor Color = Cells[Y * Size.X + X];
                if (Color.A <= 0.f) { continue; }
                const FVector2f Position = Offset + FVector2f(X * Scale, Y * Scale);
                FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
                    FVector2f(Scale - Gap, Scale - Gap), FSlateLayoutTransform(Position)),
                    Brush, ESlateDrawEffect::None, Color);
            }
        }
        return Layer;
    }

private:
    FIntPoint Size = FIntPoint::ZeroValue;
    TArray<FLinearColor> Cells;
};
}

const FString SGridLevelBrowser::DraftRoot(TEXT("/Game/Maps/Drafts"));

void SGridLevelBrowser::Construct(const FArguments& Args)
{
    Close = Args._OnClose;
    DraftDeleted = Args._OnDraftDeleted;
    OpenDraft = Args._OnOpenDraft;
    CreateDraft = Args._OnCreateDraft;
    CanOpenDraft = Args._CanOpenDraft;
    for (const auto& Entry : Args._Entries)
    {
        Entries.Add(MakeShared<FBrowserItem>(FBrowserItem{Entry, {}}));
    }

    ChildSlot
    [
        SNew(SBorder).Padding(14)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ SNew(SSearchBox).HintText(LOCTEXT("Search", "搜索草稿名称或路径"))
                    .OnTextChanged(this, &SGridLevelBrowser::Filter) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
                [ SNew(STextBlock).Text_Lambda([this] {
                    return FText::Format(LOCTEXT("Count", "{0} 项"), VisibleEntries.Num());
                }) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton).Text(LOCTEXT("UpFolder", "上一级"))
                    .IsEnabled_Lambda([this] { return CurrentFolder != DraftRoot; })
                    .OnClicked_Lambda([this] { OpenFolder(FPaths::GetPath(CurrentFolder)); return FReply::Handled(); })
                ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0)
                [
                    SNew(STextBlock).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    .Text_Lambda([this] { return CurrentFolder == DraftRoot ? LOCTEXT("AllDrafts", "全部草稿") :
                        FText::FromString(CurrentFolder.Mid(DraftRoot.Len() + 1)); })
                    .ToolTipText_Lambda([this] { return FText::FromString(CurrentFolder); })
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(140.f)
                    [
                        SAssignNew(FolderName, SEditableTextBox).HintText(LOCTEXT("FolderName", "文件夹名称"))
                        .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit) {
                            if (Commit == ETextCommit::OnEnter) { CreateFolder(); }
                        })
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(LOCTEXT("NewFolder", "新建文件夹")).OnClicked(this, &SGridLevelBrowser::CreateFolder) ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton).Text(LOCTEXT("NewDraftInFolder", "在此新建草稿"))
                    .IsEnabled_Lambda([this] { return CanOpenDraft.Get(false) && CreateDraft.IsBound(); })
                    .OnClicked_Lambda([this] {
                        StatusMessage = FText::GetEmpty();
                        if (CreateDraft.IsBound() && CreateDraft.Execute(CurrentFolder, StatusMessage) && Close.IsBound())
                        {
                            Close.Execute();
                        }
                        return FReply::Handled();
                    })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [
                SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return StatusMessage; })
                .Visibility_Lambda([this] { return StatusMessage.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
            ]
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SAssignNew(Tiles, STileView<FEntry>)
                        .ListItemsSource(&VisibleEntries)
                        .SelectionMode(ESelectionMode::Single)
                        .ItemWidth(170.f).ItemHeight(129.f)
                        .OnGenerateTile(this, &SGridLevelBrowser::MakeTile)
                        .OnSelectionChanged_Lambda([this](FEntry Entry, ESelectInfo::Type) { Select(Entry); })
                        .OnMouseButtonDoubleClick(this, &SGridLevelBrowser::Open)
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Visibility_Lambda([this] {
                        return VisibleEntries.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    }).Text(LOCTEXT("NoResults", "此目录没有符合条件的项目")) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(14, 0, 0, 0)
                [ SNew(SBox).WidthOverride(294.f) [ SAssignNew(Preview, SBox) ] ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text_Lambda([this] {
                    return CanOpenDraft.Get(false) ? LOCTEXT("OpenTip", "单击预览，双击打开") :
                        LOCTEXT("NoEditorWorld", "请先结束试玩，再载入关卡。");
                }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(LOCTEXT("Locate", "在内容浏览器中定位"))
                    .IsEnabled_Lambda([this] { return Selected.IsValid() && !Selected->IsFolder(); }).OnClicked(this, &SGridLevelBrowser::LocateSelected) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
                    .Text_Lambda([this] { return Selected && Selected->IsFolder() ? LOCTEXT("OpenFolder", "打开文件夹") :
                        LOCTEXT("Open", "载入关卡"); })
                    .IsEnabled_Lambda([this] { return Selected.IsValid() && (Selected->IsFolder() || CanOpenDraft.Get(false)); })
                    .OnClicked(this, &SGridLevelBrowser::OpenSelected) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [
                    SNew(SButton).Text(LOCTEXT("DeleteDraft", "删除关卡"))
                    .IsEnabled_Lambda([this] { return CanOpenDraft.Get(false) && Selected && !Selected->IsFolder(); })
                    .OnClicked(this, &SGridLevelBrowser::DeleteSelected)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                [ SNew(SButton).Text(LOCTEXT("Close", "关闭")).OnClicked(Close) ]
            ]
        ]
    ];
    RefreshItems();
}

void SGridLevelBrowser::Filter(const FText& Text)
{
    Search = Text.ToString().TrimStartAndEnd();
    RefreshItems();
}

void SGridLevelBrowser::OpenFolder(const FString& Folder)
{
    CurrentFolder = Folder;
    StatusMessage = FText::GetEmpty();
    RefreshItems();
    Tiles->ScrollToTop();
}

void SGridLevelBrowser::RefreshItems()
{
    Tiles->ClearSelection();
    Select(nullptr);
    VisibleEntries.Reset();

    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    TArray<FString> Folders;
    Registry.GetSubPaths(CurrentFolder, Folders, false);
    FString Directory;
    if (FPackageName::TryConvertLongPackageNameToFilename(CurrentFolder, Directory))
    {
        // Also discover empty folders that have no assets indexed after an editor restart.
        TArray<FString> OnDisk;
        IFileManager::Get().FindFiles(OnDisk, *(Directory / TEXT("*")), false, true);
        for (const auto& Leaf : OnDisk)
        {
            const FString Path = CurrentFolder / Leaf;
            if (FPackageName::IsValidLongPackageName(Path)) { Folders.AddUnique(Path); }
        }
    }
    Folders.Sort();
    for (const auto& Folder : Folders)
    {
        if (Search.IsEmpty() || FPaths::GetCleanFilename(Folder).Contains(Search))
        {
            VisibleEntries.Add(MakeShared<FBrowserItem>(FBrowserItem{{}, Folder}));
        }
    }
    for (const auto& Entry : Entries)
    {
        const FString PackageName = Entry->Asset.PackageName.ToString();
        if ((CurrentFolder == DraftRoot || Entry->Asset.PackagePath.ToString() == CurrentFolder) &&
            (Search.IsEmpty() || PackageName.Contains(Search)))
        {
            VisibleEntries.Add(Entry);
        }
    }
    Tiles->RequestListRefresh();
}

FReply SGridLevelBrowser::CreateFolder()
{
    const FString Name = FolderName->GetText().ToString().TrimStartAndEnd();
    const FString Path = CurrentFolder / Name;
    if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) ||
        !FPackageName::IsValidLongPackageName(Path))
    {
        StatusMessage = LOCTEXT("InvalidFolder", "请输入有效的文件夹名称，名称中不能包含路径分隔符。");
        return FReply::Handled();
    }

    auto* BrowserData = GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>();
    FText Error;
    if (BrowserData && BrowserData->CanCreateFolder(BrowserData->ConvertInternalPathToVirtual(*CurrentFolder), &Error))
    {
        const auto Pending = BrowserData->CreateFolder(BrowserData->ConvertInternalPathToVirtual(*Path));
        if (Pending.IsValid() && Pending.ValidateItem(Name, &Error) && Pending.FinalizeItem(Name, &Error).IsValid())
        {
            FolderName->SetText(FText::GetEmpty());
            OpenFolder(Path);
            StatusMessage = FText::Format(LOCTEXT("FolderCreated", "已创建文件夹 {0}。"), FText::FromString(Name));
            return FReply::Handled();
        }
    }
    StatusMessage = Error.IsEmpty() ? LOCTEXT("FolderFailed", "无法创建文件夹，请检查名称和目录权限。") : Error;
    return FReply::Handled();
}

TSharedRef<ITableRow> SGridLevelBrowser::MakeTile(FEntry Entry, const TSharedRef<STableViewBase>& Owner)
{
    if (Entry->IsFolder())
    {
        const FText Name = FText::FromString(FPaths::GetCleanFilename(Entry->Folder));
        return SNew(STableRow<FEntry>, Owner).Padding(5).ToolTipText(FText::FromString(Entry->Folder))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(46.f).HeightOverride(46.f)
                [ SNew(SImage).Image(FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed")) ] ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(4, 2)
            [ SNew(STextBlock).Text(Name).OverflowPolicy(ETextOverflowPolicy::Ellipsis) ]
        ];
    }
    const FAssetData& Asset = Entry->Asset;
    const auto* Draft = Cast<UGridLevelAsset>(Asset.GetAsset());
    return SNew(STableRow<FEntry>, Owner)
        .Padding(3)
        .ToolTipText(FText::FromName(Asset.PackageName))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().FillHeight(1.f)
            [ SNew(SGridDraftPreview).Draft(Draft) ]
            + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
            [ SNew(STextBlock).Text(FText::FromName(Asset.AssetName)).OverflowPolicy(ETextOverflowPolicy::Ellipsis) ]
            + SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 3)
            [ SNew(STextBlock).Text(Draft ? FText::Format(LOCTEXT("Size", "{0} × {1} 格"), Draft->Size.X, Draft->Size.Y)
                : LOCTEXT("Unavailable", "资产无法读取")) ]
        ];
}

void SGridLevelBrowser::Select(FEntry Entry)
{
    if (Selected != Entry) { StatusMessage = FText::GetEmpty(); }
    Selected = Entry;
    if (Selected && Selected->IsFolder())
    {
        Preview->SetContent(SNew(SBorder).Padding(12)
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Selected->Folder)) ]);
        return;
    }
    const auto* Draft = Selected ? Cast<UGridLevelAsset>(Selected->Asset.GetAsset()) : nullptr;
    if (!Draft)
    {
        Preview->SetContent(SNew(SBorder).Padding(12)
            [ SNew(STextBlock).AutoWrapText(true).Text(Selected ? LOCTEXT("AssetMissing", "无法读取所选草稿。")
                                                              : LOCTEXT("SelectionHint", "选择一张关卡缩略图以查看详情。")) ]);
        return;
    }
    const int32 Warnings = Draft->GetLayoutWarnings().Num();
    Preview->SetContent(SNew(SBorder).Padding(12)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                .Text(FText::FromName(Selected->Asset.AssetName)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::Format(LOCTEXT("RoomSummary", "{0}  ·  {1} × {2} 格"),
                FText::FromName(Draft->RoomId), Draft->Size.X, Draft->Size.Y)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
            [ SNew(SBox).HeightOverride(205.f) [ SNew(SGridDraftPreview).Draft(Draft) ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [ SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("Legend", "蓝色起点 · 绿色终点 · 灰色墙\n黄褐色普通箱 · 橙色爆炸箱 · 深蓝色弹射箱\n紫色尖刺与尖刺箱 · 红色敌人")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
            [ SNew(STextBlock).Text(FText::Format(LOCTEXT("ComponentSummary", "{0} 个箱子  ·  {1} 个敌人"),
                Draft->Boxes.Num(), Draft->Enemies.Num())) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(Warnings > 0 ? FText::Format(LOCTEXT("Warnings", "{0} 条布局警告"), Warnings)
                : LOCTEXT("LayoutOK", "静态布局检查通过")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 7)
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromName(Selected->Asset.PackageName)) ]
        ]);
}

void SGridLevelBrowser::Open(FEntry Entry)
{
    if (Entry && Entry->IsFolder())
    {
        OpenFolder(Entry->Folder);
        return;
    }
    if (!Entry || !CanOpenDraft.Get(false) || !OpenDraft.IsBound()) { return; }
    StatusMessage = FText::GetEmpty();
    if (OpenDraft.Execute(Entry->Asset, StatusMessage) && Close.IsBound()) { Close.Execute(); }
}

FReply SGridLevelBrowser::OpenSelected()
{
    Open(Selected);
    return FReply::Handled();
}

FReply SGridLevelBrowser::LocateSelected()
{
    if (Selected && !Selected->IsFolder())
    {
        FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets({Selected->Asset});
    }
    return FReply::Handled();
}

FReply SGridLevelBrowser::DeleteSelected()
{
    if (!Selected || Selected->IsFolder() || !CanOpenDraft.Get(false)) { return FReply::Handled(); }
    const FAssetData Asset = Selected->Asset;
    auto* Draft = Cast<UGridLevelAsset>(Asset.GetAsset());
    if (!Draft)
    {
        StatusMessage = LOCTEXT("DeleteUnreadable", "无法读取要删除的关卡，请重新打开浏览器。");
        return FReply::Handled();
    }

    const auto References = FindSequenceReferences(*Draft);

    const FText Prompt = References.IsEmpty()
        ? FText::Format(LOCTEXT("ConfirmDelete", "要删除关卡「{0}」吗？已检查关卡序列，未发现引用。"), FText::FromName(Asset.AssetName))
        : FText::Format(LOCTEXT("ConfirmReferencedDelete",
            "以下关卡序列仍引用「{0}」：\n{1}\n\n建议先在序列中替换关卡。选择「是」仍会强制删除并清空引用，缺少手工关资产的序列需要修复后才能开始。"),
            FText::FromName(Asset.AssetName), FText::FromString(FString::Join(References, TEXT("\n"))));
    if (FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No, Prompt) != EAppReturnType::Yes)
    {
        return FReply::Handled();
    }

    const TArray<UObject*> Objects{Draft};
    const int32 Deleted = References.IsEmpty() ? ObjectTools::DeleteObjects(Objects, false)
        : ObjectTools::ForceDeleteObjects(Objects, false);
    if (Deleted > 0)
    {
        Entries.RemoveAll([&](const FEntry& Entry) { return Entry->Asset.PackageName == Asset.PackageName; });
        RefreshItems();
        DraftDeleted.ExecuteIfBound();
        StatusMessage = FText::Format(LOCTEXT("DeletedDraft", "已删除关卡「{0}」。"), FText::FromName(Asset.AssetName));
    }
    else { StatusMessage = LOCTEXT("DeleteFailed", "关卡未删除。请检查引用或文件权限。"); }
    return FReply::Handled();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridLevelBrowserTest, "Sokoban.Workbench.LevelBrowser",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridLevelBrowserTest::RunTest(const FString&)
{
    auto* Draft = LoadObject<UGridLevelAsset>(nullptr, TEXT("/Game/Maps/DA_Workbench.DA_Workbench"));
    if (!TestNotNull(TEXT("Saved draft fixture"), Draft)) { return false; }
    const auto* Sequence = LoadObject<UGridCampaignSequence>(nullptr,
        TEXT("/Game/Maps/Campaign/DA_DefaultCampaign.DA_DefaultCampaign"));
    if (Sequence)
    {
        for (const auto& Entry : Sequence->PlacedRooms)
        {
            if (!Entry.Level) { continue; }
            const FString Expected = FString::Printf(TEXT("%s · 第 %d 关"), *Sequence->GetOutermost()->GetName(), Entry.Position);
            TestTrue(TEXT("Delete warning lists the saved sequence and hand-authored position"),
                FindSequenceReferences(*Entry.Level).Contains(Expected));
            break;
        }
    }
    int32 Opens = 0;
    int32 Closes = 0;
    bool bCanOpen = false;
    bool bCanBind = false;
    const auto Browser = SNew(SGridLevelBrowser)
        .Entries({FAssetData(Draft)})
        .CanOpenDraft_Lambda([&] { return bCanOpen; })
        .OnClose_Lambda([&] { ++Closes; return FReply::Handled(); })
        .OnOpenDraft_Lambda([&](const FAssetData& Entry, FText& Error) {
            ++Opens;
            TestEqual(TEXT("Browser opens the selected asset"), Entry.PackageName, Draft->GetOutermost()->GetFName());
            if (!bCanBind) { Error = FText::FromString(TEXT("Load cancelled")); }
            return bCanBind;
        });
    Browser->Filter(FText::FromString(TEXT("DA_Workbench")));
    TestEqual(TEXT("Saved draft appears in browser"), Browser->VisibleEntries.Num(), 1);
    if (Browser->VisibleEntries.Num() != 1) { return false; }
    Browser->Select(Browser->VisibleEntries[0]);
    TestTrue(TEXT("Preview is available without an active playtest scene"), Browser->Selected.IsValid());
    Browser->Open(Browser->Selected);
    TestEqual(TEXT("A draft does not open while the editor is unavailable"), Opens, 0);
    bCanOpen = true;
    Browser->OpenSelected();
    TestTrue(TEXT("The load button displays a bind failure and leaves the browser open"),
        Opens == 1 && Closes == 0 && !Browser->StatusMessage.IsEmpty());
    bCanBind = true;
    Browser->OpenSelected();
    TestTrue(TEXT("The load button opens a draft and closes the browser on success"),
        Opens == 2 && Closes == 1 && Browser->StatusMessage.IsEmpty());
    Browser->Open(Browser->Selected);
    TestTrue(TEXT("Double-click uses the same loading path"), Opens == 3 && Closes == 2);
    Browser->Filter(FText::FromString(TEXT("not_a_matching_asset_20196")));
    TestTrue(TEXT("Search removes unrelated tiles and clears stale selection"),
        Browser->VisibleEntries.IsEmpty() && !Browser->Selected.IsValid());
    Browser->Filter(FText::FromString(TEXT("da_workbench")));
    TestEqual(TEXT("Search matches names case-insensitively"), Browser->VisibleEntries.Num(), 1);
    Browser->Filter(FText::GetEmpty());
    Browser->FolderName->SetText(FText::FromString(TEXT("../invalid")));
    Browser->CreateFolder();
    TestTrue(TEXT("Path separators cannot create a folder"), Browser->CurrentFolder == SGridLevelBrowser::DraftRoot);

    const FString FolderName = TEXT("BrowserTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString FolderPath = SGridLevelBrowser::DraftRoot / FolderName;
    FString DiskPath;
    if (!FPackageName::TryConvertLongPackageNameToFilename(FolderPath, DiskPath)) { return false; }
    Browser->FolderName->SetText(FText::FromString(FolderName));
    Browser->CreateFolder();
    TestTrue(TEXT("Native folder creation creates a directory and opens it"),
        IFileManager::Get().DirectoryExists(*DiskPath) && Browser->CurrentFolder == FolderPath);
    TestTrue(TEXT("Root drafts are not listed inside a new empty folder"), Browser->VisibleEntries.IsEmpty());
    Browser->OpenFolder(SGridLevelBrowser::DraftRoot);
    Browser->Filter(FText::FromString(FolderName));
    TestTrue(TEXT("Empty folder is browsable at its parent"), Browser->VisibleEntries.Num() == 1 &&
        Browser->VisibleEntries[0]->IsFolder());
    if (Browser->VisibleEntries.Num() == 1) { Browser->Open(Browser->VisibleEntries[0]); }
    TestEqual(TEXT("Opening a folder does not open a draft"), Opens, 3);
    if (IFileManager::Get().DirectoryExists(*DiskPath))
    {
        TestTrue(TEXT("Remove empty test directory"), IFileManager::Get().DeleteDirectory(*DiskPath, true, false));
    }
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().RemovePath(FolderPath);
    return true;
}
#endif

#undef LOCTEXT_NAMESPACE
