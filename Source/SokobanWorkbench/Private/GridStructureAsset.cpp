#include "GridStructureAsset.h"

#define LOCTEXT_NAMESPACE "GridStructureAsset"

namespace
{
bool CheckFootprint(FIntPoint Size, const TSet<FIntPoint>& Voids, const TSet<FIntPoint>& Walls,
    const TSet<FIntPoint>& Rubble, const TArray<FGridBoxDraft>& Boxes,
    const TArray<FGridEnemyDraft>& Enemies, FText& Error)
{
    TSet<FIntPoint> Claimed;
    const auto Claim = [&](FIntPoint Cell) {
        if (UGridStructureAsset::Fits(Size, Cell, {1, 1}) && !Claimed.Contains(Cell))
        {
            Claimed.Add(Cell);
            return true;
        }
        Error = FText::Format(LOCTEXT("InvalidFootprint", "结构内格子（{0}, {1}）重叠或超出选区。"), Cell.X, Cell.Y);
        return false;
    };
    for (const auto& Cells : {&Voids, &Walls, &Rubble})
    {
        for (const auto Cell : *Cells) { if (!Claim(Cell)) { return false; } }
    }
    for (const auto& Box : Boxes)
    {
        if (!Claim(Box.Cell) || (Box.Spike != FIntPoint::ZeroValue && !Claim(Box.Cell + Box.Spike))) { return false; }
    }
    for (const auto& Enemy : Enemies) { if (!Claim(Enemy.Cell)) { return false; } }
    return true;
}
}

bool UGridStructureAsset::Fits(FIntPoint Board, FIntPoint Anchor, FIntPoint Extent)
{
    return Extent.X > 0 && Extent.Y > 0 && Extent.X <= Board.X && Extent.Y <= Board.Y &&
        Anchor.X >= 0 && Anchor.Y >= 0 && Anchor.X <= Board.X - Extent.X && Anchor.Y <= Board.Y - Extent.Y;
}

bool UGridStructureAsset::IsOccupied(const UGridLevelAsset& Level, FIntPoint Cell)
{
    return Cell == Level.Start || Cell == Level.Goal || Level.Walls.Contains(Cell) || Level.Rubble.Contains(Cell) ||
        Level.Boxes.ContainsByPredicate([Cell](const FGridBoxDraft& Box) {
            return Box.Cell == Cell || (Box.Spike != FIntPoint::ZeroValue && Box.Cell + Box.Spike == Cell);
        }) ||
        Level.Enemies.ContainsByPredicate([Cell](const FGridEnemyDraft& Enemy) { return Enemy.Cell == Cell; });
}

