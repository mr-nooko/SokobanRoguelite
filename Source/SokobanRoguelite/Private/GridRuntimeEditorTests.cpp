#include "GridGameMode.h"

#include "Engine/World.h"
#include "GridLevelAsset.h"
#include "GridRuntimeDraftSave.h"
#include "GridScene.h"
#include "GridProjectSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridRuntimeEditTest, "Sokoban.Preview.RuntimeLevelEditing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridRuntimeEditTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient game world"), World)) { return false; }
    auto* Controller = World->SpawnActor<AGridPlayerController>();
    auto* Scene = World->SpawnActor<AGridScene>();
    if (!Controller || !Scene)
    {
        World->DestroyWorld(false);
        return false;
    }

    auto* Original = NewObject<UGridLevelAsset>(Scene);
    Original->RoomId = TEXT("RuntimeEditingTest");
    Original->Size = {5, 5};
    Original->Start = {0, 0};
    Original->Goal = {4, 4};
    Scene->DraftLevel = Original;
    Controller->Scene = Scene;
    TestTrue(TEXT("The authored room starts normally"), Controller->TryLoadRoom(0, {1, 2}));
    Controller->Submit(EKeys::D);
    Controller->Scene->ResetOutcome();
    Controller->ToggleRuntimeEditor();
    if (!TestTrue(TEXT("F2 opens an isolated editable copy"), Controller->IsRuntimeEditing() &&
        Controller->RuntimeDraft != Original))
    {
        World->DestroyWorld(false);
        return false;
    }

    Controller->SetRuntimeTool(EGridPaintTool::SpikeCrate);
    TestFalse(TEXT("A spike may not extend beyond the board"), Controller->ApplyRuntimeTool({4, 1}));
    TestTrue(TEXT("A spike crate may occupy two free cells"), Controller->ApplyRuntimeTool({2, 1}));
    Controller->SetRuntimeTool(EGridPaintTool::Rotate);
    TestTrue(TEXT("A placed spike can be rotated"), Controller->ApplyRuntimeTool({2, 1}));
    Controller->SetRuntimeTool(EGridPaintTool::Cutout);
    TestFalse(TEXT("A spike tip blocks removing ground"), Controller->ApplyRuntimeTool({2, 2}));
    TestTrue(TEXT("An empty tile can be removed"), Controller->ApplyRuntimeTool({3, 3}));
    Controller->ResizeRuntimeDraft({4, 5});
    TestEqual(TEXT("Resizing cannot crop the goal"), Controller->GetRuntimeDraftSize(), FIntPoint(5, 5));
    Controller->ResizeRuntimeDraft({6, 5});
    TestEqual(TEXT("An expanded board is applied"), Controller->GetRuntimeDraftSize(), FIntPoint(6, 5));

    const FString Slot = FString::Printf(TEXT("RuntimeLevelDraftTest_%s"), *FGuid::NewGuid().ToString());
    Controller->SaveRuntimeDraftToSlot(Slot);
    TestTrue(TEXT("Runtime drafts save to an isolated local slot"), UGameplayStatics::DoesSaveGameExist(Slot, 0));
    Controller->SetRuntimeTool(EGridPaintTool::Restore);
    TestTrue(TEXT("A cutout can be restored"), Controller->ApplyRuntimeTool({3, 3}));
    Controller->LoadRuntimeDraftFromSlot(Slot);
    TestTrue(TEXT("Loading restores the saved layout and object settings"),
        Controller->RuntimeDraft->VoidCells.Contains({3, 3}) &&
        Controller->RuntimeDraft->Boxes.Num() == 1 && Controller->RuntimeDraft->Boxes[0].Spike == FIntPoint(0, 1));
    const FString Backup = Slot + TEXT("_Backup");
    TestTrue(TEXT("Restore a tile for a second revision"), Controller->ApplyRuntimeTool({3, 3}));
    Controller->SaveRuntimeDraftToSlot(Slot);
    Controller->LoadRuntimeDraftFromSlot(Slot);
    TestFalse(TEXT("Loading prefers the newest readable revision"), Controller->RuntimeDraft->VoidCells.Contains({3, 3}));
    TestTrue(TEXT("Simulate an unreadable newest revision"),
        UGameplayStatics::SaveGameToSlot(NewObject<UGridRuntimeDraftSave>(), Backup, 0));
    Controller->LoadRuntimeDraftFromSlot(Slot);
    TestTrue(TEXT("An unreadable newest revision falls back to the previous draft"),
        Controller->RuntimeDraft->VoidCells.Contains({3, 3}));
    const auto SavedLayout = Controller->RuntimeDraft->ToLevel();
    auto* Settings = GetMutableDefault<UGridProjectSettings>();
    {
        const TGuardValue<int32> Budget(Settings->NodeBudget, 1);
        Controller->SaveRuntimeDraftToSlot(Slot);
        TestTrue(TEXT("An unknown result blocks saving and has a visible reason"),
            Controller->GetRuntimeValidationStatus() == Sokoban::ECheck::Unknown &&
            !Controller->CanAttemptRuntimeSave() && !Controller->GetRuntimeValidationText().IsEmpty());
    }

    Sokoban::FLevel NeedsStrength;
    NeedsStrength.RoomId = TEXT("NeedsStrength");
    NeedsStrength.Size = {7, 3};
    NeedsStrength.Start = {0, 1};
    NeedsStrength.Goal = {6, 1};
    NeedsStrength.Walls = {{0, 0}, {0, 2}};
    NeedsStrength.Boxes = {{TEXT("A"), {1, 1}}, {TEXT("B"), {2, 1}}};
    Controller->RuntimeDraft->SetFromLevel(NeedsStrength);
    Controller->ValidateRuntimeDraft();
    TestTrue(TEXT("The two-box draft passes while the real inventory still contains a strength pill"),
        Controller->GetRuntimeValidationStatus() == Sokoban::ECheck::Pass);
    Controller->Session.State.Inventory.Strength = 0;
    Controller->SaveRuntimeDraftToSlot(Slot);
    TestTrue(TEXT("A draft needing a spent pill cannot save even if it passed earlier"),
        Controller->GetRuntimeValidationStatus() == Sokoban::ECheck::Fail && !Controller->CanAttemptRuntimeSave());
    Controller->LoadRuntimeDraftFromSlot(Slot);
    TestEqual(TEXT("Rejected saves preserve the prior file"), Controller->RuntimeDraft->RoomId, SavedLayout.RoomId);
    TestEqual(TEXT("An actual edit or load clears the old result"), Controller->GetRuntimeValidationStatus().IsSet(), false);
    UGameplayStatics::DeleteGameInSlot(Slot, 0);
    UGameplayStatics::DeleteGameInSlot(Backup, 0);

    Controller->TestRuntimeDraft();
    TestTrue(TEXT("The separate test starts from the edited layout with a fresh turn"),
        Controller->IsRuntimeTest() && Controller->GetGridState().Turn == 0 &&
        Controller->GetGridState().Board.Size == FIntPoint(6, 5) &&
        Controller->GetGridState().Inventory.Strength == 0);
    Controller->ToggleRuntimeEditor();
    TestTrue(TEXT("F2 returns from testing to the same edit"), Controller->IsRuntimeEditing() &&
        Controller->RuntimeDraft->VoidCells.Contains({3, 3}));
    Controller->CloseRuntimeEditor();
    TestTrue(TEXT("Closing restores the original session and leaves the content asset alone"),
        !Controller->IsRuntimeEditing() && !Controller->IsRuntimeTest() &&
        Controller->GetGridState().Turn == 1 && Controller->GetGridState().Player == FIntPoint(1, 0) &&
        Controller->GetGridState().Board.Size == FIntPoint(5, 5) &&
        Original->Boxes.IsEmpty() && Original->VoidCells.IsEmpty());
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridMenuEditorTest, "Sokoban.Preview.MenuRuntimeEditor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGridMenuEditorTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient game world"), World)) { return false; }
    auto* Controller = World->SpawnActor<AGridPlayerController>();
    auto* Scene = World->SpawnActor<AGridScene>();
    if (!Controller || !Scene)
    {
        World->DestroyWorld(false);
        return false;
    }
    Controller->Scene = Scene;
    Controller->OpenStartMenu();
    Controller->OpenRuntimeEditorFromMenu();
    TestTrue(TEXT("The start menu opens a separate editable board"),
        !Controller->IsStartMenuOpen() && Controller->IsRuntimeEditing() && Controller->WillEditorReturnToMenu());
    Controller->SetRuntimeTool(EGridPaintTool::Wall);
    TestTrue(TEXT("A component can be placed in the board from the menu"), Controller->ApplyRuntimeTool({0, 0}));

    const FString Slot = FString::Printf(TEXT("MenuRuntimeDraftTest_%s"), *FGuid::NewGuid().ToString());
    Controller->SaveRuntimeDraftToSlot(Slot);
    TestTrue(TEXT("The menu draft can be validated and saved"), UGameplayStatics::DoesSaveGameExist(Slot, 0));

    Controller->TestRuntimeDraft();
    const auto StartingInventory = GetDefault<UGridProjectSettings>()->StartingInventory();
    TestTrue(TEXT("Independent playtest uses the starting pills"),
        Controller->IsRuntimeTest() && Controller->GetGridState().Inventory.Strength == StartingInventory.Strength &&
        Controller->GetGridState().Inventory.Undo == StartingInventory.Undo);
    Controller->ToggleRuntimeEditor();
    Controller->CloseRuntimeEditor();
    TestTrue(TEXT("Closing the editor restores the start menu with no running session"),
        Controller->IsStartMenuOpen() && !Controller->IsRuntimeEditing() && !Controller->IsRuntimeTest() &&
        Controller->GetGridState().Board.RoomId.IsNone());

    Controller->OpenRuntimeEditorFromMenu();
    Controller->LoadRuntimeDraftFromSlot(Slot);
    TestTrue(TEXT("The edited component reloads after leaving and reentering"),
        Controller->RuntimeDraft && Controller->RuntimeDraft->Walls.Contains({0, 0}));
    Controller->CloseRuntimeEditor();
    Controller->StartNewRun();
    TestTrue(TEXT("Start Game works after the editor round trip"),
        !Controller->IsStartMenuOpen() && !Controller->IsRuntimeEditing() &&
        !Controller->GetGridState().Board.RoomId.IsNone());
    UGameplayStatics::DeleteGameInSlot(Slot, 0);
    World->DestroyWorld(false);
    return true;
}
#endif
