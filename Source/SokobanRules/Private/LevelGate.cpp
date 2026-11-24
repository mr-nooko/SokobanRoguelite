#include "GridRules.h"

namespace Sokoban
{
FString FLevelGate::BoardSizeError()
{
    return FString::Printf(TEXT("棋盘宽高必须在 1～%d 格之间。"), MaxBoardSide);
}

FString FLevelGate::ChainModuleAdvisory(const FLevel& Level)
{
    if (Level.StrengthLimit > 1 && Level.Boxes.Num() > 1 &&
        Level.Boxes.ContainsByPredicate([](const FBox& Box) { return Box.bEject || Box.bExplodes || Box.Spike != FIntPoint::ZeroValue; }))
    {
        return TEXT("多箱连推只移动箱体，特殊效果由玩家直接推单箱触发。");
    }
    return {};
}

TArray<FString> FLevelGate::CheckLayout(const FLevel& Level)
{
    TArray<FString> Errors;
    if (Level.RoomId.IsNone()) { Errors.Add(TEXT("请填写关卡编号。")); }
    if (!SupportsBoardSize(Level.Size))
    {
        Errors.Add(BoardSizeError());
        return Errors;
    }
    if (Level.StrengthLimit < 1)
    {
        Errors.Add(TEXT("大力士推箱上限必须大于零。"));
    }
    for (const FIntPoint Cell : Level.VoidCells)
    {
        if (!FRules::IsInBounds(Level, Cell))
        {
            Errors.Add(FString::Printf(TEXT("挖空格（%d，%d）超出棋盘尺寸。"), Cell.X, Cell.Y));
        }
    }
    TSet<FIntPoint> Used;
    TSet<FName> Ids;
    const auto Claim = [&](FIntPoint Cell, const TCHAR* Label)
    {
        if (!FRules::IsInside(Level, Cell) || Used.Contains(Cell))
        {
            Errors.Add(FString::Printf(TEXT("%s格（%d，%d）越界、位于挖空格或与其他物件重叠。"), Label, Cell.X, Cell.Y));
        }
        Used.Add(Cell);
    };
    const auto ClaimId = [&](FName Id)
    {
        if (Id.IsNone() || Ids.Contains(Id)) { Errors.Add(TEXT("每个箱子和敌人的编号必须填写，并且在关卡内唯一。")); }
        Ids.Add(Id);
    };
    for (FIntPoint Cell : Level.Walls) { Claim(Cell, TEXT("墙")); }
    for (FIntPoint Cell : Level.Rubble) { Claim(Cell, TEXT("碎墙")); }
    if (!FRules::IsInside(Level, Level.Goal) || Used.Contains(Level.Goal))
    {
        Errors.Add(TEXT("终点越界、位于挖空格或与地形重叠。"));
    }
    Claim(Level.Start, TEXT("起点"));
    for (const FBox& Box : Level.Boxes)
    {
        ClaimId(Box.Id);
        Claim(Box.Cell, TEXT("箱子"));
        if (Box.Cell == Level.Goal) { Errors.Add(TEXT("箱子占用了终点。")); }
        if (Box.Spike != FIntPoint::ZeroValue)
        {
            if (!FRules::IsCardinal(Box.Spike)) { Errors.Add(TEXT("尖刺必须朝向上、下、左、右中的一个方向。")); }
            Claim(Box.Cell + Box.Spike, TEXT("尖刺"));
            if (Box.Cell + Box.Spike == Level.Goal) { Errors.Add(TEXT("尖刺占用了终点。")); }
        }

    }
    for (const FEnemy& Enemy : Level.Enemies)
    {
        ClaimId(Enemy.Id);
        Claim(Enemy.Cell, TEXT("敌人"));
        if (Enemy.Cell == Level.Goal) { Errors.Add(TEXT("敌人占用了终点。")); }
        if (Enemy.BeamDirection == FIntPoint::ZeroValue) { continue; }
        if (!FRules::IsCardinal(Enemy.BeamDirection)) { Errors.Add(TEXT("射线必须朝向上、下、左、右中的一个方向。")); }
        else if (!Level.bBeamStopsOnTerrain.IsSet())
        {
            for (FIntPoint Cell = Enemy.Cell + Enemy.BeamDirection;
                 FRules::IsInside(Level, Cell); Cell += Enemy.BeamDirection)
            {
                if (Level.Walls.Contains(Cell) || Level.Rubble.Contains(Cell))
                {
                    Errors.Add(TEXT("射线路径经过地形，请先配置射线是否被地形阻挡。"));
                    break;
                }
            }
        }
    }
    return Errors;
}

FValidation FLevelGate::Validate(const FLevel& Level, const FResourceContext& Resources,
                                 const FSearchLimits& Limits, const ILevelSolver& Solver)
{
    FValidation Result;
    Result.Errors = CheckLayout(Level);
    if (!Result.Errors.IsEmpty())
    {
        Result.Status = Result.LegalityStatus = ECheck::Fail;
        return Result;
    }
    const int32 Threshold = Limits.DifficultyThreshold();
    const int32 Minimum = FMath::Max(1, Limits.MinimumSolutions);
    FSearchLimits ActualLimits = Limits;
    ActualLimits.MinimumSolutions = Minimum;
    if (Threshold == 0) { ActualLimits.WitnessLimit = FMath::Max(Minimum, Limits.WitnessLimit); }
    Result.Actual = Solver.SearchCheckedLayout(Level, Resources.Filter(Resources.ActualInventory), ActualLimits);

    const FInventory CapabilityInventory = Resources.Filter(Resources.CapabilityBudget);
    for (const auto& Witness : Result.Actual.Witnesses)
    {
        auto Session = FRules::Start(Level, CapabilityInventory);
        bool bValid = true;
        for (const auto& Action : Witness)
        {
            if (!FRules::Apply(Session, Action).bEffective) { bValid = false; break; }
        }
        if (!bValid || Session.State.Outcome != EOutcome::Won) { continue; }
        Result.Capability.Witnesses.Add(Witness);
        if (++Result.Capability.CountLowerBound == Minimum)
        {
            Result.Capability.Solvable = Result.Capability.MeetsMinimum = ECheck::Pass;
            break;
        }
    }
    if (Result.Capability.MeetsMinimum != ECheck::Pass)
    {
        FSearchLimits CapabilityLimits = ActualLimits;
        CapabilityLimits.RequiredWitness = {};
        CapabilityLimits.TargetDifficulty = EDifficulty::Unknown;
        CapabilityLimits.WitnessLimit = Minimum;
        Result.Capability = Solver.SearchCheckedLayout(Level, CapabilityInventory, CapabilityLimits);
    }
    const auto Combine = [](ECheck A, ECheck B) {
        if (A == ECheck::Fail || B == ECheck::Fail) { return ECheck::Fail; }
        return A == ECheck::Pass && B == ECheck::Pass ? ECheck::Pass : ECheck::Unknown;
    };
    Result.LegalityStatus = Combine(Result.Capability.MeetsMinimum, Result.Actual.MeetsMinimum);
    if (Threshold > 0)
    {
        const int32 Overflow = Limits.DifficultyOverflow();
        if (Overflow > 0 && Result.Actual.CountLowerBound >= Overflow)
        {
            Result.TargetDifficultyStatus = ECheck::Fail;
        }
        else if (Overflow == 0 && Result.Actual.CountLowerBound >= Threshold)
        {
            Result.TargetDifficultyStatus = ECheck::Pass;
        }
        else if (Result.Actual.bComplete)
        {
            Result.TargetDifficultyStatus = Result.Actual.CountLowerBound >= Threshold ? ECheck::Pass : ECheck::Fail;
        }
    }
    Result.Status = Threshold == 0 ? Result.LegalityStatus : Combine(Result.LegalityStatus, Result.TargetDifficultyStatus);
    return Result;
}

TOptional<FSession> FLevelGate::OpenFormal(const FLevel& Level, const FResourceContext& Resources,
                                           const FSearchLimits& Limits, const ILevelSolver& Solver,
                                           FValidation* OutValidation)
{
    FValidation Validation = Validate(Level, Resources, Limits, Solver);
    const bool bPassed = Validation.Status == ECheck::Pass;
    if (OutValidation) { *OutValidation = MoveTemp(Validation); }
    if (!bPassed) { return {}; }
    return FRules::Start(Level, Resources.Filter(Resources.ActualInventory));
}
}
