#include "GridGameMode.h"
#include "GridBackground.h"

#include "GridLevelAsset.h"
#include "GridPreview.h"
#include "GridProjectSettings.h"
#include "GridRuntimeDraftSave.h"
#include "GridScene.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#define LOCTEXT_NAMESPACE "GridRuntimeEditor"

namespace
{
const FString DraftSlot(TEXT("RuntimeLevelDraft"));

struct FStoredDraft
{
    UGridLevelAsset* Layout = nullptr;
    int64 Revision = -1;
};

FStoredDraft ReadDraft(UObject* Outer, const FString& Slot)
{
    if (!UGameplayStatics::DoesSaveGameExist(Slot, 0)) { return {}; }
    auto* Save = Cast<UGridRuntimeDraftSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    if (!Save || Save->Layout.IsEmpty() || Save->Revision < 0) { return {}; }
    auto* Layout = NewObject<UGridLevelAsset>(Outer);
    FMemoryReader Reader(Save->Layout);
    FObjectAndNameAsStringProxyArchive Archive(Reader, false);
    Archive.ArNoDelta = true;
    Layout->SerializeScriptProperties(Archive);
    if (Archive.IsError() || !Layout->GetLayoutWarnings().IsEmpty()) { return {}; }
    return {Layout, Save->Revision};
}

FIntPoint RotateDirection(FIntPoint Direction)
{
    return {-Direction.Y, Direction.X};
}
}

void AGridPlayerController::OpenRuntimeEditorFromMenu()
{
    if (IsGridBusy() || !IsStartMenuOpen() || !IsLocalController() || !IsValid(Scene)) { return; }

    Sokoban::FLevel Blank;
    Blank.RoomId = TEXT("RuntimeDraft");
    Blank.Size = {8, 6};
    Blank.Start = {1, 2};
    Blank.Goal = {6, 2};
    RuntimeDraft = NewObject<UGridLevelAsset>(this);
    RuntimeDraft->SetFromLevel(Blank);
    bRuntimeEditorFromMenu = true;
    bStartMenuOpen = false;
    Scene->SetVisualProfile(nullptr);
    EnterRuntimeDraftEditor();
}

void AGridPlayerController::ToggleRuntimeEditor()
{
    if (IsGridBusy()) { return; }
    if (IsStartMenuOpen() || !IsLocalController() || !IsValid(Scene)) { return; }
    if (IsRuntimeEditing()) { CloseRuntimeEditor(); return; }
    if (!RuntimeDraft)
    {
        if (IsStepPlaying() || SelectedUndoSteps > 0) { return; }
        const auto* Asset = GetRoomAsset(RoomIndex);
        const auto InitialLayout = bUsingCampaign ? CurrentRoomLayout : Asset ? Asset->ToLevel() : GridPreview::MakeRoom(RoomIndex);
        if (!Sokoban::FLevelGate::SupportsBoardSize(InitialLayout.Size)) { return; }
        RuntimeDraft = NewObject<UGridLevelAsset>(this);
        RuntimeDraft->SetFromLevel(InitialLayout);
    }
    EnterRuntimeDraftEditor();
}

void AGridPlayerController::EnterRuntimeDraftEditor()
{
    bTestingRuntimeDraft = false;
    SelectedUndoSteps = 0;
    PreferredMove = FKey();
    SetMouseUIInput(true);
    PreviewRuntimeDraft();
}

void AGridPlayerController::SetMouseUIInput(bool bEnable)
{
    if (!bEnable && IsValid(Scene)) { Scene->SetEditCursor({}); }
    if (!GetLocalPlayer() || !GetLocalPlayer()->ViewportClient) { return; }
    bShowMouseCursor = bEnable;
    if (bEnable)
    {
        SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
    }
    else { SetInputMode(FInputModeGameOnly()); }
}

