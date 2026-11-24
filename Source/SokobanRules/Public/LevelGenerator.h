#pragma once

#include "GridRules.h"

namespace Sokoban
{
class SOKOBANRULES_API FTemplateLevelGenerator final : public ILevelGenerator
{
public:
    FGenerationResult Generate(const FGenerationRequest& Request) const override;
};
}
