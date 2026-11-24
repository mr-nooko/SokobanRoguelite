#include "GridGameMode.h"
#include "GridBackground.h"
#include "GridPreview.h"
#include "GridScene.h"
#include "GridLevelAsset.h"
#include "LevelGenerator.h"
#include "GridProjectSettings.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Components/InputComponent.h"

AGridGameMode::AGridGameMode()
{
    DefaultPawnClass = nullptr;
    PlayerControllerClass = AGridPlayerController::StaticClass();
    HUDClass = AGridHUD::StaticClass();
}

void AGridPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        const bool bDraftRequested = GetWorld()->GetAuthGameMode<AGridDraftPlaytestMode>() != nullptr;
        Scene = SelectPlayScene(bDraftRequested);
        if (!Scene)
        {
            Scene = GetWorld()->SpawnActor<AGridScene>();
            bOwnsScene = IsValid(Scene);
        }
        if (Scene) { SetViewTarget(Scene); }
    }
    if (IsDraftPlaytest()) { ResetRun(); }
    else { OpenStartMenu(); }
}

AGridScene* AGridPlayerController::SelectPlayScene(bool bDraftRequested) const
{
    AGridScene* Selected = nullptr;
    for (TActorIterator<AGridScene> It(GetWorld()); It; ++It)
    {
        auto* Candidate = *It;
        if (!Selected && Candidate->bUseForPlaytest && (Candidate->DraftLevel != nullptr) == bDraftRequested)
        {
            Selected = Candidate;
        }
        else
        {
            Candidate->ClearBoardPreview();
        }
    }
    return Selected;
}

void AGridPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bOwnsScene && IsValid(Scene)) { Scene->Destroy(); }
    bGridBusy = false;
    Super::EndPlay(EndPlayReason);
}

bool AGridPlayerController::IsDraftPlaytest() const
{
    return IsValid(Scene) && Scene->DraftLevel;
}

bool AGridPlayerController::HasAuthoredSequence() const
{
    return IsValid(Scene) && !IsDraftPlaytest() && !Scene->PlaytestSequence.IsEmpty();
}

int32 AGridPlayerController::GetRoomCount() const
{
    if (IsDraftPlaytest()) { return 1; }
    if (bUsingCampaign) { return RunSlots.Num(); }
    return HasAuthoredSequence() ? Scene->PlaytestSequence.Num() : GridPreview::RoomCount;
}

const UGridLevelAsset* AGridPlayerController::GetRoomAsset(int32 Index) const
{
    if (IsDraftPlaytest()) { return Scene->DraftLevel.Get(); }
    if (const auto* Slot = GetCampaignSlot(Index))
    {
        return Slot->Source == EGridSequenceSource::Authored ? Slot->Level.Get() : nullptr;
    }
    if (bUsingCampaign) { return nullptr; }
    return HasAuthoredSequence() && Scene->PlaytestSequence.IsValidIndex(Index)
        ? Scene->PlaytestSequence[Index].Get() : nullptr;
}

FString AGridPlayerController::GetRoomTitle() const
{
    if (IsRuntimeTest()) { return TEXT("运行时草稿试玩"); }
    if (IsDraftPlaytest()) { return TEXT("草稿试玩"); }
    const auto* Slot = GetCampaignSlot(RoomIndex);
    const FString Name = Slot && !Slot->Title.IsEmpty() ? Slot->Title.ToString() :
        Slot && Slot->Source == EGridSequenceSource::Generated ? TEXT("生成关卡") :
        bUsingCampaign || HasAuthoredSequence() ? Session.State.Board.RoomId.ToString() : GridPreview::GetLesson(RoomIndex).Title;
    return FString::Printf(TEXT("%d / %d   %s"), GetRoomNumber(), GetRoomCount(), *Name);
}

FText AGridPlayerController::GetRoomHint() const
{
    if (const auto* Slot = GetCampaignSlot(RoomIndex); Slot && !Slot->Hint.IsEmpty()) { return Slot->Hint; }
    if (const auto* Room = GetRoomAsset(RoomIndex)) { return Room->PlaytestHint; }
    if (bUsingCampaign) { return FText::GetEmpty(); }
    return HasAuthoredSequence() ? FText::GetEmpty() : FText::FromString(GridPreview::GetLesson(RoomIndex).Hint);
}

