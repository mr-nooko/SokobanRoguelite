#pragma once

#include "GridRules.h"

namespace GridPreview
{
inline constexpr int32 RoomCount = 6;

struct FLesson
{
    const TCHAR* Title;
    const TCHAR* Hint;
};

Sokoban::FLevel MakeRoom(int32 Index);
const FLesson& GetLesson(int32 Index);
}
