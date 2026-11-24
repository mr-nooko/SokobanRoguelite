#include "GridScene.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
constexpr float WalkSpeed = 325.f; // World units per second.

UTexture2D* MakeScoutTexture()
{
    constexpr int32 Size = 128;
    TArray64<FColor> Pixels;
    Pixels.SetNumUninitialized(Size * Size);
    for (int32 Y = 0; Y < Size; ++Y)
    {
        for (int32 X = 0; X < Size; ++X)
        {
            const int32 Band = (X + 2 * Y) % 64;
            const bool bSeam = (X % 32 == 0) || (Y % 32 == 0);
            Pixels[Y * Size + X] = bSeam ? FColor(8, 37, 53)
                : Band < 7 ? FColor(234, 174, 74) : FColor(35, 145, Band < 30 ? 162 : 190);
        }
    }
    const TConstArrayView64<uint8> Data(reinterpret_cast<const uint8*>(Pixels.GetData()),
                                       Pixels.Num() * sizeof(FColor));
    UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None, Data);
    if (Texture)
    {
        Texture->SRGB = true;
        Texture->UpdateResource();
    }
    return Texture;
}
}

void AGridScene::BeginPlay()
{
    Super::BeginPlay();
    PlayerTexture = MakeScoutTexture();
    if (!PlayerTexture) { return; }
    for (int32 Index = 0; Index < PlayerMesh->GetNumMaterials(); ++Index)
    {
        if (auto* Material = PlayerMesh->CreateDynamicMaterialInstance(Index))
        {
            Material->SetTextureParameterValue(TEXT("Base Texture"), PlayerTexture);
        }
    }
}

void AGridScene::PresentPlayer(FIntPoint Target, bool bAnimateStep)
{
    // Same origin, cell size and floor height as the scene meshes.
    const FVector WorldTarget = PlayerGridPosition(Target);
    const FIntPoint Delta = Target - PlayerCell;
    if (bAnimateStep && bHasPlayerCell && Sokoban::FRules::IsCardinal(Delta) && WalkCycle)
    {
        StepStart = PlayerMesh->GetRelativeLocation();
        StepEnd = WorldTarget;
        StepElapsed = 0.f;
        PlayerMesh->SetRelativeRotation(FRotator(0, FVector(Delta.X, Delta.Y, 0).Rotation().Yaw - 90.f, 0));
        if (!bWalkingAnimation)
        {
            PlayerMesh->PlayAnimation(WalkCycle, true);
            PlayerMesh->SetPosition(WalkPosition, false);
            bWalkingAnimation = true;
        }
        else
        {
            PlayerMesh->Play(true);
            PlayerMesh->SetPosition(WalkPosition, false);
        }
        bStepping = true;
        SetActorTickEnabled(true);
    }
    else if (!bHasPlayerCell || Target != PlayerCell || !bAnimateStep)
    {
        PlayerMesh->SetRelativeLocation(WorldTarget);
        if (!bHasPlayerCell || Target != PlayerCell)
        {
            WalkPosition = 0.f;
            PlayIdle();
        }
    }
    bHasPlayerCell = true;
    PlayerCell = Target;
}

void AGridScene::PlayIdle()
{
    bWalkingAnimation = false;
    if (IdlePose) { PlayerMesh->PlayAnimation(IdlePose, true); }
}

void AGridScene::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bPlayingOutcome)
    {
        TickOutcome(DeltaSeconds);
        return;
    }
    if (!bStepping && !BlastCenters.IsEmpty())
    {
        TickBlast(DeltaSeconds);
        return;
    }
    if (!bStepping)
    {
        IdleDelay -= DeltaSeconds;
        if (IdleDelay <= 0.f)
        {
            PlayIdle();
            SetActorTickEnabled(false);
        }
        return;
    }
    StepElapsed += DeltaSeconds;
    const double Distance = FVector::Distance(StepStart, StepEnd);
    const double Alpha = Distance > UE_SMALL_NUMBER ? FMath::Clamp(StepElapsed * WalkSpeed / Distance, 0.0, 1.0) : 1.0;
    PlayerMesh->SetRelativeLocation(FMath::Lerp(StepStart, StepEnd, Alpha));
    if (Alpha >= 1.f)
    {
        WalkPosition = PlayerMesh->GetPosition();
        PlayerMesh->Stop();
        bStepping = false;
        if (PresentedOutcome != Sokoban::EOutcome::Running && BlastCenters.IsEmpty()) { BeginOutcome(); }
        else { IdleDelay = 0.08f; }
    }
}
