#include "GridRules.h"

namespace Sokoban
{
namespace
{
struct FNode
{
    FSession Session;
    TArray<FAction> Commands;
    int32 Depth = 0;
};
}

FSolutionReport FBoundedLevelSolver::Search(const FLevel& Level, FInventory Inventory,
                                            const FSearchLimits& Limits) const
{
    if (!FLevelGate::CheckLayout(Level).IsEmpty()) { return {}; }
    return SearchCheckedLayout(Level, Inventory, Limits);
}

FSolutionReport FBoundedLevelSolver::SearchCheckedLayout(const FLevel& Level, FInventory Inventory,
                                                         const FSearchLimits& Limits) const
{
    FSolutionReport Report;
    if (Limits.MaxActions < 0 || Limits.NodeBudget < 0)
    {
        return Report;
    }
    TArray<FNode> Frontier;
    Frontier.Add({FRules::Start(Level, Inventory), {}, 0});
    const TArray<FIntPoint, TInlineAllocator<4>> Directions{{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    // Ignore anything that can open or move so these distances remain lower bounds.
    TSet<FIntPoint> PermanentObstacles = Level.Walls;
    if (!Level.Boxes.ContainsByPredicate([](const FBox& Box) {
        return Box.Spike != FIntPoint::ZeroValue || Box.bExplodes;
    }))
    {
        for (const FEnemy& Enemy : Level.Enemies)
        {
            PermanentObstacles.Add(Enemy.Cell);
        }
    }
    TMap<FIntPoint, int32> DistanceToGoal;
    DistanceToGoal.Add(Level.Goal, 0);
    TArray<FIntPoint> DistanceQueue{Level.Goal};
    for (int32 Index = 0; Index < DistanceQueue.Num(); ++Index)
    {
        const FIntPoint Cell = DistanceQueue[Index];
        const int32 Distance = DistanceToGoal.FindChecked(Cell);
        for (const FIntPoint Direction : Directions)
        {
            const FIntPoint Next = Cell + Direction;
            if (FRules::IsInside(Level, Next) && !PermanentObstacles.Contains(Next) && !DistanceToGoal.Contains(Next))
            {
                DistanceToGoal.Add(Next, Distance + 1);
                DistanceQueue.Add(Next);
            }
        }
    }
    const auto CanReachWithin = [&](const FNode& Node) {
        const int32 Remaining = Limits.MaxActions - Node.Depth;
        const auto Fits = [&](FIntPoint Cell, int32 Budget) {
            const int32* Distance = DistanceToGoal.Find(Cell);
            return Distance && *Distance <= Budget;
        };
        if (Fits(Node.Session.State.Player, Remaining)) { return true; }
        // A rewind can jump closer to the goal, and itself takes one command.
        return Node.Session.State.Inventory.Undo > 0 &&
            Node.Session.History.ContainsByPredicate([&](const FState& Earlier) {
                return Fits(Earlier.Player, Remaining - 1);
            });
    };
    const int32 Threshold = Limits.DifficultyThreshold();
    const int32 Overflow = Limits.DifficultyOverflow();
    const int32 StopAfter = Limits.WitnessLimit > 0 ? Limits.WitnessLimit :
        Overflow > 0 ? Overflow : FMath::Max(Limits.MinimumSolutions, Threshold > 0 ? Threshold : FSearchLimits::EasyStart);
    int32 Expanded = 0;
    bool bCountCapped = false;
    while (!Frontier.IsEmpty())
    {
        FNode Node = Frontier.Pop(EAllowShrinking::No);
        if (Node.Session.State.Outcome == EOutcome::Won)
        {
            const bool bNewRequiredWitness = Limits.RequiredWitness && !Report.bHasRequiredWitness &&
                Limits.RequiredWitness(Node.Commands);
            if (Report.CountLowerBound < StopAfter)
            {
                Report.Witnesses.Add(MoveTemp(Node.Commands));
                ++Report.CountLowerBound;
            }
            else
            {
                bCountCapped = true;
                if (bNewRequiredWitness) { Report.Witnesses.Last() = MoveTemp(Node.Commands); }
            }
            Report.bHasRequiredWitness |= bNewRequiredWitness;
            if (Report.CountLowerBound >= StopAfter &&
                (Overflow > 0 || !Limits.RequiredWitness || Report.bHasRequiredWitness))
            {
                // Any remaining required witness is irrelevant once the band is exceeded.
                break;
            }
            continue;
        }
        if (Node.Session.State.Outcome == EOutcome::Dead || Node.Depth >= Limits.MaxActions) { continue; }
        if (!CanReachWithin(Node)) { continue; }
        if (Expanded == Limits.NodeBudget)
        {
            // Keep the unexpanded node as evidence that the count is only a lower bound.
            Frontier.Add(MoveTemp(Node));
            break;
        }
        ++Expanded;
        const auto Expand = [&](FAction Action, bool bArm)
        {
            FNode Child = Node;
            if (bArm)
            {
                const FAction Arm{EAction::ArmStrength};
                if (!FRules::Apply(Child.Session, Arm).bAccepted) { return; }
                Child.Commands.Add(Arm);
            }
            if (!FRules::Apply(Child.Session, Action).bEffective ||
                Child.Session.State.Outcome == EOutcome::Dead)
            {
                return;
            }
            Child.Commands.Add(Action);
            ++Child.Depth;
            Frontier.Add(MoveTemp(Child));
        };
        if (Node.Session.State.Inventory.Undo > 0)
        {
            for (int32 Steps = 1; Steps <= Node.Session.History.Num(); ++Steps)
            {
                Expand({EAction::Undo, FIntPoint::ZeroValue, Steps}, false);
            }
        }
        // The frontier is LIFO. Push less promising moves first, without dropping any actions.
        auto Ordered = Directions;
        const FIntPoint Player = Node.Session.State.Player;
        const auto DistanceAfter = [&](FIntPoint Direction) {
            const int32* Distance = DistanceToGoal.Find(Player + Direction);
            return Distance ? *Distance : MAX_int32;
        };
        Ordered.StableSort([&](FIntPoint A, FIntPoint B) { return DistanceAfter(A) > DistanceAfter(B); });
        for (FIntPoint Direction : Ordered)
        {
            const FAction Action{EAction::Move, Direction};
            if (Node.Session.State.Inventory.Strength > 0) { Expand(Action, true); }
            Expand(Action, false);
        }
    }

    Report.bSearchExhausted = Frontier.IsEmpty();
    Report.bComplete = Report.bSearchExhausted && !bCountCapped;
    const int32 Count = Report.CountLowerBound;
    Report.Solvable = Count > 0 ? ECheck::Pass : Report.bComplete ? ECheck::Fail : ECheck::Unknown;
    Report.MeetsMinimum = Count >= Limits.MinimumSolutions ? ECheck::Pass : Report.bComplete ? ECheck::Fail : ECheck::Unknown;
    if (Count >= FSearchLimits::EasyStart) { Report.Difficulty = EDifficulty::Low; }
    else if (Report.bComplete)
    {
        Report.Difficulty = Count == 0 ? EDifficulty::BelowMinimum : Count < FSearchLimits::MediumStart ? EDifficulty::High : EDifficulty::Medium;
    }
    return Report;
}
}
