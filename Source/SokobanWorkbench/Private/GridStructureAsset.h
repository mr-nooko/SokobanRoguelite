#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GridLevelAsset.h"
#include "GridStructureAsset.generated.h"

UCLASS(meta=(DisplayName="关卡结构"))
class UGridStructureAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    virtual bool IsEditorOnly() const override { return true; }

    UPROPERTY(VisibleAnywhere, Category="结构") FIntPoint Size = FIntPoint::ZeroValue;
    UPROPERTY(VisibleAnywhere, Category="结构") TSet<FIntPoint> VoidCells;
    UPROPERTY(VisibleAnywhere, Category="结构") TSet<FIntPoint> Walls;
    UPROPERTY(VisibleAnywhere, Category="结构") TSet<FIntPoint> Rubble;
    UPROPERTY(VisibleAnywhere, Category="结构") TArray<FGridBoxDraft> Boxes;
    UPROPERTY(VisibleAnywhere, Category="结构") TArray<FGridEnemyDraft> Enemies;

    bool Capture(const UGridLevelAsset& Level, const TSet<FIntPoint>& Selection, FText& Error);
    bool CanPlace(const UGridLevelAsset& Level, FIntPoint Anchor, FText& Error) const;
    void Place(UGridLevelAsset& Level, FIntPoint Anchor) const;

    static bool Fits(FIntPoint Board, FIntPoint Anchor, FIntPoint Extent);
    static bool IsOccupied(const UGridLevelAsset& Level, FIntPoint Cell);
};
