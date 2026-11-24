#pragma once

#include "CoreMinimal.h"

namespace Sokoban
{
enum class EOutcome : uint8 { Running, Won, Dead };
enum class EAction : uint8 { Move, ArmStrength, Undo };
enum class EEvent : uint8 { PlayerMoved, BoxMoved, PushRejected, EnemyKilled, BeamExposure, ContactDeath, Blast, Restored };
enum class ECheck : uint8 { Pass, Fail, Unknown };
enum class EDifficulty : uint8 { Unknown, BelowMinimum, High, Medium, Low };

struct FInventory
{
    int32 Strength = 0;
    int32 Undo = 0;
};

struct FBox
{
    FName Id;
    FIntPoint Cell = FIntPoint::ZeroValue;
    FIntPoint Spike = FIntPoint::ZeroValue;
    bool bEject = false;
    bool bExplodes = false;
    bool bExplodeWhenEjectBlocked = false;
};

struct FEnemy
{
    FName Id;
    FIntPoint Cell = FIntPoint::ZeroValue;
    FIntPoint BeamDirection = FIntPoint::ZeroValue;
};

struct FLevel
{
    FName RoomId;
    FIntPoint Size = FIntPoint::ZeroValue;
    FIntPoint Start = FIntPoint::ZeroValue;
    FIntPoint Goal = FIntPoint::ZeroValue;
    TSet<FIntPoint> Walls;
    TSet<FIntPoint> Rubble;
    TArray<FBox> Boxes;
    TArray<FEnemy> Enemies;
    int32 StrengthLimit = 2;
    TOptional<bool> bBeamStopsOnTerrain;
    TSet<FIntPoint> VoidCells;
};

struct FAction
{
    EAction Kind = EAction::Move;
    FIntPoint Direction = FIntPoint::ZeroValue;
    int32 Steps = 0;
};

struct FEvent
{
    EEvent Kind;
    FIntPoint Cell = FIntPoint::ZeroValue;
    FName Entity;
    TArray<FName> BlastBoxes;
    TArray<FIntPoint> BlastCenters;
};

struct FBeamTrace
{
    FIntPoint Origin;
    FIntPoint Direction;
    FIntPoint LastCell;
    bool bStoppedByBox = false;
};

struct FState
{
    FLevel Board;
    FIntPoint Player = FIntPoint::ZeroValue;
    FInventory Inventory;
    bool bStrengthArmed = false;
    int32 Turn = 0;
    EOutcome Outcome = EOutcome::Running;
    TSet<FIntPoint> Beam;
    TArray<FBeamTrace> BeamTraces;
};

struct FSession
{
    FState State;
    TArray<FState> History;
};

struct FTurnResult
{
    bool bAccepted = false;
    bool bEffective = false;
    TArray<FEvent> Events;
    bool bSkippedChainEffects = false;
};

class SOKOBANRULES_API FRules
{
public:
    static FSession Start(const FLevel& Level, FInventory Inventory);
    static FTurnResult Apply(FSession& Session, const FAction& Action);
    static bool IsInBounds(const FLevel& Level, FIntPoint Cell);
    static bool IsInside(const FLevel& Level, FIntPoint Cell);
    static bool IsCardinal(FIntPoint Delta);
    static int32 AvailableUndoSteps(const FSession& Session);
};

struct FSearchLimits
{
    int32 MaxActions = 12;
    int32 NodeBudget = 10000;
    EDifficulty TargetDifficulty = EDifficulty::Unknown;
    bool bRequireDifficultyBand = false;
    int32 WitnessLimit = 0;
    int32 MinimumSolutions = 1;

    static constexpr int32 MediumStart = 4;
    static constexpr int32 EasyStart = 9;
    TFunction<bool(const TArray<FAction>&)> RequiredWitness;

    int32 DifficultyOverflow() const
    {
        if (!bRequireDifficultyBand) { return 0; }
        if (TargetDifficulty == EDifficulty::High) { return MediumStart; }
        if (TargetDifficulty == EDifficulty::Medium) { return EasyStart; }
        return 0;
    }

    int32 DifficultyThreshold() const
    {
        switch (TargetDifficulty)
        {
            case EDifficulty::High: return 1;
            case EDifficulty::Medium: return MediumStart;
            case EDifficulty::Low: return EasyStart;
            default: return 0;
        }
    }
};

struct FResourceContext
{
    bool bStrengthUnlocked = false;
    bool bUndoUnlocked = false;
    FInventory CapabilityBudget;
    FInventory ActualInventory;