void AGridPlayerController::SettleRoomReward()
{
    if (IsRuntimeTest() || Session.State.Outcome != Sokoban::EOutcome::Won || bRoomRewardSettled) { return; }
    bRoomRewardSettled = true;
    FGridRoomReward Reward;
    if (const auto* Asset = GetRoomAsset(RoomIndex)) { Reward = Asset->ClearReward; }
    else if (const auto* Slot = GetCampaignSlot(RoomIndex)) { Reward = Slot->ClearReward; }

    const auto* Settings = GetDefault<UGridProjectSettings>();
    const auto Eligible = Settings->MakeResourceContext({}).Filter({FMath::Max(0, Reward.Strength), FMath::Max(0, Reward.Undo)});
    auto& Inventory = Session.State.Inventory;
    GrantedRoomReward = {FMath::Min(Eligible.Strength, MAX_int32 - Inventory.Strength),
                         FMath::Min(Eligible.Undo, MAX_int32 - Inventory.Undo)};
    Inventory.Strength += GrantedRoomReward.Strength;
    Inventory.Undo += GrantedRoomReward.Undo;
}

FText AGridPlayerController::GetRoomRewardNotice() const
{
    if (IsRuntimeTest() || Session.State.Outcome != Sokoban::EOutcome::Won) { return FText::GetEmpty(); }
    TArray<FText> Rewards;
    if (GrantedRoomReward.Strength > 0)
    {
        Rewards.Add(FText::Format(NSLOCTEXT("GridPlaytest", "StrengthReward", "大力士药丸 ×{0}"), GrantedRoomReward.Strength));
    }
    if (GrantedRoomReward.Undo > 0)
    {
        Rewards.Add(FText::Format(NSLOCTEXT("GridPlaytest", "UndoReward", "时空回溯药丸 ×{0}"), GrantedRoomReward.Undo));
    }
    return Rewards.IsEmpty() ? FText::GetEmpty() : FText::Format(
        NSLOCTEXT("GridPlaytest", "RoomReward", "通关获得  {0}"), FText::Join(FText::FromString(TEXT("    ")), Rewards));
}

bool AGridPlayerController::IsStepPlaying() const
{
    return IsValid(Scene) && Scene->IsStepping();
}

void AGridPlayerController::RefreshScene(bool bAnimateStep)
{
    if (IsValid(Scene)) { Scene->Present(ActiveSession().State, bAnimateStep); }
}

void AGridPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::F2, IE_Pressed, this, &AGridPlayerController::ToggleRuntimeEditor);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AGridPlayerController::PaintRuntimeCell);
    InputComponent->BindKey(EKeys::W, IE_Pressed, this, &AGridPlayerController::Submit);
    InputComponent->BindKey(EKeys::S, IE_Pressed, this, &AGridPlayerController::Submit);
    InputComponent->BindKey(EKeys::A, IE_Pressed, this, &AGridPlayerController::Submit);
    InputComponent->BindKey(EKeys::D, IE_Pressed, this, &AGridPlayerController::Submit);
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AGridPlayerController::Arm);
    InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AGridPlayerController::ToggleUndoSelection);
    InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AGridPlayerController::ChangeUndoSelection);
    InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AGridPlayerController::ChangeUndoSelection);
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AGridPlayerController::ResetRun);
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AGridPlayerController::ConfirmOrNextRoom);
}

void AGridPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    if (IsStartMenuOpen()) { return; }
    if (IsRuntimeEditing())
    {
        FIntPoint Cell;
        if (IsValid(Scene))
        {
            Scene->SetEditCursor(Scene->PickCell(*this, Cell) ? TOptional<FIntPoint>(Cell) : TOptional<FIntPoint>());
        }
        return;
    }
    if (!IsLocalController() || SelectedUndoSteps > 0 || IsStepPlaying() || GetWorld()->GetTimeSeconds() < NextMoveTime)
    {
        return;
    }
    if (PreferredMove.IsValid() && IsInputKeyDown(PreferredMove))
    {
        Submit(PreferredMove);
        return;
    }
    for (const FKey Key : {EKeys::W, EKeys::A, EKeys::S, EKeys::D})
    {
        if (IsInputKeyDown(Key)) { Submit(Key); return; }
    }
}

