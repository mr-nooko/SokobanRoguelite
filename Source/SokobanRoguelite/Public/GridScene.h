#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridRules.h"
#include "GridVisualProfile.h"
#include "GridScene.generated.h"

class UGridLevelAsset;
class UAnimSequence;
class UMaterialBillboardComponent;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UTexture2D;
class UCameraComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UProceduralMeshComponent;

UCLASS(Blueprintable, meta=(DisplayName="关卡预览"))
class SOKOBANROGUELITE_API AGridScene : public AActor
{
    GENERATED_BODY()

public:
    AGridScene();
    void Present(const Sokoban::FState& State, bool bAnimateStep = false);
    bool IsStepping() const { return bStepping || bPlayingOutcome || !BlastCenters.IsEmpty(); }
    void ResetOutcome();
    void QueueBlast(const Sokoban::FTurnResult& Result);
    void ClearBoardPreview();
    bool PickCell(const APlayerController& Controller, FIntPoint& Cell) const;
    void SetEditCursor(TOptional<FIntPoint> Cell);
    void PlayTurnSounds(const Sokoban::FTurnResult& Result);
    void SetVisualProfile(UGridVisualProfile* Profile);
    static UGridVisualProfile* GetDefaultArtProfile();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditInstanceOnly, Category="关卡预览", meta=(DisplayName="关卡草稿"))
    TObjectPtr<UGridLevelAsset> DraftLevel;

    UPROPERTY(EditInstanceOnly, Category="关卡预览", meta=(DisplayName="试玩关卡序列"))
    TArray<TObjectPtr<UGridLevelAsset>> PlaytestSequence;

    UPROPERTY(EditInstanceOnly, Category="关卡预览", meta=(DisplayName="试玩起始关编号", ClampMin="1"))
    int32 PreviewRoom = 1;

    UPROPERTY(EditInstanceOnly, Category="关卡预览", meta=(DisplayName="用于试玩"))
    bool bUseForPlaytest = true;

    UPROPERTY(VisibleInstanceOnly, Category="关卡预览", meta=(DisplayName="布局警告"))
    TArray<FString> LayoutWarnings;

    UFUNCTION(BlueprintCallable, CallInEditor, Category="关卡预览", meta=(DisplayName="刷新关卡预览"))
    void RefreshEditorPreview();

protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;

private:
    enum class EMesh : uint8 { Cube, Sphere, Cone, Cylinder };
    enum class EColor : uint8 { Plinth, Slate, TileA, TileB, Wall, Rubble, Box, Accent, Spike, Player, Enemy, Beam, Goal, Dark, Eject };
    using FMeshPool = TArray<TObjectPtr<UStaticMeshComponent>>;

    void Layout(const Sokoban::FLevel& Level);
    double GroundHeight() const;
    FVector EntityAt(FIntPoint Cell, double Height = 0.0) const;
    FVector PlayerGridPosition(FIntPoint Cell) const;
    void PresentPlayer(FIntPoint Target, bool bAnimateStep);
    void PlayIdle();
    void BeginOutcome();
    void PlayGridSound(EGridSoundCue Cue, FVector LocalPosition) const;
    void TickOutcome(float DeltaSeconds);
    void TickBlast(float DeltaSeconds);
    void ClearBlast();
    void SpawnGoalParticles();
    void ClearGoalParticles();
    void AddMesh(FMeshPool& Pool, int32& Used, EMesh Mesh, EColor Color,
                 FVector Position, FVector Scale, FRotator Rotation = FRotator::ZeroRotator,
                 EGridVisualRole VisualRole = EGridVisualRole::None);
    const FGridVisualSlot* DefaultSlot(EGridVisualRole VisualRole) const;
    const FGridVisualSlot* OverrideSlot(EGridVisualRole VisualRole) const;
    bool HasOverrideMesh(EGridVisualRole VisualRole) const;
    bool HasVisualMesh(EGridVisualRole VisualRole) const;
    void DrawBeams(const Sokoban::FState& State);
    void DrawTerrain(const Sokoban::FState& State);
    void DrawEntities(const Sokoban::FState& State);
    UMaterialInterface* Material(EColor Color);

    UPROPERTY(Transient) TObjectPtr<UGridVisualProfile> VisualProfile;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> Sphere;
    UPROPERTY() TObjectPtr<UStaticMesh> Cone;
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY() TObjectPtr<UGridVisualProfile> DefaultVisualProfile;
    UPROPERTY() TObjectPtr<UMaterialInterface> EmissiveMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> GoalSparkMaterial;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialBillboardComponent>> GoalParticles;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> GoalParticleMaterials;
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Ground;
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UStaticMeshComponent>> FloorMeshes;
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UStaticMeshComponent>> EditCursorMeshes;
    TOptional<FIntPoint> EditCursorCell;
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UStaticMeshComponent>> StateMeshes;

    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> PlayerMesh;
    UPROPERTY() TObjectPtr<UAnimSequence> WalkCycle;
    UPROPERTY() TObjectPtr<UAnimSequence> IdlePose;
    UPROPERTY(Transient, DuplicateTransient) TObjectPtr<UTexture2D> PlayerTexture;

    Sokoban::EOutcome PresentedOutcome = Sokoban::EOutcome::Running;
    bool bPlayingOutcome = false;
    bool bCapturedOutcomePose = false;
    float OutcomeTime = 0.f;
    FVector OutcomeOrigin = FVector::ZeroVector;
    FVector OutcomeScale = FVector::OneVector;
    FQuat OutcomeRotation = FQuat::Identity;
    TArray<FVector> GoalParticleVelocities;
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UStaticMeshComponent>> BlastMeshes;
    TArray<FIntPoint> BlastCenters;
    float BlastTime = 0.f;

    FVector StepStart = FVector::ZeroVector;
    FVector StepEnd = FVector::ZeroVector;
    FIntPoint PlayerCell = FIntPoint::ZeroValue;
    float StepElapsed = 0.f;
    float IdleDelay = 0.f;
    float WalkPosition = 0.f;
    bool bWalkingAnimation = false;
    bool bStepping = false;
    bool bHasPlayerCell = false;

    FName CurrentRoom;
    FIntPoint BoardSize = FIntPoint::ZeroValue;
    TSet<FIntPoint> BoardVoids;
    double BoardGroundHeight = 0.0;
    int32 StateUsed = 0;
};



