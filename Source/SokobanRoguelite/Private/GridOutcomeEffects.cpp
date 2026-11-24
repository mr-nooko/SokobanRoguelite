#include "GridScene.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
constexpr float FallDuration = 0.57f;
constexpr float EscapeDuration = 0.95f;
constexpr float EscapeHideTime = 0.34f;
}

void AGridScene::ResetOutcome()
{
    if (bCapturedOutcomePose)
    {
        PlayerMesh->SetRelativeScale3D(OutcomeScale);
        PlayerMesh->SetRelativeRotation(OutcomeRotation);
    }
    ClearGoalParticles();
    ClearBlast();
    PlayerMesh->SetVisibility(true);
    PresentedOutcome = Sokoban::EOutcome::Running;
    bPlayingOutcome = false;
    bCapturedOutcomePose = false;
    bStepping = false;
    bHasPlayerCell = false;
    SetActorTickEnabled(false);
}

void AGridScene::QueueBlast(const Sokoban::FTurnResult& Result)
{
    for (const auto& Event : Result.Events)
    {
        if (Event.Kind == Sokoban::EEvent::Blast) { BlastCenters.Append(Event.BlastCenters); }
    }
    if (!BlastCenters.IsEmpty()) { SetActorTickEnabled(true); }
}

void AGridScene::TickBlast(float DeltaSeconds)
{
    constexpr float Duration = 0.32f;
    if (BlastTime == 0.f) { PlayGridSound(EGridSoundCue::Blast, PlayerGridPosition(BlastCenters[0])); }
    BlastTime += DeltaSeconds;
    if (BlastTime >= Duration)
    {
        ClearBlast();
        if (PresentedOutcome != Sokoban::EOutcome::Running) { BeginOutcome(); }
        else { IdleDelay = 0.08f; }
        return;
    }

    const float Alpha = BlastTime / Duration;
    int32 Used = 0;
    for (const FIntPoint Cell : BlastCenters)
    {
        const float Radius = FMath::Lerp(0.15f, 1.9f, FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.f));
        const float Height = FMath::Lerp(0.23f, 0.025f, Alpha);
        AddMesh(BlastMeshes, Used, EMesh::Sphere, EColor::Beam, EntityAt(Cell, 25.0),
            FVector(Radius, Radius, Height));
    }
}

void AGridScene::ClearBlast()
{
    BlastCenters.Reset();
    BlastTime = 0.f;
    for (auto& Mesh : BlastMeshes)
    {
        if (IsValid(Mesh)) { Mesh->SetVisibility(false); }
    }
}

void AGridScene::BeginOutcome()
{
    OutcomeTime = 0.f;
    OutcomeOrigin = PlayerMesh->GetRelativeLocation();
    OutcomeScale = PlayerMesh->GetRelativeScale3D();
    OutcomeRotation = PlayerMesh->GetRelativeRotation().Quaternion();
    bCapturedOutcomePose = true;
    bPlayingOutcome = true;
    PlayerMesh->Stop();
    if (PresentedOutcome == Sokoban::EOutcome::Won)
    {
        PlayGridSound(EGridSoundCue::Goal, OutcomeOrigin);
        SpawnGoalParticles();
    }
    else if (PresentedOutcome == Sokoban::EOutcome::Dead)
    {
        PlayGridSound(EGridSoundCue::Death, OutcomeOrigin);
    }
    SetActorTickEnabled(true);
}

void AGridScene::TickOutcome(float DeltaSeconds)
{
    OutcomeTime += DeltaSeconds;
    if (PresentedOutcome == Sokoban::EOutcome::Dead)
    {
        const float Alpha = FMath::Clamp(OutcomeTime / FallDuration, 0.f, 1.f);
        const float Fall = FMath::InterpEaseIn(0.f, 1.f, Alpha, 2.f);
        const float Angle = Fall * HALF_PI;
        const FQuat Tilt(FVector::XAxisVector, Angle);
        const FVector Back = -OutcomeRotation.RotateVector(FVector::YAxisVector);
        PlayerMesh->SetRelativeRotation(OutcomeRotation * Tilt);
        PlayerMesh->SetRelativeLocation(OutcomeOrigin + Back * (Fall * 15.f) +
            FVector(0, 0, FMath::Sin(Angle) * 17.f));
        if (Alpha < 1.f) { return; }
    }
    else
    {
        const float Fade = FMath::Clamp(OutcomeTime / EscapeHideTime, 0.f, 1.f);
        PlayerMesh->SetRelativeScale3D(OutcomeScale * (1.f - 0.98f * Fade));
        PlayerMesh->SetVisibility(Fade < 1.f);
        const float Alpha = FMath::Clamp(OutcomeTime / EscapeDuration, 0.f, 1.f);
        for (int32 Index = 0; Index < GoalParticles.Num(); ++Index)
        {
            const FVector Velocity = GoalParticleVelocities[Index];
            const float Delay = (Index % 6) * 0.025f;
            const float Age = FMath::Max(0.f, OutcomeTime - Delay);
            const float Opacity = OutcomeTime < Delay ? 0.f : FMath::Square(1.f - Alpha);
            const FVector Start = OutcomeOrigin + FVector(0, 0, 24.f + (Index % 9) * 9.f);
            GoalParticles[Index]->SetRelativeLocation(Start + Velocity * Age + FVector(0, 0, 42.f * Age * Age));
            GoalParticleMaterials[Index]->SetScalarParameterValue(TEXT("Fade"), Opacity);
        }
        if (Alpha < 1.f) { return; }
        ClearGoalParticles();
    }
    bPlayingOutcome = false;
    SetActorTickEnabled(false);
}

void AGridScene::SpawnGoalParticles()
{
    UMaterialInterface* Spark = VisualProfile && VisualProfile->GoalParticleMaterial
        ? VisualProfile->GoalParticleMaterial.Get() : DefaultVisualProfile && DefaultVisualProfile->GoalParticleMaterial
        ? DefaultVisualProfile->GoalParticleMaterial.Get() : GoalSparkMaterial.Get();
    if (!Spark) { return; }
    constexpr int32 Count = 36;
    FRandomStream Random(752);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        auto* Billboard = NewObject<UMaterialBillboardComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
        auto* Material = UMaterialInstanceDynamic::Create(Spark, Billboard);
        Material->SetVectorParameterValue(TEXT("Glow"),
            Index % 3 == 0 ? FLinearColor(4.f, 2.2f, .55f) : FLinearColor(.3f, 3.4f, 1.9f));
        Material->SetScalarParameterValue(TEXT("Fade"), 0.f);
        Billboard->SetupAttachment(GetRootComponent());
        const float Radius = Random.FRandRange(4.f, 11.f);
        Billboard->AddElement(Material, nullptr, false, Radius, Radius, nullptr);
        Billboard->RegisterComponent();
        GoalParticles.Add(Billboard);
        GoalParticleMaterials.Add(Material);
        const float Heading = (2.f * PI * Index) / Count;
        const float Speed = Random.FRandRange(30.f, 125.f);
        GoalParticleVelocities.Add(FVector(FMath::Cos(Heading) * Speed, FMath::Sin(Heading) * Speed,
                                         Random.FRandRange(28.f, 120.f)));
    }
}

void AGridScene::ClearGoalParticles()
{
    for (auto& Particle : GoalParticles)
    {
        if (IsValid(Particle)) { Particle->DestroyComponent(); }
    }
    GoalParticles.Reset();
    GoalParticleMaterials.Reset();
    GoalParticleVelocities.Reset();
}
