#include "SokobanGenerateCommandlet.h"

#include "Dom/JsonObject.h"
#include "GridLevelAsset.h"
#include "GridProjectSettings.h"
#include "JsonObjectConverter.h"
#include "LevelGenerator.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"

namespace
{
using namespace Sokoban;

const TCHAR* StatusName(ECheck Status)
{
    switch (Status)
    {
        case ECheck::Pass: return TEXT("Pass");
        case ECheck::Fail: return TEXT("Fail");
        default: return TEXT("Unknown");
    }
}

TSharedPtr<FJsonObject> MakeReport(const FGenerationRequest& Request, const FGenerationResult& Result)
{
    const auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("formatVersion"), 1);
    Report->SetNumberField(TEXT("seed"), Result.Seed);
    Report->SetNumberField(TEXT("requestedComplexity"), Request.Complexity);
    Report->SetNumberField(TEXT("maxAttempts"), Request.MaxAttempts);
    Report->SetStringField(TEXT("status"), StatusName(Result.Status));
    Report->SetNumberField(TEXT("maxActions"), Request.Search.MaxActions);
    Report->SetNumberField(TEXT("nodeBudgetPerStage"), Request.Search.NodeBudget);
    Report->SetNumberField(TEXT("minimumLegalSolutions"), Request.Search.MinimumSolutions);
    Report->SetNumberField(TEXT("targetSolutions"), Request.Search.DifficultyThreshold());
    Report->SetBoolField(TEXT("strengthUnlocked"), Request.Resources.bStrengthUnlocked);
    Report->SetBoolField(TEXT("undoUnlocked"), Request.Resources.bUndoUnlocked);
    const auto WriteInventory = [&](const TCHAR* Name, FInventory Stock) {
        const auto Inventory = MakeShared<FJsonObject>();
        Inventory->SetNumberField(TEXT("strength"), Stock.Strength);
        Inventory->SetNumberField(TEXT("undo"), Stock.Undo);
        Report->SetObjectField(Name, Inventory);
    };
    WriteInventory(TEXT("actualInventory"), Request.Resources.ActualInventory);
    WriteInventory(TEXT("capabilityInventory"), Request.Resources.CapabilityBudget);
    Report->SetBoolField(TEXT("acceptedFallback"), Result.bFallback);
    TArray<TSharedPtr<FJsonValue>> Attempts;
    for (const auto& Entry : Result.Trace)
    {
        const auto Attempt = MakeShared<FJsonObject>();
        Attempt->SetNumberField(TEXT("seed"), Entry.Seed);
        Attempt->SetNumberField(TEXT("complexity"), Entry.Complexity);
        Attempt->SetNumberField(TEXT("attemptComplexity"), Entry.Complexity);
        if (Entry.TemplateComplexity.IsSet())
        {
            Attempt->SetNumberField(TEXT("templateComplexity"), Entry.TemplateComplexity.GetValue());
        }
        Attempt->SetStringField(TEXT("template"), Entry.Template.ToString());
        Attempt->SetStringField(TEXT("status"), StatusName(Entry.Status));
        Attempt->SetNumberField(TEXT("capabilitySolutions"), Entry.CapabilitySolutions);
        Attempt->SetNumberField(TEXT("actualSolutions"), Entry.ActualSolutions);
        Attempt->SetBoolField(TEXT("fallback"), Entry.bFallback);
        TArray<TSharedPtr<FJsonValue>> Errors;
        for (const auto& Error : Entry.LayoutErrors) { Errors.Add(MakeShared<FJsonValueString>(Error)); }
        Attempt->SetArrayField(TEXT("layoutErrors"), MoveTemp(Errors));
        Attempt->SetStringField(TEXT("designIssue"), Entry.DesignIssue);
        Attempts.Add(MakeShared<FJsonValueObject>(Attempt));
    }
    Report->SetArrayField(TEXT("attempts"), MoveTemp(Attempts));
    if (Result.Level.IsSet())
    {
        auto* Draft = NewObject<UGridLevelAsset>();
        Draft->SetFromLevel(Result.Level.GetValue());
        auto Layout = MakeShared<FJsonObject>();
        if (!FJsonObjectConverter::UStructToJsonObject(UGridLevelAsset::StaticClass(), Draft, Layout)) { return nullptr; }
        Report->SetObjectField(TEXT("level"), Layout);
        Report->SetBoolField(TEXT("solutionCountComplete"), Result.Validation.Actual.bComplete);
        TArray<TSharedPtr<FJsonValue>> Witnesses;
        for (const auto& Sequence : Result.Validation.Actual.Witnesses)
        {
            TArray<TSharedPtr<FJsonValue>> Commands;
            for (const auto& Action : Sequence)
            {
                auto Command = MakeShared<FJsonObject>();
                switch (Action.Kind)
                {
                    case EAction::ArmStrength: Command->SetStringField(TEXT("kind"), TEXT("ArmStrength")); break;
                    case EAction::Undo:
                        Command->SetStringField(TEXT("kind"), TEXT("Undo"));
                        Command->SetNumberField(TEXT("steps"), Action.Steps);
                        break;
                    case EAction::Move:
                        Command->SetStringField(TEXT("kind"), TEXT("Move"));
                        Command->SetNumberField(TEXT("dx"), Action.Direction.X);
                        Command->SetNumberField(TEXT("dy"), Action.Direction.Y);
                        break;
                }
                Commands.Add(MakeShared<FJsonValueObject>(Command));
            }
            Witnesses.Add(MakeShared<FJsonValueArray>(MoveTemp(Commands)));
        }
        Report->SetArrayField(TEXT("witnesses"), MoveTemp(Witnesses));
    }
    return Report;
}
}