bool UGridStructureAsset::Capture(const UGridLevelAsset& Level, const TSet<FIntPoint>& Selection, FText& Error)
{
    if (Selection.IsEmpty()) { return false; }
    FIntPoint Anchor = Level.Size;
    FIntPoint Max = FIntPoint::ZeroValue;
    for (const FIntPoint Cell : Selection)
    {
        if (!Fits(Level.Size, Cell, FIntPoint(1, 1)))
        {
            Error = LOCTEXT("SelectionOutOfBounds", "选区已超出棋盘，请重新框选。");
            return false;
        }
        Anchor = FIntPoint(FMath::Min(Anchor.X, Cell.X), FMath::Min(Anchor.Y, Cell.Y));
        Max = FIntPoint(FMath::Max(Max.X, Cell.X), FMath::Max(Max.Y, Cell.Y));
    }
    const auto Contains = [&](FIntPoint Cell) { return Selection.Contains(Cell); };
    for (const auto& Box : Level.Boxes)
    {
        if (Box.Spike != FIntPoint::ZeroValue && Contains(Box.Cell) != Contains(Box.Cell + Box.Spike))
        {
            Error = LOCTEXT("PartialSpike", "选区切到了尖刺箱，请同时框住箱体和尖刺。");
            return false;
        }
    }
    const auto CopyCells = [&](const TSet<FIntPoint>& Source) {
        TSet<FIntPoint> Local;
        for (const auto Cell : Source)
        {
            if (Contains(Cell)) { Local.Add(Cell - Anchor); }
        }
        return Local;
    };
    auto LocalVoids = CopyCells(Level.VoidCells);
    auto LocalWalls = CopyCells(Level.Walls);
    auto LocalRubble = CopyCells(Level.Rubble);
    TArray<FGridBoxDraft> LocalBoxes;
    TArray<FGridEnemyDraft> LocalEnemies;
    for (auto Box : Level.Boxes)
    {
        if (!Contains(Box.Cell)) { continue; }
        Box.Id = NAME_None;
        Box.Cell -= Anchor;
        LocalBoxes.Add(MoveTemp(Box));
    }
    for (auto Enemy : Level.Enemies)
    {
        if (!Contains(Enemy.Cell)) { continue; }
        Enemy.Id = NAME_None;
        Enemy.Cell -= Anchor;
        LocalEnemies.Add(MoveTemp(Enemy));
    }
    if (LocalVoids.IsEmpty() && LocalWalls.IsEmpty() && LocalRubble.IsEmpty() &&
        LocalBoxes.IsEmpty() && LocalEnemies.IsEmpty())
    {
        Error = LOCTEXT("EmptyStructure", "选区没有可保存的结构。起点、终点和普通地板不会被记录。");
        return false;
    }
    const FIntPoint LocalSize = Max - Anchor + FIntPoint(1, 1);
    if (!CheckFootprint(LocalSize, LocalVoids, LocalWalls, LocalRubble, LocalBoxes, LocalEnemies, Error))
    {
        return false;
    }
    Size = LocalSize;
    VoidCells = MoveTemp(LocalVoids);
    Walls = MoveTemp(LocalWalls);
    Rubble = MoveTemp(LocalRubble);
    Boxes = MoveTemp(LocalBoxes);
    Enemies = MoveTemp(LocalEnemies);
    return true;
}

bool UGridStructureAsset::CanPlace(const UGridLevelAsset& Level, FIntPoint Anchor, FText& Error) const
{
    if (!Fits(Level.Size, Anchor, Size))
    {
        Error = LOCTEXT("PlaceOutOfBounds", "结构超出棋盘，本次未摆放。");
        return false;
    }
    if (!CheckFootprint(Size, VoidCells, Walls, Rubble, Boxes, Enemies, Error)) { return false; }
    const auto Available = [&](FIntPoint Local, bool bVoidCell = false) {
        const FIntPoint Dest = Anchor + Local;
        if (!IsOccupied(Level, Dest) &&
            (bVoidCell || !Level.VoidCells.Contains(Dest)))
        {
            return true;
        }
        Error = FText::Format(LOCTEXT("PlaceBlocked", "结构在格子（{0}, {1}）遇到占用或挖空，本次未摆放。"), Dest.X, Dest.Y);
        return false;
    };
    for (const auto Cell : VoidCells) { if (!Available(Cell, true)) { return false; } }
    for (const auto Cell : Walls) { if (!Available(Cell)) { return false; } }
    for (const auto Cell : Rubble) { if (!Available(Cell)) { return false; } }
    for (const auto& Box : Boxes)
    {
        if (!Available(Box.Cell) ||
            (Box.Spike != FIntPoint::ZeroValue && !Available(Box.Cell + Box.Spike))) { return false; }
    }
    for (const auto& Enemy : Enemies) { if (!Available(Enemy.Cell)) { return false; } }
    return true;
}

void UGridStructureAsset::Place(UGridLevelAsset& Level, FIntPoint Anchor) const
{
    for (const auto Cell : VoidCells) { Level.VoidCells.Add(Anchor + Cell); }
    for (const auto Cell : Walls) { Level.Walls.Add(Anchor + Cell); }
    for (const auto Cell : Rubble) { Level.Rubble.Add(Anchor + Cell); }
    for (auto Box : Boxes)
    {
        Box.Cell += Anchor;
        Box.Id = Level.AllocateEntityId(TEXT("B"), Box.Cell);
        Level.Boxes.Add(MoveTemp(Box));
    }
    for (auto Enemy : Enemies)
    {
        Enemy.Cell += Anchor;
        Enemy.Id = Level.AllocateEntityId(TEXT("E"), Enemy.Cell);
        Level.Enemies.Add(MoveTemp(Enemy));
    }
}

#undef LOCTEXT_NAMESPACE
