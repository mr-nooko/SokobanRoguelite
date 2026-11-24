#include "GridScene.h"
#include "GridLevelAsset.h"
#include "GridProjectSettings.h"
#include "GridPreview.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"

#include "Camera/CameraComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr double CellSize = 125.0;
constexpr double FloorZ = 300.0;

struct FBoardTrimMesh
{
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;

    void AddQuad(FVector A, FVector B, FVector C, FVector D)
    {
        const int32 First = Vertices.Num();
        Vertices.Append({A, B, C, D});
        Indices.Append({First, First + 2, First + 1, First, First + 3, First + 2});
        const FVector Normal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
        Normals.Append({Normal, Normal, Normal, Normal});
    }

    void AddTop(double X0, double Y0, double X1, double Y1, double Z)
    {
        AddQuad({X0, Y0, Z}, {X1, Y0, Z}, {X1, Y1, Z}, {X0, Y1, Z});
    }

    void AddWall(double X0, double Y0, double X1, double Y1, double Base)
    {
        constexpr double Height = 68.0;
        const double Top = Base + Height;
        AddTop(X0, Y0, X1, Y1, Top);
        AddQuad({X0, Y0, Base}, {X1, Y0, Base}, {X1, Y0, Top}, {X0, Y0, Top});
        AddQuad({X1, Y0, Base}, {X1, Y1, Base}, {X1, Y1, Top}, {X1, Y0, Top});
        AddQuad({X1, Y1, Base}, {X0, Y1, Base}, {X0, Y1, Top}, {X1, Y1, Top});
        AddQuad({X0, Y1, Base}, {X0, Y0, Base}, {X0, Y0, Top}, {X0, Y1, Top});
    }

    void Apply(UProceduralMeshComponent& Mesh, int32 Section) const
    {
        Mesh.CreateMeshSection_LinearColor(Section, Vertices, Indices, Normals, {}, {}, {}, false);
    }
};

void BuildBoardTrim(UProceduralMeshComponent& Mesh, const Sokoban::FLevel& Level, double Z)
{
    constexpr double WallWidth = 12.0;
    constexpr double HalfLine = 2.5;
    const double LineZ = Z + 0.8;
    FBoardTrimMesh Lines;
    FBoardTrimMesh Walls;
    for (int32 Y = 0; Y < Level.Size.Y; ++Y)
    {
        for (int32 X = 0; X < Level.Size.X; ++X)
        {
            const FIntPoint Cell(X, Y);
            if (!Sokoban::FRules::IsInside(Level, Cell)) { continue; }
            const bool bNorth = Sokoban::FRules::IsInside(Level, Cell + FIntPoint(0, -1));
            const bool bSouth = Sokoban::FRules::IsInside(Level, Cell + FIntPoint(0, 1));
            const bool bWest = Sokoban::FRules::IsInside(Level, Cell + FIntPoint(-1, 0));
            const bool bEast = Sokoban::FRules::IsInside(Level, Cell + FIntPoint(1, 0));
            const double X0 = (X - .5) * CellSize;
            const double Y0 = (Y - .5) * CellSize;
            const double X1 = X0 + CellSize;
            const double Y1 = Y0 + CellSize;

            // Draw each edge from its own half of the cell and keep corners disjoint.
            if (bNorth) { Lines.AddTop(X0, Y0, X1, Y0 + HalfLine, LineZ); }
            else { Walls.AddWall(X0, Y0, X1, Y0 + WallWidth, Z); }
            if (bSouth) { Lines.AddTop(X0, Y1 - HalfLine, X1, Y1, LineZ); }
            else { Walls.AddWall(X0, Y1 - WallWidth, X1, Y1, Z); }
            const double LineStart = Y0 + (bNorth ? HalfLine : 0.0);
            const double LineEnd = Y1 - (bSouth ? HalfLine : 0.0);
            const double WallStart = Y0 + (bNorth ? 0.0 : WallWidth);
            const double WallEnd = Y1 - (bSouth ? 0.0 : WallWidth);
            if (bWest) { Lines.AddTop(X0, LineStart, X0 + HalfLine, LineEnd, LineZ); }
            else { Walls.AddWall(X0, WallStart, X0 + WallWidth, WallEnd, Z); }
            if (bEast) { Lines.AddTop(X1 - HalfLine, LineStart, X1, LineEnd, LineZ); }
            else { Walls.AddWall(X1 - WallWidth, WallStart, X1, WallEnd, Z); }
        }
    }
    Lines.Apply(Mesh, 1);
    Walls.Apply(Mesh, 2);
}

