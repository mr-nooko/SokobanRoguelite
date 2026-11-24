#include "GridGameMode.h"

#include "GridProjectSettings.h"
#include "GridScene.h"
#include "Math/RandomStream.h"

#define LOCTEXT_NAMESPACE "GridCampaign"

const FGridSequenceSlot* AGridPlayerController::GetCampaignSlot(int32 Index) const
{
    return bUsingCampaign && RunSlots.IsValidIndex(Index) ? &RunSlots[Index] : nullptr;
}

int32 AGridPlayerController::GetSlotSeed(int32 Index) const
{
    FRandomStream Stream(RunSeed);
    for (int32 Slot = 0; Slot < Index; ++Slot) { Stream.GetUnsignedInt(); }
    return static_cast<int32>(Stream.GetUnsignedInt());
}

void AGridPlayerController::OpenStartMenu()
{
    bStartMenuOpen = true;
    bUsingCampaign = false;
    RunSlots.Reset();
    RecentGeneratedTemplates.Reset();
    RoomIndex = 0;
    Session = {};
    GrantedRoomReward = {};
    bRoomRewardSettled = false;
    CurrentRoomLayout = {};
    StartMenuError = FText::GetEmpty();
    TransitionError = FText::GetEmpty();
    DraftError.Reset();
    SelectedUndoSteps = 0;
    PreferredMove = FKey();
    if (IsValid(Scene))
    {
        Scene->ResetOutcome();
        Scene->ClearBoardPreview();
    }

    if (HasAuthoredSequence())
    {
        CampaignSummary = FText::Format(LOCTEXT("LegacySummary", "当前地图的试玩序列 · {0} 关"), Scene->PlaytestSequence.Num());
    }
    else if (const auto* Sequence = GetDefault<UGridProjectSettings>()->CampaignSequence.LoadSynchronous())
    {
        const FText Title = Sequence->Title.IsEmpty() ? FText::FromName(Sequence->GetFName()) : Sequence->Title;
        CampaignSummary = FText::Format(LOCTEXT("CampaignSummary", "{0} · {1} 关"), Title, Sequence->GetRoomCount());
    }
    else
    {
        CampaignSummary = LOCTEXT("TutorialSummary", "推箱子教学");
    }
    SetMouseUIInput(true);
}

bool AGridPlayerController::PrepareCampaign()
{
    const auto& Setting = GetDefault<UGridProjectSettings>()->CampaignSequence;
    if (HasAuthoredSequence() || Setting.IsNull()) { return true; }
    const auto* Sequence = Setting.LoadSynchronous();
    if (!Sequence)
    {
        StartMenuError = LOCTEXT("MissingSequence", "无法读取默认关卡序列，请检查项目设置。");
        return false;
    }
    if (!Sequence->BuildSlots(RunSlots, StartMenuError)) { return false; }
    RunSeed = Sequence->bUseFixedSeed ? Sequence->FixedSeed : static_cast<int32>(FGuid::NewGuid().A);
    bUsingCampaign = true;
    return true;
}

void AGridPlayerController::StartNewRun()
{
    if (IsGridBusy() || !IsStartMenuOpen()) { return; }
    StartMenuError = FText::GetEmpty();
    RunSlots.Reset();
    bUsingCampaign = false;
    if (PrepareCampaign()) { TryLoadRoom(0, GetDefault<UGridProjectSettings>()->StartingInventory()); }
}

#undef LOCTEXT_NAMESPACE
