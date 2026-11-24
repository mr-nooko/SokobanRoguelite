#include "GridPreview.h"
#include "GridGameMode.h"
#include "GridScene.h"
#include "GridLevelAsset.h"
#include "GridProjectSettings.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridLegacyEjectTest, "Sokoban.Levels.LegacyEjectMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridLegacyEjectTest::RunTest(const FString&)
{
    auto* Before = NewObject<UGridLevelAsset>();
    auto& Box = Before->Boxes.AddDefaulted_GetRef();
    Box.EjectDirections = {{0, -1}, {-1, 0}};
    TArray<uint8> Bytes;
    FObjectWriter Writer(Before, Bytes);
    auto* Loaded = NewObject<UGridLevelAsset>();
    FObjectReader Reader(Loaded, Bytes);
    if (!TestTrue(TEXT("Loaded the old box"), Loaded->Boxes.Num() == 1)) { return false; }
    TestTrue(TEXT("Legacy direction sequence becomes an enabled straight eject"),
        Loaded->Boxes[0].bEject && Loaded->Boxes[0].EjectDirections.IsEmpty() && Loaded->ToLevel().Boxes[0].bEject);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPreviewWalkthrough, "Sokoban.Preview.HandAuthoredRooms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPreviewWalkthrough::RunTest(const FString&)
{
    using namespace Sokoban;
    const TCHAR* Paths[] = {TEXT("RRRRRUUU"), TEXT("URRURRRU"), TEXT("RRUURRRU"),
        TEXT("RRRRRR"), TEXT("RURRRRUU"), TEXT("URRRRRUU")};
    static_assert(UE_ARRAY_COUNT(Paths) == GridPreview::RoomCount);
    const FBoundedLevelSolver Solver;
    const FAction Push{EAction::Move, {1, 0}};
    for (int32 Room = 0; Room < GridPreview::RoomCount; ++Room)
    {
        const FLevel Level = GridPreview::MakeRoom(Room);
        const FValidation Report = FLevelGate::Validate(Level, {true, true, {1, 2}, {}},
            {12, 10000, EDifficulty::High}, Solver);
        TestTrue(*FString::Printf(TEXT("Lesson %d qualifies with no pills (status %d, counts %d / %d)"), Room + 1, int32(Report.Status), Report.Capability.CountLowerBound, Report.Actual.CountLowerBound), Report.Status == ECheck::Pass);
        TestTrue(TEXT("Lesson has a hint"), FCString::Strlen(GridPreview::GetLesson(Room).Hint) > 0);
        FSession Session = FRules::Start(Level, {});
        for (const TCHAR Letter : FString(Paths[Room]))
        {
            FRules::Apply(Session, Letter == 'U' ? FAction{EAction::Move, {0, -1}} : Push);
        }
        TestTrue(*FString::Printf(TEXT("Lesson %d walkthrough succeeds"), Room + 1), Session.State.Outcome == EOutcome::Won);
        if (Room == 2) { TestTrue(TEXT("Side sweep killed the guard"), Session.State.Board.Enemies.IsEmpty()); }
        if (Room == 3)
        {
            TestTrue(TEXT("Bomb removed rubble and guard but preserved wall"), Session.State.Board.Rubble.IsEmpty() &&
                Session.State.Board.Enemies.IsEmpty() && Session.State.Board.Walls.Contains({1, 3}));
        }
        if (Room == 4)
        {
            TestEqual(TEXT("Eject box stops on the last cell before the wall"), Session.State.Board.Boxes[0].Cell, FIntPoint(6, 4));
        }
    }

    FSession Practice = FRules::Start(GridPreview::MakeRoom(5), {1, 1});
    TestFalse(TEXT("Two crates need a strength pill"), FRules::Apply(Practice, Push).bEffective);
    FRules::Apply(Practice, {EAction::ArmStrength});
    TestTrue(TEXT("Strength pill pushes both crates"), FRules::Apply(Practice, Push).bEffective &&
        Practice.State.Board.Boxes[1].Cell == FIntPoint(4, 4));
    FRules::Apply(Practice, {EAction::Undo, FIntPoint::ZeroValue, 1});
    TestTrue(TEXT("Time pill restores the crates and strength pill but is spent"),
        Practice.State.Board.Boxes[1].Cell == FIntPoint(3, 4) &&
        Practice.State.Inventory.Strength == 1 && Practice.State.Inventory.Undo == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridRoomRewardTest, "Sokoban.Preview.RoomRewardAndDeath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridRoomRewardTest::RunTest(const FString&)
{
    using namespace Sokoban;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world"), World)) { return false; }
    auto* Controller = World->SpawnActor<AGridPlayerController>();
    auto* Scene = World->SpawnActor<AGridScene>();
    if (!Controller || !Scene) { World->DestroyWorld(false); return false; }
    Controller->Scene = Scene;
    const auto NewRoom = [](FName Id) {
        auto* Room = NewObject<UGridLevelAsset>();
        Room->RoomId = Id;
        Room->Size = {4, 4};
        Room->Start = {0, 0};
        Room->Goal = {1, 0};
        return Room;
    };
    auto* First = NewRoom(TEXT("RewardRoom"));
    First->PlaytestHint = FText::FromString(TEXT("Walk east"));
    First->ClearReward.Strength = 2;
    First->ClearReward.Undo = 3;
    auto* Next = NewRoom(TEXT("NextRoom"));
    Next->Enemies.Add({TEXT("Guard"), {0, 1}});
    Scene->PlaytestSequence = {First, nullptr};
    TestTrue(TEXT("First room loads"), Controller->TryLoadRoom(0, {1, 1}));
    TestTrue(TEXT("The authored hint is exposed and the reward is hidden before victory"),
        Controller->GetRoomHint().EqualTo(First->PlaytestHint) && Controller->GetRoomRewardNotice().IsEmpty());
    Controller->Submit(EKeys::D);
    Controller->SettleRoomReward();
    const auto& State = Controller->GetGridState();
    TestTrue(TEXT("Victory adds both pills exactly once and exposes a receipt"),
        State.Outcome == EOutcome::Won && State.Inventory.Strength == 3 && State.Inventory.Undo == 4 &&
        !Controller->GetRoomRewardNotice().IsEmpty());
    Scene->ResetOutcome();
    Controller->ConfirmOrNextRoom();
    TestTrue(TEXT("Invalid next room preserves the receipt and inventory"),
        Controller->GetRoomNumber() == 1 && !Controller->GetTransitionError().IsEmpty() &&
        State.Inventory.Strength == 3 && State.Inventory.Undo == 4 && !Controller->GetRoomRewardNotice().IsEmpty());
    Scene->PlaytestSequence[1] = Next;
    Controller->ConfirmOrNextRoom();
    TestTrue(TEXT("Enter loads the validated room with the reward included and clears the old receipt"),
        Controller->GetRoomNumber() == 2 && State.Inventory.Strength == 3 && State.Inventory.Undo == 4 &&
        Controller->GetRoomRewardNotice().IsEmpty());
    Controller->Submit(EKeys::S);
    TestTrue(TEXT("Contact death clears the entire inventory and undo history"),
        State.Outcome == EOutcome::Dead && State.Inventory.Strength == 0 && State.Inventory.Undo == 0 &&
        Controller->Session.History.IsEmpty() && Controller->GetRoomRewardNotice().IsEmpty());
    TestTrue(TEXT("A freshly loaded room can be played without the previous reward"),
        Controller->TryLoadRoom(1, {}));
    Controller->Submit(EKeys::D);
    TestTrue(TEXT("A victory without a reward has no receipt"),
        State.Outcome == EOutcome::Won && Controller->GetRoomRewardNotice().IsEmpty());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridSequenceGateTest, "Sokoban.Preview.ManualSequenceGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridSequenceGateTest::RunTest(const FString&)
{
    using namespace Sokoban;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient game world"), World)) { return false; }
    auto* Controller = World->SpawnActor<AGridPlayerController>();
    auto* Scene = World->SpawnActor<AGridScene>();
    if (!Controller || !Scene)
    {
        World->DestroyWorld(false);
        return false;
    }
    Controller->Scene = Scene;
    const auto NewRoom = [](FName Id) {
        auto* Room = NewObject<UGridLevelAsset>();
        Room->RoomId = Id;
        Room->Size = {3, 3};
        Room->Start = {0, 0};
        Room->Goal = {2, 2};
        return Room;
    };
    auto* First = NewRoom(TEXT("First"));
    auto* Next = NewRoom(TEXT("Next"));
    Scene->PlaytestSequence = {First, nullptr};
    TestTrue(TEXT("First authored room passes shared validation"), Controller->TryLoadRoom(0, {0, 1}));
    Controller->NextRoom();
    TestEqual(TEXT("No switch before winning"), Controller->RoomIndex, 0);
    Controller->Session.State.Outcome = EOutcome::Won;
    Controller->NextRoom();
    TestTrue(TEXT("Missing next slot preserves the win and carried pills"),
        Controller->RoomIndex == 0 && Controller->GetGridState().Outcome == EOutcome::Won &&
        Controller->GetGridState().Inventory.Undo == 1 && !Controller->GetTransitionError().IsEmpty());
    Scene->PlaytestSequence[1] = Next;
    Next->Size.X = FLevelGate::MaxBoardSide + 1;
    Controller->NextRoom();
    TestTrue(TEXT("Static check and formal entry share the size limit"),
        Next->GetLayoutWarnings().Contains(FLevelGate::BoardSizeError()) && Controller->RoomIndex == 0 &&
        Controller->GetTransitionError().ToString() == FLevelGate::BoardSizeError());
    Next->Size.X = 3;
    Next->Walls.Add(Next->Goal);
    Controller->NextRoom();
    TestEqual(TEXT("Invalid next layout does not advance"), Controller->RoomIndex, 0);

    Next->Walls = {{0, 0}, {0, 2}};
    Next->Size = {7, 3};
    Next->Start = {0, 1};
    Next->Goal = {6, 1};
    Next->Boxes.Add({TEXT("A"), {1, 1}});
    Next->Boxes.Add({TEXT("B"), {2, 1}});
    const FBoundedLevelSolver Solver;
    TestTrue(TEXT("Candidate is valid with a strength pill"), FLevelGate::Validate(Next->ToLevel(),
        {true, true, {1, 2}, {1, 1}}, {12, 10000, EDifficulty::High}, Solver).Status == ECheck::Pass);
    Controller->NextRoom();
    TestEqual(TEXT("A room that needs a spent strength pill does not advance"), Controller->RoomIndex, 0);

    Next->Walls.Reset();
    Next->Boxes.Reset();
    Next->Size = {3, 3};
    Next->Start = {0, 0};
    Next->Goal = {2, 2};
    auto* Settings = GetMutableDefault<UGridProjectSettings>();
    {
        const TGuardValue<int32> Budget(Settings->NodeBudget, 1);
        const auto Report = FLevelGate::Validate(Next->ToLevel(), Settings->MakeResourceContext({0, 1}),
            Settings->SearchLimits(), Solver);
        Controller->NextRoom();
        TestTrue(TEXT("A search budget change produces Unknown and prevents formal entry"),
            Report.Status == ECheck::Unknown && Controller->RoomIndex == 0 &&
            Controller->GetTransitionError().ToString().Contains(TEXT("搜索预算")));
    }
    Controller->Session.History.Add(Controller->Session.State);
    Controller->NextRoom();
    TestTrue(TEXT("Manual retry loads corrected room with remaining inventory and fresh history"),
        Controller->RoomIndex == 1 && Controller->GetGridState().Board.RoomId == Next->RoomId &&
        Controller->GetGridState().Inventory.Undo == 1 && Controller->GetGridState().Turn == 0 &&
        Controller->Session.History.IsEmpty() && Controller->GetTransitionError().IsEmpty());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridUndoSelectionTest, "Sokoban.Preview.UndoSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridUndoSelectionTest::RunTest(const FString&)
{
    using namespace Sokoban;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient game world"), World)) { return false; }
    auto* Controller = World->SpawnActor<AGridPlayerController>();
    if (!TestNotNull(TEXT("Playtest controller"), Controller))
    {
        World->DestroyWorld(false);
        return false;
    }

    FLevel Level;
    Level.RoomId = TEXT("UndoSelection");
    Level.Size = {3, 3};
    Level.Start = {0, 0};
    Level.Goal = {2, 2};
    Controller->Session = FRules::Start(Level, {1, 2});
    Controller->ToggleUndoSelection();
    TestEqual(TEXT("No history means no selection"), Controller->GetSelectedUndoSteps(), 0);
    for (int32 Step = 0; Step < 12; ++Step)
    {
        FRules::Apply(Controller->Session, {EAction::Move, {Step % 2 == 0 ? 1 : -1, 0}});
    }
    Controller->ToggleUndoSelection();
    Controller->ChangeUndoSelection(EKeys::Down);
    TestEqual(TEXT("Selection starts at one and cannot go lower"), Controller->GetSelectedUndoSteps(), 1);
    for (int32 Step = 0; Step < 12; ++Step) { Controller->ChangeUndoSelection(EKeys::Up); }
    TestEqual(TEXT("Selection cannot exceed ten"), Controller->GetSelectedUndoSteps(), 10);
    Controller->Submit(EKeys::D);
    Controller->Arm();
    TestEqual(TEXT("Movement is paused while selecting"), Controller->GetGridState().Turn, 12);
    TestEqual(TEXT("Strength is not consumed while selecting"), Controller->GetGridState().Inventory.Strength, 1);
    Controller->ToggleUndoSelection();
    TestEqual(TEXT("Cancel spends nothing"), Controller->GetGridState().Inventory.Undo, 2);

    Controller->ToggleUndoSelection();
    Controller->ChangeUndoSelection(EKeys::Up);
    Controller->ChangeUndoSelection(EKeys::Up);
    Controller->ConfirmOrNextRoom();
    TestEqual(TEXT("Confirm restores exactly three steps"), Controller->GetGridState().Turn, 9);
    TestEqual(TEXT("Multiple steps cost one item"), Controller->GetGridState().Inventory.Undo, 1);
    Controller->Session.State.Outcome = EOutcome::Dead;
    Controller->ToggleUndoSelection();
    TestEqual(TEXT("Death disables selection"), Controller->GetSelectedUndoSteps(), 0);
    World->DestroyWorld(false);
    return true;
}
#endif

