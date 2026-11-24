#include "GridCampaignSequence.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "GridCampaignSequence"

namespace
{
bool CheckPositions(int32 Total, const TArray<FGridPlacedRoom>& Manual, const TArray<FGridGenerationBand>& Bands, FText& Error)
{
    if (Total < 1 || Total > 1000)
    {
        Error = LOCTEXT("TotalInvalid", "关卡序列的总关数须为 1 至 1000。");
        return false;
    }

    TSet<int32> Positions;
    for (const auto& Entry : Manual)
    {
        if (Entry.Position < 1 || Entry.Position > Total)
        {
            Error = FText::Format(LOCTEXT("OutsideSequence", "手工关位置 {0} 超出总关数 {1}。"), Entry.Position, Total);
            return false;
        }
        if (Positions.Contains(Entry.Position))
        {
            Error = FText::Format(LOCTEXT("DuplicatePosition", "第 {0} 关已安排手工关。"), Entry.Position);
            return false;
        }
        Positions.Add(Entry.Position);
    }

    TBitArray<> Assigned(false, Total);
    for (const auto& Band : Bands)
    {
        if (Band.FirstRoom < 1 || Band.LastRoom < Band.FirstRoom || Band.LastRoom > Total)
        {
            Error = FText::Format(LOCTEXT("InvalidBand", "生成范围 {0}～{1} 无效。请填写 1～{2} 内的正序关号。"),
                Band.FirstRoom, Band.LastRoom, Total);
            return false;
        }
        for (int32 Room = Band.FirstRoom; Room <= Band.LastRoom; ++Room)
        {
            if (Positions.Contains(Room))
            {
                Error = FText::Format(LOCTEXT("BandCoversManual", "第 {0} 关已安排手工关。请先确定手工关位置，再填写仅包含空位的生成范围。"), Room);
                return false;
            }
            if (Assigned[Room - 1])
            {
                Error = FText::Format(LOCTEXT("OverlappingBands", "第 {0} 关出现在多个生成难度分段。"), Room);
                return false;
            }
            Assigned[Room - 1] = true;
        }
    }
    return true;
}
}

bool UGridCampaignSequence::BuildSlots(TArray<FGridSequenceSlot>& Out, FText& Error) const
{
    Out.Reset();
    Error = FText::GetEmpty();
    if (TotalRooms == 0 && !Slots.IsEmpty() && PlacedRooms.IsEmpty() && GenerationBands.IsEmpty())
    {
        Out = Slots;
        return true;
    }
    if (!CheckPositions(TotalRooms, PlacedRooms, GenerationBands, Error)) { return false; }
    for (const auto& Entry : PlacedRooms)
    {
        if (!Entry.Level)
        {
            Error = FText::Format(LOCTEXT("MissingAsset", "第 {0} 关没有选择手工关卡资产。"), Entry.Position);
            return false;
        }
    }

    FGridSequenceSlot Generated;
    Generated.Source = EGridSequenceSource::Generated;
    Generated.Template = AutoRooms.Template;
    Generated.Complexity = AutoRooms.Complexity;
    Generated.MaxAttempts = AutoRooms.MaxAttempts;
    Generated.bAllowFallback = AutoRooms.bAllowFallback;
    Generated.Fallback = AutoRooms.Fallback;
    Generated.Hint = AutoRooms.Hint;
    Generated.ClearReward = AutoRooms.ClearReward;
    Out.Init(Generated, TotalRooms);
    for (const auto& Band : GenerationBands)
    {
        for (int32 Index = Band.FirstRoom - 1; Index < Band.LastRoom; ++Index)
        {
            Out[Index].Complexity = Band.Complexity;
            Out[Index].Difficulty = Band.Difficulty;
            Out[Index].MaxActions = Band.MaxActions;
            if (!Band.Hint.IsEmpty()) { Out[Index].Hint = Band.Hint; }
            if (Band.bOverrideClearReward) { Out[Index].ClearReward = Band.ClearReward; }
        }
    }
    for (const auto& Entry : PlacedRooms)
    {
        FGridSequenceSlot Handcrafted;
        Handcrafted.Level = Entry.Level;
        Handcrafted.Title = Entry.Title;
        Handcrafted.Hint = Entry.Hint;
        Out[Entry.Position - 1] = MoveTemp(Handcrafted);
    }
    return true;
}

#if WITH_EDITOR
void UGridCampaignSequence::PostLoad()
{
    Super::PostLoad();
    RefreshConfigurationStatus();
}

void UGridCampaignSequence::PreEditChange(FProperty* Property)
{
    PreviousTotalRooms = TotalRooms;
    PreviousPlacedRooms = PlacedRooms;
    PreviousGenerationBands = GenerationBands;
    bHasPreviousConfiguration = true;
    Super::PreEditChange(Property);
}

