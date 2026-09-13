// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/PerforceTools.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISourceControlChangelist.h"
#include "ISourceControlChangelistState.h"
#include "SourceControlOperations.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

static FString JsonStr(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	return Out;
}

static bool GetProvider(ISourceControlProvider*& OutProvider, FString& OutError)
{
	ISourceControlModule& SCModule = ISourceControlModule::Get();
	if (!SCModule.IsEnabled())
	{
		OutError = TEXT("Source control is not enabled in Unreal Editor. Go to Edit > Editor Preferences > Source Control to configure.");
		return false;
	}
	OutProvider = &SCModule.GetProvider();
	return true;
}

void PerforceTools::HandleP4ConnectionInfo(FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	auto Result = MakeShared<FJsonObject>();
	FName ProviderName = Provider->GetName();
	Result->SetStringField(TEXT("provider"), ProviderName.ToString());
	Result->SetBoolField(TEXT("enabled"), Provider->IsEnabled());
	Result->SetBoolField(TEXT("available"), Provider->IsAvailable());
	Result->SetBoolField(TEXT("is_perforce"), ProviderName == FName("Perforce"));

	TMap<ISourceControlProvider::EStatus, FString> Status = Provider->GetStatus();

	auto SetIfExists = [&](const FString& Key, ISourceControlProvider::EStatus StatusKey) {
		if (const FString* Val = Status.Find(StatusKey))
			Result->SetStringField(Key, *Val);
	};

	SetIfExists(TEXT("port"), ISourceControlProvider::EStatus::Port);
	SetIfExists(TEXT("user"), ISourceControlProvider::EStatus::User);
	SetIfExists(TEXT("client"), ISourceControlProvider::EStatus::Client);
	SetIfExists(TEXT("workspace"), ISourceControlProvider::EStatus::Workspace);
	SetIfExists(TEXT("workspace_path"), ISourceControlProvider::EStatus::WorkspacePath);
	SetIfExists(TEXT("branch"), ISourceControlProvider::EStatus::Branch);
	SetIfExists(TEXT("changeset"), ISourceControlProvider::EStatus::Changeset);

	Result->SetBoolField(TEXT("success"), true);
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4OpenedFiles(FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> UpdateOp = ISourceControlOperation::Create<FUpdateStatus>();
	UpdateOp->SetGetOpenedOnly(true);
	TArray<FString> ContentFiles = { FPaths::ProjectContentDir() };
	Provider->Execute(UpdateOp, ContentFiles, EConcurrency::Synchronous);

	TArray<FString> AllFiles;
	IFileManager::Get().FindFilesRecursive(AllFiles, *FPaths::ProjectContentDir(), TEXT("*.uasset"), true, false);
	IFileManager::Get().FindFilesRecursive(AllFiles, *FPaths::ProjectContentDir(), TEXT("*.umap"),  true, false);

	TArray<FSourceControlStateRef> States;
	if (AllFiles.Num() > 0)
		Provider->GetState(AllFiles, States, EStateCacheUsage::Use);

	TArray<TSharedPtr<FJsonValue>> FilesArr;
	auto Result = MakeShared<FJsonObject>();
	int32 EditCount = 0, AddCount = 0, DeleteCount = 0;

	for (const FSourceControlStateRef& State : States)
	{
		if (!State->IsCheckedOut() && !State->IsAdded() && !State->IsDeleted()) continue;

		auto FileObj = MakeShared<FJsonObject>();
		FString RelPath = State->GetFilename();
		FPaths::MakePathRelativeTo(RelPath, *FPaths::ProjectDir());
		FileObj->SetStringField(TEXT("file"), RelPath);

		if (State->IsAdded())        { FileObj->SetStringField(TEXT("status"), TEXT("add"));    AddCount++; }
		else if (State->IsDeleted()) { FileObj->SetStringField(TEXT("status"), TEXT("delete")); DeleteCount++; }
		else                         { FileObj->SetStringField(TEXT("status"), TEXT("edit"));   EditCount++; }

		FilesArr.Add(MakeShared<FJsonValueObject>(FileObj));
	}

	Result->SetArrayField(TEXT("files"), FilesArr);
	Result->SetNumberField(TEXT("edit_count"), EditCount);
	Result->SetNumberField(TEXT("add_count"), AddCount);
	Result->SetNumberField(TEXT("delete_count"), DeleteCount);
	Result->SetBoolField(TEXT("success"), true);
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4PendingChangelists(FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FGetPendingChangelists, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FGetPendingChangelists>();
	ECommandResult::Type CmdResult = Provider->Execute(Op, EConcurrency::Synchronous);

	if (CmdResult != ECommandResult::Succeeded)
	{
		OutError = TEXT("Failed to get pending changelists");
		return;
	}

	TArray<FSourceControlChangelistRef> Changelists;
	TArray<FSourceControlChangelistStateRef> ChangelistStates;

	Provider->GetState(Changelists, ChangelistStates, EStateCacheUsage::ForceUpdate);

	TArray<TSharedPtr<FJsonValue>> CLArr;
	for (const FSourceControlChangelistStateRef& CLState : ChangelistStates)
	{
		auto CLObj = MakeShared<FJsonObject>();
		CLObj->SetStringField(TEXT("id"), CLState->GetChangelist()->GetIdentifier());
		CLObj->SetStringField(TEXT("description"), CLState->GetDescriptionText().ToString());
		CLObj->SetNumberField(TEXT("file_count"), CLState->GetFilesStatesNum());
		CLObj->SetNumberField(TEXT("shelved_count"), CLState->GetShelvedFilesStatesNum());
		CLObj->SetBoolField(TEXT("is_default"), CLState->GetChangelist()->IsDefault());
		CLArr.Add(MakeShared<FJsonValueObject>(CLObj));
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("changelists"), CLArr);
	Result->SetBoolField(TEXT("success"), true);
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4Submit(const FString& Description, const TArray<FString>& Files, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	if (Description.IsEmpty())
	{
		OutError = TEXT("Changelist description is required");
		return;
	}

	TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOp = ISourceControlOperation::Create<FCheckIn>();
	CheckInOp->SetDescription(FText::FromString(Description));

	ECommandResult::Type CmdResult = Provider->Execute(CheckInOp, Files, EConcurrency::Synchronous);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	if (CmdResult != ECommandResult::Succeeded)
		Result->SetStringField(TEXT("error"), TEXT("Submit failed"));
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4Sync(bool bForce, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FSync, ESPMode::ThreadSafe> SyncOp = ISourceControlOperation::Create<FSync>();
	SyncOp->SetHeadRevisionFlag(true);
	if (bForce) SyncOp->SetForce(true);

	TArray<FString> ProjectFiles;
	ProjectFiles.Add(FPaths::ProjectContentDir());

	ECommandResult::Type CmdResult = Provider->Execute(SyncOp, ProjectFiles, EConcurrency::Synchronous);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	if (CmdResult != ECommandResult::Succeeded)
		Result->SetStringField(TEXT("error"), TEXT("Sync failed"));
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4Shelve(const FString& Description, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FShelve, ESPMode::ThreadSafe> ShelveOp = ISourceControlOperation::Create<FShelve>();
	ShelveOp->SetDescription(FText::FromString(Description.IsEmpty() ? TEXT("Shelved from plugin") : Description));

	ECommandResult::Type CmdResult = Provider->Execute(ShelveOp, EConcurrency::Synchronous);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	if (CmdResult != ECommandResult::Succeeded)
		Result->SetStringField(TEXT("error"), TEXT("Shelve failed"));
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4Unshelve(const FString& ChangelistId, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FUnshelve, ESPMode::ThreadSafe> UnshelveOp = ISourceControlOperation::Create<FUnshelve>();

	TArray<FString> Files;
	ECommandResult::Type CmdResult = Provider->Execute(UnshelveOp, Files, EConcurrency::Synchronous);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	if (CmdResult != ECommandResult::Succeeded)
		Result->SetStringField(TEXT("error"), TEXT("Unshelve failed"));
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4Revert(const TArray<FString>& Files, bool bUnchangedOnly, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	ECommandResult::Type CmdResult;
	if (bUnchangedOnly)
	{
		TSharedRef<FRevertUnchanged, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FRevertUnchanged>();
		CmdResult = Provider->Execute(Op, Files, EConcurrency::Synchronous);
	}
	else
	{
		TSharedRef<FRevert, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FRevert>();
		CmdResult = Provider->Execute(Op, Files, EConcurrency::Synchronous);
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	if (CmdResult != ECommandResult::Succeeded)
		Result->SetStringField(TEXT("error"), TEXT("Revert failed"));
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4SubmittedChangelists(int32 MaxCount, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FGetSubmittedChangelists, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FGetSubmittedChangelists>();
	Op->SetPaginationLimit(MaxCount > 0 ? MaxCount : 20);
	Op->SetOwnedFilter(true);

	ECommandResult::Type CmdResult = Provider->Execute(Op, EConcurrency::Synchronous);

	TArray<TSharedPtr<FJsonValue>> CLArr;
	if (CmdResult == ECommandResult::Succeeded)
	{
		const TArray<FSourceControlChangelistRef>& SubmittedCLs = Op->GetSubmittedChangelists();
		for (const FSourceControlChangelistRef& CL : SubmittedCLs)
		{
			auto CLObj = MakeShared<FJsonObject>();
			CLObj->SetStringField(TEXT("id"), CL->GetIdentifier());
			CLArr.Add(MakeShared<FJsonValueObject>(CLObj));
		}
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("changelists"), CLArr);
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4Checkout(const TArray<FString>& Files, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FCheckOut, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FCheckOut>();
	ECommandResult::Type CmdResult = Provider->Execute(Op, Files, EConcurrency::Synchronous);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	if (CmdResult != ECommandResult::Succeeded)
		Result->SetStringField(TEXT("error"), TEXT("Checkout failed"));
	OutJson = JsonStr(Result);
}

void PerforceTools::HandleP4MarkForAdd(const TArray<FString>& Files, FString& OutJson, FString& OutError)
{
	ISourceControlProvider* Provider = nullptr;
	if (!GetProvider(Provider, OutError)) return;

	TSharedRef<FMarkForAdd, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FMarkForAdd>();
	ECommandResult::Type CmdResult = Provider->Execute(Op, Files, EConcurrency::Synchronous);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), CmdResult == ECommandResult::Succeeded);
	if (CmdResult != ECommandResult::Succeeded)
		Result->SetStringField(TEXT("error"), TEXT("Mark for add failed"));
	OutJson = JsonStr(Result);
}
