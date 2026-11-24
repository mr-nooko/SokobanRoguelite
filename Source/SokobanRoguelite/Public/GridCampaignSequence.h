#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GridRoomReward.h"
#include "GridCampaignSequence.generated.h"

class UGridLevelAsset;

UENUM(BlueprintType)
enum class EGridSequenceSource : uint8
{
    Authored UMETA(DisplayName="手工关卡"),
    Generated UMETA(DisplayName="生成关卡")
};

UENUM(BlueprintType)
enum class EGridSequenceTemplate : uint8
{
    Any UMETA(DisplayName="随复杂度选择"),
    Detour UMETA(DisplayName="绕行"),
    BeamCover UMETA(DisplayName="射线掩体"),
    SpikeSweep UMETA(DisplayName="尖刺扫击"),
    BlastRubble UMETA(DisplayName="爆炸碎墙"),
    StrengthChain UMETA(DisplayName="大力士推链"),
    EjectCorridor UMETA(DisplayName="弹射通道")
};

UENUM(BlueprintType)
enum class EGridGeneratedDifficulty : uint8
{
    Project UMETA(DisplayName="只检查项目合法性"),
    High UMETA(DisplayName="困难（1～3 解）"),
    Medium UMETA(DisplayName="中等（4～8 解）"),
    Low UMETA(DisplayName="简单（至少 9 解）")
};

USTRUCT(BlueprintType)
struct FGridSequenceSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="关卡", meta=(DisplayName="来源"))
    EGridSequenceSource Source = EGridSequenceSource::Authored;

    UPROPERTY(EditAnywhere, Category="关卡", meta=(DisplayName="显示名称"))
    FText Title;

    UPROPERTY(EditAnywhere, Category="关卡", meta=(DisplayName="关卡提示", MultiLine="true"))
    FText Hint;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="通关赠药", EditCondition="Source == EGridSequenceSource::Generated", EditConditionHides))
    FGridRoomReward ClearReward;

    UPROPERTY(EditAnywhere, Category="关卡", meta=(DisplayName="手工关卡资产", EditCondition="Source == EGridSequenceSource::Authored", EditConditionHides))
    TObjectPtr<UGridLevelAsset> Level;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="生成模板", EditCondition="Source == EGridSequenceSource::Generated", EditConditionHides))
    EGridSequenceTemplate Template = EGridSequenceTemplate::Any;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="复杂度", ClampMin="0", ClampMax="3", EditCondition="Source == EGridSequenceSource::Generated", EditConditionHides))
    int32 Complexity = 2;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="尝试次数", ClampMin="0", ClampMax="64", EditCondition="Source == EGridSequenceSource::Generated", EditConditionHides))
    int32 MaxAttempts = 8;

    EGridGeneratedDifficulty Difficulty = EGridGeneratedDifficulty::Project;
    int32 MaxActions = 0;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="启用后备", EditCondition="Source == EGridSequenceSource::Generated", EditConditionHides))
    bool bAllowFallback = true;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="自定义后备关卡", EditCondition="Source == EGridSequenceSource::Generated && bAllowFallback", EditConditionHides))
    TObjectPtr<UGridLevelAsset> Fallback;
};

USTRUCT(BlueprintType)
struct FGridPlacedRoom
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="手工关", meta=(DisplayName="第几关", NoSpinbox="true"))
    int32 Position = 1;

    UPROPERTY(EditAnywhere, Category="手工关", meta=(DisplayName="手工关卡"))
    TObjectPtr<UGridLevelAsset> Level;

    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="手工关", meta=(DisplayName="显示名称"))
    FText Title;

    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="手工关", meta=(DisplayName="提示", MultiLine="true"))
    FText Hint;
};

USTRUCT(BlueprintType)
struct FGridAutoRoomSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="关卡文字说明", MultiLine="true"))
    FText Hint;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="每关通关赠药"))
    FGridRoomReward ClearReward;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="模板"))
    EGridSequenceTemplate Template = EGridSequenceTemplate::Any;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="复杂度", ClampMin="0", ClampMax="3"))
    int32 Complexity = 2;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="单关最大尝试次数", ClampMin="0", ClampMax="64"))
    int32 MaxAttempts = 8;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="启用后备"))
    bool bAllowFallback = true;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="自定义后备关", EditCondition="bAllowFallback"))
    TObjectPtr<UGridLevelAsset> Fallback;
};

USTRUCT(BlueprintType)
struct FGridGenerationBand
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="区间", meta=(DisplayName="起始关号", NoSpinbox="true"))
    int32 FirstRoom = 1;

    UPROPERTY(EditAnywhere, Category="区间", meta=(DisplayName="结束关号", NoSpinbox="true"))
    int32 LastRoom = 1;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="目标难度分档"))
    EGridGeneratedDifficulty Difficulty = EGridGeneratedDifficulty::Project;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="布局复杂度", ClampMin="0", ClampMax="3"))
    int32 Complexity = 2;

    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="生成", meta=(DisplayName="搜索步数上限（0 继承项目）", ClampMin="0"))
    int32 MaxActions = 0;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="关卡文字说明（留空继承）", MultiLine="true"))
    FText Hint;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="覆盖默认通关赠药"))
    bool bOverrideClearReward = false;

    UPROPERTY(EditAnywhere, Category="生成", meta=(DisplayName="每关通关赠药", EditCondition="bOverrideClearReward"))
    FGridRoomReward ClearReward;
};

UCLASS(BlueprintType, meta=(DisplayName="混合关卡序列"))
class SOKOBANROGUELITE_API UGridCampaignSequence : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="序列", meta=(DisplayName="序列名称"))
    FText Title;

    UPROPERTY(EditAnywhere, Category="序列", meta=(DisplayName="总关数", NoSpinbox="true"))
    int32 TotalRooms = 0;

    UPROPERTY(EditAnywhere, Category="序列", meta=(DisplayName="手工关位置", TitleProperty="Position"))
    TArray<FGridPlacedRoom> PlacedRooms;

    UPROPERTY(EditAnywhere, Category="生成设置", meta=(DisplayName="空位生成设置"))
    FGridAutoRoomSettings AutoRooms;

    UPROPERTY(EditAnywhere, Category="生成设置", meta=(DisplayName="生成难度分段", TitleProperty="FirstRoom"))
    TArray<FGridGenerationBand> GenerationBands;

    UPROPERTY(EditAnywhere, AdvancedDisplay, Category="序列", meta=(DisplayName="旧版逐关配置", EditCondition="TotalRooms == 0", EditConditionHides))
    TArray<FGridSequenceSlot> Slots;

    int32 GetRoomCount() const { return TotalRooms == 0 ? Slots.Num() : TotalRooms; }
    bool BuildSlots(TArray<FGridSequenceSlot>& Out, FText& Error) const;

    UPROPERTY(EditAnywhere, Category="随机", meta=(DisplayName="使用固定种子"))
    bool bUseFixedSeed = false;

    UPROPERTY(EditAnywhere, Category="随机", meta=(DisplayName="固定种子", EditCondition="bUseFixedSeed"))
    int32 FixedSeed = 42;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Transient, Category="生成设置", meta=(DisplayName="配置检查"))
    FText ConfigurationStatus;
#endif

#if WITH_EDITOR
    virtual void PostLoad() override;
    virtual void PreEditChange(FProperty* Property) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;

private:
    void RefreshConfigurationStatus();
    int32 PreviousTotalRooms = 0;
    TArray<FGridPlacedRoom> PreviousPlacedRooms;
    TArray<FGridGenerationBand> PreviousGenerationBands;
    bool bHasPreviousConfiguration = false;
#endif
};
