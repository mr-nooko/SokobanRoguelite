#include "GridGameMode.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SokobanHud"

namespace
{
const FLinearColor Panel(.035f, .055f, .08f, .96f);
const FLinearColor Accent(.44f, .84f, .93f);
const FLinearColor Secondary(.71f, .78f, .84f);
const FLinearColor ErrorColor(1.f, .51f, .56f);
const FLinearColor Victory(.43f, .95f, .71f);

class SGridHudOverlay final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGridHudOverlay) {}
        SLATE_ARGUMENT(AGridPlayerController*, Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Controller = Args._Controller;
        ChildSlot
        [
            SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().VAlign(VAlign_Top)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(Panel).Padding(FMargin(24, 8, 24, 10))
                .Visibility_Lambda([this] { return StartMenuOpen() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(HasDraftError() ? ErrorColor : Accent); })
                        .Text_Lambda([this] {
                            if (Editing()) { return LOCTEXT("RuntimeEditor", "运行时关卡编辑"); }
                            if (HasDraftError()) { return LOCTEXT("DraftErrorTitle", "关卡无法试玩"); }
                            const auto* PC = Controller.Get();
                            return PC ? FText::FromString(PC->GetRoomTitle()) : FText::GetEmpty();
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                    [
                        SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                        .ColorAndOpacity(Secondary)
                        .Text(this, &SGridHudOverlay::GetStatus)
                    ]
                ]
            ]
            + SOverlay::Slot().VAlign(VAlign_Bottom)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(Panel).Padding(FMargin(24, 8, 24, 10))
                .Visibility_Lambda([this] {
                    return HasPlayableRoom() && !Editing() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                        .Text(this, &SGridHudOverlay::GetControls)
                        .ColorAndOpacity_Lambda([this] {
                            const auto* PC = Controller.Get();
                            const auto Outcome = PC ? PC->GetGridState().Outcome : Sokoban::EOutcome::Running;
                            return FSlateColor(Outcome == Sokoban::EOutcome::Dead ? ErrorColor :
                                Outcome == Sokoban::EOutcome::Won ? Victory : FLinearColor::White);
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                    [
                        SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                        .ColorAndOpacity(Victory)
                        .Text_Lambda([this] { return Controller.IsValid() ? Controller->GetRoomRewardNotice() : FText::GetEmpty(); })
                        .Visibility_Lambda([this] {
                            return Controller.IsValid() && !Controller->GetRoomRewardNotice().IsEmpty()
                                ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                    [
                        SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                        .Text(this, &SGridHudOverlay::GetFooterMessage)
                        .ColorAndOpacity_Lambda([this] {
                            const auto* PC = Controller.Get();
                            return FSlateColor(PC && !PC->GetTransitionError().IsEmpty() ? ErrorColor : Secondary);
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                    [
                        SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                        .ColorAndOpacity(Accent)
                        .Text(this, &SGridHudOverlay::GetLesson)
                        .Visibility_Lambda([this] { return GetLesson().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                    ]
                ]
            ]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 61, 12, 10)
            [ MakeEditorPanel() ]
            + SOverlay::Slot()
            [ MakeStartMenu() ]
            + SOverlay::Slot()
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, .52f))
                .HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16)
                .Visibility_Lambda([this] {
                    const auto* PC = Controller.Get();
                    return PC && PC->GetSelectedUndoSteps() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(Panel).Padding(FMargin(24, 16))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
                            .ColorAndOpacity(Accent).Text(LOCTEXT("UndoTitle", "选择倒放步数"))
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
                        [
                            SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 19))
                            .ColorAndOpacity(FLinearColor::White)
                            .Text_Lambda([this] {
                                const auto* PC = Controller.Get();
                                return PC ? FText::Format(LOCTEXT("UndoSteps", "倒放 {0} 步（最多 {1} 步）"),
                                    PC->GetSelectedUndoSteps(), PC->GetAvailableUndoSteps()) : FText::GetEmpty();
                            })
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(Secondary)
                            .Text(LOCTEXT("UndoHelp", "确认后消耗 1 枚时空回溯药丸\n↑ / ↓ 调整    回车 确认    Z 取消"))
                        ]
                    ]
                ]
            ]
        ];
    }

private:
    bool StartMenuOpen() const { return Controller.IsValid() && Controller->IsStartMenuOpen(); }

    TSharedRef<SWidget> MakeStartMenu()
    {
        return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.022f, .038f, .058f, 1.f))
            .HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(18)
            .Visibility_Lambda([this] { return StartMenuOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
            [
                SNew(SBox).MaxDesiredWidth(520)
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(Panel).Padding(FMargin(34, 24))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
                            .ColorAndOpacity(FLinearColor::White).Text(LOCTEXT("GameTitle", "推箱子战斗"))
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 20)
                        [
                            SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                            .AutoWrapText(true).ColorAndOpacity(Secondary)
                            .Text_Lambda([this] { return Controller.IsValid() ? Controller->GetCampaignSummary() : FText::GetEmpty(); })
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SButton).IsFocusable(false).HAlign(HAlign_Center).ContentPadding(FMargin(22, 10))
                            .ButtonColorAndOpacity(Accent)
                            .IsEnabled_Lambda([this] { return Controller.IsValid() && !Controller->IsGridBusy(); })
                            .OnClicked_Lambda([this] {
                                if (auto* PC = Controller.Get()) { PC->StartNewRun(); }
                                return FReply::Handled();
                            })
                            [
                                SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                                .Text_Lambda([this] { return Controller.IsValid() && Controller->IsGridBusy()
                                    ? LOCTEXT("LoadingFirstRoom", "正在准备第一关…") : LOCTEXT("StartGame", "开始游戏"); })
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                        [
                            SNew(SButton).IsFocusable(false).HAlign(HAlign_Center).ContentPadding(FMargin(22, 8))
                            .Text(LOCTEXT("StartMenuEditor", "关卡编辑器"))
                            .IsEnabled_Lambda([this] { return Controller.IsValid() && !Controller->IsGridBusy(); })
                            .OnClicked_Lambda([this] {
                                if (auto* PC = Controller.Get()) { PC->OpenRuntimeEditorFromMenu(); }
                                return FReply::Handled();
                            })
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                        [
                            SNew(STextBlock).ColorAndOpacity(Secondary).Text(LOCTEXT("StartKey", "回车也可开始"))
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                        [
                            SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(ErrorColor)
                            .Text_Lambda([this] { return Controller.IsValid() ? Controller->GetStartMenuError() : FText::GetEmpty(); })
                            .Visibility_Lambda([this] { return Controller.IsValid() && !Controller->GetStartMenuError().IsEmpty()
                                ? EVisibility::Visible : EVisibility::Collapsed; })
                        ]
                    ]
                ]
            ];
    }

    bool Editing() const { return Controller.IsValid() && Controller->IsRuntimeEditing(); }

    TSharedRef<SWidget> EditorButton(const FText& Text, void (AGridPlayerController::*Action)(),
        FLinearColor Tint = FLinearColor::White)
    {
        return SNew(SButton).IsFocusable(false).Text(Text).ButtonColorAndOpacity(Tint).OnClicked_Lambda([this, Action] {
            if (auto* PC = Controller.Get()) { (PC->*Action)(); }
            return FReply::Handled();
        });
    }

    TSharedRef<SWidget> Dimension(const FText& Text, bool bWidth)
    {
        return SNew(SNumericEntryBox<int32>).AllowSpin(false).MinValue(1).MaxValue(Sokoban::FLevelGate::MaxBoardSide)
            .Label()[ SNew(STextBlock).Text(Text) ]
            .Value_Lambda([this, bWidth]() -> TOptional<int32> {
                if (!Editing()) { return {}; }
                const FIntPoint Size = Controller->GetRuntimeDraftSize();
                return bWidth ? Size.X : Size.Y;
            })
            .OnValueCommitted_Lambda([this, bWidth](int32 Value, ETextCommit::Type Commit) {
                if (!Editing() || Commit == ETextCommit::OnCleared) { return; }
                FIntPoint Size = Controller->GetRuntimeDraftSize();
                (bWidth ? Size.X : Size.Y) = Value;
                Controller->ResizeRuntimeDraft(Size);
            });
    }

    TSharedRef<SWidget> MakeEditorPanel()
    {
        struct FToolEntry { EGridPaintTool Tool; FText Label; };
        using ETool = EGridPaintTool;
        const FToolEntry Tools[] = {
            {ETool::Wall, LOCTEXT("Wall", "墙")}, {ETool::Rubble, LOCTEXT("Rubble", "碎墙")},
            {ETool::Crate, LOCTEXT("Crate", "普通箱")}, {ETool::SpikeCrate, LOCTEXT("Spike", "尖刺箱")},
            {ETool::BlastCrate, LOCTEXT("Blast", "爆炸箱")}, {ETool::EjectCrate, LOCTEXT("Eject", "弹射箱")},
            {ETool::Enemy, LOCTEXT("Enemy", "接触敌人")}, {ETool::BeamEnemy, LOCTEXT("Beam", "射线敌人")},
            {ETool::Start, LOCTEXT("Start", "起点")}, {ETool::Goal, LOCTEXT("Goal", "终点")},
            {ETool::Cutout, LOCTEXT("Cutout", "挖空")}, {ETool::Restore, LOCTEXT("Restore", "补地面")},
            {ETool::Rotate, LOCTEXT("Rotate", "旋转朝向")}, {ETool::Erase, LOCTEXT("Erase", "擦除")}
        };
        auto Palette = SNew(SUniformGridPanel).SlotPadding(FMargin(2));
        for (int32 I = 0; I < UE_ARRAY_COUNT(Tools); ++I)
        {
            const auto Tool = Tools[I].Tool;
            Palette->AddSlot(I % 2, I / 2)
            [
                SNew(SButton).IsFocusable(false).Text(Tools[I].Label)
                .ButtonColorAndOpacity_Lambda([this, Tool] {
                    return Controller.IsValid() && Controller->GetRuntimeTool() == Tool ? Accent : FLinearColor::White;
                })
                .OnClicked_Lambda([this, Tool] {
                    if (auto* PC = Controller.Get()) { PC->SetRuntimeTool(Tool); }
                    return FReply::Handled();
                })
            ];
        }

        return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(8)
            .Visibility_Lambda([this] { return Editing() ? EVisibility::Visible : EVisibility::Collapsed; })
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SButton).IsFocusable(false)
                    .Text_Lambda([this] { return bShowTools ? LOCTEXT("HideTools", "收起工具") : LOCTEXT("ShowTools", "展开工具"); })
                    .OnClicked_Lambda([this] { bShowTools = !bShowTools; return FReply::Handled(); })
                ]
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    SNew(SBox).WidthOverride(268)
                    .Visibility_Lambda([this] { return bShowTools ? EVisibility::Visible : EVisibility::Collapsed; })
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight().Padding(2, 8, 2, 4)
                            [ SNew(STextBlock).Text(LOCTEXT("BoardSettings", "棋盘设置")).ColorAndOpacity(Accent) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1.f)[ Dimension(LOCTEXT("Width", "宽"), true) ]
                                + SHorizontalBox::Slot().FillWidth(1.f)[ Dimension(LOCTEXT("Height", "高"), false) ]
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2)
                            [
                                SNew(SButton).IsFocusable(false)
                                .Text_Lambda([this] { return Controller.IsValid() ? Controller->GetRuntimeBeamTerrainText() : FText::GetEmpty(); })
                                .OnClicked_Lambda([this] {
                                    if (auto* PC = Controller.Get()) { PC->ToggleRuntimeBeamTerrain(); }
                                    return FReply::Handled();
                                })
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2, 10, 2, 4)
                            [ SNew(STextBlock).Text(LOCTEXT("Tools", "摆放工具")).ColorAndOpacity(Accent) ]
                            + SVerticalBox::Slot().AutoHeight()[ Palette ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2, 10, 2, 4)
                            [ SNew(STextBlock).Text(LOCTEXT("SaveCheck", "保存校验")).ColorAndOpacity(Accent) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2)
                            [
                                SNew(STextBlock).ColorAndOpacity(Secondary).AutoWrapText(true).Text_Lambda([this] {
                                    return Controller.IsValid() ? Controller->GetRuntimeInventoryText() : FText::GetEmpty();
                                })
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2)
                            [
                                SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
                                    return Controller.IsValid() ? Controller->GetRuntimeValidationText() : FText::GetEmpty();
                                }).ColorAndOpacity_Lambda([this] {
                                    const auto Status = Controller.IsValid() ? Controller->GetRuntimeValidationStatus()
                                        : TOptional<Sokoban::ECheck>();
                                    return FSlateColor(!Status.IsSet() ? Secondary
                                        : Status.GetValue() == Sokoban::ECheck::Pass ? Victory
                                        : Status.GetValue() == Sokoban::ECheck::Fail ? ErrorColor : FLinearColor(1.f, .78f, .42f));
                                })
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 2, 0)
                                [ EditorButton(LOCTEXT("ValidateDraft", "校验关卡"), &AGridPlayerController::ValidateRuntimeDraft) ]
                                + SHorizontalBox::Slot().FillWidth(1.f)
                                [ EditorButton(LOCTEXT("TestDraft", "试玩草稿"), &AGridPlayerController::TestRuntimeDraft) ]
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 2, 0)
                                [
                                    SNew(SButton).IsFocusable(false).Text(LOCTEXT("SaveDraft", "保存本地草稿"))
                                    .ButtonColorAndOpacity(Accent)
                                    .IsEnabled_Lambda([this] { return Controller.IsValid() && Controller->CanAttemptRuntimeSave(); })
                                    .OnClicked_Lambda([this] {
                                        if (auto* PC = Controller.Get()) { PC->SaveRuntimeDraft(); }
                                        return FReply::Handled();
                                    })
                                ]
                                + SHorizontalBox::Slot().FillWidth(1.f)
                                [ EditorButton(LOCTEXT("LoadDraft", "读取本地草稿"), &AGridPlayerController::LoadRuntimeDraft) ]
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(2)
                            [
                                SNew(SButton).IsFocusable(false)
                                .Text_Lambda([this] {
                                    const auto* PC = Controller.Get();
                                    return PC && PC->WillEditorReturnToMenu() ? LOCTEXT("EditorBackToMenu", "返回开始界面")
                                        : LOCTEXT("ExitEditor", "结束编辑并返回对局");
                                })
                                .IsEnabled_Lambda([this] { return Controller.IsValid() && !Controller->IsGridBusy(); })
                                .OnClicked_Lambda([this] {
                                    if (auto* PC = Controller.Get()) { PC->CloseRuntimeEditor(); }
                                    return FReply::Handled();
                                })
                            ]
                        ]
                    ]
                ]
            ];
    }

    bool HasDraftError() const
    {
        const auto* PC = Controller.Get();
        return PC && !PC->IsRuntimeTest() && !PC->IsRuntimeEditing() && !PC->GetDraftError().IsEmpty();
    }

    bool HasPlayableRoom() const
    {
        const auto* PC = Controller.Get();
        return PC && !HasDraftError() && PC->GetGridState().Board.Size.X > 0;
    }

    FText GetStatus() const
    {
        const auto* PC = Controller.Get();
        if (!PC) { return FText::GetEmpty(); }
        if (Editing())
        {
            return PC->GetEditNotice().IsEmpty()
                ? LOCTEXT("RuntimeEditorHelp", "左键应用工具 · F2 结束编辑 · 棋盘一侧被面板挡住时可收起工具")
                : PC->GetEditNotice();
        }
        if (HasDraftError()) { return FText::FromString(PC->GetDraftError()); }
        const auto& State = PC->GetGridState();
        return FText::Format(LOCTEXT("Status", "回合 {0}     大力士药丸 {1}{2}     时空回溯药丸 {3}     {4}"),
            State.Turn, State.Inventory.Strength, State.bStrengthArmed ? LOCTEXT("Armed", "（下一步生效）") : FText::GetEmpty(),
            State.Inventory.Undo, PC->IsRuntimeTest() ? LOCTEXT("BackToEditor", "F2 返回编辑") : LOCTEXT("OpenRuntimeEditor", "F2 编辑关卡"));
    }

    FText GetControls() const
    {
        const auto* PC = Controller.Get();
        if (!PC) { return FText::GetEmpty(); }
        switch (PC->GetGridState().Outcome)
        {
            case Sokoban::EOutcome::Dead:
                return PC->IsDraftPlaytest() || PC->IsRuntimeTest() ? LOCTEXT("DraftEnded", "试玩结束    R 重开")
                    : LOCTEXT("RunEnded", "本局结束 · 背包已清空    R 返回开始界面");
            case Sokoban::EOutcome::Won:
                if (PC->IsDraftPlaytest() || PC->IsRuntimeTest()) { return LOCTEXT("Cleared", "试玩通关    R 重开"); }
                return PC->GetRoomNumber() == PC->GetRoomCount()
                    ? LOCTEXT("CampaignCleared", "本轮通关    R 返回开始界面")
                    : LOCTEXT("RoomCleared", "房间通关    回车 校验并进入下一关");
            default: return LOCTEXT("Controls", "WASD 移动    E 大力士药丸    Z 时空回溯药丸");
        }
    }

    FText GetFooterMessage() const
    {
        const auto* PC = Controller.Get();
        if (!PC) { return FText::GetEmpty(); }
        if (PC->IsGridBusy()) { return LOCTEXT("LoadingNextRoom", "正在准备并验证下一关…"); }
        if (!PC->IsRuntimeTest() && !PC->GetTransitionError().IsEmpty())
        {
            return FText::Format(LOCTEXT("TransitionError", "无法进入关卡 · {0}"), PC->GetTransitionError());
        }
        return PC->GetUndoNotice().IsEmpty()
            ? LOCTEXT("Legend", "绿色：终点    红色：射线    琥珀色：箱子") : PC->GetUndoNotice();
    }

    FText GetLesson() const
    {
        const auto* PC = Controller.Get();
        return PC && !PC->IsRuntimeTest() && PC->GetGridState().Outcome == Sokoban::EOutcome::Running ? PC->GetRoomHint() : FText::GetEmpty();
    }

    bool bShowTools = true;
    TWeakObjectPtr<AGridPlayerController> Controller;
};
}

void AGridHUD::DrawHUD()
{
    Super::DrawHUD();
    if (Overlay) { return; }
    auto* Controller = Cast<AGridPlayerController>(GetOwningPlayerController());
    auto* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    auto* Viewport = Player ? Player->ViewportClient.Get() : nullptr;
    if (!Viewport) { return; }

    Overlay = SNew(SGridHudOverlay).Controller(Controller).Visibility(EVisibility::SelfHitTestInvisible);
    OverlayViewport = Viewport;
    OverlayPlayer = Player;
    Viewport->AddViewportWidgetForPlayer(Player, Overlay.ToSharedRef(), 10);
}

void AGridHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (OverlayViewport.IsValid() && OverlayPlayer.IsValid() && Overlay)
    {
        OverlayViewport->RemoveViewportWidgetForPlayer(OverlayPlayer.Get(), Overlay.ToSharedRef());
    }
    Overlay.Reset();
    Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
