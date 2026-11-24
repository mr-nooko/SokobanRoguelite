#pragma once

#include "Commandlets/Commandlet.h"
#include "SokobanGenerateCommandlet.generated.h"

UCLASS()
class USokobanGenerateCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    virtual int32 Main(const FString& Params) override;
};
