#include "GridScene.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridOutcomePresentation, "Sokoban.Preview.OutcomePresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridOutcomePresentation::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient game world"), World)) { return false; }
    AGridScene* Scene = World->SpawnActor<AGridScene>();
    if (!TestNotNull(TEXT("Preview scene"), Scene))
    {
        World->DestroyWorld(false);
        return false;
    }
    auto* Manny = Scene->FindComponentByClass<USkeletalMeshComponent>();
    if (!TestNotNull(TEXT("Manny"), Manny))
    {
        World->DestroyWorld(false);
        return false;
    }
    Sokoban::FLevel Level;
    Level.RoomId = TEXT("OutcomeTest");
    Level.Size = {3, 3};
    Level.Start = {1, 1};
    Level.Goal = {1, 2};
    Sokoban::FState State = Sokoban::FRules::Start(Level, {}).State;
    Scene->Present(State);
    const FQuat Upright = Manny->GetRelativeRotation().Quaternion();
    State.Outcome = Sokoban::EOutcome::Dead;
    Scene->Present(State);
    TestTrue(TEXT("Fall locks input during presentation"), Scene->IsStepping());
    Scene->Tick(1.f);
    TestTrue(TEXT("Body stays down and visible"), Manny->IsVisible() &&
        FMath::Abs(Manny->GetRelativeRotation().Quaternion().GetUpVector().Z) < .05);
    TestFalse(TEXT("Fall finishes"), Scene->IsStepping());
    Scene->ResetOutcome();
    TestTrue(TEXT("Reset restores rotation"), Manny->GetRelativeRotation().Quaternion().Equals(Upright));

    State.Outcome = Sokoban::EOutcome::Won;
    State.Player = Level.Goal;
    Scene->Present(State);
    TArray<UMaterialBillboardComponent*> Particles;
    Scene->GetComponents(Particles);
    TestEqual(TEXT("Goal spawns imported spark sprites"), Particles.Num(), 36);
    Scene->Tick(.4f);
    TestFalse(TEXT("Player is hidden during the burst"), Manny->IsVisible());
    TestTrue(TEXT("Burst is still finishing"), Scene->IsStepping());
    Scene->Tick(1.f);
    Particles.Reset();
    Scene->GetComponents(Particles);
    TestTrue(TEXT("Expired particles are removed"), Particles.IsEmpty());
    Scene->ResetOutcome();
    TestTrue(TEXT("Reset makes Manny visible again"), Manny->IsVisible());

    Scene->ResetOutcome();
    Sokoban::FTurnResult Blast;
    auto& Wave = Blast.Events.AddDefaulted_GetRef();
    Wave.Kind = Sokoban::EEvent::Blast;
    Wave.BlastCenters = {{1, 0}, {1, 1}};
    Scene->QueueBlast(Blast);
    TestTrue(TEXT("A stationary chained blast locks input"), Scene->IsStepping());
    Scene->Tick(0.1f);
    TestTrue(TEXT("The blast is still playing in the same action"), Scene->IsStepping());
    Scene->Tick(0.3f);
    TestFalse(TEXT("Input unlocks after all blast centers finish"), Scene->IsStepping());
    World->DestroyWorld(false);
    return true;
}
#endif