void AGridPlayerController::Submit(FKey Key)
{
    if (IsGridBusy()) { return; }
    if (IsStartMenuOpen() || IsRuntimeEditing() || SelectedUndoSteps > 0) { return; }
    PreferredMove = Key;
    if (IsStepPlaying() || (!IsRuntimeTest() && !DraftError.IsEmpty()) || ActiveSession().State.Outcome != Sokoban::EOutcome::Running) { return; }
    UndoNotice = FText::GetEmpty();
    NextMoveTime = GetWorld()->GetTimeSeconds() + 0.18;
    const FIntPoint Direction = Key == EKeys::W ? FIntPoint(0, -1) : Key == EKeys::S ? FIntPoint(0, 1)
                              : Key == EKeys::A ? FIntPoint(-1, 0) : FIntPoint(1, 0);
    const auto Result = Sokoban::FRules::Apply(ActiveSession(), {Sokoban::EAction::Move, Direction});
    SettleRoomReward();
    if (Result.bSkippedChainEffects)
    {
        UndoNotice = NSLOCTEXT("GridPlaytest", "ChainModulesSkipped", "本次多箱连推只移动箱体。特殊效果由玩家直接推单箱触发。");
    }
    if (Scene) { Scene->QueueBlast(Result); Scene->PlayTurnSounds(Result); }
    RefreshScene(true);
}

void AGridPlayerController::Arm()
{
    if (IsGridBusy()) { return; }
    if (IsStartMenuOpen() || IsRuntimeEditing() || SelectedUndoSteps > 0 || IsStepPlaying() || (!IsRuntimeTest() && !DraftError.IsEmpty())) { return; }
    Sokoban::FRules::Apply(ActiveSession(), {Sokoban::EAction::ArmStrength});
    RefreshScene();
}

void AGridPlayerController::ToggleUndoSelection()
{
    if (IsGridBusy()) { return; }
    if (SelectedUndoSteps > 0)
    {
        SelectedUndoSteps = 0;
        return;
    }
    if (IsStartMenuOpen() || IsRuntimeEditing() || IsStepPlaying() || (!IsRuntimeTest() && !DraftError.IsEmpty())) { return; }
    if (ActiveSession().State.Outcome != Sokoban::EOutcome::Running) { return; }
    if (GetAvailableUndoSteps() == 0)
    {
        UndoNotice = ActiveSession().State.Inventory.Undo <= 0
            ? NSLOCTEXT("GridPlaytest", "NoUndoItems", "没有剩余时空回溯药丸")
            : NSLOCTEXT("GridPlaytest", "NoUndoHistory", "当前关卡还没有可倒放的行动");
        return;
    }
    UndoNotice = FText::GetEmpty();
    SelectedUndoSteps = 1;
}

void AGridPlayerController::ChangeUndoSelection(FKey Key)
{
    if (SelectedUndoSteps == 0) { return; }
    const int32 Delta = Key == EKeys::Up ? 1 : -1;
    SelectedUndoSteps = FMath::Clamp(SelectedUndoSteps + Delta, 1, GetAvailableUndoSteps());
}

void AGridPlayerController::ConfirmOrNextRoom()
{
    if (IsGridBusy()) { return; }
    if (IsStartMenuOpen()) { StartNewRun(); return; }
    if (IsRuntimeEditing()) { return; }
    if (SelectedUndoSteps == 0)
    {
        NextRoom();
        return;
    }
    const int32 Steps = SelectedUndoSteps;
    SelectedUndoSteps = 0;
    const auto Result = Sokoban::FRules::Apply(ActiveSession(), {Sokoban::EAction::Undo, FIntPoint::ZeroValue, Steps});
    if (!Result.bAccepted) { return; }
    if (Scene) { Scene->PlayTurnSounds(Result); }
    UndoNotice = FText::Format(NSLOCTEXT("GridPlaytest", "UndoApplied", "已倒放 {0} 步，消耗 1 枚时空回溯药丸"), Steps);
    NextMoveTime = GetWorld()->GetTimeSeconds() + 0.18;
    RefreshScene();
}

