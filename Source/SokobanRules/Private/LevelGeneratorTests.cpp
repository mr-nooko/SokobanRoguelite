#include "LevelGenerator.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridGeneratorTest, "Sokoban.Levels.SeededGenerator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridGeneratorTest::RunTest(const FString&)
{
    using namespace Sokoban;
    const FTemplateLevelGenerator Generator;
    FGenerationRequest Request;
    Request.Seed = 42;
    Request.Complexity = 2;
    Request.Search = {12, 10000, EDifficulty::High};
    Request.AllowedTemplates = {EGenerationTemplate::BeamCover};
    Request.bAllowFallback = false;
    const auto First = Generator.Generate(Request);
    const auto Again = Generator.Generate(Request);
    if (!TestTrue(TEXT("A zero-stock beam room passes both gates"), First.Level.IsSet() && Again.Level.IsSet()))
    {
        return false;
    }

    const auto& Room = First.Level.GetValue();
    const auto& Repeated = Again.Level.GetValue();
    TestTrue(TEXT("A fixed seed reproduces the board and attempt trace"),
        Room.RoomId == Repeated.RoomId && Room.Size == Repeated.Size &&
        Room.Start == Repeated.Start && Room.Goal == Repeated.Goal &&
        Room.Boxes[0].Cell == Repeated.Boxes[0].Cell &&
        Room.Enemies[0].BeamDirection == Repeated.Enemies[0].BeamDirection &&
        First.Attempts == Again.Attempts && First.Trace.Last().Seed == Again.Trace.Last().Seed);
    TestTrue(TEXT("A generated beam activates direction-sensitive rules"),
        FRules::IsCardinal(Room.Enemies[0].BeamDirection) && Room.bBeamStopsOnTerrain.IsSet());
    TestEqual(TEXT("A targeted witness can be found at the one-solution floor"), First.Validation.Actual.CountLowerBound, 1);

    for (const auto& Witness : First.Validation.Actual.Witnesses)
    {
        auto Session = FRules::Start(Room, {});
        bool bMovedCover = false;
        for (const auto& Action : Witness)
        {
            const auto Turn = FRules::Apply(Session, Action);
            TestTrue(TEXT("Each recorded witness command is accepted"), Turn.bAccepted);
            bMovedCover |= Turn.Events.ContainsByPredicate([](const Sokoban::FEvent& Event) { return Event.Kind == EEvent::BoxMoved; });
        }
        TestTrue(TEXT("Witnesses replay to victory from the actual inventory"), Session.State.Outcome == EOutcome::Won);
        TestTrue(TEXT("The selected beam route cannot win without moving its cover"), bMovedCover);
    }

    for (const auto Kind : {EGenerationTemplate::SpikeSweep, EGenerationTemplate::BlastRubble})
    {
        Request.AllowedTemplates = {Kind};
        const auto ModuleRoom = Generator.Generate(Request);
        TestTrue(TEXT("The fixed-seed spike and blast templates are solvable without items"),
            ModuleRoom.Level.IsSet() && ModuleRoom.Status == ECheck::Pass);
    }
    Request.AllowedTemplates = {EGenerationTemplate::Detour};
    Request.Search.TargetDifficulty = EDifficulty::Medium;
    const auto Medium = Generator.Generate(Request);
    TestTrue(TEXT("Medium stops when four solutions have been proven"),
        Medium.Status == ECheck::Pass && Medium.Validation.Actual.CountLowerBound == 4);

    FGenerationRequest Variety;
    Variety.Complexity = 3;
    Variety.Search = {12, 10000, EDifficulty::High};
    TSet<FName> Shapes;
    int32 Passed = 0;
    int32 Fallbacks = 0;
    int32 AcceptedAtTarget = 0;
    bool bVerifiedBreach = false;
    bool bVerifiedSlide = false;
    for (int32 Seed = 12; Seed < 36; ++Seed)
    {
        Variety.Seed = Seed;
        const auto Sample = Generator.Generate(Variety);
        if (!Sample.Level.IsSet()) { continue; }
        ++Passed;
        if (Sample.bFallback) { ++Fallbacks; }
        const auto Trace = Sample.Trace.Last();
        Shapes.Add(Trace.Template);
        if (Trace.Complexity == Variety.Complexity) { ++AcceptedAtTarget; }
        if (Trace.Template == FName(TEXT("DoubleBreach")) && !bVerifiedBreach)
        {
            auto Run = FRules::Start(Sample.Level.GetValue(), {});
            int32 Blasts = 0;
            for (const auto& Action : Sample.Validation.Actual.Witnesses[0])
            {
                const auto Turn = FRules::Apply(Run, Action);
                for (const auto& Event : Turn.Events) { if (Event.Kind == EEvent::Blast) { ++Blasts; } }
            }
            TestTrue(TEXT("The two-gate solution actually breaks both barriers"),
                Run.State.Outcome == EOutcome::Won && Blasts == 2 && Run.State.Board.Rubble.IsEmpty());
            bVerifiedBreach = true;
        }
        if (Trace.Template == FName(TEXT("SlideAndBreach")) && !bVerifiedSlide)
        {
            auto Run = FRules::Start(Sample.Level.GetValue(), {});
            const auto Jelly = Run.State.Board.Boxes.FindByPredicate([](const Sokoban::FBox& Box) { return Box.bEject; });
            const FName JellyId = Jelly ? Jelly->Id : NAME_None;
            bool bEjectedIntoBlast = false;
            for (const auto& Action : Sample.Validation.Actual.Witnesses[0])
            {
                const auto Turn = FRules::Apply(Run, Action);
                int32 Moves = 0;
                bool bBlast = false;
                for (const auto& Event : Turn.Events)
                {
                    if (Event.Kind == EEvent::BoxMoved && Event.Entity == JellyId) { ++Moves; }
                    if (Event.Kind == EEvent::Blast) { bBlast = true; }
                }
                bEjectedIntoBlast |= Moves > 1 && bBlast;
            }
            TestTrue(TEXT("Slide and breach uses a multi-cell ejection to trigger the explosion"),
                Run.State.Outcome == EOutcome::Won && bEjectedIntoBlast && Run.State.Board.Rubble.IsEmpty());
            bVerifiedSlide = true;
        }
    }
    AddInfo(FString::Printf(TEXT("Mixed complexity 3, empty inventory: seeds 12-35, passed=%d, fallback=%d, requested complexity=%d, distinct templates=%d"),
        Passed, Fallbacks, AcceptedAtTarget, Shapes.Num()));
    TestTrue(TEXT("Zero-stock high complexity produces multiple verified structures with limited fallback"),
        Passed == 24 && AcceptedAtTarget >= 20 && Fallbacks <= 2 && Shapes.Num() >= 3 &&
        bVerifiedBreach && bVerifiedSlide);

    Variety.Seed = 42;
    Variety.AllowedTemplates = {EGenerationTemplate::BeamCover};
    Variety.Complexity = 2;
    const auto OriginalBeam = Generator.Generate(Variety);
    if (TestTrue(TEXT("Initial beam shape qualifies"), OriginalBeam.Status == ECheck::Pass))
    {
        Variety.RecentTemplates = {OriginalBeam.Trace.Last().Template};
        const auto NewBeam = Generator.Generate(Variety);
        TestTrue(TEXT("Recently used structure is skipped when another qualifies"),
            NewBeam.Status == ECheck::Pass && NewBeam.Trace.Last().Template != OriginalBeam.Trace.Last().Template);
    }

    Request.Search.TargetDifficulty = EDifficulty::High;
    Request.Resources = {true, false, {1, 0}, {}};
    Request.AllowedTemplates = {EGenerationTemplate::StrengthChain};
    Request.MaxAttempts = 1;
    const auto Spent = Generator.Generate(Request);
    TestTrue(TEXT("Capability alone cannot admit a level requiring a spent pill"),
        !Spent.Level.IsSet() && Spent.Status == ECheck::Fail &&
        Spent.Validation.Capability.MeetsMinimum == ECheck::Pass && Spent.Validation.Actual.Solvable == ECheck::Fail);

    auto Invalid = First.Level.GetValue();
    Invalid.Goal = {-1, -1};
    Request.AuthoredTemplates = {Invalid};
    Request.Complexity = 3;
    Request.MaxAttempts = 3;
    Request.bAllowFallback = true;
    const auto Fallback = Generator.Generate(Request);
    if (!TestTrue(TEXT("Three rejected layouts lead to a validated fallback"),
        Fallback.Level.IsSet() && Fallback.bFallback && Fallback.Trace.Num() == 4))
    {
        return false;
    }
    TestTrue(TEXT("Layout rejection and decreasing complexity are recorded"),
        !Fallback.Trace[0].LayoutErrors.IsEmpty() && Fallback.Trace[0].Complexity == 3 &&
        Fallback.Trace[1].Complexity == 2 && Fallback.Trace[2].Complexity == 1 && Fallback.Trace[3].bFallback);

    Request.Search.NodeBudget = 0;
    const auto Exhausted = Generator.Generate(Request);
    TestTrue(TEXT("Fallback is revalidated using the request budget"),
        !Exhausted.Level.IsSet() && Exhausted.Status == ECheck::Unknown &&
        Exhausted.Trace.Last().bFallback && Exhausted.Trace.Last().Status == ECheck::Unknown);
    Request.FallbackLevel = Invalid;
    const auto InvalidFallback = Generator.Generate(Request);
    TestTrue(TEXT("An invalid custom fallback cannot be returned"),
        !InvalidFallback.Level.IsSet() && InvalidFallback.Status == ECheck::Fail &&
        !InvalidFallback.Trace.Last().LayoutErrors.IsEmpty());
    for (const auto Target : {EDifficulty::High, EDifficulty::Medium})
    {
        FGenerationRequest Exact;
        Exact.MaxAttempts = 0;
        Exact.Search = {Target == EDifficulty::High ? 6 : 7, 10000, Target};
        Exact.Search.bRequireDifficultyBand = true;
        for (int32 Strength = 0; Strength <= 1; ++Strength)
        {
            for (int32 Undo = 0; Undo <= 2; ++Undo)
            {
                Exact.Resources = {true, true, {1, 2}, {Strength, Undo}};
                const auto ExactFallback = Generator.Generate(Exact);
                TestTrue(FString::Printf(TEXT("Fallback is verified in band %d with strength %d and undo %d"),
                    static_cast<int32>(Target), Strength, Undo),
                    ExactFallback.Status == ECheck::Pass && ExactFallback.bFallback &&
                    ExactFallback.Validation.Actual.Difficulty == Target);
            }
        }
    }
    FGenerationRequest MediumAtProjectBudget;
    MediumAtProjectBudget.MaxAttempts = 0;
    MediumAtProjectBudget.Resources = {true, true, {1, 2}, {1, 2}};
    MediumAtProjectBudget.Search = {16, 10000, EDifficulty::Medium};
    MediumAtProjectBudget.Search.bRequireDifficultyBand = true;
    const auto MediumFallback = Generator.Generate(MediumAtProjectBudget);
    TestTrue(TEXT("Medium fallback passes at the project step limit with the starting inventory"),
        MediumFallback.Status == ECheck::Pass && MediumFallback.bFallback &&
        MediumFallback.Validation.Actual.Difficulty == EDifficulty::Medium &&
        MediumFallback.Validation.Actual.CountLowerBound == 4);
    return true;
}
#endif
