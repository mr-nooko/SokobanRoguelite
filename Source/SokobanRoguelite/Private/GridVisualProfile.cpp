#include "GridVisualProfile.h"

UGridVisualProfile::UGridVisualProfile()
{
    for (EGridVisualRole Role : {
        EGridVisualRole::Floor, EGridVisualRole::Wall, EGridVisualRole::Rubble, EGridVisualRole::Crate,
        EGridVisualRole::SpikeCrate, EGridVisualRole::ExplosiveCrate, EGridVisualRole::Spike,
        EGridVisualRole::ContactEnemy, EGridVisualRole::BeamEnemy, EGridVisualRole::Goal, EGridVisualRole::Beam, EGridVisualRole::EjectCrate })
    {
        Slots.Add(Role);
    }
    for (const EGridSoundCue Cue : {
        EGridSoundCue::Step, EGridSoundCue::Push, EGridSoundCue::PushRejected,
        EGridSoundCue::EnemyKilled, EGridSoundCue::Blast, EGridSoundCue::Restored,
        EGridSoundCue::Death, EGridSoundCue::Goal })
    {
        Sounds.Add(Cue);
    }
}

EGridVisualRole GetGridBoxVisualRole(FIntPoint Spike, bool bExplodes, bool bEject)
{
    if (bExplodes) { return EGridVisualRole::ExplosiveCrate; }
    if (Spike != FIntPoint::ZeroValue) { return EGridVisualRole::SpikeCrate; }
    return bEject ? EGridVisualRole::EjectCrate : EGridVisualRole::Crate;
}

FText GetGridBoxVisualLabel(FIntPoint Spike, bool bExplodes, bool bEject)
{
    const EGridVisualRole Role = GetGridBoxVisualRole(Spike, bExplodes, bEject);
    FString Label = StaticEnum<EGridVisualRole>()->GetDisplayNameTextByValue(static_cast<int64>(Role)).ToString();
    if (Spike != FIntPoint::ZeroValue && Role != EGridVisualRole::SpikeCrate) { Label += TEXT(" · 尖刺"); }
    if (bEject && Role != EGridVisualRole::EjectCrate) { Label += TEXT(" · 弹射"); }
    return FText::FromString(Label);
}

namespace
{
bool UsesLocalTransform(EGridTransformOverride Mode, const FVector& Value, const FVector& Neutral)
{
    return Mode == EGridTransformOverride::Override ||
        (Mode == EGridTransformOverride::Compatibility && Value != Neutral);
}
}

FVector FGridVisualSlot::ResolveOffset(const FGridVisualSlot* Local, const FGridVisualSlot* Default)
{
    return Local && UsesLocalTransform(Local->OffsetMode, Local->Offset, FVector::ZeroVector)
        ? Local->Offset : Default ? Default->Offset : FVector::ZeroVector;
}

FVector FGridVisualSlot::ResolveScale(const FGridVisualSlot* Local, const FGridVisualSlot* Default)
{
    return Local && UsesLocalTransform(Local->ScaleMode, Local->ScaleMultiplier, FVector::OneVector)
        ? Local->ScaleMultiplier : Default ? Default->ScaleMultiplier : FVector::OneVector;
}
