#pragma once

#include "CoreMinimal.h"
#include "GridRoomReward.generated.h"

USTRUCT(BlueprintType)
struct FGridRoomReward
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="通关赠药", meta=(DisplayName="大力士药丸", ClampMin="0"))
    int32 Strength = 0;

    UPROPERTY(EditAnywhere, Category="通关赠药", meta=(DisplayName="时空回溯药丸", ClampMin="0"))
    int32 Undo = 0;
};
