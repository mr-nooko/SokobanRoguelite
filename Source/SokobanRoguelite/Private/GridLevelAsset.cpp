#include "GridLevelAsset.h"

#include "Serialization/Archive.h"
#include "UObject/UnrealType.h"

void FGridBoxDraft::PostSerialize(const FArchive& Archive)
{
    if (Archive.IsLoading() && (!EjectDirections.IsEmpty() || SlideSteps > 0))
    {
        bEject = true;
        EjectDirections.Reset();
        SlideSteps = 0;
    }
}

void FGridBoxDraft::SetExplosionEnabled(bool bEnabled)
{
    bExplodes = bEnabled;
    if (!bEnabled)
    {
        bExplodeWhenEjectBlocked = false;
    }
}

FName UGridLevelAsset::AllocateEntityId(const TCHAR* Prefix, FIntPoint Cell) const
{
    const FString Base = FString::Printf(TEXT("%s_%02d_%02d"), Prefix, Cell.X, Cell.Y);
    const auto IsUsed = [this](FName Id)
    {
        return Boxes.ContainsByPredicate([Id](const FGridBoxDraft& Box) { return Box.Id == Id; }) ||
               Enemies.ContainsByPredicate([Id](const FGridEnemyDraft& Enemy) { return Enemy.Id == Id; });
    };
    FName Candidate(*Base);
    for (int32 Suffix = 1; IsUsed(Candidate); ++Suffix)
    {
        Candidate = FName(*FString::Printf(TEXT("%s_%d"), *Base, Suffix));
    }
    return Candidate;
}

#if WITH_EDITOR
void UGridLevelAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& Event)
{
    if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FGridBoxDraft, bExplodes))
    {
        const int32 Index = Event.GetArrayIndex(TEXT("Boxes"));
        if (Boxes.IsValidIndex(Index)) { Boxes[Index].SetExplosionEnabled(Boxes[Index].bExplodes); }
    }
    Super::PostEditChangeChainProperty(Event);
}
#endif

Sokoban::FLevel UGridLevelAsset::ToLevel() const
{
    Sokoban::FLevel Level;
    Level.RoomId = RoomId;
    Level.Size = Size;
    Level.VoidCells = VoidCells;
    Level.Start = Start;
    Level.Goal = Goal;
    Level.Walls = Walls;
    Level.Rubble = Rubble;
    Level.StrengthLimit = StrengthLimit;
    if (bConfigureBeamTerrain) { Level.bBeamStopsOnTerrain = bBeamStopsOnTerrain; }
    for (const FGridBoxDraft& Box : Boxes)
    {
        Level.Boxes.Add({Box.Id, Box.Cell, Box.Spike, Box.bEject,
                         Box.bExplodes, Box.bExplodeWhenEjectBlocked});
    }
    for (const FGridEnemyDraft& Enemy : Enemies)
    {
        Level.Enemies.Add({Enemy.Id, Enemy.Cell, Enemy.BeamDirection});
    }
    return Level;
}

TArray<FString> UGridLevelAsset::GetLayoutWarnings() const
{
    return Sokoban::FLevelGate::CheckLayout(ToLevel());
}

void UGridLevelAsset::SetFromLevel(const Sokoban::FLevel& Level)
{
    RoomId = Level.RoomId;
    Size = Level.Size;
    Start = Level.Start;
    Goal = Level.Goal;
    VoidCells = Level.VoidCells;
    Walls = Level.Walls;
    Rubble = Level.Rubble;
    StrengthLimit = Level.StrengthLimit;
    bConfigureBeamTerrain = Level.bBeamStopsOnTerrain.IsSet();
    bBeamStopsOnTerrain = Level.bBeamStopsOnTerrain.Get(false);
    Boxes.Reset(Level.Boxes.Num());
    for (const auto& Box : Level.Boxes)
    {
        auto& DraftBox = Boxes.AddDefaulted_GetRef();
        DraftBox.Id = Box.Id;
        DraftBox.Cell = Box.Cell;
        DraftBox.Spike = Box.Spike;
        DraftBox.bEject = Box.bEject;
        DraftBox.bExplodes = Box.bExplodes;
        DraftBox.bExplodeWhenEjectBlocked = Box.bExplodeWhenEjectBlocked;
    }
    Enemies.Reset(Level.Enemies.Num());
    for (const auto& Enemy : Level.Enemies) { Enemies.Add({Enemy.Id, Enemy.Cell, Enemy.BeamDirection}); }
}