bool AGridPlayerController::TryLoadRoom(int32 Index, Sokoban::FInventory Inventory)
{
    using namespace Sokoban;
    if (IsGridBusy()) { return false; }
    const UGridLevelAsset* Asset = GetRoomAsset(Index);
    const auto* Slot = GetCampaignSlot(Index);
    const bool bGenerated = Slot && Slot->Source == EGridSequenceSource::Generated;
    if ((bUsingCampaign && (!Slot || (!bGenerated && !Asset))) ||
        (!bUsingCampaign && HasAuthoredSequence() && !Asset))
    {
        const FText Error = NSLOCTEXT("GridPlaytest", "MissingLevel", "序列中的关卡资产为空。请检查试玩关卡序列。");
        if (IsStartMenuOpen()) { StartMenuError = Error; }
        else if (Session.State.Board.Size.X <= 0) { DraftError = Error.ToString(); }
        else { TransitionError = Error; }
        return false;
    }
    FLevel Level = Asset ? Asset->ToLevel() : FLevel();
    const auto* Settings = GetDefault<UGridProjectSettings>();
    const FResourceContext Resources = Settings->MakeResourceContext(Inventory);
    const bool bDraft = IsDraftPlaytest();
    if (!bDraft && !bGenerated && !Asset) { Level = GridPreview::MakeRoom(Index); }
    const FSearchLimits Limits = Settings->SearchLimits();
    FGenerationRequest Request;
    if (bGenerated)
    {
        Request.Seed = GetSlotSeed(Index);
        Request.Complexity = Slot->Complexity;
        Request.MaxAttempts = Slot->MaxAttempts;
        Request.Resources = Resources;
        Request.Search = Settings->SearchLimits();
        if (Slot->MaxActions > 0) { Request.Search.MaxActions = Slot->MaxActions; }
        switch (Slot->Difficulty)
        {
            case EGridGeneratedDifficulty::High: Request.Search.TargetDifficulty = EDifficulty::High; break;
            case EGridGeneratedDifficulty::Medium: Request.Search.TargetDifficulty = EDifficulty::Medium; break;
            case EGridGeneratedDifficulty::Low: Request.Search.TargetDifficulty = EDifficulty::Low; break;
            default: break;
        }
        Request.Search.bRequireDifficultyBand = Slot->Difficulty != EGridGeneratedDifficulty::Project;
        Request.RecentTemplates = RecentGeneratedTemplates;
        Request.bAllowFallback = Slot->bAllowFallback;
        if (Slot->Fallback) { Request.FallbackLevel = Slot->Fallback->ToLevel(); }
        switch (Slot->Template)
        {
            case EGridSequenceTemplate::Detour: Request.AllowedTemplates.Add(EGenerationTemplate::Detour); break;
            case EGridSequenceTemplate::BeamCover: Request.AllowedTemplates.Add(EGenerationTemplate::BeamCover); break;
            case EGridSequenceTemplate::SpikeSweep: Request.AllowedTemplates.Add(EGenerationTemplate::SpikeSweep); break;
            case EGridSequenceTemplate::BlastRubble: Request.AllowedTemplates.Add(EGenerationTemplate::BlastRubble); break;
            case EGridSequenceTemplate::EjectCorridor: Request.AllowedTemplates.Add(EGenerationTemplate::EjectCorridor); break;
            case EGridSequenceTemplate::StrengthChain: Request.AllowedTemplates.Add(EGenerationTemplate::StrengthChain); break;
            default: break;
        }
    }

    struct FAdmission
    {
        FLevel Level;
        FText Error;
        FName GeneratedTemplate;
        bool bFallback = false;
    };
    auto Check = [Level = MoveTemp(Level), Request = MoveTemp(Request), Resources, Limits, bDraft, bGenerated]() mutable -> FAdmission {
        if (bGenerated)
        {
            auto Generated = FTemplateLevelGenerator().Generate(Request);
            if (Generated.Status == ECheck::Pass && Generated.Level.IsSet())
            {
                return {MoveTemp(Generated.Level.GetValue()), {}, Generated.Trace.Last().Template, Generated.bFallback};
            }
            return {{}, Generated.Status == ECheck::Unknown
                ? NSLOCTEXT("GridCampaign", "GenerationUnknown", "生成关卡的验证预算已耗尽，尚未确认解法达标。")
                : NSLOCTEXT("GridCampaign", "GenerationFailed", "候选和后备关卡均未通过当前背包下的验证。")};
        }
        if (bDraft)
        {
            const auto Errors = FLevelGate::CheckLayout(Level);
            if (!Errors.IsEmpty()) { return {{}, FText::FromString(Errors[0])}; }
        }
        else
        {
            const auto Validation = FLevelGate::Validate(Level, Resources, Limits, FBoundedLevelSolver());
            if (!Validation.Errors.IsEmpty()) { return {{}, FText::FromString(Validation.Errors[0])}; }
            if (Validation.Status != ECheck::Pass)
            {
                const FText Marker = Validation.Status == ECheck::Unknown
                    ? NSLOCTEXT("GridPlaytest", "ValidationUnknown", "搜索预算内尚未确认准入（能力和当前背包分别至少 {0} 解）。")
                    : NSLOCTEXT("GridPlaytest", "ValidationFailed", "关卡未达准入门槛（能力和当前背包分别至少 {0} 解）。");
                return {{}, FText::Format(Marker, Limits.MinimumSolutions)};
            }
        }
        return {MoveTemp(Level)};
    };
    auto Complete = [Index, Resources, bGenerated,
        MainAsset = TWeakObjectPtr<const UGridLevelAsset>(Asset),
        FallbackAsset = TWeakObjectPtr<const UGridLevelAsset>(Slot ? Slot->Fallback.Get() : nullptr)]
        (AGridPlayerController& Controller, FAdmission Result) {
        Controller.bGridBusy = false;
        if (!Result.Error.IsEmpty())
        {
            if (Controller.IsStartMenuOpen()) { Controller.StartMenuError = FText::Format(
                NSLOCTEXT("GridCampaign", "SlotError", "第 {0} 关无法进入 · {1}"), Index + 1, Result.Error); }
            else if (Controller.Session.State.Board.Size.X <= 0) { Controller.DraftError = Result.Error.ToString(); }
            else { Controller.TransitionError = Result.Error; }
            return;
        }
        if (bGenerated)
        {
            Controller.RecentGeneratedTemplates.Add(Result.GeneratedTemplate);
            if (Controller.RecentGeneratedTemplates.Num() > 2) { Controller.RecentGeneratedTemplates.RemoveAt(0); }
        }
        Controller.Session = FRules::Start(Result.Level, Resources.Filter(Resources.ActualInventory));
        Controller.CurrentRoomLayout = MoveTemp(Result.Level);
        Controller.RoomIndex = Index;
        Controller.GrantedRoomReward = {};
        Controller.bRoomRewardSettled = false;
        Controller.SettleRoomReward();
        Controller.SelectedUndoSteps = 0;
        Controller.UndoNotice = FText::GetEmpty();
        Controller.TransitionError = FText::GetEmpty();
        Controller.DraftError.Reset();
        Controller.PreferredMove = FKey();
        if (IsValid(Controller.Scene))
        {
            const auto* VisualAsset = Result.bFallback ? FallbackAsset.Get() : MainAsset.Get();
            Controller.Scene->SetVisualProfile(VisualAsset ? VisualAsset->VisualProfile.Get() : nullptr);
            Controller.Scene->ResetOutcome();
        }
        if (Controller.IsStartMenuOpen())
        {
            Controller.bStartMenuOpen = false;
            Controller.SetMouseUIInput(false);
        }
        Controller.RefreshScene();
    };
    if (bDraft || !GetLocalPlayer())
    {
        auto Result = Check();
        const bool bAdmitted = Result.Error.IsEmpty();
        Complete(*this, MoveTemp(Result));
        return bAdmitted;
    }
    bGridBusy = true;
    DispatchGridWork(*this, MoveTemp(Check), MoveTemp(Complete));
    return true;
}

void AGridPlayerController::ResetRun()
{
    if (IsGridBusy()) { return; }
    if (IsRuntimeEditing() || SelectedUndoSteps > 0 || IsStepPlaying()) { return; }
    if (IsRuntimeTest()) { TestRuntimeDraft(); return; }
    if (IsDraftPlaytest()) { TryLoadRoom(0, GetDefault<UGridProjectSettings>()->StartingInventory()); }
    else if (!IsStartMenuOpen()) { OpenStartMenu(); }
}

void AGridPlayerController::NextRoom()
{
    if (!IsStartMenuOpen() && !RuntimeDraft && !IsStepPlaying() && !IsDraftPlaytest() && Session.State.Outcome == Sokoban::EOutcome::Won &&
        RoomIndex + 1 < GetRoomCount())
    {
        TryLoadRoom(RoomIndex + 1, Session.State.Inventory);
    }
}
