#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GridRules.h"
#include "GridVisualProfile.h"
#include "GridRoomReward.h"
#include "GridLevelAsset.generated.h"

USTRUCT(BlueprintType)
struct FGridBoxDraft
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="实体编号")) FName Id;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="所在格")) FIntPoint Cell = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="尖刺方向")) FIntPoint Spike = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="沿推动方向弹射")) bool bEject = false;

    // Old assets stored a list of directions. A nonempty list now enables a straight eject.
    UPROPERTY() TArray<FIntPoint> EjectDirections;
    UPROPERTY() int32 SlideSteps = 0;
    void PostSerialize(const FArchive& Archive);
    SOKOBANROGUELITE_API void SetExplosionEnabled(bool bEnabled);
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="启用爆炸")) bool bExplodes = false;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="弹射起步受阻时爆炸")) bool bExplodeWhenEjectBlocked = false;
};

template <>
struct TStructOpsTypeTraits<FGridBoxDraft> : public TStructOpsTypeTraitsBase2<FGridBoxDraft>
{
    enum { WithPostSerialize = true };
};

USTRUCT(BlueprintType)
struct FGridEnemyDraft
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="实体编号")) FName Id;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="所在格")) FIntPoint Cell = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="射线方向")) FIntPoint BeamDirection = FIntPoint::ZeroValue;
};

UCLASS(BlueprintType, meta=(DisplayName="关卡草稿"))
class SOKOBANROGUELITE_API UGridLevelAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="关卡编号")) FName RoomId;
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="棋盘尺寸")) FIntPoint Size = FIntPoint(8, 6);
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="起点")) FIntPoint Start = FIntPoint(1, 1);
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="终点")) FIntPoint Goal = FIntPoint(6, 4);
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="挖空格"))
    TSet<FIntPoint> VoidCells;

    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="墙")) TSet<FIntPoint> Walls;
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="碎墙")) TSet<FIntPoint> Rubble;
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="箱子")) TArray<FGridBoxDraft> Boxes;
    UPROPERTY(EditAnywhere, Category="关卡数据", meta=(DisplayName="敌人")) TArray<FGridEnemyDraft> Enemies;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="大力士推箱上限", ClampMin="1")) int32 StrengthLimit = 2;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="配置射线地形遮挡")) bool bConfigureBeamTerrain = false;
    UPROPERTY(EditAnywhere, Category="物件与规则", meta=(DisplayName="地形阻挡射线", EditCondition="bConfigureBeamTerrain")) bool bBeamStopsOnTerrain = false;

    UPROPERTY(EditAnywhere, Category="关卡说明与通关奖励", meta=(DisplayName="关卡文字说明", MultiLine="true"))
    FText PlaytestHint;

    UPROPERTY(EditAnywhere, Category="关卡说明与通关奖励", meta=(DisplayName="通关赠药"))
    FGridRoomReward ClearReward;

    UPROPERTY(EditAnywhere, Category="美术表现", meta=(DisplayName="关卡美术配置"))
    TObjectPtr<UGridVisualProfile> VisualProfile;

    FName AllocateEntityId(const TCHAR* Prefix, FIntPoint Cell) const;
    Sokoban::FLevel ToLevel() const;
    void SetFromLevel(const Sokoban::FLevel& Level);

#if WITH_EDITOR
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

    UFUNCTION(BlueprintPure, Category="关卡数据", meta=(DisplayName="获取布局警告"))
    TArray<FString> GetLayoutWarnings() const;
};