void AGridPlayerController::CloseRuntimeEditor()
{
    if (IsGridBusy()) { return; }
    if (!RuntimeDraft) { return; }
    const bool bReturnToMenu = bRuntimeEditorFromMenu;
    bRuntimeEditorFromMenu = false;
    RuntimeDraft = nullptr;
    RuntimeTest = {};
    bTestingRuntimeDraft = false;
    SelectedUndoSteps = 0;
    PreferredMove = FKey();
    UndoNotice = FText::GetEmpty();
    if (bReturnToMenu)
    {
        OpenStartMenu();
        return;
    }
    SetMouseUIInput(false);
    if (IsValid(Scene))
    {
        Scene->ResetOutcome();
        if (Sokoban::FLevelGate::SupportsBoardSize(Session.State.Board.Size)) { RefreshScene(); }
        else { Scene->ClearBoardPreview(); }
    }
}

void AGridPlayerController::PreviewRuntimeDraft()
{
    if (!IsRuntimeEditing() || !IsValid(Scene)) { return; }
    const auto Level = RuntimeDraft->ToLevel();
    const auto Warnings = Sokoban::FLevelGate::CheckLayout(Level);
    EditNotice = FText::FromString(Sokoban::FLevelGate::ChainModuleAdvisory(Level));
    RuntimeValidationStatus.Reset();
    RuntimeValidationText = LOCTEXT("Pending", "布局检查通过。保存前会按当前背包检验解法。");
    if (!Warnings.IsEmpty())
    {
        RuntimeValidationStatus = Sokoban::ECheck::Fail;
        RuntimeValidationText = FText::Format(LOCTEXT("LayoutBlocked", "无法保存 · {0}"), FText::FromString(Warnings[0]));
    }
    auto State = Sokoban::FRules::Start(Level, {}).State;
    State.Outcome = Sokoban::EOutcome::Running;
    Scene->ResetOutcome();
    Scene->Present(State);
}

void AGridPlayerController::TestRuntimeDraft()
{
    if (IsGridBusy()) { return; }
    if (!RuntimeDraft || !IsValid(Scene)) { return; }
    const auto Level = RuntimeDraft->ToLevel();
    const auto Warnings = Sokoban::FLevelGate::CheckLayout(Level);
    if (!Warnings.IsEmpty()) { EditNotice = FText::FromString(Warnings[0]); return; }
    RuntimeTest = Sokoban::FRules::Start(Level, GetRuntimeValidationInventory());
    bTestingRuntimeDraft = true;
    SelectedUndoSteps = 0;
    PreferredMove = FKey();
    UndoNotice = FText::GetEmpty();
    SetMouseUIInput(false);
    Scene->ResetOutcome();
    RefreshScene();
}

FIntPoint AGridPlayerController::GetRuntimeDraftSize() const
{
    return RuntimeDraft ? RuntimeDraft->Size : FIntPoint::ZeroValue;
}

void AGridPlayerController::ResizeRuntimeDraft(FIntPoint Size)
{
    if (IsGridBusy()) { return; }
    if (!IsRuntimeEditing() || !Sokoban::FLevelGate::SupportsBoardSize(Size) || Size == RuntimeDraft->Size) { return; }
    auto Level = RuntimeDraft->ToLevel();
    Level.Size = Size;
    for (auto It = Level.VoidCells.CreateIterator(); It; ++It)
    {
        if (!Sokoban::FRules::IsInBounds(Level, *It)) { It.RemoveCurrent(); }
    }
    const auto Warnings = Sokoban::FLevelGate::CheckLayout(Level);
    if (!Warnings.IsEmpty()) { EditNotice = FText::FromString(Warnings[0]); return; }
    RuntimeDraft->SetFromLevel(Level);
    PreviewRuntimeDraft();
}

void AGridPlayerController::ToggleRuntimeBeamTerrain()
{
    if (IsGridBusy()) { return; }
    if (!IsRuntimeEditing()) { return; }
    RuntimeDraft->bBeamStopsOnTerrain = !RuntimeDraft->bConfigureBeamTerrain || !RuntimeDraft->bBeamStopsOnTerrain;
    RuntimeDraft->bConfigureBeamTerrain = true;
    PreviewRuntimeDraft();
}