    FInventory Filter(FInventory Stock) const
    {
        return {bStrengthUnlocked ? FMath::Max(0, Stock.Strength) : 0,
                bUndoUnlocked ? FMath::Max(0, Stock.Undo) : 0};
    }
};

struct FSolutionReport
{
    ECheck Solvable = ECheck::Unknown;
    ECheck MeetsMinimum = ECheck::Unknown;
    EDifficulty Difficulty = EDifficulty::Unknown;
    int32 CountLowerBound = 0;
    bool bComplete = false;
    bool bHasRequiredWitness = false;
    bool bSearchExhausted = false;
    TArray<TArray<FAction>> Witnesses;
};

struct FValidation
{
    ECheck Status = ECheck::Unknown;
    TArray<FString> Errors;
    FSolutionReport Capability;
    FSolutionReport Actual;
    ECheck LegalityStatus = ECheck::Unknown;
    ECheck TargetDifficultyStatus = ECheck::Unknown;
};

class SOKOBANRULES_API ILevelSolver
{
public:
    virtual ~ILevelSolver() = default;
    virtual FSolutionReport Search(const FLevel& Level, FInventory Inventory,
                                   const FSearchLimits& Limits) const = 0;

private:
    friend class FLevelGate;
    virtual FSolutionReport SearchCheckedLayout(const FLevel& Level, FInventory Inventory,
                                               const FSearchLimits& Limits) const
    {
        return Search(Level, Inventory, Limits);
    }
};

class SOKOBANRULES_API FBoundedLevelSolver final : public ILevelSolver
{
public:
    virtual FSolutionReport Search(const FLevel& Level, FInventory Inventory,
                                   const FSearchLimits& Limits) const override;

private:
    virtual FSolutionReport SearchCheckedLayout(const FLevel& Level, FInventory Inventory,
                                               const FSearchLimits& Limits) const override;
};

enum class EGenerationTemplate : uint8 { Detour, BeamCover, SpikeSweep, BlastRubble, StrengthChain, EjectCorridor };

struct FGenerationAttempt
{
    int32 Seed = 0;
    int32 Complexity = 0;
    FName Template;
    ECheck Status = ECheck::Unknown;
    int32 CapabilitySolutions = 0;
    int32 ActualSolutions = 0;
    bool bFallback = false;
    TArray<FString> LayoutErrors;
    FString DesignIssue;
    TOptional<int32> TemplateComplexity;
};

struct FGenerationRequest
{
    int32 Seed = 0;
    int32 Complexity = 0;
    int32 MaxAttempts = 8;
    FResourceContext Resources;
    FSearchLimits Search;
    TArray<EGenerationTemplate> AllowedTemplates;
    TArray<FLevel> AuthoredTemplates;
    TArray<FName> RecentTemplates;
    TOptional<FLevel> FallbackLevel;
    bool bAllowFallback = true;
};

struct FGenerationResult
{
    ECheck Status = ECheck::Unknown;
    TOptional<FLevel> Level;
    FValidation Validation;
    int32 Attempts = 0;
    bool bFallback = false;
    int32 Seed = 0;
    TArray<FGenerationAttempt> Trace;
};

class SOKOBANRULES_API ILevelGenerator
{
public:
    virtual ~ILevelGenerator() = default;
    virtual FGenerationResult Generate(const FGenerationRequest& Request) const = 0;
};

class SOKOBANRULES_API FLevelGate
{
public:
    static constexpr int32 MaxBoardSide = 64;
    static bool SupportsBoardSize(FIntPoint Size)
    {
        return Size.X >= 1 && Size.Y >= 1 && Size.X <= MaxBoardSide && Size.Y <= MaxBoardSide;
    }
    static FString BoardSizeError();
    static FString ChainModuleAdvisory(const FLevel& Level);
    static TArray<FString> CheckLayout(const FLevel& Level);
    static FValidation Validate(const FLevel& Level, const FResourceContext& Resources,
                                const FSearchLimits& Limits, const ILevelSolver& Solver);
    static TOptional<FSession> OpenFormal(const FLevel& Level, const FResourceContext& Resources,
                                          const FSearchLimits& Limits, const ILevelSolver& Solver,
                                          FValidation* OutValidation = nullptr);
};

class SOKOBANRULES_API FLevelDraft
{
public:
    explicit FLevelDraft(FLevel Level) : Data(MoveTemp(Level)) {}
    FLevel& Edit() { return Data; }
    const FLevel& Read() const { return Data; }
    TArray<FString> Warnings() const { return FLevelGate::CheckLayout(Data); }
private:
    FLevel Data;
};
}