void UGridCampaignSequence::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    if (Event.ChangeType & EPropertyChangeType::Interactive)
    {
        Super::PostEditChangeProperty(Event);
        return;
    }
    FText Rejected;
    if (bHasPreviousConfiguration)
    {
        const FName Property = Event.GetPropertyName();
        const FName Member = Event.GetMemberPropertyName();
        const FName ManualField = GET_MEMBER_NAME_CHECKED(UGridCampaignSequence, PlacedRooms);
        const FName BandsField = GET_MEMBER_NAME_CHECKED(UGridCampaignSequence, GenerationBands);
        const auto AddedIndex = [&](FName Field, int32 Count) {
            const int32 Index = Event.GetArrayIndex(Field.ToString());
            return Index >= 0 && Index < Count ? Index : Count - 1;
        };
        if ((Member == ManualField || Property == ManualField) && PlacedRooms.Num() == PreviousPlacedRooms.Num() + 1 &&
            (Event.ChangeType & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::Duplicate)))
        {
            int32 Last = 0;
            for (const auto& Entry : PreviousPlacedRooms) { Last = FMath::Max(Last, Entry.Position); }
            PlacedRooms[AddedIndex(ManualField, PlacedRooms.Num())].Position = Last + 1;
        }
        if ((Member == BandsField || Property == BandsField) && GenerationBands.Num() == PreviousGenerationBands.Num() + 1 &&
            (Event.ChangeType & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::Duplicate)))
        {
            int32 Room = 1;
            for (; Room <= FMath::Clamp(TotalRooms, 0, 1000); ++Room)
            {
                if (!PlacedRooms.ContainsByPredicate([Room](const auto& Entry) { return Entry.Position == Room; }) &&
                    !PreviousGenerationBands.ContainsByPredicate([Room](const auto& Band) {
                        return Band.FirstRoom <= Room && Room <= Band.LastRoom;
                    })) { break; }
            }
            auto& Added = GenerationBands[AddedIndex(BandsField, GenerationBands.Num())];
            Added.FirstRoom = Added.LastRoom = Room;
        }
        if (Property == GET_MEMBER_NAME_CHECKED(UGridCampaignSequence, TotalRooms) &&
            TotalRooms >= 1 && TotalRooms <= 1000 && TotalRooms < PreviousTotalRooms)
        {
            GenerationBands.RemoveAll([this](const auto& Band) { return Band.FirstRoom > TotalRooms; });
            for (auto& Band : GenerationBands) { Band.LastRoom = FMath::Min(Band.LastRoom, TotalRooms); }
        }
        if (Property == GET_MEMBER_NAME_CHECKED(UGridCampaignSequence, TotalRooms) &&
            TotalRooms > PreviousTotalRooms && TotalRooms <= 1000 && PreviousTotalRooms > 0)
        {
            for (auto& Band : GenerationBands)
            {
                if (Band.LastRoom == PreviousTotalRooms) { Band.LastRoom = TotalRooms; }
            }
        }
        const bool bNewLegacy = PreviousTotalRooms == 0 && TotalRooms == 0 && PlacedRooms.IsEmpty() && GenerationBands.IsEmpty();
        FText OldError;
        const bool bOldValid = CheckPositions(PreviousTotalRooms, PreviousPlacedRooms, PreviousGenerationBands, OldError);
        if (!bNewLegacy && !CheckPositions(TotalRooms, PlacedRooms, GenerationBands, Rejected))
        {
            const bool bRemovingConflict = !bOldValid && (PlacedRooms.Num() < PreviousPlacedRooms.Num() ||
                GenerationBands.Num() < PreviousGenerationBands.Num());
            if (!bRemovingConflict)
            {
                TotalRooms = PreviousTotalRooms;
                PlacedRooms = MoveTemp(PreviousPlacedRooms);
                GenerationBands = MoveTemp(PreviousGenerationBands);
            }
        }
        PreviousPlacedRooms.Reset();
        PreviousGenerationBands.Reset();
        bHasPreviousConfiguration = false;
    }
    RefreshConfigurationStatus();
    if (!Rejected.IsEmpty())
    {
        ConfigurationStatus = FText::Format(LOCTEXT("EditRejected", "{0} 请先安排手工关，再填写生成分段。"), Rejected);
    }
    Super::PostEditChangeProperty(Event);
}

void UGridCampaignSequence::PostEditUndo()
{
    RefreshConfigurationStatus();
    Super::PostEditUndo();
}

void UGridCampaignSequence::RefreshConfigurationStatus()
{
    TArray<FGridSequenceSlot> Resolved;
    FText Error;
    ConfigurationStatus = BuildSlots(Resolved, Error)
        ? FText::Format(LOCTEXT("Ready", "共 {0} 关。请先安排手工关，再填写生成分段。其余空位使用默认设置。"), Resolved.Num())
        : Error;
}
#endif

#undef LOCTEXT_NAMESPACE