FRotator PointAlong(FIntPoint Direction)
{
    return FRotationMatrix::MakeFromZ(FVector(Direction.X, Direction.Y, 0)).Rotator();
}
}

AGridScene::AGridScene()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

    PlayerMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Manny"));
    PlayerMesh->SetupAttachment(GetRootComponent());
    PlayerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PlayerMesh->SetGenerateOverlapEvents(false);
    PlayerMesh->SetCanEverAffectNavigation(false);
    PlayerMesh->SetRelativeScale3D(FVector(0.76));
    PlayerMesh->SetRelativeRotation(FRotator(0, -90, 0));
    PlayerMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyAsset(
        TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkAsset(
        TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Walk_InPlace.MM_Walk_InPlace"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAsset(
        TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Idle.MM_Idle"));
    PlayerMesh->SetSkeletalMeshAsset(MannyAsset.Object);
    WalkCycle = WalkAsset.Object;
    IdlePose = IdleAsset.Object;

    Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ContinuousGround"));
    Ground->SetupAttachment(GetRootComponent());
    Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ground->SetCanEverAffectNavigation(false);
    Ground->SetCastShadow(false);

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("TableCamera"));
    Camera->SetupAttachment(GetRootComponent());
    Camera->FieldOfView = 54.0f;
    Camera->bConstrainAspectRatio = true;
    Camera->AspectRatio = 16.0f / 9.0f;
    // Manual exposure is independent of the project's legacy luminance / EV100 switch.
    Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Camera->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    Camera->PostProcessSettings.AutoExposureBias = 0.5f;
    Camera->PostProcessSettings.bOverride_BloomIntensity = true;
    Camera->PostProcessSettings.BloomIntensity = 0.4f;

    // Directional lights give the whole board readable fill in a blank workbench map.
    auto* Key = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("BoardKey"));
    Key->SetupAttachment(GetRootComponent());
    Key->SetMobility(EComponentMobility::Movable);
    Key->SetRelativeRotation(FRotator(-58, -45, 0));
    Key->SetIntensity(4.0f);
    Key->SetLightColor(FLinearColor(0.93f, 0.97f, 1.0f));

    auto* Fill = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("BoardFill"));
    Fill->SetupAttachment(GetRootComponent());
    Fill->SetMobility(EComponentMobility::Movable);
    Fill->SetRelativeRotation(FRotator(-50, 145, 0));
    Fill->SetIntensity(1.75f);
    Fill->SetLightColor(FLinearColor(1.0f, 0.91f, 0.82f));
    Fill->SetCastShadows(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialAsset(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BeamAsset(
        TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> GoalSparkAsset(
        TEXT("/Game/VFX/M_GoalSpark.M_GoalSpark"));
    GoalSparkMaterial = GoalSparkAsset.Object;
    Cylinder = CylinderAsset.Object;
    EmissiveMaterial = BeamAsset.Object;
    Cube = CubeAsset.Object;
    Sphere = SphereAsset.Object;
    Cone = ConeAsset.Object;
    BaseMaterial = MaterialAsset.Object;

    DefaultVisualProfile = GetDefaultArtProfile();
}

UMaterialInterface* AGridScene::Material(EColor Color)
{
    // Project-owned palette on engine primitives. Color carries the same meaning in every room.
    static const FColor Palette[] = {
        FColor(27, 34, 39), FColor(53, 67, 75), FColor(70, 88, 109), FColor(82, 101, 124),
        FColor(135, 152, 175), FColor(154, 113, 79), FColor(207, 134, 51), FColor(255, 218, 145),
        FColor(241, 178, 87), FColor(75, 208, 241), FColor(231, 89, 133), FColor(255, 48, 77),
        FColor(68, 229, 155), FColor(23, 31, 39), FColor(66, 165, 239)
    };
    const int32 Index = static_cast<int32>(Color);
    if (Materials.Num() == 0) { Materials.SetNum(UE_ARRAY_COUNT(Palette)); }
    if (!Materials[Index] && BaseMaterial)
    {
        const bool bEmissive = Color == EColor::Beam && EmissiveMaterial;
        auto* Instance = UMaterialInstanceDynamic::Create(bEmissive ? EmissiveMaterial.Get() : BaseMaterial.Get(), this);
        Instance->SetVectorParameterValue(TEXT("Color"),
            bEmissive ? FLinearColor(4.0f, .065f, .12f) : FLinearColor(Palette[Index]));
        Materials[Index] = Instance;
    }
    return Materials[Index];
}

void AGridScene::SetVisualProfile(UGridVisualProfile* Profile)
{
    auto* ProjectProfile = GetDefaultArtProfile();
    if (VisualProfile == Profile && DefaultVisualProfile == ProjectProfile) { return; }
    DefaultVisualProfile = ProjectProfile;
    VisualProfile = Profile;
    BoardSize = FIntPoint::ZeroValue;
}

UGridVisualProfile* AGridScene::GetDefaultArtProfile()
{
    return GetDefault<UGridProjectSettings>()->BoardArtProfile.LoadSynchronous();
}

const FGridVisualSlot* AGridScene::DefaultSlot(EGridVisualRole VisualRole) const
{
    return DefaultVisualProfile ? DefaultVisualProfile->FindSlot(VisualRole) : nullptr;
}

const FGridVisualSlot* AGridScene::OverrideSlot(EGridVisualRole VisualRole) const
{
    return VisualProfile && VisualProfile != DefaultVisualProfile ? VisualProfile->FindSlot(VisualRole) : nullptr;
}

bool AGridScene::HasOverrideMesh(EGridVisualRole VisualRole) const
{
    const auto* Slot = OverrideSlot(VisualRole);
    const auto* Default = DefaultSlot(VisualRole);
    return Slot && Slot->Mesh && (!Default || Slot->Mesh != Default->Mesh);
}

bool AGridScene::HasVisualMesh(EGridVisualRole VisualRole) const
{
    const auto* Slot = OverrideSlot(VisualRole);
    const auto* Default = DefaultSlot(VisualRole);
    return (Slot && Slot->Mesh) || (Default && Default->Mesh);
}

void AGridScene::AddMesh(FMeshPool& Pool, int32& Used, EMesh Mesh, EColor Color,
                          FVector Position, FVector Scale, FRotator Rotation, EGridVisualRole VisualRole)
{
    UStaticMesh* Shape = Mesh == EMesh::Cube ? Cube.Get() : Mesh == EMesh::Sphere ? Sphere.Get() : Mesh == EMesh::Cone ? Cone.Get() : Cylinder.Get();
    const auto* Slot = OverrideSlot(VisualRole);
    const auto* Default = DefaultSlot(VisualRole);
    const auto* MeshSlot = Slot && Slot->Mesh ? Slot : Default;
    if (MeshSlot && MeshSlot->Mesh) { Shape = MeshSlot->Mesh; }
    if (!Shape) { return; }

    Position += Rotation.RotateVector(FGridVisualSlot::ResolveOffset(Slot, Default));
    Scale *= FGridVisualSlot::ResolveScale(Slot, Default);
    if (MeshSlot && MeshSlot->Mesh && MeshSlot->bFitToCell)
    {
        const FBoxSphereBounds Bounds = Shape->GetBounds();
        const double Width = 2.0 * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
        if (Width > KINDA_SMALL_NUMBER)
        {
            const double FloorOffset = 50.0 * Scale.Z;
            Scale *= 100.0 / Width;
            Position.Z += Bounds.BoxExtent.Z * Scale.Z - FloorOffset;
            Position -= Rotation.RotateVector(Bounds.Origin * Scale);
        }
    }

    const bool bUsingCustomMesh = HasOverrideMesh(VisualRole);
    const auto* MaterialSlot = Slot && Slot->Material ? Slot : bUsingCustomMesh ? nullptr : Default;
    UMaterialInterface* Surface = MaterialSlot ? MaterialSlot->Material.Get() : nullptr;
    if (!Surface && !bUsingCustomMesh) { Surface = Material(Color); }
    UStaticMeshComponent* Component;
    if (Pool.IsValidIndex(Used) && IsValid(Pool[Used]))
    {
        Component = Pool[Used];
    }
    else
    {
        Component = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
        Component->SetupAttachment(GetRootComponent());
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->RegisterComponent();
        if (Pool.IsValidIndex(Used)) { Pool[Used] = Component; }
        else { Pool.Add(Component); }
    }
    ++Used;
    Component->SetStaticMesh(Shape);
    Component->SetCastShadow(Color != EColor::Beam);
    // Render components are pooled across object types. Clear an earlier slot override.
    Component->SetMaterial(0, Surface);
    Component->SetRelativeTransform(FTransform(Rotation, Position, Scale));
    Component->SetVisibility(true);
}

void AGridScene::Layout(const Sokoban::FLevel& Level)
{
    const FIntPoint Size = Level.Size;
    CurrentRoom = Level.RoomId;
    BoardSize = Size;
    BoardVoids = Level.VoidCells;
    const FVector Center = EntityAt(FIntPoint::ZeroValue) +
        FVector((Size.X - 1) * CellSize * 0.5, (Size.Y - 1) * CellSize * 0.5, 0);
    const double Frame = FMath::Max(Size.X / 8.0, Size.Y / 6.0);
    const FVector View = Center + FVector(0, 610.0, 1330.0) * Frame;
    Camera->SetRelativeLocationAndRotation(View, (Center - View).Rotation());

    const double Z = GroundHeight();
    BoardGroundHeight = Z;
    int32 Used = 0;
    AddMesh(FloorMeshes, Used, EMesh::Cube, EColor::Dark, Center - FVector(0, 0, 72),
            FVector(Size.X * 4.0, Size.Y * 4.0, .12));
    if (Level.VoidCells.IsEmpty())
    {
        AddMesh(FloorMeshes, Used, EMesh::Cube, EColor::Plinth, Center - FVector(0, 0, 35),
                FVector(Size.X * 1.28 + .28, Size.Y * 1.28 + .28, .58));
        AddMesh(FloorMeshes, Used, EMesh::Cube, EColor::Slate, Center - FVector(0, 0, 7),
                FVector(Size.X * 1.25 + .13, Size.Y * 1.25 + .13, .12));
    }
    const auto* LocalFloor = OverrideSlot(EGridVisualRole::Floor);
    const auto* DefaultFloor = DefaultSlot(EGridVisualRole::Floor);
    TArray<FVector> Vertices;
    TArray<FVector2D> UVs;
    TArray<int32> Indices;
    TArray<int32> Corners;
    Corners.Init(INDEX_NONE, (Size.X + 1) * (Size.Y + 1));
    const auto Corner = [&](int32 X, int32 Y)
    {
        int32& Index = Corners[Y * (Size.X + 1) + X];
        if (Index == INDEX_NONE)
        {
            Index = Vertices.Add(FVector((X - .5) * CellSize, (Y - .5) * CellSize, Z));
            UVs.Add(FVector2D(X, Y));
        }
        return Index;
    };
    for (int32 Y = 0; Y < Size.Y; ++Y)
    {
        for (int32 X = 0; X < Size.X; ++X)
        {
            const FIntPoint Cell(X, Y);
            if (!Sokoban::FRules::IsInside(Level, Cell)) { continue; }
            if (!Level.VoidCells.IsEmpty())
            {
                AddMesh(FloorMeshes, Used, EMesh::Cube, EColor::Plinth, EntityAt(Cell, -28),
                        FVector(1.24, 1.24, .56));
            }
            const int32 A = Corner(X, Y);
            const int32 B = Corner(X + 1, Y);
            const int32 C = Corner(X + 1, Y + 1);
            const int32 D = Corner(X, Y + 1);
            Indices.Append({A, C, B, A, D, C});
        }
    }
    TArray<FVector> Normals;
    Normals.Init(FVector::UpVector, Vertices.Num());
    Ground->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UVs, {}, {}, false);
    UMaterialInterface* Surface = LocalFloor && LocalFloor->Material ? LocalFloor->Material.Get() :
        DefaultFloor && DefaultFloor->Material ? DefaultFloor->Material.Get() : Material(EColor::TileA);
    Ground->SetMaterial(0, Surface);
    BuildBoardTrim(*Ground, Level, Z);
    Ground->SetMaterial(1, Material(EColor::Dark));
    Ground->SetMaterial(2, Material(EColor::Wall));
    Ground->SetVisibility(true);
    for (int32 I = Used; I < FloorMeshes.Num(); ++I) { FloorMeshes[I]->SetVisibility(false); }
}

double AGridScene::GroundHeight() const
{
    return FloorZ + 4.0 +
        FGridVisualSlot::ResolveOffset(OverrideSlot(EGridVisualRole::Floor), DefaultSlot(EGridVisualRole::Floor)).Z;
}

void AGridScene::SetEditCursor(TOptional<FIntPoint> Cell)
{
    if (EditCursorCell == Cell) { return; }
    EditCursorCell = Cell;
    for (const auto& Mesh : EditCursorMeshes) { Mesh->SetVisibility(false); }
    if (!Cell.IsSet()) { return; }

    int32 Used = 0;
    const FVector Center = EntityAt(Cell.GetValue(), 6.0);
    const double HalfSide = CellSize * .46;
    for (const double Sign : {-1.0, 1.0})
    {
        AddMesh(EditCursorMeshes, Used, EMesh::Cube, EColor::Beam, Center + FVector(0, Sign * HalfSide, 0),
                FVector(2 * HalfSide / 100, .035, .02));
        AddMesh(EditCursorMeshes, Used, EMesh::Cube, EColor::Beam, Center + FVector(Sign * HalfSide, 0, 0),
                FVector(.035, 2 * HalfSide / 100, .02));
    }
}

bool AGridScene::PickCell(const APlayerController& Controller, FIntPoint& Cell) const
{
    FVector Origin, Direction;
    if (!Controller.DeprojectMousePositionToWorld(Origin, Direction)) { return false; }
    const FTransform Transform = GetActorTransform();
    Origin = Transform.InverseTransformPosition(Origin);
    Direction = Transform.InverseTransformVector(Direction);
    if (FMath::IsNearlyZero(Direction.Z)) { return false; }
    const double Distance = (GroundHeight() - Origin.Z) / Direction.Z;
    if (Distance < 0.0) { return false; }
    const FVector Hit = Origin + Distance * Direction;
    Cell = FIntPoint(FMath::FloorToInt(Hit.X / CellSize + .5), FMath::FloorToInt(Hit.Y / CellSize + .5));
    return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < BoardSize.X && Cell.Y < BoardSize.Y;
}

void AGridScene::DrawTerrain(const Sokoban::FState& State)
{
    const auto Solid = [&](FIntPoint Cell, double Height, FVector Scale, EColor Color, EGridVisualRole VisualRole = EGridVisualRole::None)
    {
        AddMesh(StateMeshes, StateUsed, EMesh::Cube, Color, EntityAt(Cell, Height), Scale, FRotator::ZeroRotator, VisualRole);
    };
    for (FIntPoint Cell : State.Board.Walls)
    {
        Solid(Cell, 54, FVector(1.12, 1.12, 1.0), EColor::Wall, EGridVisualRole::Wall);
        if (!HasVisualMesh(EGridVisualRole::Wall))
        {
            Solid(Cell, 108, FVector(1.16, 1.16, .10), EColor::Slate);
        }
    }
    for (FIntPoint Cell : State.Board.Rubble)
    {
        AddMesh(StateMeshes, StateUsed, EMesh::Cube, EColor::Rubble, EntityAt(Cell, 25),
                FVector(.67, .73, .44), FRotator(7, 28, -8), EGridVisualRole::Rubble);
        if (!HasOverrideMesh(EGridVisualRole::Rubble))
        {
            AddMesh(StateMeshes, StateUsed, EMesh::Cube, EColor::Rubble, EntityAt(Cell, 20) + FVector(33, 24, 0),
                    FVector(.32, .38, .31), FRotator(-8, -21, 13), EGridVisualRole::Rubble);
        }
    }
    Solid(State.Board.Goal, 7, FVector(.97, .97, .09), EColor::Goal, EGridVisualRole::Goal);
    if (!HasVisualMesh(EGridVisualRole::Goal))
    {
        Solid(State.Board.Goal, 13, FVector(.75, .75, .07), EColor::Dark);
        Solid(State.Board.Goal, 17, FVector(.42, .42, .06), EColor::Goal, EGridVisualRole::Goal);
    }
}

void AGridScene::DrawBeams(const Sokoban::FState& State)
{
    for (const auto& Trace : State.BeamTraces)
    {
        const FVector Direction(Trace.Direction.X, Trace.Direction.Y, 0.0);
        const FVector Start = EntityAt(Trace.Origin, 63.0) + Direction * 52.0;
        // Stop at a crate's front face, or the edge of the last traversable cell.
        const FVector End = EntityAt(Trace.LastCell, 63.0) +
            Direction * (Trace.bStoppedByBox ? CellSize - 44.5 : CellSize * 0.5);
        const double Length = FVector::DotProduct(End - Start, Direction);
        if (Length <= 0.0) { continue; }
        AddMesh(StateMeshes, StateUsed, EMesh::Cylinder, EColor::Beam, (Start + End) * 0.5,
                FVector(.067, .067, Length / 100.0), PointAlong(Trace.Direction), EGridVisualRole::Beam);
        if (Trace.bStoppedByBox)
        {
            AddMesh(StateMeshes, StateUsed, EMesh::Sphere, EColor::Beam, End, FVector(.12));
        }
    }
}

void AGridScene::DrawEntities(const Sokoban::FState& State)
{
    for (const auto& Box : State.Board.Boxes)
    {
        const bool bEjectBomb = Box.bEject && Box.bExplodes;
        const auto VisualRole = bEjectBomb ? EGridVisualRole::EjectCrate
            : GetGridBoxVisualRole(Box.Spike, Box.bExplodes, Box.bEject);
        AddMesh(StateMeshes, StateUsed, EMesh::Cube, Box.bEject ? EColor::Eject : EColor::Box, EntityAt(Box.Cell, 50), FVector(.89, .89, .92),
                FRotator::ZeroRotator, VisualRole);
        if (!HasVisualMesh(VisualRole))
        {
            AddMesh(StateMeshes, StateUsed, EMesh::Cube, Box.bExplodes ? EColor::Enemy : EColor::Accent,
                    EntityAt(Box.Cell, 100), FVector(.67, .67, .09), FRotator(0, 45, 0));
        }
        if (Box.bExplodes && (bEjectBomb || !HasOverrideMesh(VisualRole)))
        {
            const double MarkerHeight = HasVisualMesh(VisualRole) ? 89 : 113;
            AddMesh(StateMeshes, StateUsed, EMesh::Sphere, EColor::Dark, EntityAt(Box.Cell, MarkerHeight), FVector(.32, .32, .27));
            AddMesh(StateMeshes, StateUsed, EMesh::Sphere, EColor::Beam, EntityAt(Box.Cell, MarkerHeight + 13),
                    FVector(.105, .105, .105));
        }
        if (Box.Spike != FIntPoint::ZeroValue)
        {
            const FVector Tip = EntityAt(Box.Cell + Box.Spike, 45);
            AddMesh(StateMeshes, StateUsed, EMesh::Cone, EColor::Spike, Tip,
                    FVector(.55, .55, 1.35), PointAlong(Box.Spike), EGridVisualRole::Spike);
        }
    }
    for (const auto& Enemy : State.Board.Enemies)
    {
        const auto VisualRole = Enemy.BeamDirection == FIntPoint::ZeroValue ? EGridVisualRole::ContactEnemy : EGridVisualRole::BeamEnemy;
        if (!HasVisualMesh(VisualRole))
        {
            AddMesh(StateMeshes, StateUsed, EMesh::Cube, EColor::Dark, EntityAt(Enemy.Cell, 20), FVector(.83, .83, .32));
        }
        const FRotator Facing = VisualRole == EGridVisualRole::BeamEnemy && HasVisualMesh(VisualRole)
            ? FVector(Enemy.BeamDirection.X, Enemy.BeamDirection.Y, 0).Rotation() : FRotator::ZeroRotator;
        AddMesh(StateMeshes, StateUsed, EMesh::Sphere, EColor::Enemy, EntityAt(Enemy.Cell, 63), FVector(.71, .71, .69), Facing, VisualRole);
        if (Enemy.BeamDirection != FIntPoint::ZeroValue)
        {
            const FVector Direction(Enemy.BeamDirection.X, Enemy.BeamDirection.Y, 0);
            AddMesh(StateMeshes, StateUsed, EMesh::Cone, EColor::Beam, EntityAt(Enemy.Cell, 63) + Direction * 43.0,
                    FVector(.43, .43, .79), PointAlong(Enemy.BeamDirection));
        }
    }
}

FVector AGridScene::EntityAt(FIntPoint Cell, double Height) const
{
    return FVector(Cell.X * CellSize, Cell.Y * CellSize, GroundHeight() - 4.0 + Height);
}

FVector AGridScene::PlayerGridPosition(FIntPoint Cell) const
{
    return EntityAt(Cell, 4.0);
}

void AGridScene::Present(const Sokoban::FState& State, bool bAnimateStep)
{
    if (CurrentRoom != State.Board.RoomId || BoardSize != State.Board.Size || BoardGroundHeight != GroundHeight() ||
        BoardVoids.Num() != State.Board.VoidCells.Num() || !BoardVoids.Includes(State.Board.VoidCells))
    {
        Layout(State.Board);
        if (EditCursorCell.IsSet())
        {
            const FIntPoint Cell = EditCursorCell.GetValue();
            SetEditCursor({});
            if (Sokoban::FRules::IsInBounds(State.Board, Cell)) { SetEditCursor(Cell); }
        }
        bHasPlayerCell = false;
    }
    PresentPlayer(State.Player, bAnimateStep);
    if (GetWorld() && GetWorld()->IsGameWorld() && PresentedOutcome == Sokoban::EOutcome::Running
        && State.Outcome != Sokoban::EOutcome::Running)
    {
        PresentedOutcome = State.Outcome;
        if (!bStepping && BlastCenters.IsEmpty()) { BeginOutcome(); }
    }
    StateUsed = 0;
    DrawTerrain(State);
    DrawEntities(State);
    DrawBeams(State);
    for (int32 I = StateUsed; I < StateMeshes.Num(); ++I) { StateMeshes[I]->SetVisibility(false); }
}



void AGridScene::ClearBoardPreview()
{
    for (auto& Mesh : FloorMeshes) { if (IsValid(Mesh)) { Mesh->SetVisibility(false); } }
    for (auto& Mesh : StateMeshes) { if (IsValid(Mesh)) { Mesh->SetVisibility(false); } }
    SetEditCursor({});
    Ground->SetVisibility(false);
    PlayerMesh->SetVisibility(false);
    BoardSize = FIntPoint::ZeroValue;
}

void AGridScene::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshEditorPreview();
}

