#include "GridScene.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

void AGridScene::PlayGridSound(EGridSoundCue Cue, FVector LocalPosition) const
{
    if (!GetWorld() || !GetWorld()->IsGameWorld()) { return; }
    const auto* Sound = VisualProfile ? VisualProfile->Sounds.Find(Cue) : nullptr;
    if ((!Sound || !Sound->Get()) && DefaultVisualProfile) { Sound = DefaultVisualProfile->Sounds.Find(Cue); }
    if (Sound && Sound->Get())
    {
        UGameplayStatics::PlaySoundAtLocation(this, Sound->Get(), GetActorTransform().TransformPosition(LocalPosition));
    }
}

void AGridScene::PlayTurnSounds(const Sokoban::FTurnResult& Result)
{
    using Sokoban::EEvent;
    TSet<EGridSoundCue> Played;
    const bool bPushed = Result.Events.ContainsByPredicate([](const Sokoban::FEvent& Event) {
        return Event.Kind == EEvent::BoxMoved;
    });
    for (const auto& Event : Result.Events)
    {
        EGridSoundCue Cue;
        switch (Event.Kind)
        {
            case EEvent::PlayerMoved:
                if (bPushed) { continue; }
                Cue = EGridSoundCue::Step;
                break;
            case EEvent::BoxMoved: Cue = EGridSoundCue::Push; break;
            case EEvent::PushRejected: Cue = EGridSoundCue::PushRejected; break;
            case EEvent::EnemyKilled: Cue = EGridSoundCue::EnemyKilled; break;
            case EEvent::Blast: continue; // The scene plays one sound when the entire wave starts.
            case EEvent::Restored: Cue = EGridSoundCue::Restored; break;
            default: continue;
        }
        if (!Played.Contains(Cue))
        {
            Played.Add(Cue);
            PlayGridSound(Cue, PlayerGridPosition(Event.Cell));
        }
    }
}
