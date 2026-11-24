#include "GridProjectSettings.h"

Sokoban::FInventory UGridProjectSettings::StartingInventory() const
{
    const Sokoban::FInventory Starting{StartingStrength, StartingUndo};
    return MakeResourceContext(Starting).Filter(Starting);
}

Sokoban::FResourceContext UGridProjectSettings::MakeResourceContext(Sokoban::FInventory Actual) const
{
    return {bStrengthUnlocked, bUndoUnlocked, {CapabilityStrength, CapabilityUndo}, Actual};
}

Sokoban::FSearchLimits UGridProjectSettings::SearchLimits() const
{
    Sokoban::FSearchLimits Limits{FMath::Max(1, MaxActions), FMath::Max(1, NodeBudget)};
    Limits.MinimumSolutions = FMath::Max(1, MinimumSolutions);
    return Limits;
}