void AGridScene::RefreshEditorPreview()
{
#if WITH_EDITOR
    if (!GetWorld() || GetWorld()->IsGameWorld() || IsTemplate()) { return; }

    const UGridLevelAsset* PreviewAsset = DraftLevel.Get();
    const bool bSequence = !PreviewAsset && !PlaytestSequence.IsEmpty();
    if (bSequence) { PreviewAsset = PlaytestSequence[FMath::Clamp(PreviewRoom, 1, PlaytestSequence.Num()) - 1].Get(); }
    SetVisualProfile(PreviewAsset ? PreviewAsset->VisualProfile.Get() : nullptr);
    const Sokoban::FLevel Level = PreviewAsset ? PreviewAsset->ToLevel() : bSequence ? Sokoban::FLevel() :
        GridPreview::MakeRoom(FMath::Clamp(PreviewRoom, 1, GridPreview::RoomCount) - 1);
    LayoutWarnings = Sokoban::FLevelGate::CheckLayout(Level);
    if (!Sokoban::FLevelGate::SupportsBoardSize(Level.Size))
    {
        ClearBoardPreview();
        return;
    }
    PlayerMesh->SetVisibility(true);
    BoardSize = FIntPoint::ZeroValue;
    Present(Sokoban::FRules::Start(Level, GetDefault<UGridProjectSettings>()->StartingInventory()).State);
#endif
}


