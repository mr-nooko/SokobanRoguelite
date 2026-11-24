#pragma once

#include "GameFramework/SaveGame.h"
#include "GridRuntimeDraftSave.generated.h"

UCLASS()
class UGridRuntimeDraftSave : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY() int64 Revision = 0;
    UPROPERTY() TArray<uint8> Layout;
};
