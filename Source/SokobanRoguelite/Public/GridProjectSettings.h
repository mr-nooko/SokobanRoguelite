#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GridRules.h"
#include "GridProjectSettings.generated.h"

class UGridVisualProfile;
class UGridCampaignSequence;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="推箱子关卡"))
class SOKOBANROGUELITE_API UGridProjectSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    virtual FName GetCategoryName() const override { return TEXT("Game"); }

    UPROPERTY(Config, EditAnywhere, Category="关卡合法性校验", meta=(DisplayName="合法关卡所需最少解法数", ClampMin="1"))
    int32 MinimumSolutions = 1;

    UPROPERTY(Config, EditAnywhere, Category="关卡合法性校验", meta=(DisplayName="动作步数上限", ClampMin="1"))
    int32 MaxActions = 12;

    UPROPERTY(Config, EditAnywhere, Category="关卡合法性校验", meta=(DisplayName="每阶段搜索节点预算", ClampMin="1"))
    int32 NodeBudget = 10000;

    UPROPERTY(Config, EditAnywhere, Category="道具", meta=(DisplayName="解锁大力士药丸"))
    bool bStrengthUnlocked = true;

    UPROPERTY(Config, EditAnywhere, Category="道具", meta=(DisplayName="解锁时空回溯药丸"))
    bool bUndoUnlocked = true;

    UPROPERTY(Config, EditAnywhere, Category="道具", meta=(DisplayName="能力验证用大力士药丸", ClampMin="0"))
    int32 CapabilityStrength = 1;

    UPROPERTY(Config, EditAnywhere, Category="道具", meta=(DisplayName="能力验证用时空回溯药丸", ClampMin="0"))
    int32 CapabilityUndo = 2;

    UPROPERTY(Config, EditAnywhere, Category="道具", meta=(DisplayName="开局大力士药丸", ClampMin="0"))
    int32 StartingStrength = 1;

    UPROPERTY(Config, EditAnywhere, Category="道具", meta=(DisplayName="开局时空回溯药丸", ClampMin="0"))
    int32 StartingUndo = 2;

    UPROPERTY(Config, EditAnywhere, Category="美术", meta=(DisplayName="项目默认美术配置"))
    TSoftObjectPtr<UGridVisualProfile> BoardArtProfile;

    UPROPERTY(Config, EditAnywhere, Category="关卡序列", meta=(DisplayName="默认关卡序列"))
    TSoftObjectPtr<UGridCampaignSequence> CampaignSequence;

    Sokoban::FInventory StartingInventory() const;
    Sokoban::FResourceContext MakeResourceContext(Sokoban::FInventory Actual) const;
    Sokoban::FSearchLimits SearchLimits() const;
};
