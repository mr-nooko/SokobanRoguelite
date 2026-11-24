#include "GridScene.h"
#include "GridLevelAsset.h"
#include "GridProjectSettings.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridArtOverride, "Sokoban.Preview.ArtProfileOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridArtOverride::RunTest(const FString&)
{
    auto* Draft = NewObject<UGridLevelAsset>();
    Draft->RoomId = TEXT("ArtTest");
    Draft->Size = {3, 3};
    Draft->Start = {0, 0};
    Draft->Goal = {2, 2};
    auto& Wood = Draft->Boxes.AddDefaulted_GetRef();
    Wood.Id = TEXT("Wood");
    Wood.Cell = {1, 0};
    auto& Jelly = Draft->Boxes.AddDefaulted_GetRef();
    Jelly.Id = TEXT("Jelly");
    Jelly.Cell = {1, 1};
    Jelly.bEject = true;
    Draft->Walls.Add({2, 0});
    Draft->VisualProfile = NewObject<UGridVisualProfile>(Draft);
    auto& Floor = Draft->VisualProfile->Slots.FindChecked(EGridVisualRole::Floor);
    Floor.Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!TestNotNull(TEXT("Replacement floor surface"), Floor.Material.Get())) { return false; }

    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Transient game world"), World)) { return false; }
    auto* Scene = World->SpawnActor<AGridScene>();
    if (!TestNotNull(TEXT("Grid scene"), Scene))
    {
        World->DestroyWorld(false);
        return false;
    }

    const auto* ProjectProfile = AGridScene::GetDefaultArtProfile();
    TestTrue(TEXT("Default board art comes from the saved configuration"),
        ProjectProfile && FSoftObjectPath(ProjectProfile) == GetDefault<UGridProjectSettings>()->BoardArtProfile.ToSoftObjectPath());
    if (ProjectProfile)
    {
        const auto* FloorSlot = ProjectProfile->FindSlot(EGridVisualRole::Floor);
        TestTrue(TEXT("The continuous floor material is assigned in the editor asset"), FloorSlot && FloorSlot->Material);
        for (const auto Role : {EGridVisualRole::Wall, EGridVisualRole::Rubble, EGridVisualRole::Crate,
                               EGridVisualRole::SpikeCrate, EGridVisualRole::ExplosiveCrate, EGridVisualRole::EjectCrate})
        {
            const auto* Slot = ProjectProfile->FindSlot(Role);
            TestTrue(TEXT("Board role has an editable mesh and material"), Slot && Slot->Mesh && Slot->Material);
        }
    }
    FGridVisualSlot ProjectTransform;
    ProjectTransform.Offset = {12, 4, 8};
    ProjectTransform.ScaleMultiplier = {2, 3, 4};
    FGridVisualSlot LocalTransform;
    TestEqual(TEXT("An empty local slot inherits the project offset"), FGridVisualSlot::ResolveOffset(&LocalTransform, &ProjectTransform),
        ProjectTransform.Offset);
    TestEqual(TEXT("An empty local slot inherits the project scale"), FGridVisualSlot::ResolveScale(&LocalTransform, &ProjectTransform),
        ProjectTransform.ScaleMultiplier);
    LocalTransform.OffsetMode = EGridTransformOverride::Override;
    LocalTransform.ScaleMode = EGridTransformOverride::Override;
    TestEqual(TEXT("Explicit zero offset overrides the project"), FGridVisualSlot::ResolveOffset(&LocalTransform, &ProjectTransform),
        FVector::ZeroVector);
    TestEqual(TEXT("Explicit unit scale overrides the project"), FGridVisualSlot::ResolveScale(&LocalTransform, &ProjectTransform),
        FVector::OneVector);
    LocalTransform.OffsetMode = EGridTransformOverride::Compatibility;
    LocalTransform.Offset = {1, 2, 3};
    TestEqual(TEXT("Existing nondefault local values still apply"), FGridVisualSlot::ResolveOffset(&LocalTransform, &ProjectTransform),
        LocalTransform.Offset);
    const FText MixedLabel = GetGridBoxVisualLabel({1, 0}, true, true);
    TestTrue(TEXT("A combined box has one visual role and both secondary modules in the label"),
        GetGridBoxVisualRole({1, 0}, true, true) == EGridVisualRole::ExplosiveCrate &&
        MixedLabel.ToString().Contains(TEXT("尖刺")) && MixedLabel.ToString().Contains(TEXT("弹射")));

    const auto Level = Draft->ToLevel();
    Scene->SetVisualProfile(Draft->VisualProfile);
    Scene->Present(Sokoban::FRules::Start(Level, {}).State);
    auto* Ground = Scene->FindComponentByClass<UProceduralMeshComponent>();
    const auto* Surface = Ground ? Ground->GetProcMeshSection(0) : nullptr;
    TestTrue(TEXT("Connected floor shares the 16 vertices of a 3 by 3 board"),
        Surface && Surface->ProcVertexBuffer.Num() == 16 && Surface->ProcIndexBuffer.Num() == 54);
    const auto* Lines = Ground ? Ground->GetProcMeshSection(1) : nullptr;
    const auto* Walls = Ground ? Ground->GetProcMeshSection(2) : nullptr;
    TestTrue(TEXT("Internal grid lines sit above the ground without splitting the surface"),
        Lines && Lines->ProcVertexBuffer.Num() == 96 && Surface &&
        Lines->ProcVertexBuffer[0].Position.Z > Surface->ProcVertexBuffer[0].Position.Z);
    TestTrue(TEXT("A full 3 by 3 board has twelve exterior wall segments"),
        Walls && Walls->ProcVertexBuffer.Num() == 12 * 20);
    TestTrue(TEXT("Floor uses the configured material"), Ground && Ground->GetMaterial(0) == Floor.Material);
    TestEqual(TEXT("Art reference does not alter logical goal"), Level.Goal, FIntPoint(2, 2));

    auto WithHole = Level;
    WithHole.VoidCells.Add({1, 2});
    Scene->Present(Sokoban::FRules::Start(WithHole, {}).State);
    Surface = Ground ? Ground->GetProcMeshSection(0) : nullptr;
    TestTrue(TEXT("Changing the cutout at the same board size removes two triangles"),
        Surface && Surface->ProcIndexBuffer.Num() == 48);

    Walls = Ground ? Ground->GetProcMeshSection(2) : nullptr;
    TestTrue(TEXT("A cutout on the same size board rebuilds the perimeter"),
        Walls && Walls->ProcVertexBuffer.Num() == 14 * 20);
    auto InteriorHole = Level;
    InteriorHole.Boxes.RemoveAt(1);
    InteriorHole.VoidCells.Add({1, 1});
    Scene->Present(Sokoban::FRules::Start(InteriorHole, {}).State);
    Walls = Ground ? Ground->GetProcMeshSection(2) : nullptr;
    TestTrue(TEXT("An interior hole is also surrounded by walls"),
        Walls && Walls->ProcVertexBuffer.Num() == 16 * 20);

    Scene->SetVisualProfile(nullptr);
    Scene->Present(Sokoban::FRules::Start(Level, {}).State);
    TestTrue(TEXT("Clearing the profile restores the configured project stone"),
        Ground && Ground->GetMaterial(0) && Ground->GetMaterial(0)->GetFName() == FName(TEXT("MI_BoardStoneLight")));
    TArray<UStaticMeshComponent*> Meshes;
    Scene->GetComponents(Meshes);
    const auto HasSurface = [&](FName MeshName, FName MaterialName) {
        return Meshes.ContainsByPredicate([&](const UStaticMeshComponent* Component) {
            const UStaticMesh* Mesh = Component->GetStaticMesh();
            const auto* Material = Component->GetMaterial(0);
            return Component->IsVisible() && Mesh && Material &&
                Mesh->GetFName() == MeshName && Material->GetFName() == MaterialName;
        });
    };
    TestTrue(TEXT("Ordinary crate uses the slatted mesh and wood texture"), HasSurface(TEXT("SM_SlatCrate"), TEXT("M_BoardSlat")));
    TestTrue(TEXT("Wall shares the board stone material family"), HasSurface(TEXT("SM_BoardStoneBlock"), TEXT("MI_BoardStoneWall")));
    TestTrue(TEXT("Eject crate uses rounded jelly mesh and material"), HasSurface(TEXT("SM_JellyCrate"), TEXT("M_BoardJelly")));
    auto WithEjectBomb = Level;
    WithEjectBomb.Boxes[1].bExplodes = true;
    Scene->Present(Sokoban::FRules::Start(WithEjectBomb, {}).State);
    Scene->GetComponents(Meshes);
    TestTrue(TEXT("An eject bomb still uses the configured jelly base"), HasSurface(TEXT("SM_JellyCrate"), TEXT("M_BoardJelly")));
    Scene->SetVisualProfile(Draft->VisualProfile);
    Scene->Present(Sokoban::FRules::Start(Level, {}).State);
    Scene->GetComponents(Meshes);
    const auto FindCrate = [&] {
        const auto* Found = Meshes.FindByPredicate([](const UStaticMeshComponent* Component) {
            const UStaticMesh* Mesh = Component->GetStaticMesh();
            return Component->IsVisible() && Mesh && Mesh->GetFName() == FName(TEXT("SM_SlatCrate"));
        });
        return Found ? *Found : nullptr;
    };
    if (auto* Crate = FindCrate(); TestNotNull(TEXT("Slatted mesh is present before shifting the floor"), Crate) &&
        Ground && Ground->GetProcMeshSection(0))
    {
        const double CrateZ = Crate->GetComponentLocation().Z;
        const double GroundZ = Ground->GetProcMeshSection(0)->ProcVertexBuffer[0].Position.Z;
        const auto* ProjectFloor = ProjectProfile ? ProjectProfile->FindSlot(EGridVisualRole::Floor) : nullptr;
        Floor.OffsetMode = EGridTransformOverride::Override;
        Floor.Offset.Z = FGridVisualSlot::ResolveOffset(nullptr, ProjectFloor).Z + 120.0;
        Scene->Present(Sokoban::FRules::Start(Level, {}).State);
        Scene->GetComponents(Meshes);
        Crate = FindCrate();
        const auto* RaisedSurface = Ground->GetProcMeshSection(0);
        TestTrue(TEXT("Raising the floor also raises the crate by the same amount"),
            Crate && RaisedSurface && FMath::IsNearlyEqual(Crate->GetComponentLocation().Z - CrateZ, 120.0) &&
            FMath::IsNearlyEqual(RaisedSurface->ProcVertexBuffer[0].Position.Z - GroundZ, 120.0));
    }
    World->DestroyWorld(false);
    return true;
}
#endif