FText AGridPlayerController::GetRuntimeBeamTerrainText() const
{
    if (!RuntimeDraft || !RuntimeDraft->bConfigureBeamTerrain) { return LOCTEXT("BeamUnset", "地形阻挡射线：未设置"); }
    return RuntimeDraft->bBeamStopsOnTerrain ? LOCTEXT("BeamBlocked", "地形阻挡射线：是")
        : LOCTEXT("BeamPass", "地形阻挡射线：否");
}

void AGridPlayerController::PaintRuntimeCell()
{
    FIntPoint Cell;
    if (IsRuntimeEditing() && IsValid(Scene) && Scene->PickCell(*this, Cell) && ApplyRuntimeTool(Cell))
    {
        PreviewRuntimeDraft();
    }
}

bool AGridPlayerController::ApplyRuntimeTool(FIntPoint Cell)
{
    if (IsGridBusy() || !IsRuntimeEditing()) { return false; }
    auto& Draft = *RuntimeDraft;
    const auto Level = Draft.ToLevel();
    if (!Sokoban::FRules::IsInBounds(Level, Cell)) { return false; }
    if (RuntimeTool == EGridPaintTool::Restore) { return Draft.VoidCells.Remove(Cell) > 0; }
    if (Draft.VoidCells.Contains(Cell))
    {
        EditNotice = LOCTEXT("Void", "请先补回这个地块。");
        return false;
    }
    const auto BoxAt = [&](FIntPoint Point) {
        return Draft.Boxes.IndexOfByPredicate([Point](const FGridBoxDraft& Box) {
            return Box.Cell == Point || (Box.Spike != FIntPoint::ZeroValue && Box.Cell + Box.Spike == Point);
        });
    };
    const auto EnemyAt = [&](FIntPoint Point) {
        return Draft.Enemies.IndexOfByPredicate([Point](const FGridEnemyDraft& Enemy) { return Enemy.Cell == Point; });
    };
    const auto IsEmpty = [&](FIntPoint Point) {
        return Sokoban::FRules::IsInside(Level, Point) && Point != Draft.Start && Point != Draft.Goal &&
            !Draft.Walls.Contains(Point) && !Draft.Rubble.Contains(Point) &&
            BoxAt(Point) == INDEX_NONE && EnemyAt(Point) == INDEX_NONE;
    };
    const int32 BoxIndex = BoxAt(Cell);
    const int32 EnemyIndex = EnemyAt(Cell);

    if (RuntimeTool == EGridPaintTool::Erase)
    {
        if (BoxIndex != INDEX_NONE) { Draft.Boxes.RemoveAt(BoxIndex); return true; }
        if (EnemyIndex != INDEX_NONE) { Draft.Enemies.RemoveAt(EnemyIndex); return true; }
        return Draft.Walls.Remove(Cell) + Draft.Rubble.Remove(Cell) > 0;
    }
    if (RuntimeTool == EGridPaintTool::Rotate)
    {
        if (BoxIndex != INDEX_NONE && Draft.Boxes[BoxIndex].Spike != FIntPoint::ZeroValue)
        {
            auto& Box = Draft.Boxes[BoxIndex];
            const FIntPoint Direction = RotateDirection(Box.Spike);
            if (IsEmpty(Box.Cell + Direction)) { Box.Spike = Direction; return true; }
            EditNotice = LOCTEXT("RotationBlocked", "尖刺转向后的格子不可用。");
            return false;
        }
        if (EnemyIndex != INDEX_NONE && Draft.Enemies[EnemyIndex].BeamDirection != FIntPoint::ZeroValue)
        {
            auto& Direction = Draft.Enemies[EnemyIndex].BeamDirection;
            Direction = RotateDirection(Direction);
            return true;
        }
        return false;
    }
    if (!IsEmpty(Cell) || (RuntimeTool == EGridPaintTool::SpikeCrate && !IsEmpty(Cell + FIntPoint(1, 0))))
    {
        EditNotice = LOCTEXT("Occupied", "目标格或尖刺占格不可用，请选择空格。");
        return false;
    }
    switch (RuntimeTool)
    {
        case EGridPaintTool::Wall: Draft.Walls.Add(Cell); break;
        case EGridPaintTool::Rubble: Draft.Rubble.Add(Cell); break;
        case EGridPaintTool::Start: Draft.Start = Cell; break;
        case EGridPaintTool::Goal: Draft.Goal = Cell; break;
        case EGridPaintTool::Cutout: Draft.VoidCells.Add(Cell); break;
        case EGridPaintTool::Crate:
        case EGridPaintTool::SpikeCrate:
        case EGridPaintTool::BlastCrate:
        case EGridPaintTool::EjectCrate:
        {
            const auto Id = Draft.AllocateEntityId(TEXT("B"), Cell);
            auto& Box = Draft.Boxes.AddDefaulted_GetRef();
            Box.Id = Id;
            Box.Cell = Cell;
            if (RuntimeTool == EGridPaintTool::SpikeCrate) { Box.Spike = {1, 0}; }
            Box.SetExplosionEnabled(RuntimeTool == EGridPaintTool::BlastCrate);
            Box.bEject = RuntimeTool == EGridPaintTool::EjectCrate;
            break;
        }
        case EGridPaintTool::Enemy:
        case EGridPaintTool::BeamEnemy:
        {
            const auto Id = Draft.AllocateEntityId(TEXT("E"), Cell);
            Draft.Enemies.Add({Id, Cell, RuntimeTool == EGridPaintTool::BeamEnemy ? FIntPoint(1, 0) : FIntPoint::ZeroValue});
            break;
        }
        default: return false;
    }
    return true;
}

