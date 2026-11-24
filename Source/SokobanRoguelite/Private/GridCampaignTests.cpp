#include "GridGameMode.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GridLevelAsset.h"
#include "GridProjectSettings.h"
#include "GridScene.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridMixedCampaignTest, "Sokoban.Preview.MixedCampaign",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridMixedCampaignTest::RunTest(const FString&)
{
    using namespace Sokoban;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Temporary world"), World)) { return false; }
    auto* Controller = World->SpawnActor<AGridPlayerController>();
    Controller->Scene = World->SpawnActor<AGridScene>();

    auto* Sequence = NewObject<UGridCampaignSequence>(Controller);
    Sequence->bUseFixedSeed = true;
    Sequence->FixedSeed = 42;
    auto* First = NewObject<UGridLevelAsset>(Sequence);
    First->RoomId = TEXT("FirstManual");
    First->Size = {3, 3};
    First->Start = {0, 0};
    First->Goal = {1, 1};
    auto* EditingScene = World->SpawnActor<AGridScene>();
    EditingScene->DraftLevel = First;
    TestTrue(TEXT("Regular play selects the formal scene even on a map with a draft"),
        Controller->SelectPlayScene(false) == Controller->Scene);
    TestTrue(TEXT("Explicit draft playtest selects the draft scene"),
        Controller->SelectPlayScene(true) == EditingScene);

    auto* Third = NewObject<UGridLevelAsset>(Sequence);
    Third->SetFromLevel(First->ToLevel());
    Third->RoomId = TEXT("ThirdManual");
    Sequence->Slots.SetNum(3);
    Sequence->Slots[0].Level = First;
    Sequence->Slots[1].Source = EGridSequenceSource::Generated;
    Sequence->Slots[1].Template = EGridSequenceTemplate::StrengthChain;
    Sequence->Slots[1].MaxAttempts = 1;
    Sequence->Slots[1].bAllowFallback = false;
    Sequence->Slots[2].Level = Third;
    auto* Settings = GetMutableDefault<UGridProjectSettings>();
    {
        const TGuardValue<TSoftObjectPtr<UGridCampaignSequence>> Source(Settings->CampaignSequence, Sequence);
        Controller->OpenStartMenu();
        TestTrue(TEXT("The menu opens before a room starts"), Controller->IsStartMenuOpen() &&
            Controller->GetGridState().Board.RoomId.IsNone());
        Controller->ConfirmOrNextRoom();
        TestTrue(TEXT("Enter starts the configured manual room"), !Controller->IsStartMenuOpen() &&
            Controller->GetRoomCount() == 3 && Controller->GetGridState().Board.RoomId == First->RoomId);
        Controller->ConfirmOrNextRoom();
        TestEqual(TEXT("The sequence does not advance before winning"), Controller->GetRoomNumber(), 1);

        Controller->Session.State.Outcome = EOutcome::Won;
        Controller->Session.State.Inventory = {0, 0};
        Controller->ConfirmOrNextRoom();
        TestTrue(TEXT("Generated slots use the carried stock and preserve the prior win on failure"),
            Controller->GetRoomNumber() == 1 && !Controller->GetTransitionError().IsEmpty() &&
            Controller->GetGridState().Outcome == EOutcome::Won);

        Controller->RunSlots[1].bAllowFallback = true;
        Controller->RunSlots[1].MaxAttempts = 0;
        Controller->ConfirmOrNextRoom();
        TestTrue(TEXT("A validated generated room enters in the second position"),
            Controller->GetRoomNumber() == 2 && Controller->GetGridState().Board.RoomId.ToString().Contains(TEXT("FallbackLoop")) &&
            Controller->GetGridState().Inventory.Strength == 0);
        const FName GeneratedId = Controller->GetGridState().Board.RoomId;
        Controller->ToggleRuntimeEditor();
        TestTrue(TEXT("Runtime editing copies the admitted generated layout"),
            Controller->RuntimeDraft && Controller->RuntimeDraft->RoomId == GeneratedId);
        Controller->CloseRuntimeEditor();

        Controller->Session.State.Outcome = EOutcome::Won;
        Controller->ConfirmOrNextRoom();
        TestTrue(TEXT("The manual slot stays third even after generation"),
            Controller->GetRoomNumber() == 3 && Controller->GetGridState().Board.RoomId == Third->RoomId);
        Controller->ResetRun();
        Swap(Sequence->Slots[0], Sequence->Slots[2]);
        Controller->StartNewRun();
        TestTrue(TEXT("The next run uses the new authored ordering from position one"),
            Controller->RunSeed == 42 && Controller->GetRoomNumber() == 1 &&
            Controller->GetGridState().Board.RoomId == Third->RoomId);
    }

    auto* Automatic = NewObject<UGridCampaignSequence>(Controller);
    Automatic->TotalRooms = 6;
    Automatic->PlacedRooms.SetNum(2);
    Automatic->PlacedRooms[0].Position = 5;
    Automatic->PlacedRooms[0].Level = Third;
    Automatic->PlacedRooms[1].Position = 2;
    Automatic->PlacedRooms[1].Level = First;
    Automatic->AutoRooms.Template = EGridSequenceTemplate::Detour;
    TArray<FGridSequenceSlot> Filled;
    FText Error;
    if (TestTrue(TEXT("Manual entries can be listed out of order"), Automatic->BuildSlots(Filled, Error) && Filled.Num() == 6))
    {
        TestTrue(TEXT("One-based positions pin the manual stages and fill the rest"),
            Filled[1].Level == First && Filled[4].Level == Third &&
            Filled[0].Source == EGridSequenceSource::Generated && Filled[2].Source == EGridSequenceSource::Generated &&
            Filled[3].Source == EGridSequenceSource::Generated && Filled[5].Source == EGridSequenceSource::Generated &&
            Filled[5].Template == EGridSequenceTemplate::Detour);
    }
    Automatic->PlacedRooms[1].Position = 5;
    TestTrue(TEXT("Duplicate positions are rejected without publishing a partial sequence"),
        !Automatic->BuildSlots(Filled, Error) && Filled.IsEmpty() && !Error.IsEmpty());
    Automatic->PlacedRooms[1].Position = 7;
    TestTrue(TEXT("Positions above the total are rejected"), !Automatic->BuildSlots(Filled, Error) && !Error.IsEmpty());
    Automatic->PlacedRooms[1].Position = 2;
    Automatic->PlacedRooms[1].Level = nullptr;
    TestTrue(TEXT("Missing manual assets are rejected"), !Automatic->BuildSlots(Filled, Error) && !Error.IsEmpty());
    Automatic->PlacedRooms.Reset();
    TestTrue(TEXT("No manual entries produces an entirely generated sequence"),
        Automatic->BuildSlots(Filled, Error) && Filled.Num() == 6 &&
        Filled[0].Source == EGridSequenceSource::Generated);

    Automatic->GenerationBands = {
        {1, 2, EGridGeneratedDifficulty::Low, 1, 10},
        {6, 6, EGridGeneratedDifficulty::High, 3, 14}
    };
    Automatic->PlacedRooms.SetNum(1);
    Automatic->PlacedRooms[0].Position = 5;
    Automatic->PlacedRooms[0].Level = Third;
    if (TestTrue(TEXT("Disjoint generation bands expand to the sequence"), Automatic->BuildSlots(Filled, Error)))
    {
        TestTrue(TEXT("Only generated rooms inherit difficulty and layout settings from their band"),
            Filled[0].Difficulty == EGridGeneratedDifficulty::Low && Filled[0].Complexity == 1 && Filled[0].MaxActions == 10 &&
            Filled[2].Difficulty == EGridGeneratedDifficulty::Project &&
            Filled[4].Source == EGridSequenceSource::Authored && Filled[4].Difficulty == EGridGeneratedDifficulty::Project &&
            Filled[5].Difficulty == EGridGeneratedDifficulty::High && Filled[5].Complexity == 3 && Filled[5].MaxActions == 14);
    }
    Automatic->GenerationBands[0].LastRoom = 6;
    TestTrue(TEXT("Overlapping bands fail with an error"), !Automatic->BuildSlots(Filled, Error) && !Error.IsEmpty());
    Automatic->GenerationBands[0].LastRoom = 2;
    Automatic->GenerationBands[1].LastRoom = 7;
    TestTrue(TEXT("A band beyond the total fails with an error"), !Automatic->BuildSlots(Filled, Error) && !Error.IsEmpty());

    Automatic->PlacedRooms.Reset();
    Automatic->GenerationBands = {
        {1, 2, EGridGeneratedDifficulty::Low, 1, 10},
        {3, 4, EGridGeneratedDifficulty::Medium, 2, 12},
        {5, 6, EGridGeneratedDifficulty::High, 3, 14}
    };
    auto* TotalProperty = FindFProperty<FProperty>(UGridCampaignSequence::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UGridCampaignSequence, TotalRooms));
    const auto EditTotal = [&](int32 Total)
    {
        Automatic->PreEditChange(TotalProperty);
        Automatic->TotalRooms = Total;
        FPropertyChangedEvent Change(TotalProperty, EPropertyChangeType::ValueSet);
        Automatic->PostEditChangeProperty(Change);
    };
    EditTotal(8);
    TestTrue(TEXT("Extending the sequence extends a band that reached its old end"),
        Automatic->GenerationBands.Last().LastRoom == 8 && Automatic->BuildSlots(Filled, Error) && Filled.Num() == 8 &&
        Filled.Last().Difficulty == EGridGeneratedDifficulty::High);

    EditTotal(3);
    TestTrue(TEXT("Shortening trims the surviving band and removes bands entirely beyond the new end"),
        Automatic->GenerationBands.Num() == 2 && Automatic->GenerationBands.Last().LastRoom == 3 &&
        Automatic->BuildSlots(Filled, Error) && Filled.Num() == 3 &&
        Filled.Last().Difficulty == EGridGeneratedDifficulty::Medium);

    auto* BandsProperty = FindFProperty<FProperty>(UGridCampaignSequence::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UGridCampaignSequence, GenerationBands));
    auto* EndProperty = FindFProperty<FProperty>(FGridGenerationBand::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FGridGenerationBand, LastRoom));
    Automatic->PreEditChange(EndProperty);
    Automatic->GenerationBands.Last().LastRoom = 99;
    FPropertyChangedEvent EndEdit(EndProperty, EPropertyChangeType::ValueSet);
    EndEdit.SetActiveMemberProperty(BandsProperty);
    Automatic->PostEditChangeProperty(EndEdit);
    TestTrue(TEXT("Direct edits cannot set a band end above the total"),
        Automatic->GenerationBands.Last().LastRoom == 3);
    Automatic->PreEditChange(BandsProperty);
    Automatic->GenerationBands.Last().FirstRoom = 2;
    FPropertyChangedEvent BandEdit(BandsProperty, EPropertyChangeType::ValueSet);
    Automatic->PostEditChangeProperty(BandEdit);
    TestTrue(TEXT("Overlaps appear directly in the sequence details"),
        Automatic->BuildSlots(Filled, Error) && Automatic->GenerationBands.Last().FirstRoom == 3);

    Automatic->TotalRooms = 6;
    Automatic->GenerationBands.Reset();
    Automatic->PlacedRooms = {{2, First}};
    auto* ManualProperty = FindFProperty<FProperty>(UGridCampaignSequence::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UGridCampaignSequence, PlacedRooms));
    Automatic->PreEditChange(ManualProperty);
    Automatic->PlacedRooms.AddDefaulted();
    FPropertyChangedEvent AddManual(ManualProperty, EPropertyChangeType::ArrayAdd);
    Automatic->PostEditChangeProperty(AddManual);
    TestEqual(TEXT("New manual entry defaults to last position plus one"), Automatic->PlacedRooms.Last().Position, 3);
    Automatic->PlacedRooms.Last().Level = Third;
    Automatic->PreEditChange(BandsProperty);
    Automatic->GenerationBands.Add({2, 4});
    FPropertyChangedEvent CoversManual(BandsProperty, EPropertyChangeType::ValueSet);
    Automatic->PostEditChangeProperty(CoversManual);
    TestTrue(TEXT("A generated range cannot cover hand-authored positions"), Automatic->GenerationBands.IsEmpty());
    Automatic->PreEditChange(ManualProperty);
    Automatic->PlacedRooms.Last().Position = 2;
    FPropertyChangedEvent DuplicateManual(ManualProperty, EPropertyChangeType::ValueSet);
    Automatic->PostEditChangeProperty(DuplicateManual);
    TestEqual(TEXT("A conflicting manual position is restored"), Automatic->PlacedRooms.Last().Position, 3);

    auto* DefaultSequence = LoadObject<UGridCampaignSequence>(nullptr,
        TEXT("/Game/Maps/Campaign/DA_DefaultCampaign.DA_DefaultCampaign"));
    TestTrue(TEXT("The project ships a ready-to-edit mixed sequence"),
        DefaultSequence && DefaultSequence->GetRoomCount() > 0);
    if (DefaultSequence)
    {
        const TGuardValue<TSoftObjectPtr<UGridCampaignSequence>> Source(Settings->CampaignSequence, DefaultSequence);
        Controller->OpenStartMenu();
        Controller->StartNewRun();
        for (int32 Index = 0; Index < DefaultSequence->GetRoomCount(); ++Index)
        {
            TestEqual(FString::Printf(TEXT("The default campaign admits position %d (seed %d, error %s)"),
                Index + 1, Controller->RunSeed, *Controller->GetTransitionError().ToString()), Controller->GetRoomNumber(), Index + 1);
            if (Controller->IsStartMenuOpen()) { break; }
            Controller->Session.State.Outcome = EOutcome::Won;
            Controller->ConfirmOrNextRoom();
        }
    }
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridBackgroundAdmissionTest, "Sokoban.Preview.BackgroundAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridBackgroundAdmissionTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Temporary world"), World)) { return false; }
    auto* Controller = World->SpawnActor<AGridPlayerController>();
    Controller->Scene = World->SpawnActor<AGridScene>();
    Controller->Player = NewObject<ULocalPlayer>(GEngine);
    auto* Sequence = NewObject<UGridCampaignSequence>(Controller);
    auto* Room = NewObject<UGridLevelAsset>(Sequence);
    Room->RoomId = TEXT("BackgroundAdmission");
    Room->Size = {3, 3};
    Room->Start = {0, 0};
    Room->Goal = {2, 2};
    Sequence->Slots.SetNum(2);
    Sequence->Slots[0].Level = Room;
    auto& NextSlot = Sequence->Slots[1];
    NextSlot.Source = EGridSequenceSource::Generated;
    NextSlot.MaxAttempts = 0;
    NextSlot.bAllowFallback = true;
    {
        auto* Settings = GetMutableDefault<UGridProjectSettings>();
        const TGuardValue<TSoftObjectPtr<UGridCampaignSequence>> Source(Settings->CampaignSequence, Sequence);
        Controller->OpenStartMenu();
        Controller->StartNewRun();
    }
    TestTrue(TEXT("The menu stays open while the formal check is pending"),
        Controller->IsStartMenuOpen() && Controller->IsGridBusy() && Controller->GetGridState().Board.RoomId.IsNone());
    Controller->StartNewRun();
    const double Deadline = FPlatformTime::Seconds() + 30.0;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Controller, Deadline, Stage = 0]() mutable {
        if (Controller->IsGridBusy() && FPlatformTime::Seconds() < Deadline) { return false; }
        if (!TestFalse(TEXT("Background check completes within the deadline"), Controller->IsGridBusy()))
        {
            Controller->bGridBusy = false;
            World->DestroyWorld(false);
            return true;
        }
        if (Stage == 0)
        {
            TestTrue(TEXT("Opening commits the checked room on completion"),
                !Controller->IsStartMenuOpen() && Controller->GetGridState().Board.RoomId == TEXT("BackgroundAdmission"));
            Controller->Session.State.Outcome = Sokoban::EOutcome::Won;
            Controller->ConfirmOrNextRoom();
            Controller->ConfirmOrNextRoom();
            TestTrue(TEXT("Repeated confirmation leaves the won room intact while generating"),
                Controller->IsGridBusy() && Controller->GetRoomNumber() == 1 &&
                Controller->GetGridState().Outcome == Sokoban::EOutcome::Won);
            ++Stage;
            return false;
        }
        if (Stage == 1)
        {
            TestEqual(TEXT("Only one checked generated room is entered"), Controller->GetRoomNumber(), 2);
            Controller->ToggleRuntimeEditor();
            Controller->ValidateRuntimeDraft();
            Controller->CloseRuntimeEditor();
            TestTrue(TEXT("The runtime draft stays open while its snapshot is verified"),
                Controller->IsGridBusy() && Controller->IsRuntimeEditing() && !Controller->CanAttemptRuntimeSave());
            ++Stage;
            return false;
        }
        TestTrue(TEXT("Runtime validation publishes the background result"),
            Controller->GetRuntimeValidationStatus() == Sokoban::ECheck::Pass);
        Controller->Player = nullptr;
        World->DestroyWorld(false);
        return true;
    }));
    return true;
}

#endif
