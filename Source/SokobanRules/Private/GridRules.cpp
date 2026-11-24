#include "GridRules.h"

namespace Sokoban
{
namespace
{
constexpr int32 MaxUndoSteps = 10;

void EndLife(FSession& Session)
{
    Session.State.Outcome = EOutcome::Dead;
    Session.State.Inventory = {};
    Session.State.bStrengthArmed = false;
    Session.History.Reset();
}

bool IsTerrain(const FLevel& Level, FIntPoint Cell)
{
    return !FRules::IsInside(Level, Cell) || Level.Walls.Contains(Cell) || Level.Rubble.Contains(Cell);
}

bool HasSpikeAt(const FLevel& Level, FIntPoint Cell, const FBox* Ignored = nullptr)
{
    return Level.Boxes.ContainsByPredicate([Cell, Ignored](const FBox& Box) {
        return &Box != Ignored && Box.Spike != FIntPoint::ZeroValue && Box.Cell + Box.Spike == Cell;
    });
}

bool HasEnemyAt(const FLevel& Level, FIntPoint Cell)
{
    return Level.Enemies.ContainsByPredicate([Cell](const FEnemy& Enemy) { return Enemy.Cell == Cell; });
}

void Add(FTurnResult& Result, EEvent Kind, FIntPoint Cell, FName Id = NAME_None)
{
    Result.Events.Add({Kind, Cell, Id, {}});
}

void UpdateBeam(FState& State, FTurnResult& Result, bool& bFatal)
{
    State.Beam.Reset();
    State.BeamTraces.Reset();
    for (const FEnemy& Enemy : State.Board.Enemies)
    {
        if (!FRules::IsCardinal(Enemy.BeamDirection)) { continue; }
        FBeamTrace Trace{Enemy.Cell, Enemy.BeamDirection, Enemy.Cell};
        for (FIntPoint Cell = Enemy.Cell + Enemy.BeamDirection; FRules::IsInside(State.Board, Cell);
             Cell += Enemy.BeamDirection)
        {
            Trace.bStoppedByBox = State.Board.Boxes.ContainsByPredicate(
                [Cell](const FBox& Box) { return Box.Cell == Cell; });
            if (Trace.bStoppedByBox || (State.Board.bBeamStopsOnTerrain.Get(false) && IsTerrain(State.Board, Cell)))
            {
                break;
            }
            State.Beam.Add(Cell);
            Trace.LastCell = Cell;
        }
        State.BeamTraces.Add(Trace);
    }
    if (!bFatal && State.Beam.Contains(State.Player))
    {
        bFatal = true;
        Add(Result, EEvent::BeamExposure, State.Player);
    }
}

void Sweep(FState& State, FTurnResult& Result, const FBox& Box, FIntPoint Body, FIntPoint Direction)
{
    if (Box.Spike == FIntPoint::ZeroValue || Box.Spike == FIntPoint(-Direction.X, -Direction.Y)) { return; }
    const FIntPoint Tip = Body + Box.Spike;
    State.Board.Enemies.RemoveAll([&](const FEnemy& Enemy)
    {
        if (Enemy.Cell != Tip) { return false; }
        Add(Result, EEvent::EnemyKilled, Tip, Enemy.Id);
        return true;
    });
}

bool CheckPlacement(const FState& State, const TArray<int32>& Moving, FIntPoint Delta, FIntPoint Player, bool bAllowSpikeHit = true)
{
    TSet<FIntPoint> Occupancy;
    for (int32 Index = 0; Index < State.Board.Boxes.Num(); ++Index)
    {
        if (Moving.Contains(Index)) { continue; }
        const FBox& Box = State.Board.Boxes[Index];
        Occupancy.Add(Box.Cell);
        if (Box.Spike != FIntPoint::ZeroValue) { Occupancy.Add(Box.Cell + Box.Spike); }
    }
    const auto Claim = [&](FIntPoint Cell)
    {
        if (Cell == Player || Cell == State.Board.Goal || IsTerrain(State.Board, Cell) ||
            Occupancy.Contains(Cell))
        {
            return false;
        }
        Occupancy.Add(Cell);
        return true;
    };
    for (int32 Index : Moving)
    {
        const FBox& Box = State.Board.Boxes[Index];
        const FIntPoint Cell = Box.Cell + Delta;
        const FIntPoint Tip = Cell + Box.Spike;
        if (State.Board.Enemies.ContainsByPredicate([&](const FEnemy& Enemy) {
                return Enemy.Cell == Cell || (!bAllowSpikeHit && Enemy.Cell == Tip);
            }) || !Claim(Cell) || (Box.Spike != FIntPoint::ZeroValue && !Claim(Tip)))
        {
            return false;
        }
    }
    return true;
}

void Blast(FState& State, const TArray<int32>& Roots, FTurnResult& Result, bool& bFatal)
{
    TArray<int32> Wave = Roots;
    TSet<FIntPoint> Hit;
    TSet<FIntPoint> Rubble;
    const FLevel& Level = State.Board;
    for (int32 I = 0; I < Wave.Num(); ++I)
    {
        const FIntPoint Center = Level.Boxes[Wave[I]].Cell;
        for (int32 Y = -1; Y <= 1; ++Y)
        {
            for (int32 X = -1; X <= 1; ++X)
            {
                const FIntPoint Cell = Center + FIntPoint(X, Y);
                if ((!X && !Y) || !FRules::IsInside(Level, Cell) || Level.Walls.Contains(Cell) ||
                    (X && Y && IsTerrain(Level, Center + FIntPoint(X, 0)) &&
                     IsTerrain(Level, Center + FIntPoint(0, Y))))
                {
                    continue;
                }
                if (Level.Rubble.Contains(Cell)) { Rubble.Add(Cell); continue; }
                Hit.Add(Cell);
                for (int32 BoxIndex = 0; BoxIndex < Level.Boxes.Num(); ++BoxIndex)
                {
                    const FBox& Box = Level.Boxes[BoxIndex];
                    if (Box.bExplodes && Box.Cell == Cell) { Wave.AddUnique(BoxIndex); }
                }
            }
        }
    }
    if (Wave.IsEmpty()) { return; }
    FEvent Batch{EEvent::Blast, Level.Boxes[Wave[0]].Cell, NAME_None, {}};
    for (int32 Index : Wave)
    {
        Batch.BlastBoxes.Add(Level.Boxes[Index].Id);
        Batch.BlastCenters.Add(Level.Boxes[Index].Cell);
    }
    Result.Events.Add(Batch);
    State.Board.Enemies.RemoveAll([&](const FEnemy& Enemy)
    {
        if (!Hit.Contains(Enemy.Cell)) { return false; }
        Add(Result, EEvent::EnemyKilled, Enemy.Cell, Enemy.Id);
        return true;
    });
    for (const FIntPoint Cell : Rubble) { State.Board.Rubble.Remove(Cell); }
    State.Board.Boxes.RemoveAll([&](const FBox& Box) { return Batch.BlastBoxes.Contains(Box.Id); });
    UpdateBeam(State, Result, bFatal);
}

bool MoveEffect(FState& State, int32 Index, FIntPoint Delta, FTurnResult& Result, bool& bFatal)
{
    FBox& Box = State.Board.Boxes[Index];
    if (!CheckPlacement(State, {Index}, Delta, State.Player)) { return false; }
    Box.Cell += Delta;
    Add(Result, EEvent::BoxMoved, Box.Cell, Box.Id);
    Sweep(State, Result, Box, Box.Cell, Delta);
    UpdateBeam(State, Result, bFatal);
    return true;
}

void ApplyBoxEffects(FState& State, int32 Index, FIntPoint Direction, FTurnResult& Result, bool& bFatal)
{
    const FBox& Box = State.Board.Boxes[Index];
    bool bEjectBlockedAtStart = false;
    if (Box.bEject)
    {
        // Each extra step stays on the push axis, so the board bounds guarantee termination.
        bEjectBlockedAtStart = !MoveEffect(State, Index, Direction, Result, bFatal);
        if (!bEjectBlockedAtStart)
        {
            while (MoveEffect(State, Index, Direction, Result, bFatal)) {}
        }
    }
    if (Box.bExplodes && (!bEjectBlockedAtStart || Box.bExplodeWhenEjectBlocked))
    {
        Blast(State, {Index}, Result, bFatal);
    }
}

void SaveHistory(FSession& Session)
{
    FState Before = Session.State;
    if (Before.bStrengthArmed)
    {
        ++Before.Inventory.Strength;
        Before.bStrengthArmed = false;
    }
    if (Session.History.Num() == MaxUndoSteps) { Session.History.RemoveAt(0); }
    Session.History.Add(MoveTemp(Before));
}
}

bool FRules::IsInBounds(const FLevel& Level, FIntPoint Cell)
{
    return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Level.Size.X && Cell.Y < Level.Size.Y;
}

bool FRules::IsInside(const FLevel& Level, FIntPoint Cell)
{
    return IsInBounds(Level, Cell) && !Level.VoidCells.Contains(Cell);
}

bool FRules::IsCardinal(FIntPoint Delta)
{
    return (Delta.X == 0 && (Delta.Y == 1 || Delta.Y == -1)) ||
           (Delta.Y == 0 && (Delta.X == 1 || Delta.X == -1));
}

FSession FRules::Start(const FLevel& Level, FInventory Inventory)
{
    FSession Session;
    Session.State.Board = Level;
    Session.State.Player = Level.Start;
    Session.State.Inventory = Inventory;
    bool bFatal = false;
    FTurnResult Ignored;
    UpdateBeam(Session.State, Ignored, bFatal);
    if (Level.Start == Level.Goal) { Session.State.Outcome = EOutcome::Won; }
    else if (bFatal) { EndLife(Session); }
    return Session;
}

int32 FRules::AvailableUndoSteps(const FSession& Session)
{
    const auto& State = Session.State;
    if (State.Outcome != EOutcome::Running || State.Inventory.Undo <= 0) { return 0; }
    int32 Steps = 0;
    for (int32 Index = Session.History.Num() - 1; Index >= 0 && Steps < MaxUndoSteps; --Index)
    {
        if (Session.History[Index].Board.RoomId != State.Board.RoomId) { break; }
        ++Steps;
    }
    return Steps;
}

FTurnResult FRules::Apply(FSession& Session, const FAction& Action)
{
    FTurnResult Result;
    FState& State = Session.State;
    if (State.Outcome != EOutcome::Running) { return Result; }
    if (Action.Kind == EAction::ArmStrength)
    {
        if (!State.bStrengthArmed && State.Inventory.Strength > 0)
        {
            --State.Inventory.Strength;
            State.bStrengthArmed = true;
            Result.bAccepted = Result.bEffective = true;
        }
        return Result;
    }
    if (Action.Kind == EAction::Undo)
    {
        if (Action.Steps <= 0 || Action.Steps > AvailableUndoSteps(Session)) { return Result; }
        const int32 First = Session.History.Num() - Action.Steps;
        const int32 Tokens = State.Inventory.Undo - 1;
        State = Session.History[First];
        State.Inventory.Undo = FMath::Min(State.Inventory.Undo, Tokens);
        Session.History.SetNum(First);
        Result.bAccepted = Result.bEffective = true;
        Add(Result, EEvent::Restored, State.Player);
        return Result;
    }
    if (Action.Kind != EAction::Move || !IsCardinal(Action.Direction)) { return Result; }

    SaveHistory(Session);
    const bool bStrength = State.bStrengthArmed;
    State.bStrengthArmed = false;
    ++State.Turn;
    Result.bAccepted = true;
    const FIntPoint Target = State.Player + Action.Direction;
    if (HasEnemyAt(State.Board, Target))
    {
        EndLife(Session);
        Result.bEffective = true;
        Add(Result, EEvent::ContactDeath, Target);
        return Result;
    }
    if (IsTerrain(State.Board, Target) || HasSpikeAt(State.Board, Target))
    {
        return Result;
    }

    TArray<int32> Chain;
    FIntPoint Cell = Target;
    for (;;)
    {
        const int32 Index = State.Board.Boxes.IndexOfByPredicate([Cell](const FBox& Box) { return Box.Cell == Cell; });
        if (Index == INDEX_NONE) { break; }
        Chain.Add(Index);
        Cell += Action.Direction;
    }
    bool bFatal = false;
    if (!Chain.IsEmpty())
    {
        if (Chain.Num() > (bStrength ? State.Board.StrengthLimit : 1) ||
            !CheckPlacement(State, Chain, Action.Direction, Target, Chain.Num() == 1))
        {
            const FBox& FirstBox = State.Board.Boxes[Chain[0]];
            if (Chain.Num() == 1 && FirstBox.bExplodes &&
                (HasEnemyAt(State.Board, Cell) || State.Board.Rubble.Contains(Cell) ||
                 HasSpikeAt(State.Board, Cell, &FirstBox)))
            {
                // The blocked bomb detonates in place before the player can move.
                Blast(State, Chain, Result, bFatal);
                Result.bEffective = true;
                if (bFatal) { EndLife(Session); }
                return Result;
            }
            Add(Result, EEvent::PushRejected, Cell);
            if (bFatal) { EndLife(Session); }
            return Result;
        }
        for (int32 I = Chain.Num() - 1; I >= 0; --I)
        {
            FBox& Box = State.Board.Boxes[Chain[I]];
            Box.Cell += Action.Direction;
            Add(Result, EEvent::BoxMoved, Box.Cell, Box.Id);
            if (Chain.Num() == 1) { Sweep(State, Result, Box, Box.Cell, Action.Direction); }
        }
    }

    State.Player = Target;
    Result.bEffective = true;
    Add(Result, EEvent::PlayerMoved, Target);
    UpdateBeam(State, Result, bFatal);
    if (Chain.Num() == 1) { ApplyBoxEffects(State, Chain[0], Action.Direction, Result, bFatal); }
    else if (Chain.Num() > 1)
    {
        Result.bSkippedChainEffects = Chain.ContainsByPredicate([&](int32 Index) {
            const auto& Box = State.Board.Boxes[Index];
            return Box.bEject || Box.bExplodes || Box.Spike != FIntPoint::ZeroValue;
        });
    }
    if (Target == State.Board.Goal) { State.Outcome = EOutcome::Won; }
    else if (bFatal) { EndLife(Session); }
    return Result;
}
}