int32 USokobanGenerateCommandlet::Main(const FString& Params)
{
    const auto* Settings = GetDefault<UGridProjectSettings>();
    FInventory Stock = Settings->StartingInventory();
    FParse::Value(*Params, TEXT("Strength="), Stock.Strength);
    FParse::Value(*Params, TEXT("Undo="), Stock.Undo);
    FGenerationRequest Request;
    Request.Complexity = 2;
    Request.Resources = Settings->MakeResourceContext(Stock);
    Request.Search = Settings->SearchLimits();
    FParse::Value(*Params, TEXT("Seed="), Request.Seed);
    FParse::Value(*Params, TEXT("Complexity="), Request.Complexity);
    FParse::Value(*Params, TEXT("Attempts="), Request.MaxAttempts);
    FParse::Value(*Params, TEXT("MaxActions="), Request.Search.MaxActions);
    FParse::Value(*Params, TEXT("NodeBudget="), Request.Search.NodeBudget);
    Request.bAllowFallback = !FParse::Param(*Params, TEXT("NoFallback"));
    FString Difficulty;
    if (FParse::Value(*Params, TEXT("Difficulty="), Difficulty))
    {
        if (Difficulty.Equals(TEXT("High"), ESearchCase::IgnoreCase)) { Request.Search.TargetDifficulty = EDifficulty::High; }
        else if (Difficulty.Equals(TEXT("Medium"), ESearchCase::IgnoreCase)) { Request.Search.TargetDifficulty = EDifficulty::Medium; }
        else if (Difficulty.Equals(TEXT("Low"), ESearchCase::IgnoreCase)) { Request.Search.TargetDifficulty = EDifficulty::Low; }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Difficulty must be High, Medium or Low."));
            return 3;
        }
    }

    FString Template;
    Request.Search.bRequireDifficultyBand = !Difficulty.IsEmpty();
    if (FParse::Value(*Params, TEXT("Template="), Template))
    {
        const TPair<const TCHAR*, EGenerationTemplate> Choices[] = {
            {TEXT("Detour"), EGenerationTemplate::Detour}, {TEXT("BeamCover"), EGenerationTemplate::BeamCover},
            {TEXT("SpikeSweep"), EGenerationTemplate::SpikeSweep}, {TEXT("BlastRubble"), EGenerationTemplate::BlastRubble},
            {TEXT("StrengthChain"), EGenerationTemplate::StrengthChain},
            {TEXT("EjectCorridor"), EGenerationTemplate::EjectCorridor}
        };
        for (const auto& Choice : Choices)
        {
            if (Template.Equals(Choice.Key, ESearchCase::IgnoreCase)) { Request.AllowedTemplates.Add(Choice.Value); }
        }
        if (Request.AllowedTemplates.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("Unknown level template: %s"), *Template);
            return 3;
        }
    }

    const FGenerationResult Result = FTemplateLevelGenerator().Generate(Request);
    FString Output;
    if (FParse::Value(*Params, TEXT("Output="), Output))
    {
        const auto Report = MakeReport(Request, Result);
        FString Json;
        const auto Writer = TJsonWriterFactory<>::Create(&Json);
        if (!Report || !FJsonSerializer::Serialize(Report.ToSharedRef(), Writer) ||
            !FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            UE_LOG(LogTemp, Error, TEXT("Could not write generation report: %s"), *Output);
            return 3;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Generation status=%s seed=%d attempts=%d fallback=%d actualSolutions=%d"),
        StatusName(Result.Status), Result.Seed, Result.Attempts, Result.bFallback, Result.Validation.Actual.CountLowerBound);
    switch (Result.Status)
    {
        case ECheck::Pass: return 0;
        case ECheck::Fail: return 1;
        default: return 2;
    }
}