Sokoban::FResourceContext AGridPlayerController::GetRuntimeValidationResources() const
{
    const auto* Settings = GetDefault<UGridProjectSettings>();
    return Settings->MakeResourceContext(bRuntimeEditorFromMenu ? Settings->StartingInventory() : Session.State.Inventory);
}

Sokoban::FInventory AGridPlayerController::GetRuntimeValidationInventory() const
{
    const auto Resources = GetRuntimeValidationResources();
    return Resources.Filter(Resources.ActualInventory);
}

FText AGridPlayerController::GetRuntimeInventoryText() const
{
    const auto Inventory = GetRuntimeValidationInventory();
    return FText::Format(LOCTEXT("ValidationStock", "当前背包 · 大力士 {0} · 回溯 {1}"), Inventory.Strength, Inventory.Undo);
}

bool AGridPlayerController::CanAttemptRuntimeSave() const
{
    return !IsGridBusy() && IsRuntimeEditing() &&
        (!RuntimeValidationStatus.IsSet() || RuntimeValidationStatus.GetValue() == Sokoban::ECheck::Pass);
}

void AGridPlayerController::ValidateRuntimeDraft()
{
    RequestRuntimeValidation({});
}

void AGridPlayerController::RequestRuntimeValidation(TOptional<FString> SaveSlot)
{
    if (IsGridBusy() || !IsRuntimeEditing()) { return; }
    EditNotice = FText::GetEmpty();
    const auto* Settings = GetDefault<UGridProjectSettings>();
    const auto Limits = Settings->SearchLimits();
    const auto Resources = GetRuntimeValidationResources();
    auto Check = [Level = RuntimeDraft->ToLevel(), Resources, Limits] {
        return Sokoban::FLevelGate::Validate(Level, Resources, Limits, Sokoban::FBoundedLevelSolver());
    };
    auto Complete = [SaveSlot = MoveTemp(SaveSlot), Threshold = Limits.MinimumSolutions]
        (AGridPlayerController& Controller, const Sokoban::FValidation& Result) {
        Controller.bGridBusy = false;
        if (Controller.PresentRuntimeValidation(Result, Threshold) && SaveSlot.IsSet())
        {
            Controller.WriteRuntimeDraft(SaveSlot.GetValue());
        }
    };
    if (!GetLocalPlayer())
    {
        Complete(*this, Check());
        return;
    }
    bGridBusy = true;
    RuntimeValidationStatus.Reset();
    RuntimeValidationText = LOCTEXT("ValidationRunning", "正在检验关卡与当前背包，请稍候。");
    DispatchGridWork(*this, MoveTemp(Check), MoveTemp(Complete));
}

