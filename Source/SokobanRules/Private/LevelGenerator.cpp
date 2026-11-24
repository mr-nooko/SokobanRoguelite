#include "LevelGenerator.h"
#include "Math/RandomStream.h"

namespace Sokoban
{
namespace
{
struct FTemplate
{
    EGenerationTemplate Kind;
    const TCHAR* Name;
    int32 Complexity;
    const TCHAR* Layout;
};

constexpr FTemplate Templates[] = {
    {EGenerationTemplate::Detour, TEXT("Courtyard"), 0, TEXT("XX....X\nX..#..X\n.S.#.G.\n...#...\nX.....X")},
    {EGenerationTemplate::Detour, TEXT("S_Bend"), 0, TEXT("S..#...\n.#.#.#.\n.....G.\nXX...XX")},
    {EGenerationTemplate::Detour, TEXT("ZigzagArcade"), 0, TEXT("XX...XX\nX..#..X\nXS##.GX\nX....XX\nXX...XX")},
    {EGenerationTemplate::BlastRubble, TEXT("Breach"), 1, TEXT("...X...\n...#...\n.SBr.G.\n...#...\nXX.X..X")},
    {EGenerationTemplate::EjectCorridor, TEXT("SlideLane"), 1, TEXT("XXXXX..\n...X.GG\n.SJ....\n...X...\nXX.X..X")},
    {EGenerationTemplate::SpikeSweep, TEXT("GuardedCorner"), 2, TEXT("XXX...XX\nX..e.G.X\nXS^..XXX\nX..XXXXX")},
    {EGenerationTemplate::SpikeSweep, TEXT("GuardedBridge"), 2, TEXT("..X....X\n...e.G..\n.S^..XXX\n...XXXXX\n..XXXXXX")},
    {EGenerationTemplate::BeamCover, TEXT("BeamCover"), 2, TEXT("..X..G.\n..#....\n...C..<\n.S#....\nXXX....")},
    {EGenerationTemplate::BeamCover, TEXT("SplitCrossing"), 2, TEXT("..X.G...\n..X.....\n.C....<.\n.S.X....\nX..X..XX")},
    {EGenerationTemplate::BeamCover, TEXT("MaintenanceCrossing"), 2, TEXT("XXXVXXXX\n......G.\n...C....\n.S..#...\nXXX.XXXX")},
    {EGenerationTemplate::BlastRubble, TEXT("SideBreach"), 2, TEXT("XXXX...\nXX...G.\n##r####\n..B..XX\n...S.XX")},
    {EGenerationTemplate::BlastRubble, TEXT("DoubleBreach"), 3, TEXT("aa.X..X.bb\n...#..#...\n.SBr.Br.G.\n...#..#...\nXX.X..X..X")},
    {EGenerationTemplate::BlastRubble, TEXT("CornerFuse"), 3, TEXT("XXa..#.\nXX..BrG\n####r##\n..#.B#X\nbS...#X")},
    {EGenerationTemplate::EjectCorridor, TEXT("SlideAndBreach"), 3, TEXT("a.XX..X...\n...#..#...\n.SK...r..G\n...#..#...\nXX.X..X.bb")},
    {EGenerationTemplate::EjectCorridor, TEXT("FoundryTurn"), 3, TEXT("XXaa..XXXX\nX..#..XXXX\n.SK...r...\nXX.X..X..X\nXXXXXX..GX")},
    {EGenerationTemplate::StrengthChain, TEXT("StrengthChain"), 3, TEXT("#......\nSCC...G\n#......")}
};

constexpr int32 MaxComplexity = 3;
constexpr int32 MaxAttempts = 64;

FLevel MakeBuiltIn(const FTemplate& Template, FRandomStream& Random)
{
    FLevel Level;
    Level.RoomId = Template.Name;
    TArray<FString> Rows;
    FString(Template.Layout).ParseIntoArrayLines(Rows);
    Level.Size = {Rows[0].Len(), Rows.Num()};
    TArray<FIntPoint> Ends;
    const TCHAR ClosedWing = Random.RandRange(0, 1) == 0 ? 'a' : 'b';
    for (int32 Y = 0; Y < Rows.Num(); ++Y)
    {
        check(Rows[Y].Len() == Level.Size.X);
        for (int32 X = 0; X < Level.Size.X; ++X)
        {
            const FIntPoint Cell(X, Y);
            const TCHAR Tile = Rows[Y][X];
            switch (Tile)
            {
                case 'S': Level.Start = Cell; break;
                case 'G': Ends.Add(Cell); break;
                case 'X': Level.VoidCells.Add(Cell); break;
                case 'a':
                case 'b':
                    if (Tile == ClosedWing) { Level.VoidCells.Add(Cell); }
                    break;
                case '#': Level.Walls.Add(Cell); break;
                case 'r': Level.Rubble.Add(Cell); break;
                case '<':
                case 'V':
                case 'e':
                {
                    const FIntPoint Beam = Tile == '<' ? FIntPoint(-1, 0)
                        : Tile == 'V' ? FIntPoint(0, 1) : FIntPoint::ZeroValue;
                    Level.Enemies.Add({FName(*FString::Printf(TEXT("Enemy%d"), Level.Enemies.Num() + 1)),
                        Cell, Beam});
                    if (Beam != FIntPoint::ZeroValue) { Level.bBeamStopsOnTerrain = true; }
                    break;
                }
                case '^':
                case 'B':
                case 'K':
                case 'C':
                case 'J':
                {
                    FBox Box{FName(*FString::Printf(TEXT("Box%d"), Level.Boxes.Num() + 1)), Cell};
                    if (Tile == '^') { Box.Spike = {0, -1}; }
                    Box.bExplodes = Tile == 'B' || Tile == 'K';
                    Box.bEject = Tile == 'J' || Tile == 'K';
                    Level.Boxes.Add(Box);
                    break;
                }
            }
        }
    }
    check(!Ends.IsEmpty());
    Level.Goal = Ends[Random.RandRange(0, Ends.Num() - 1)];
    return Level;
}

const FTemplate* ChooseTemplate(const FGenerationRequest& Request, int32 Complexity, const TSet<FName>& AttemptedTemplates, FRandomStream& Random)
{
    TArray<const FTemplate*, TInlineAllocator<UE_ARRAY_COUNT(Templates)>> Pool;
    int32 Closest = MAX_int32;
    const bool bCanUseStrength = Request.Resources.Filter(Request.Resources.ActualInventory).Strength > 0;
    for (const FTemplate& Template : Templates)
    {
        if (Request.AllowedTemplates.IsEmpty())
        {
            if (Template.Kind == EGenerationTemplate::StrengthChain && !bCanUseStrength) { continue; }
        }
        else if (!Request.AllowedTemplates.Contains(Template.Kind)) { continue; }

        const int32 Distance = FMath::Abs(Template.Complexity - Complexity);
        if (Distance > Closest) { continue; }
        if (Distance < Closest)
        {
            Pool.Reset();
            Closest = Distance;
        }
        Pool.Add(&Template);
    }
    if (Pool.IsEmpty()) { return nullptr; }
    decltype(Pool) Fresh = Pool;
    Fresh.RemoveAll([&](const FTemplate* Template) { return AttemptedTemplates.Contains(FName(Template->Name)); });
    auto Preferred = Fresh.IsEmpty() ? Pool : Fresh;
    Preferred.RemoveAll([&](const FTemplate* Template) { return Request.RecentTemplates.Contains(FName(Template->Name)); });
    const auto& Choices = !Preferred.IsEmpty() ? Preferred : !Fresh.IsEmpty() ? Fresh : Pool;
    return Choices[Random.RandRange(0, Choices.Num() - 1)];
}

void Orient(FLevel& Level, FRandomStream& Random)
{
    const bool bMirror = Random.RandRange(0, 1) != 0;
    const int32 Turns = Random.RandRange(0, 3);
    const FIntPoint SourceSize = Level.Size;

    const auto Transform = [&](FIntPoint Value, bool bDirection) {
        const int32 Edge = bDirection ? 0 : 1;
        FIntPoint Size = SourceSize;
        if (bMirror) { Value.X = Edge * (Size.X - 1) - Value.X; }
        for (int32 Turn = 0; Turn < Turns; ++Turn)
        {
            Value = {Edge * (Size.Y - 1) - Value.Y, Value.X};
            Swap(Size.X, Size.Y);
        }
        return Value;
    };
    const auto TransformCells = [&](TSet<FIntPoint>& Cells) {
        TSet<FIntPoint> Rotated;
        Rotated.Reserve(Cells.Num());
        for (FIntPoint Cell : Cells) { Rotated.Add(Transform(Cell, false)); }
        Cells = MoveTemp(Rotated);
    };
    if (Turns % 2 != 0) { Swap(Level.Size.X, Level.Size.Y); }
    Level.Start = Transform(Level.Start, false);
    Level.Goal = Transform(Level.Goal, false);
    TransformCells(Level.Walls);
    TransformCells(Level.Rubble);
    TransformCells(Level.VoidCells);
    for (auto& Box : Level.Boxes)
    {
        Box.Cell = Transform(Box.Cell, false);
        Box.Spike = Transform(Box.Spike, true);
    }
    for (auto& Enemy : Level.Enemies)
    {
        Enemy.Cell = Transform(Enemy.Cell, false);
        Enemy.BeamDirection = Transform(Enemy.BeamDirection, true);
    }
}

bool HasWalkingShortcut(const FLevel& Level)
{
    const auto Initial = FRules::Start(Level, {}).State;
    if (Initial.Outcome != EOutcome::Running) { return Initial.Outcome == EOutcome::Won; }
    TSet<FIntPoint> Blocked = Level.Walls;
    Blocked.Append(Level.Rubble);
    Blocked.Append(Initial.Beam);
    Blocked.Remove(Level.Goal);
    for (const FBox& Box : Level.Boxes)
    {
        Blocked.Add(Box.Cell);
        if (Box.Spike != FIntPoint::ZeroValue) { Blocked.Add(Box.Cell + Box.Spike); }
    }
    for (const FEnemy& Enemy : Level.Enemies) { Blocked.Add(Enemy.Cell); }

    TArray<FIntPoint> Queue{Level.Start};
    TSet<FIntPoint> Seen{Level.Start};
    const FIntPoint Directions[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const FIntPoint Cell = Queue[Index];
        if (Cell == Level.Goal) { return true; }
        for (FIntPoint Delta : Directions)
        {
            const FIntPoint Next = Cell + Delta;
            if (FRules::IsInside(Level, Next) && !Blocked.Contains(Next) && !Seen.Contains(Next))
            {
                Seen.Add(Next);
                Queue.Add(Next);
            }
        }
    }
    return false;
}

bool WitnessUsesMechanic(EGenerationTemplate Kind, const FLevel& Level, FInventory Inventory, const TArray<FAction>& Witness)
{
    auto Session = FRules::Start(Level, Inventory);
    TSet<FName> Ejectors;
    for (const auto& Box : Level.Boxes) { if (Box.bEject) { Ejectors.Add(Box.Id); } }

    bool bUsed = false;
    for (const auto& Action : Witness)
    {
        const auto Turn = FRules::Apply(Session, Action);
        TSet<FName> MovedBoxes;
        TSet<FName> MovedEjectors;
        for (const auto& Event : Turn.Events)
        {
            if (Event.Kind == EEvent::BoxMoved)
            {
                MovedBoxes.Add(Event.Entity);
                if (Ejectors.Contains(Event.Entity))
                {
                    bUsed |= Kind == EGenerationTemplate::EjectCorridor && MovedEjectors.Contains(Event.Entity);
                    MovedEjectors.Add(Event.Entity);
                }
            }
            bUsed |= Kind == EGenerationTemplate::SpikeSweep && Event.Kind == EEvent::EnemyKilled;
        }
        bUsed |= Kind == EGenerationTemplate::BeamCover && !MovedBoxes.IsEmpty();
        bUsed |= Kind == EGenerationTemplate::StrengthChain && MovedBoxes.Num() > 1;
    }
    bUsed |= Kind == EGenerationTemplate::BlastRubble && Session.State.Board.Rubble.Num() < Level.Rubble.Num();
    return Session.State.Outcome == EOutcome::Won && bUsed;
}

constexpr FTemplate FallbackLoop{EGenerationTemplate::Detour, TEXT("FallbackLoop"), 0,
    TEXT("..#..\n.S#G.\n.....\nX...X")};
constexpr FTemplate HighFallbacks[] = {
    {EGenerationTemplate::Detour, TEXT("FallbackTurn"), 0, TEXT("XXSGX\nXXXXX")},
    {EGenerationTemplate::Detour, TEXT("FallbackLanes"), 0, TEXT("#..G#\n.....\n.#.#.\n.....\n#..S#")}
};
constexpr FTemplate MediumFallbacks[] = {
    {EGenerationTemplate::Detour, TEXT("FallbackRamp"), 0, TEXT("S.....G")},
    {EGenerationTemplate::Detour, TEXT("FallbackCourtyard"), 0, TEXT("#..G#\n.....\n.#.#.\n.....\n#..S#")}
};
}

FGenerationResult FTemplateLevelGenerator::Generate(const FGenerationRequest& Request) const
{
    FGenerationResult Result;
    Result.Seed = Request.Seed;
    FRandomStream Sequence(Request.Seed);
    FBoundedLevelSolver Solver;
    TOptional<FValidation> FirstUnknown;
    TSet<FName> AttemptedTemplates;
    const auto TryCandidate = [&](FLevel Level, int32 Seed, int32 Complexity, bool bFallback, const FTemplate* BuiltIn = nullptr)
    {
        const FName Template = Level.RoomId;
        if (!Level.RoomId.IsNone())
        {
            Level.RoomId = FName(*FString::Printf(TEXT("Generated_%08X_%02d_%s"),
                static_cast<uint32>(Request.Seed), Result.Trace.Num() + 1, *Template.ToString()));
        }
        auto Limits = Request.Search;
        const auto ActualInventory = Request.Resources.Filter(Request.Resources.ActualInventory);
        const bool bNeedsMechanic = BuiltIn && BuiltIn->Kind != EGenerationTemplate::Detour;
        const bool bShortcut = bNeedsMechanic && HasWalkingShortcut(Level);
        if (bNeedsMechanic && !bShortcut)
        {
            Limits.RequiredWitness = [&](const TArray<FAction>& Witness) {
                return WitnessUsesMechanic(BuiltIn->Kind, Level, ActualInventory, Witness);
            };
        }
        Result.Validation = FLevelGate::Validate(Level, Request.Resources, Limits, Solver);
        const auto& Report = Result.Validation;
        FString DesignIssue;
        ECheck Status = Report.Status;
        if (Status == ECheck::Pass && bShortcut)
        {
            DesignIssue = TEXT("起点到终点存在完全绕过机关的纯走路路线。");
            Status = ECheck::Fail;
        }
        else if (Status == ECheck::Pass && bNeedsMechanic && !Report.Actual.bHasRequiredWitness)
        {
            DesignIssue = Report.Actual.bSearchExhausted ? TEXT("完整搜索未找到展示主机关的通关解法。")
                : TEXT("搜索预算内尚未找到展示主机关的通关解法。");
            Status = Report.Actual.bSearchExhausted ? ECheck::Fail : ECheck::Unknown;
        }
        Result.Trace.Add({Seed, Complexity, Template, Status, Report.Capability.CountLowerBound,
            Report.Actual.CountLowerBound, bFallback, Report.Errors, MoveTemp(DesignIssue),
            BuiltIn ? TOptional<int32>(BuiltIn->Complexity) : TOptional<int32>()});
        Result.Attempts = Result.Trace.Num();
        if (Status == ECheck::Pass)
        {
            Result.Status = ECheck::Pass;
            Result.Level = MoveTemp(Level);
            Result.bFallback = bFallback;
            return true;
        }
        if (Status == ECheck::Unknown && !FirstUnknown.IsSet()) { FirstUnknown = Report; }
        return false;
    };

    const int32 AttemptLimit = FMath::Clamp(Request.MaxAttempts, 0, MaxAttempts);
    const int32 InitialComplexity = FMath::Clamp(Request.Complexity, 0, MaxComplexity);
    for (int32 Attempt = 0; Attempt < AttemptLimit; ++Attempt)
    {
        const int32 Seed = static_cast<int32>(Sequence.GetUnsignedInt());
        FRandomStream Random(Seed);
        const int32 Complexity = FMath::Max(0, InitialComplexity - Attempt / (Request.AuthoredTemplates.IsEmpty() ? 2 : 1));
        FLevel Level;
        const FTemplate* BuiltIn = nullptr;
        if (!Request.AuthoredTemplates.IsEmpty())
        {
            const int32 Index = Random.RandRange(0, Request.AuthoredTemplates.Num() - 1);
            Level = Request.AuthoredTemplates[Index];
        }
        else
        {
            BuiltIn = ChooseTemplate(Request, Complexity, AttemptedTemplates, Random);
            if (!BuiltIn) { break; }
            Level = MakeBuiltIn(*BuiltIn, Random);
            AttemptedTemplates.Add(Level.RoomId);
        }
        // Validate external data before transforming coordinates supplied by a caller.
        if (Request.AuthoredTemplates.IsEmpty() || FLevelGate::CheckLayout(Level).IsEmpty()) { Orient(Level, Random); }
        if (TryCandidate(MoveTemp(Level), Seed, Complexity, false, BuiltIn)) { return Result; }
    }

    if (Request.bAllowFallback)
    {
        if (Request.FallbackLevel.IsSet())
        {
            const int32 Seed = static_cast<int32>(Sequence.GetUnsignedInt());
            if (TryCandidate(Request.FallbackLevel.GetValue(), Seed, 0, true)) { return Result; }
        }
        else
        {
            const auto TryBuiltInFallback = [&](const FTemplate& Candidate)
            {
                const int32 Seed = static_cast<int32>(Sequence.GetUnsignedInt());
                FRandomStream Random(Seed);
                return TryCandidate(MakeBuiltIn(Candidate, Random), Seed, 0, true, &Candidate);
            };
            const int32 Overflow = Request.Search.DifficultyOverflow();
            if (Overflow > 0)
            {
                const TArrayView<const FTemplate> Options = Request.Search.TargetDifficulty == EDifficulty::High
                    ? MakeArrayView(HighFallbacks) : MakeArrayView(MediumFallbacks);
                for (const auto& Candidate : Options)
                {
                    if (TryBuiltInFallback(Candidate)) { return Result; }
                }
                const bool bPushChain = Request.Resources.Filter(Request.Resources.ActualInventory).Strength == 1;
                const int32 Width = FMath::Max(0, Request.Search.MaxActions) - (bPushChain ? 1 : 0);
                if (Request.Search.TargetDifficulty == EDifficulty::Medium &&
                    Width >= FSearchLimits::MediumStart + (bPushChain ? 1 : 0) &&
                    FLevelGate::SupportsBoardSize({Width, bPushChain ? 3 : 2}))
                {
                    // With one pill, the entrance uses it for a mandatory chain push.
                    // The final two rows have four shortest routes and no room for detours.
                    const FString Entrance = bPushChain
                        ? FString(TEXT("SCC.")) + FString::ChrN(Width - 4, 'X') + TEXT("\nX")
                        : FString(TEXT("S"));
                    const FString Layout = Entrance + FString::ChrN(Width - 1, '.') + TEXT("\n") +
                        FString::ChrN(Width - FSearchLimits::MediumStart, 'X') +
                        FString::ChrN(FSearchLimits::MediumStart - 1, '.') + TEXT("G");
                    const FTemplate Candidate{EGenerationTemplate::Detour, TEXT("FallbackFourPaths"), 0, *Layout};
                    if (TryBuiltInFallback(Candidate)) { return Result; }
                }
            }
            if (TryBuiltInFallback(FallbackLoop)) { return Result; }
        }
    }
    Result.Status = FirstUnknown.IsSet() || Result.Trace.IsEmpty() ? ECheck::Unknown : ECheck::Fail;
    if (FirstUnknown.IsSet()) { Result.Validation = MoveTemp(FirstUnknown.GetValue()); }
    return Result;
}
}
