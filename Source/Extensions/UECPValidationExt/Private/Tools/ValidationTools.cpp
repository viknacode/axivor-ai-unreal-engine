// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/ValidationTools.h"
#include "UECPValidationExtModule.h"

#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Misc/DataValidation.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ValidationTools
{
	namespace
	{
		IAssetRegistry& GetAssetRegistry()
		{
			return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		}

		FString ResultToString(EDataValidationResult Result)
		{
			switch (Result)
			{
				case EDataValidationResult::Valid:   return TEXT("valid");
				case EDataValidationResult::Invalid: return TEXT("invalid");
				default:                             return TEXT("not_validated");
			}
		}

		FString SerializeObject(const TSharedRef<FJsonObject>& Obj)
		{
			FString Out;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Obj, Writer);
			return Out;
		}

		// Runs Data Validation over the given assets and serializes a summary + capped issue list.
		void RunValidation(const TArray<FAssetData>& Assets, int32 MaxReported, FString& OutJsonString, FString& OutError)
		{
			if (!GEditor)
			{
				OutError = TEXT("Editor is not available (validation requires an interactive editor).");
				return;
			}
			UEditorValidatorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
			if (!Subsystem)
			{
				OutError = TEXT("Data Validation subsystem is unavailable. Enable the Data Validation plugin and restart.");
				return;
			}

			if (Assets.Num() == 0)
			{
				OutError = TEXT("No assets matched the request.");
				return;
			}

			FValidateAssetsSettings Settings;
			Settings.bCollectPerAssetDetails = true;
			Settings.bShowIfNoFailures       = false;
			Settings.bLoadAssetsForValidation = true;
			Settings.ValidationUsecase       = EDataValidationUsecase::Manual;

			FValidateAssetsResults Results;
			Subsystem->ValidateAssetsWithSettings(Assets, Settings, Results);

			// Collect only the assets that actually have issues.
			TArray<TSharedPtr<FJsonValue>> Issues;
			int32 TotalProblems = 0;
			for (const TPair<FString, FValidateAssetsDetails>& KV : Results.AssetsDetails)
			{
				const FValidateAssetsDetails& Details = KV.Value;
				const bool bHasIssues =
					Details.ValidationErrors.Num() > 0 ||
					Details.ValidationWarnings.Num() > 0 ||
					Details.Result == EDataValidationResult::Invalid;
				if (!bHasIssues) continue;

				++TotalProblems;
				if (Issues.Num() >= MaxReported) continue;

				TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
				A->SetStringField(TEXT("asset"), KV.Key);
				A->SetStringField(TEXT("result"), ResultToString(Details.Result));

				TArray<TSharedPtr<FJsonValue>> Errors;
				for (const FText& E : Details.ValidationErrors) Errors.Add(MakeShared<FJsonValueString>(E.ToString()));
				A->SetArrayField(TEXT("errors"), Errors);

				TArray<TSharedPtr<FJsonValue>> Warnings;
				for (const FText& W : Details.ValidationWarnings) Warnings.Add(MakeShared<FJsonValueString>(W.ToString()));
				A->SetArrayField(TEXT("warnings"), Warnings);

				Issues.Add(MakeShared<FJsonValueObject>(A));
			}

			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetBoolField(TEXT("success"), true);
			Root->SetNumberField(TEXT("num_checked"), Results.NumChecked);
			Root->SetNumberField(TEXT("num_valid"), Results.NumValid);
			Root->SetNumberField(TEXT("num_invalid"), Results.NumInvalid);
			Root->SetNumberField(TEXT("num_warnings"), Results.NumWarnings);
			Root->SetNumberField(TEXT("num_skipped"), Results.NumSkipped);
			Root->SetNumberField(TEXT("num_unable_to_validate"), Results.NumUnableToValidate);
			Root->SetArrayField(TEXT("issues"), Issues);
			Root->SetBoolField(TEXT("truncated"), TotalProblems > Issues.Num());
			Root->SetNumberField(TEXT("total_problem_assets"), TotalProblems);

			OutJsonString = SerializeObject(Root);
		}
	}

	void HandleValidateAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		FString AssetPath;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("asset_path"), AssetPath);
		if (AssetPath.IsEmpty())
		{
			OutError = TEXT("asset_path is required (e.g. /Game/Path/BP_Foo).");
			return;
		}

		// Accept either a package path (/Game/Path/BP_Foo) or a full object path
		// (/Game/Path/BP_Foo.BP_Foo); derive the object path when only the package was given.
		FString ObjectPath = AssetPath;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			FString ShortName = FPackageName::GetShortName(AssetPath);
			ObjectPath = AssetPath + TEXT(".") + ShortName;
		}

		const FAssetData Asset = GetAssetRegistry().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (!Asset.IsValid())
		{
			OutError = FString::Printf(TEXT("Asset '%s' not found in the Asset Registry."), *AssetPath);
			return;
		}

		RunValidation({ Asset }, /*MaxReported*/ 1, OutJsonString, OutError);
	}

	void HandleValidateAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		FString Folder;
		bool bRecursive = true;
		int32 MaxReported = 100;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("folder_path"), Folder);
			Args->TryGetBoolField(TEXT("recursive"), bRecursive);
			double N = 0;
			if (Args->TryGetNumberField(TEXT("max_reported"), N)) MaxReported = FMath::Clamp((int32)N, 1, 1000);
		}
		if (Folder.IsEmpty())
		{
			OutError = TEXT("folder_path is required (e.g. /Game/Weapons).");
			return;
		}

		TArray<FAssetData> Assets;
		GetAssetRegistry().GetAssetsByPath(FName(*Folder), Assets, bRecursive);
		if (Assets.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No assets found under '%s'."), *Folder);
			return;
		}

		RunValidation(Assets, MaxReported, OutJsonString, OutError);
	}

	void HandleValidateProjectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		int32 MaxReported = 100;
		if (Args.IsValid())
		{
			double N = 0;
			if (Args->TryGetNumberField(TEXT("max_reported"), N)) MaxReported = FMath::Clamp((int32)N, 1, 1000);
		}

		TArray<FAssetData> Assets;
		GetAssetRegistry().GetAssetsByPath(FName(TEXT("/Game")), Assets, /*bRecursive*/ true);
		if (Assets.Num() == 0)
		{
			OutError = TEXT("No /Game assets found to validate.");
			return;
		}

		RunValidation(Assets, MaxReported, OutJsonString, OutError);
	}
}