bool AGridPlayerController::PresentRuntimeValidation(const Sokoban::FValidation& Result, int32 Threshold)
{
    using namespace Sokoban;
    RuntimeValidationStatus = Result.Status;
    if (!Result.Errors.IsEmpty())
    {
        RuntimeValidationText = FText::Format(LOCTEXT("LayoutRejected", "无法保存 · {0}"), FText::FromString(Result.Errors[0]));
    }
    else if (Result.Status == ECheck::Pass)
    {
        RuntimeValidationText = FText::Format(LOCTEXT("ValidationPassed", "校验通过 · 当前背包已找到至少 {0} 解。"),
            Result.Actual.CountLowerBound);
    }
    else if (Result.Status == ECheck::Unknown)
    {
        RuntimeValidationText = LOCTEXT("ValidationUnknown", "暂无法保存 · 搜索预算内未能确认达标。");
    }
    else if (Result.Actual.Solvable == ECheck::Fail)
    {
        RuntimeValidationText = LOCTEXT("ValidationNoSolution", "无法保存 · 当前背包在配置的行动步数内无解。");
    }
    else
    {
        RuntimeValidationText = FText::Format(LOCTEXT("ValidationInsufficient", "无法保存 · 能力和当前背包分别需至少 {0} 解。当前分别找到 {1} 解、{2} 解。"),
            Threshold, Result.Capability.CountLowerBound, Result.Actual.CountLowerBound);
    }
    return Result.Status == ECheck::Pass;
}

void AGridPlayerController::SaveRuntimeDraft()
{
    SaveRuntimeDraftToSlot(DraftSlot);
}

void AGridPlayerController::SaveRuntimeDraftToSlot(const FString& Slot)
{
    RequestRuntimeValidation(Slot);
}

void AGridPlayerController::WriteRuntimeDraft(const FString& Slot)
{
    const FString Backup = Slot + TEXT("_Backup");
    const auto Primary = ReadDraft(this, Slot);
    const auto Secondary = ReadDraft(this, Backup);
    const FString& Destination = Primary.Layout && Primary.Revision >= Secondary.Revision ? Backup : Slot;
    const int64 Previous = FMath::Max(Primary.Revision, Secondary.Revision);
    if (Previous == MAX_int64)
    {
        EditNotice = LOCTEXT("RevisionLimit", "草稿版本号已达上限，现有存档已保留。");
        return;
    }
    auto* Save = NewObject<UGridRuntimeDraftSave>();
    Save->Revision = Previous + 1;
    FMemoryWriter Writer(Save->Layout);
    FObjectAndNameAsStringProxyArchive Archive(Writer, false);
    Archive.ArNoDelta = true;
    RuntimeDraft->SerializeScriptProperties(Archive);
    const bool bSaved = !Archive.IsError() && !Writer.IsError() &&
        UGameplayStatics::SaveGameToSlot(Save, Destination, 0) &&
        ReadDraft(this, Destination).Revision == Save->Revision;
    EditNotice = bSaved
        ? LOCTEXT("Saved", "已保存到本地运行时草稿槽。")
        : LOCTEXT("SaveFailed", "本地草稿保存失败，原有可读取版本已保留。");
}

void AGridPlayerController::LoadRuntimeDraft()
{
    LoadRuntimeDraftFromSlot(DraftSlot);
}

void AGridPlayerController::LoadRuntimeDraftFromSlot(const FString& Slot)
{
    if (IsGridBusy()) { return; }
    if (!IsRuntimeEditing()) { return; }
    const auto Primary = ReadDraft(this, Slot);
    const auto Secondary = ReadDraft(this, Slot + TEXT("_Backup"));
    auto* Loaded = Primary.Revision >= Secondary.Revision ? Primary.Layout : Secondary.Layout;
    if (!Loaded)
    {
        EditNotice = LOCTEXT("NoReadableSave", "没有可读取的本地草稿。存档可能不存在，或两份存档均已损坏。");
        return;
    }
    RuntimeDraft = Loaded;
    PreviewRuntimeDraft();
    EditNotice = LOCTEXT("Loaded", "已载入本地草稿。");
}

#undef LOCTEXT_NAMESPACE
