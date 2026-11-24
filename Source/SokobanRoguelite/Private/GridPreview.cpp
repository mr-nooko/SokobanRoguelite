#include "GridPreview.h"

const GridPreview::FLesson& GridPreview::GetLesson(int32 Index)
{
    static const FLesson Lessons[] = {
        {TEXT("移动与终点"), TEXT("用 WASD 移动。墙和挖空格无法通行，走到绿色终点即可过关。")},
        {TEXT("用箱子挡射线"), TEXT("先向上，再向右推箱子。普通箱能挡住光束，从箱子背光的一侧绕过去。")},
        {TEXT("尖刺侧扫"), TEXT("连续向右推尖刺箱。向上的尖刺会随箱子横扫敌人，别直接碰到敌人。")},
        {TEXT("爆炸与碎墙"), TEXT("向右推爆炸箱，把它推向碎墙。爆炸能炸碎墙和击杀敌人，普通墙会留下。")},
        {TEXT("弹射箱"), TEXT("向右推一次。蓝色弹射箱沿推动方向一直滑到墙前，玩家只前进一格。")},
        {TEXT("两种药丸"), TEXT("按 E 吃大力士药丸，下一步可连推两箱。\n按 Z 选回溯步数，回车消耗一枚时空回溯药丸。死亡后不能回溯。")}
    };
    static_assert(UE_ARRAY_COUNT(Lessons) == RoomCount);
    return Lessons[FMath::Clamp(Index, 0, RoomCount - 1)];
}

Sokoban::FLevel GridPreview::MakeRoom(int32 Index)
{
    Sokoban::FLevel Level;
    Level.RoomId = FName(*FString::Printf(TEXT("Tutorial%02d"), Index + 1));
    Level.Size = {8, 6};
    Level.Start = {1, 4};
    Level.Goal = {6, 1};

    switch (Index)
    {
        case 0:
            Level.Walls.Add({3, 3});
            Level.Walls.Add({3, 2});
            Level.VoidCells.Add({0, 0});
            break;
        case 1:
            Level.Boxes.Add({TEXT("Shield"), {3, 3}});
            Level.Enemies.Add({TEXT("Laser"), {6, 3}, {-1, 0}});
            break;
        case 2:
            Level.Boxes.Add({TEXT("Spikes"), {2, 4}, {0, -1}});
            Level.Enemies.Add({TEXT("Guard"), {3, 3}});
            Level.Walls.Add({1, 2});
            Level.Walls.Add({2, 2});
            break;
        case 3:
        {
            Sokoban::FBox Bomb{TEXT("Bomb"), {2, 4}};
            Bomb.bExplodes = true;
            Level.Boxes.Add(Bomb);
            Level.Rubble.Add({3, 4});
            Level.Goal = {6, 4};
            Level.Enemies.Add({TEXT("Guard"), {3, 3}});
            Level.Walls.Add({1, 3});
            break;
        }
        case 4:
        {
            Sokoban::FBox Slider{TEXT("Slider"), {2, 4}};
            Slider.bEject = true;
            Level.Boxes.Add(Slider);
            Level.Walls.Add({7, 4});
            break;
        }
        case 5:
            Level.Boxes.Add({TEXT("FirstCrate"), {2, 4}});
            Level.Boxes.Add({TEXT("SecondCrate"), {3, 4}});
            break;
    }
    return Level;
}
