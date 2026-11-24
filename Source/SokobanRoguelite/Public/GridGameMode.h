#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GridRules.h"
#include "GridCampaignSequence.h"
#include "GridGameMode.generated.h"

class AGridScene;
class UGridLevelAsset;
class SWidget;
class UGameViewportClient;
class ULocalPlayer;

enum class EGridPaintTool : uint8
{
    Erase, Wall, Rubble, Crate, SpikeCrate, BlastCrate, EjectCrate, Enemy, BeamEnemy, Start, Goal, Cutout, Restore, Rotate
};

UCLASS()
class SOKOBANROGUELITE_API AGridGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AGridGameMode();
};

UCLASS()
class SOKOBANROGUELITE_API AGridDraftPlaytestMode : public AGridGameMode
{
    GENERATED_BODY()
};

UCLASS()
class SOKOBANROGUELITE_API AGridPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    const Sokoban::FState& GetGridState() const { return ActiveSession().State; }
    int32 GetRoomNumber() const { return RoomIndex + 1; }
    bool IsDraftPlaytest() const;
    int32 GetRoomCount() const;
    FString GetRoomTitle() const;
    FText GetRoomHint() const;
    FText GetRoomRewardNotice() const;
    const FText& GetTransitionError() const { return TransitionError; }
    const FString& GetDraftError() const { return DraftError; }
    int32 GetSelectedUndoSteps() const { return SelectedUndoSteps; }
    int32 GetAvailableUndoSteps() const { return Sokoban::FRules::AvailableUndoSteps(ActiveSession()); }
    const FText& GetUndoNotice() const { return UndoNotice; }
    bool IsGridBusy() const { return bGridBusy; }
    bool IsStartMenuOpen() const { return bStartMenuOpen; }
    const FText& GetStartMenuError() const { return StartMenuError; }
    const FText& GetCampaignSummary() const { return CampaignSummary; }
    void StartNewRun();
    void OpenRuntimeEditorFromMenu();
    void ToggleRuntimeEditor();
    void CloseRuntimeEditor();
    void TestRuntimeDraft();
    void SaveRuntimeDraft();
    void ValidateRuntimeDraft();
    void LoadRuntimeDraft();
    bool WillEditorReturnToMenu() const { return bRuntimeEditorFromMenu; }
    bool IsRuntimeEditing() const { return RuntimeDraft && !bTestingRuntimeDraft; }
    bool IsRuntimeTest() const { return bTestingRuntimeDraft; }
    const FText& GetEditNotice() const { return EditNotice; }
    const FText& GetRuntimeValidationText() const { return RuntimeValidationText; }
    TOptional<Sokoban::ECheck> GetRuntimeValidationStatus() const { return RuntimeValidationStatus; }
    bool CanAttemptRuntimeSave() const;
    FText GetRuntimeInventoryText() const;
    void SetRuntimeTool(EGridPaintTool Tool) { RuntimeTool = Tool; }
    EGridPaintTool GetRuntimeTool() const { return RuntimeTool; }
    FIntPoint GetRuntimeDraftSize() const;
    void ResizeRuntimeDraft(FIntPoint Size);
    void ToggleRuntimeBeamTerrain();
    FText GetRuntimeBeamTerrainText() const;

protected:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    Sokoban::FSession& ActiveSession() { return bTestingRuntimeDraft ? RuntimeTest : Session; }
    const Sokoban::FSession& ActiveSession() const { return bTestingRuntimeDraft ? RuntimeTest : Session; }
    void PaintRuntimeCell();
    void SaveRuntimeDraftToSlot(const FString& Slot);
    void LoadRuntimeDraftFromSlot(const FString& Slot);
    bool ApplyRuntimeTool(FIntPoint Cell);
    void EnterRuntimeDraftEditor();
    void PreviewRuntimeDraft();
    void RequestRuntimeValidation(TOptional<FString> SaveSlot);
    bool PresentRuntimeValidation(const Sokoban::FValidation& Result, int32 Threshold);
    void WriteRuntimeDraft(const FString& Slot);
    Sokoban::FResourceContext GetRuntimeValidationResources() const;
    Sokoban::FInventory GetRuntimeValidationInventory() const;
    void SetMouseUIInput(bool bEnable);
    void Submit(FKey Key);
    void Arm();
    friend class FGridUndoSelectionTest;
    friend class FGridSequenceGateTest;
    friend class FGridRoomRewardTest;
    friend class FGridMixedCampaignTest;
    friend class FGridBackgroundAdmissionTest;
    friend class FGridRuntimeEditTest;
    friend class FGridMenuEditorTest;
    void ToggleUndoSelection();
    void ChangeUndoSelection(FKey Key);
    void ConfirmOrNextRoom();
    void ResetRun();
    void OpenStartMenu();
    AGridScene* SelectPlayScene(bool bDraftRequested) const;
    bool PrepareCampaign();
    const FGridSequenceSlot* GetCampaignSlot(int32 Index) const;
    int32 GetSlotSeed(int32 Index) const;
    void NextRoom();
    void SettleRoomReward();
    bool TryLoadRoom(int32 Index, Sokoban::FInventory Inventory);
    const UGridLevelAsset* GetRoomAsset(int32 Index) const;
    bool HasAuthoredSequence() const;
    void RefreshScene(bool bAnimateStep = false);
    bool IsStepPlaying() const;
    UPROPERTY() TObjectPtr<AGridScene> Scene;
    FKey PreferredMove;
    double NextMoveTime = 0.0;
    bool bOwnsScene = false;
    int32 SelectedUndoSteps = 0;
    FText UndoNotice;
    FText TransitionError;
    FString DraftError;
    Sokoban::FSession Session;
    Sokoban::FInventory GrantedRoomReward;
    bool bRoomRewardSettled = false;
    UPROPERTY(Transient) TObjectPtr<UGridLevelAsset> RuntimeDraft;
    Sokoban::FSession RuntimeTest;
    bool bTestingRuntimeDraft = false;
    bool bRuntimeEditorFromMenu = false;
    EGridPaintTool RuntimeTool = EGridPaintTool::Wall;
    FText EditNotice;
    FText RuntimeValidationText;
    TOptional<Sokoban::ECheck> RuntimeValidationStatus;
    UPROPERTY(Transient) TArray<FGridSequenceSlot> RunSlots;
    Sokoban::FLevel CurrentRoomLayout;
    TArray<FName> RecentGeneratedTemplates;
    bool bGridBusy = false;
    bool bStartMenuOpen = false;
    bool bUsingCampaign = false;
    int32 RunSeed = 0;
    FText StartMenuError;
    FText CampaignSummary;
    int32 RoomIndex = 0;
};

UCLASS()
class SOKOBANROGUELITE_API AGridHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    TSharedPtr<SWidget> Overlay;
    TWeakObjectPtr<UGameViewportClient> OverlayViewport;
    TWeakObjectPtr<ULocalPlayer> OverlayPlayer;
};

