#include "GridRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
Sokoban::FLevel Room()
{
    Sokoban::FLevel Level;
    Level.RoomId = TEXT("Test");
    Level.Size = {7, 5};
    Level.Start = {1, 2};
    Level.Goal = {6, 4};
    return Level;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridSpikeAndUndo, "Sokoban.Rules.SpikeAndUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridSpikeAndUndo::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Boxes.Add({TEXT("Spike"), {2, 2}, {1, 0}});
    Level.Enemies.Add({TEXT("Guard"), {4, 2}});
    FSession Session = FRules::Start(Level, {0, 1});
    FRules::Apply(Session, {EAction::Move, {1, 0}});
    TestEqual(TEXT("Spike kills without box taking the enemy cell"), Session.State.Board.Boxes[0].Cell, FIntPoint(3, 2));
    TestTrue(TEXT("Enemy killed"), Session.State.Board.Enemies.IsEmpty());
    FRules::Apply(Session, {EAction::Undo, FIntPoint::ZeroValue, 1});
    TestEqual(TEXT("Enemy restored"), Session.State.Board.Enemies.Num(), 1);
    TestEqual(TEXT("Undo spent"), Session.State.Inventory.Undo, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridBeamBeforeBlast, "Sokoban.Rules.BeamBeforeBlast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridBeamBeforeBlast::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Start = {2, 3};
    Sokoban::FBox Box{TEXT("Bomb"), {2, 2}};
    Box.bExplodes = true;
    Level.Boxes.Add(Box);
    Level.Enemies.Add({TEXT("Laser"), {1, 2}, {1, 0}});
    FSession Session = FRules::Start(Level, {0, 1});
    const FTurnResult Result = FRules::Apply(Session, {EAction::Move, {0, -1}});
    const int32 Exposure = Result.Events.IndexOfByPredicate([](const Sokoban::FEvent& E) { return E.Kind == EEvent::BeamExposure; });
    const int32 BlastEvent = Result.Events.IndexOfByPredicate([](const Sokoban::FEvent& E) { return E.Kind == EEvent::Blast; });
    TestTrue(TEXT("Exposure recorded before blast"), Exposure != INDEX_NONE && BlastEvent > Exposure);
    TestTrue(TEXT("Blast still finishes"), Session.State.Board.Enemies.IsEmpty());
    TestTrue(TEXT("Death stays final"), Session.State.Outcome == EOutcome::Dead);
    TestFalse(TEXT("Dead cannot undo"), FRules::Apply(Session, {EAction::Undo, FIntPoint::ZeroValue, 1}).bAccepted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridGate, "Sokoban.Levels.DraftGateAndSequenceCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridGate::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Size = {2, 2};
    Level.Start = {0, 0};
    Level.Goal = {1, 1};
    FBoundedLevelSolver Solver;
    const FValidation Validated = FLevelGate::Validate(Level, {}, {4, 200}, Solver);
    TestTrue(TEXT("One solution in both stages proves legality"), Validated.Status == ECheck::Pass);
    TestEqual(TEXT("Different sequences sharing same goal count separately"), Solver.Search(Level, {}, {4, 200}).CountLowerBound, 6);
    TestTrue(TEXT("Interrupted search reports unknown"), Solver.Search(Level, {}, {4, 1}).MeetsMinimum == ECheck::Unknown);
    FLevel Single = Level;
    Single.Size = {2, 1};
    Single.Goal = {1, 0};
    TestTrue(TEXT("A single solution is legal by default"), FLevelGate::Validate(Single, {}, {4, 200}, Solver).Status == ECheck::Pass);
    FSearchLimits Stricter{4, 200};
    Stricter.MinimumSolutions = 2;
    TestTrue(TEXT("The configured minimum applies to both stages"), FLevelGate::Validate(Single, {}, Stricter, Solver).Status == ECheck::Fail);
    FLevelDraft Draft(Level);
    Draft.Edit().Boxes.Add({TEXT("Invalid"), Level.Goal});
    TestFalse(TEXT("Invalid data stays editable"), Draft.Warnings().IsEmpty());
    TestFalse(TEXT("Invalid draft cannot be opened in formal runtime"),
        FLevelGate::OpenFormal(Draft.Read(), {}, {4, 200}, Solver).IsSet());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridTargetDifficulty, "Sokoban.Levels.TargetDifficultyStopsEarly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridTargetDifficulty::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Size = {2, 2};
    Level.Start = {0, 0};
    Level.Goal = {1, 1};
    const FBoundedLevelSolver Solver;
    for (const EDifficulty Target : {EDifficulty::High, EDifficulty::Medium, EDifficulty::Low})
    {
        const FSearchLimits Limits{8, 200, Target};
        const FValidation Report = FLevelGate::Validate(Level, {}, Limits, Solver);
        TestTrue(TEXT("Target is accepted without exhaustive enumeration"), Report.Status == ECheck::Pass &&
            Report.TargetDifficultyStatus == ECheck::Pass && !Report.Actual.bComplete);
        TestEqual(TEXT("Capability stops at the global minimum"), Report.Capability.CountLowerBound, 1);
        TestEqual(TEXT("Actual stops at the requested threshold"), Report.Actual.CountLowerBound, Limits.DifficultyThreshold());
        if (Target != EDifficulty::Low)
        {
            TestTrue(TEXT("Acceptance preserves unknown exact difficulty"), Report.Actual.Difficulty == EDifficulty::Unknown);
        }
    }
    for (const auto Target : {EDifficulty::High, EDifficulty::Medium, EDifficulty::Low})
    {
        FSearchLimits Exact{8, 200, Target};
        Exact.bRequireDifficultyBand = true;
        const auto Report = FLevelGate::Validate(Level, {}, Exact, Solver);
        TestTrue(TEXT("A thirty-solution layout is accepted only in the low band"),
            Report.Status == (Target == EDifficulty::Low ? ECheck::Pass : ECheck::Fail));
    }
    FSearchLimits ExactHigh{6, 200, EDifficulty::High};
    ExactHigh.bRequireDifficultyBand = true;
    ExactHigh.MaxActions = 4;
    const auto Medium = FLevelGate::Validate(Level, {}, ExactHigh, Solver);
    TestTrue(TEXT("Six solutions exceed the hard band"), Medium.Status == ECheck::Fail && Medium.LegalityStatus == ECheck::Pass);
    ExactHigh.TargetDifficulty = EDifficulty::Medium;
    ExactHigh.NodeBudget = 5;
    const auto IncompleteHigh = FLevelGate::Validate(Level, {}, ExactHigh, Solver);
    TestTrue(TEXT("An uncertified upper bound stays unknown"), IncompleteHigh.Status == ECheck::Unknown);
    FSearchLimits Required{8, 200, EDifficulty::High};
    int32 Seen = 0;
    Required.RequiredWitness = [&Seen](const TArray<FAction>&) { return ++Seen == 6; };
    const auto LaterWitness = FLevelGate::Validate(Level, {}, Required, Solver);
    TestTrue(TEXT("A required sixth witness is found after the minimum is met"),
        LaterWitness.Status == ECheck::Pass && LaterWitness.Actual.bHasRequiredWitness && Seen == 6);
    const auto Below = FLevelGate::Validate(Level, {}, {2, 200, EDifficulty::Medium}, Solver);
    TestTrue(TEXT("Two legal solutions are below the medium target"),
        Below.Actual.MeetsMinimum == ECheck::Pass && Below.Status == ECheck::Fail);
    const auto Budget = FLevelGate::Validate(Level, {}, {8, 1, EDifficulty::Medium}, Solver);
    TestTrue(TEXT("An interrupted search below target stays unknown"), Budget.Status == ECheck::Unknown);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridChainModuleNotice, "Sokoban.Rules.StrengthChainModuleNotice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridChainModuleNotice::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Boxes.Add({TEXT("Front"), {3, 2}});
    Level.Boxes.Add({TEXT("Rear"), {2, 2}});
    Level.StrengthLimit = 2;
    auto Session = FRules::Start(Level, {1, 0});
    TestTrue(TEXT("Two ordinary boxes do not need a module notice"), FLevelGate::ChainModuleAdvisory(Level).IsEmpty());
    FRules::Apply(Session, {EAction::ArmStrength});
    TestFalse(TEXT("Ordinary strength chain emits no module notice"),
        FRules::Apply(Session, {EAction::Move, {1, 0}}).bSkippedChainEffects);
    Level.Boxes[0].bEject = true;
    Session = FRules::Start(Level, {1, 0});
    TestFalse(TEXT("Designers are warned about chained motion effects"), FLevelGate::ChainModuleAdvisory(Level).IsEmpty());
    FRules::Apply(Session, {EAction::ArmStrength});
    const auto Result = FRules::Apply(Session, {EAction::Move, {1, 0}});
    TestTrue(TEXT("Successful chain movement reports the skipped module"), Result.bEffective && Result.bSkippedChainEffects);
    TestEqual(TEXT("The cabinet moves one cell under the existing chain rule"),
        Session.State.Board.Boxes[0].Cell, FIntPoint(4, 2));
    Level.Boxes[0].Spike = {0, 1};
    Level.Enemies.Add({TEXT("Guard"), {4, 3}});
    Session = FRules::Start(Level, {1, 0});
    FRules::Apply(Session, {EAction::ArmStrength});
    const auto Blocked = FRules::Apply(Session, {EAction::Move, {1, 0}});
    TestTrue(TEXT("An inactive spike remains solid and cannot kill or overlap a guard in a chain"),
        Blocked.bAccepted && !Blocked.bEffective && Session.State.Board.Enemies.Num() == 1 &&
        Session.State.Board.Boxes[0].Cell == FIntPoint(3, 2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridBeamEndpoints, "Sokoban.Rules.BeamEndpoints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridBeamEndpoints::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Start = {4, 3};
    Level.Boxes.Add({TEXT("Shield"), {4, 2}});
    Level.Enemies.Add({TEXT("West"), {0, 2}, {1, 0}});
    Level.Enemies.Add({TEXT("North"), {5, 0}, {0, 1}});
    FSession Session = FRules::Start(Level, {});
    const auto& State = Session.State;
    if (!TestEqual(TEXT("Independent emitter traces"), State.BeamTraces.Num(), 2)) { return false; }
    TestEqual(TEXT("First trace stops at shield"), State.BeamTraces[0].LastCell, FIntPoint(3, 2));
    TestTrue(TEXT("Shield endpoint is identified"), State.BeamTraces[0].bStoppedByBox);
    TestEqual(TEXT("Second trace reaches edge"), State.BeamTraces[1].LastCell, FIntPoint(5, 4));
    TestFalse(TEXT("Shield cell stays out of exposure mask"), State.Beam.Contains(FIntPoint(4, 2)));
    FRules::Apply(Session, {EAction::Move, {0, -1}});
    TestEqual(TEXT("Trace extends immediately when shield moves"), State.BeamTraces[0].LastCell, FIntPoint(6, 2));
    TestTrue(TEXT("Player exposure remains immediate"), State.Outcome == EOutcome::Dead);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridIrregularBoard, "Sokoban.Rules.IrregularBoard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridIrregularBoard::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Start = {1, 2};
    Level.Boxes.Add({TEXT("Box"), {2, 2}});
    Level.Enemies.Add({TEXT("Beam"), {4, 2}, {-1, 0}});
    Level.VoidCells.Add({3, 2});
    TestTrue(TEXT("A valid cutout is accepted"), FLevelGate::CheckLayout(Level).IsEmpty());
    FSession Session = FRules::Start(Level, {});
    if (!TestEqual(TEXT("One emitter trace"), Session.State.BeamTraces.Num(), 1)) { return false; }
    TestEqual(TEXT("A laser ends before the void"), Session.State.BeamTraces[0].LastCell, FIntPoint(4, 2));
    TestFalse(TEXT("The void stops the beam before the box"), Session.State.BeamTraces[0].bStoppedByBox);
    TestFalse(TEXT("A void blocks a push"), FRules::Apply(Session, {EAction::Move, {1, 0}}).bEffective);
    TestEqual(TEXT("Box stays on its original floor"), Session.State.Board.Boxes[0].Cell, FIntPoint(2, 2));
    Level.Boxes.Reset();
    Level.Start = {2, 2};
    Session = FRules::Start(Level, {});
    TestFalse(TEXT("A void blocks a walk"), FRules::Apply(Session, {EAction::Move, {1, 0}}).bEffective);
    Level.Goal = {3, 2};
    TestFalse(TEXT("A goal on a cutout is invalid"), FLevelGate::CheckLayout(Level).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridDiagonalBlast, "Sokoban.Rules.DiagonalBlastCorner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridDiagonalBlast::RunTest(const FString&)
{
    using namespace Sokoban;
    enum class ESide { Floor, Wall, Void };
    const struct FCorner
    {
        const TCHAR* Name;
        ESide East;
        ESide South;
        bool bBlocked;
    } Corners[] = {
        {TEXT("Two walls"), ESide::Wall, ESide::Wall, true},
        {TEXT("One wall"), ESide::Wall, ESide::Floor, false},
        {TEXT("Wall and void"), ESide::Wall, ESide::Void, true},
        {TEXT("Void and wall"), ESide::Void, ESide::Wall, true},
        {TEXT("Two voids"), ESide::Void, ESide::Void, true},
        {TEXT("One void"), ESide::Floor, ESide::Void, false}
    };
    for (const auto& Corner : Corners)
    {
        FLevel Level = Room();
        Level.Boxes.Add_GetRef(Sokoban::FBox{TEXT("Bomb"), {2, 2}}).bExplodes = true;
        const FIntPoint Center(3, 2);
        const FIntPoint Target(4, 3);
        const auto AddSide = [&](FIntPoint Cell, ESide Side) {
            if (Side == ESide::Wall) { Level.Walls.Add(Cell); }
            if (Side == ESide::Void) { Level.VoidCells.Add(Cell); }
        };
        AddSide(Center + FIntPoint(1, 0), Corner.East);
        AddSide(Center + FIntPoint(0, 1), Corner.South);
        Level.Enemies.Add({TEXT("Guard"), Target});
        const FAction Push{EAction::Move, {1, 0}};
        auto Session = FRules::Start(Level, {});
        const auto Turn = FRules::Apply(Session, Push);
        const auto IsBlastAtCenter = [Center](const Sokoban::FEvent& Event) {
            return Event.Kind == EEvent::Blast && Event.Cell == Center;
        };
        TestTrue(FString::Printf(TEXT("%s: source detonates"), Corner.Name),
            Turn.Events.ContainsByPredicate(IsBlastAtCenter));
        TestEqual(FString::Printf(TEXT("%s: diagonal enemy survives when blocked"), Corner.Name),
            !Session.State.Board.Enemies.IsEmpty(), Corner.bBlocked);

        Level.Enemies.Reset();
        Level.Boxes.Add_GetRef(Sokoban::FBox{TEXT("DiagonalBomb"), Target}).bExplodes = true;
        Session = FRules::Start(Level, {});
        const auto ChainTurn = FRules::Apply(Session, Push);
        const auto* Wave = ChainTurn.Events.FindByPredicate(IsBlastAtCenter);
        TestTrue(FString::Printf(TEXT("%s: chain uses the same corner rule"), Corner.Name),
            Wave && Wave->BlastCenters.Contains(Target) == !Corner.bBlocked);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridBlastAgainstRubble, "Sokoban.Rules.BlastAgainstRubble",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridBlastAgainstRubble::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Start = {1, 2};
    Level.Boxes.Add({TEXT("Bomb"), {2, 2}});
    Level.Boxes[0].bExplodes = true;
    Level.Rubble.Add({3, 2});
    Level.Enemies.Add({TEXT("BehindWall"), {4, 2}});
    const FAction Push{EAction::Move, {1, 0}};
    FSession Session = FRules::Start(Level, {0, 1});

    const auto BlastTurn = FRules::Apply(Session, Push);
    TestTrue(TEXT("Blast makes the blocked push effective"), BlastTurn.bEffective);
    TestEqual(TEXT("Actor stays on the original cell"), Session.State.Player, FIntPoint(1, 2));
    TestTrue(TEXT("Bomb is consumed"), Session.State.Board.Boxes.IsEmpty());
    TestTrue(TEXT("Rubble is removed"), Session.State.Board.Rubble.IsEmpty());
    TestEqual(TEXT("Same blast does not travel through rubble"), Session.State.Board.Enemies.Num(), 1);
    TestTrue(TEXT("The explosion is emitted at the original crate cell"), BlastTurn.Events.ContainsByPredicate(
        [](const Sokoban::FEvent& Event) { return Event.Kind == EEvent::Blast && Event.Cell == FIntPoint(2, 2); }));

    TestTrue(TEXT("Player can move into the cleared route"), FRules::Apply(Session, Push).bEffective);
    FRules::Apply(Session, {EAction::Undo, FIntPoint::ZeroValue, 2});
    TestEqual(TEXT("Undo restores crate"), Session.State.Board.Boxes.Num(), 1);
    TestTrue(TEXT("Undo restores rubble"), Session.State.Board.Rubble.Contains(FIntPoint(3, 2)));

    FLevel SolidWall = Level;
    SolidWall.Rubble.Reset();
    SolidWall.Walls.Add({3, 2});
    Session = FRules::Start(SolidWall, {});
    TestFalse(TEXT("Solid wall does not trigger contact blast"), FRules::Apply(Session, Push).bEffective);
    TestEqual(TEXT("Bomb remains before solid wall"), Session.State.Board.Boxes.Num(), 1);

    FLevel SpikedBomb = SolidWall;
    SpikedBomb.Walls.Reset();
    SpikedBomb.Walls.Add({4, 2});
    SpikedBomb.Enemies.Reset();
    SpikedBomb.Boxes[0].Spike = {1, 0};
    Session = FRules::Start(SpikedBomb, {});
    TestFalse(TEXT("A bomb does not detonate against its own spike"), FRules::Apply(Session, Push).bEffective);

    FLevel SpikeBlock = Level;
    SpikeBlock.Rubble.Reset();
    SpikeBlock.Enemies.Reset();
    SpikeBlock.Boxes.Add({TEXT("SpikeBox"), {4, 2}, {-1, 0}});
    TestTrue(TEXT("A spike tip is a valid static blocker"), FLevelGate::CheckLayout(SpikeBlock).IsEmpty());
    Session = FRules::Start(SpikeBlock, {});
    const auto SpikeContact = FRules::Apply(Session, Push);
    TestTrue(TEXT("A bomb directly blocked by a spike tip detonates"), SpikeContact.bEffective &&
        SpikeContact.Events.ContainsByPredicate([](const Sokoban::FEvent& Event) { return Event.Kind == EEvent::Blast; }));
    TestEqual(TEXT("Bomb detonates in place"), Session.State.Player, SpikeBlock.Start);
    TestTrue(TEXT("Ordinary spike box survives the blast"), Session.State.Board.Boxes.Num() == 1 &&
        Session.State.Board.Boxes[0].Id == FName(TEXT("SpikeBox")));
    SpikeBlock.Boxes[1].Cell = {3, 2};
    SpikeBlock.Boxes[1].Spike = {1, 0};
    Session = FRules::Start(SpikeBlock, {});
    TestFalse(TEXT("The body of a spike box is still an ordinary blocker"), FRules::Apply(Session, Push).bEffective);

    Level.Boxes[0].bExplodes = false;
    Session = FRules::Start(Level, {});
    TestFalse(TEXT("Ordinary crate cannot break rubble"), FRules::Apply(Session, Push).bEffective);
    TestTrue(TEXT("Ordinary push leaves rubble intact"), Session.State.Board.Rubble.Contains(FIntPoint(3, 2)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridBlastAgainstEnemy, "Sokoban.Rules.BlastAgainstEnemy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridBlastAgainstEnemy::RunTest(const FString&)
{
    using namespace Sokoban;
    const FAction Push{EAction::Move, {1, 0}};
    FLevel Level = Room();
    Level.Start = {1, 2};
    Level.Boxes.Add({TEXT("Bomb"), {2, 2}});
    Level.Boxes[0].bExplodes = true;
    Level.Enemies.Add({TEXT("Emitter"), {3, 2}, {-1, 0}});

    FSession Session = FRules::Start(Level, {0, 1});
    const auto Turn = FRules::Apply(Session, Push);
    const auto* Wave = Turn.Events.FindByPredicate([](const Sokoban::FEvent& Event) { return Event.Kind == EEvent::Blast; });
    TestTrue(TEXT("An adjacent enemy triggers the blast at the blocked bomb's original cell"),
        Turn.bEffective && Wave && Wave->Cell == FIntPoint(2, 2));
    TestEqual(TEXT("The player stays on the original cell"), Session.State.Player, Level.Start);
    TestEqual(TEXT("The blocked blast spends one move"), Session.State.Turn, 1);
    TestTrue(TEXT("Removing the emitter also removes its beam"), Session.State.Board.Boxes.IsEmpty() &&
        Session.State.Board.Enemies.IsEmpty() && Session.State.Beam.IsEmpty() &&
        Session.State.Outcome == EOutcome::Running);
    FRules::Apply(Session, {EAction::Undo, FIntPoint::ZeroValue, 1});
    TestTrue(TEXT("Undo restores the bomb and emitter and spends the pill"),
        Session.State.Board.Boxes.Num() == 1 && Session.State.Board.Enemies.Num() == 1 &&
        Session.State.Inventory.Undo == 0);

    Level.Enemies[0].BeamDirection = FIntPoint::ZeroValue;
    Level.Enemies.Add({TEXT("DistantEmitter"), {4, 2}, {-1, 0}});
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestTrue(TEXT("An emitter beyond the blast radius survives and can kill once the bomb is gone"),
        Session.State.Board.Enemies.Num() == 1 && Session.State.Board.Enemies[0].Id == FName(TEXT("DistantEmitter")) &&
        Session.State.Outcome == EOutcome::Dead);

    Level.Enemies.Reset();
    FLevel Blocked = Level;
    Blocked.Goal = {3, 2};
    Session = FRules::Start(Blocked, {});
    TestFalse(TEXT("A goal stops a bomb without igniting it"), FRules::Apply(Session, Push).bEffective);
    Blocked.Goal = Level.Goal;
    Blocked.VoidCells.Add({3, 2});
    Session = FRules::Start(Blocked, {});
    TestFalse(TEXT("A void stops a bomb without igniting it"), FRules::Apply(Session, Push).bEffective);
    Blocked.VoidCells.Reset();
    Blocked.Size.X = 3;
    Blocked.Goal = {2, 4};
    Session = FRules::Start(Blocked, {});
    TestFalse(TEXT("The board edge stops a bomb without igniting it"), FRules::Apply(Session, Push).bEffective);
    Level.Boxes.Add({TEXT("Crate"), {3, 2}});
    Level.Enemies.Add({TEXT("GuardBehindChain"), {4, 2}});
    Session = FRules::Start(Level, {1, 0});
    FRules::Apply(Session, {EAction::ArmStrength});
    TestFalse(TEXT("An enemy blocking a strength chain does not detonate the bomb"),
        FRules::Apply(Session, Push).bEffective);
    TestTrue(TEXT("The failed chain keeps both boxes and the enemy"),
        Session.State.Board.Boxes.Num() == 2 && Session.State.Board.Enemies.Num() == 1);

    // Reproduce the route from the start of DA_CampaignFinale to its bomb and enemy columns.
    FLevel Finale = Room();
    Finale.Size = {10, 10};
    Finale.Goal = {8, 7};
    Finale.Boxes.Add({TEXT("EntranceBomb"), {2, 2}});
    Finale.Boxes.Last().bExplodes = true;
    for (int32 Y = 1; Y <= 8; ++Y)
    {
        Finale.Boxes.Add({FName(TEXT("Bomb"), Y), {5, Y}});
        Finale.Boxes.Last().bExplodes = true;
        Finale.Enemies.Add({FName(TEXT("Guard"), Y), {6, Y}});
    }
    const FIntPoint NormalCrates[] = {{2, 1}, {2, 3}, {1, 3}, {7, 6}, {7, 7}, {7, 8}, {8, 8}};
    for (const FIntPoint Cell : NormalCrates)
    {
        Finale.Boxes.Add({FName(TEXT("Crate"), Finale.Boxes.Num()), Cell});
    }
    Session = FRules::Start(Finale, {});
    for (int32 Step = 0; Step < 3; ++Step) { FRules::Apply(Session, Push); }
    TestEqual(TEXT("First three pushes reach the finale's bomb column"), Session.State.Player, FIntPoint(4, 2));
    const auto Chain = FRules::Apply(Session, Push);
    const auto Blasts = Chain.Events.FilterByPredicate([](const Sokoban::FEvent& Event) { return Event.Kind == EEvent::Blast; });
    TestTrue(TEXT("The finale's eight bombs ignite exactly once in a single wave"),
        Blasts.Num() == 1 && Blasts[0].BlastCenters.Num() == 8);
    TestTrue(TEXT("The finale's eight adjacent enemies die and the normal boxes remain"),
        Session.State.Board.Enemies.IsEmpty() && Session.State.Board.Boxes.Num() == UE_ARRAY_COUNT(NormalCrates));
    TestEqual(TEXT("The player stays outside the finale wave"), Session.State.Player, FIntPoint(4, 2));
    TestEqual(TEXT("Explosion damage does not kill the player"), Session.State.Outcome, EOutcome::Running);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridStraightEject, "Sokoban.Rules.StraightEjectUntilBlocked",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridStraightEject::RunTest(const FString&)
{
    using namespace Sokoban;
    FLevel Level = Room();
    Level.Size = {11, 5};
    Level.Boxes.Add({TEXT("Eject"), {2, 2}});
    Level.Boxes[0].bEject = true;
    const FAction Push{EAction::Move, {1, 0}};

    FSession Session = FRules::Start(Level, {});
    const auto Eject = FRules::Apply(Session, Push);
    TestEqual(TEXT("The board edge stops a long eject"), Session.State.Board.Boxes[0].Cell, FIntPoint(10, 2));
    TestEqual(TEXT("The player follows only the first step"), Session.State.Player, FIntPoint(2, 2));
    TestEqual(TEXT("All sliding cells are reported"), Eject.Events.FilterByPredicate([](const Sokoban::FEvent& Event) {
        return Event.Kind == EEvent::BoxMoved;
    }).Num(), 8);

    Level.Boxes.Add({TEXT("Blocker"), {8, 2}});
    Level.Boxes[1].bEject = true;
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestTrue(TEXT("Stops before another box and does not chain eject"),
        Session.State.Board.Boxes[0].Cell == FIntPoint(7, 2) &&
        Session.State.Board.Boxes[1].Cell == FIntPoint(8, 2));

    Level.Boxes.Pop();
    Level.VoidCells.Add({8, 2});
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestEqual(TEXT("Cannot enter a void cell"), Session.State.Board.Boxes[0].Cell, FIntPoint(7, 2));
    Level.VoidCells.Reset();
    Level.Enemies.Add({TEXT("Enemy"), {8, 2}});
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestTrue(TEXT("Stops at a contact enemy without killing it"), Session.State.Board.Boxes[0].Cell == FIntPoint(7, 2) &&
        Session.State.Board.Enemies.Num() == 1);
    Level.Enemies.Reset();
    Level.Goal = {8, 2};
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestEqual(TEXT("Cannot slide onto the goal"), Session.State.Board.Boxes[0].Cell, FIntPoint(7, 2));

    Level.Goal = {6, 4};
    Level.Walls.Add({8, 2});
    Level.Boxes[0].bExplodes = true;
    Session = FRules::Start(Level, {});
    const auto Blast = FRules::Apply(Session, Push);
    TestTrue(TEXT("A successful eject explodes at its resting cell"), Blast.Events.ContainsByPredicate([](const Sokoban::FEvent& Event) {
        return Event.Kind == EEvent::Blast && Event.Cell == FIntPoint(7, 2);
    }));
    Level.Walls.Reset();
    Level.Enemies.Add({TEXT("GuardAtEjectEnd"), {8, 2}});
    Session = FRules::Start(Level, {});
    const auto EnemyAtEnd = FRules::Apply(Session, Push);
    TestTrue(TEXT("An eject that ends against an enemy blasts from the resting cell"),
        EnemyAtEnd.Events.ContainsByPredicate([](const Sokoban::FEvent& Event) {
            return Event.Kind == EEvent::Blast && Event.Cell == FIntPoint(7, 2);
        }) && Session.State.Board.Enemies.IsEmpty());
    Level.Enemies[0].Cell = {4, 2};
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestTrue(TEXT("An enemy blocking the first extra step still uses the start-blocked option"),
        Session.State.Board.Boxes.Num() == 1 && Session.State.Board.Enemies.Num() == 1);
    Level.Boxes[0].bExplodeWhenEjectBlocked = true;
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestTrue(TEXT("The start-blocked option can blast the enemy after the initial push"),
        Session.State.Board.Boxes.IsEmpty() && Session.State.Board.Enemies.IsEmpty());

    Level.Enemies.Reset();
    Level.Boxes[0].bExplodeWhenEjectBlocked = false;
    Level.Walls.Add({4, 2});
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestTrue(TEXT("A blocked first extra step requires the start-blocked option"), Session.State.Board.Boxes.Num() == 1);
    Level.Boxes[0].bExplodeWhenEjectBlocked = true;
    Session = FRules::Start(Level, {});
    FRules::Apply(Session, Push);
    TestTrue(TEXT("Start-blocked explosion can be enabled"), Session.State.Board.Boxes.IsEmpty());
    return true;
}

#endif



