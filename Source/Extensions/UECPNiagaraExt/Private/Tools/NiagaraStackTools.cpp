// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/NiagaraStackTools.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraCommon.h"
#include "NiagaraScriptSourceBase.h"

#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "EditorAssetLibrary.h"
#include "Misc/Guid.h"

namespace NiagaraStackTools
{

namespace
{
	UNiagaraSystem* LoadSystem(const FString& Path, FString& OutError)
	{
		UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Path));
		if (!System) OutError = FString::Printf(TEXT("NiagaraSystem not found at '%s'"), *Path);
		return System;
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	const TCHAR* SeverityToString(EStackIssueSeverity Severity)
	{
		switch (Severity)
		{
			case EStackIssueSeverity::Error:   return TEXT("error");
			case EStackIssueSeverity::Warning: return TEXT("warning");
			case EStackIssueSeverity::Info:    return TEXT("info");
			default: return TEXT("none");
		}
	}

	TSharedPtr<FNiagaraSystemViewModel> MakeDataOnlyViewModel(UNiagaraSystem& System)
	{
		TSharedPtr<FNiagaraSystemViewModel> VM = MakeShared<FNiagaraSystemViewModel>();
		FNiagaraSystemViewModelOptions Options;
		Options.bCanAutoCompile = false;
		Options.bCanSimulate = false;
		Options.bIsForDataProcessingOnly = true;
		Options.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
		Options.MessageLogGuid = FGuid::NewGuid();
		VM->Initialize(System, Options);
		return VM;
	}

	void WalkStack(UNiagaraStackEntry* Entry, TFunctionRef<void(UNiagaraStackEntry*)> Visitor)
	{
		if (!Entry) return;
		Visitor(Entry);
		TArray<UNiagaraStackEntry*> Children;
		Entry->GetFilteredChildren(Children);
		for (UNiagaraStackEntry* Child : Children) WalkStack(Child, Visitor);
	}

	void GatherIssues(FNiagaraSystemViewModel& VM, TArray<TPair<UNiagaraStackEntry*, UNiagaraStackEntry::FStackIssue>>& OutPairs)
	{
		auto Collect = [&](UNiagaraStackEntry* Entry)
		{
			if (!Entry) return;
			for (const UNiagaraStackEntry::FStackIssue& Issue : Entry->GetIssues())
			{
				OutPairs.Emplace(Entry, Issue);
			}
		};

		if (UNiagaraStackViewModel* SysStack = VM.GetSystemStackViewModel())
		{
			SysStack->GetRootEntry()->RefreshChildren();
			WalkStack(SysStack->GetRootEntry(), Collect);
		}
		for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterVM : VM.GetEmitterHandleViewModels())
		{
			if (UNiagaraStackViewModel* EmStack = EmitterVM->GetEmitterStackViewModel())
			{
				EmStack->GetRootEntry()->RefreshChildren();
				WalkStack(EmStack->GetRootEntry(), Collect);
			}
		}
	}

	TSharedPtr<FJsonObject> SerializeIssue(UNiagaraStackEntry* Entry, const UNiagaraStackEntry::FStackIssue& Issue)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("issue_id"), Issue.GetUniqueIdentifier());
		Out->SetStringField(TEXT("severity"), SeverityToString(Issue.GetSeverity()));
		Out->SetStringField(TEXT("summary"), Issue.GetShortDescription().ToString());
		const FString Long = Issue.GetLongDescription().ToString();
		if (!Long.IsEmpty()) Out->SetStringField(TEXT("description"), Long);
		Out->SetStringField(TEXT("source"), Entry ? Entry->GetDisplayName().ToString() : FString());
		Out->SetBoolField(TEXT("can_be_dismissed"), Issue.GetCanBeDismissed());

		TArray<TSharedPtr<FJsonValue>> Fixes;
		for (const UNiagaraStackEntry::FStackIssueFix& Fix : Issue.GetFixes())
		{
			TSharedPtr<FJsonObject> FixObj = MakeShared<FJsonObject>();
			FixObj->SetStringField(TEXT("fix_id"), Fix.GetUniqueIdentifier());
			FixObj->SetStringField(TEXT("description"), Fix.GetDescription().ToString());
			FixObj->SetStringField(TEXT("style"), Fix.GetStyle() == UNiagaraStackEntry::EStackIssueFixStyle::Fix ? TEXT("fix") : TEXT("link"));
			Fixes.Add(MakeShared<FJsonValueObject>(FixObj));
		}
		Out->SetArrayField(TEXT("fixes"), Fixes);
		return Out;
	}
}

void HandleGetStackIssuesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath; Args->TryGetStringField(TEXT("system_path"), SystemPath);
	UNiagaraSystem* System = LoadSystem(SystemPath, OutError); if (!System) return;

	TSharedPtr<FNiagaraSystemViewModel> VM = MakeDataOnlyViewModel(*System);
	if (!VM->GetSystemStackViewModel()) { OutError = TEXT("Failed to initialize transient FNiagaraSystemViewModel"); return; }

	TArray<TPair<UNiagaraStackEntry*, UNiagaraStackEntry::FStackIssue>> Pairs;
	GatherIssues(*VM, Pairs);

	int32 ErrorCount = 0, WarningCount = 0, InfoCount = 0;
	TArray<TSharedPtr<FJsonValue>> Issues;
	for (const auto& Pair : Pairs)
	{
		Issues.Add(MakeShared<FJsonValueObject>(SerializeIssue(Pair.Key, Pair.Value)));
		switch (Pair.Value.GetSeverity())
		{
			case EStackIssueSeverity::Error:   ++ErrorCount; break;
			case EStackIssueSeverity::Warning: ++WarningCount; break;
			case EStackIssueSeverity::Info:    ++InfoCount; break;
			default: break;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Issues.Num());
	Result->SetNumberField(TEXT("error_count"), ErrorCount);
	Result->SetNumberField(TEXT("warning_count"), WarningCount);
	Result->SetNumberField(TEXT("info_count"), InfoCount);
	Result->SetArrayField(TEXT("issues"), Issues);
	OutJson = SerializeJson(Result);
}

void HandleApplyStackIssueFixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, IssueId, FixId;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("issue_id"), IssueId);
	Args->TryGetStringField(TEXT("fix_id"), FixId);
	if (IssueId.IsEmpty() || FixId.IsEmpty()) { OutError = TEXT("issue_id and fix_id required"); return; }
	UNiagaraSystem* System = LoadSystem(SystemPath, OutError); if (!System) return;

	TSharedPtr<FNiagaraSystemViewModel> VM = MakeDataOnlyViewModel(*System);
	if (!VM->GetSystemStackViewModel()) { OutError = TEXT("Failed to initialize transient FNiagaraSystemViewModel"); return; }

	TArray<TPair<UNiagaraStackEntry*, UNiagaraStackEntry::FStackIssue>> Pairs;
	GatherIssues(*VM, Pairs);

	bool bApplied = false;
	FString AppliedDescription;
	for (const auto& Pair : Pairs)
	{
		if (Pair.Value.GetUniqueIdentifier() != IssueId) continue;
		for (const UNiagaraStackEntry::FStackIssueFix& Fix : Pair.Value.GetFixes())
		{
			if (Fix.GetUniqueIdentifier() != FixId) continue;
			if (Fix.GetStyle() != UNiagaraStackEntry::EStackIssueFixStyle::Fix)
			{
				OutError = FString::Printf(TEXT("fix '%s' is a Link-style action, not an automatic fix"), *FixId);
				return;
			}
			Fix.GetFixDelegate().ExecuteIfBound();
			bApplied = true;
			AppliedDescription = Fix.GetDescription().ToString();
			break;
		}
		if (bApplied) break;
	}

	if (!bApplied)
	{
		OutError = FString::Printf(TEXT("issue_id '%s' / fix_id '%s' not found in current stack"), *IssueId, *FixId);
		return;
	}

	System->MarkPackageDirty();

	TArray<TPair<UNiagaraStackEntry*, UNiagaraStackEntry::FStackIssue>> PostFix;
	GatherIssues(*VM, PostFix);
	TArray<TSharedPtr<FJsonValue>> PostFixIssues;
	for (const auto& Pair : PostFix)
	{
		PostFixIssues.Add(MakeShared<FJsonValueObject>(SerializeIssue(Pair.Key, Pair.Value)));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("applied_fix_description"), AppliedDescription);
	Result->SetNumberField(TEXT("post_fix_issue_count"), PostFixIssues.Num());
	Result->SetArrayField(TEXT("post_fix_issues"), PostFixIssues);
	OutJson = SerializeJson(Result);
}

void HandleGetSystemCompileStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath; Args->TryGetStringField(TEXT("system_path"), SystemPath);
	UNiagaraSystem* System = LoadSystem(SystemPath, OutError); if (!System) return;

	int32 ErrorCount = 0, WarningCount = 0;
	TArray<TSharedPtr<FJsonValue>> Scripts;

	auto AddScript = [&](const FString& ScriptDisplayName, UNiagaraScript* Script)
	{
		if (!Script) return;
		const FNiagaraVMExecutableData& VMData = Script->GetVMExecutableData();
		TSharedPtr<FJsonObject> ScriptObj = MakeShared<FJsonObject>();
		ScriptObj->SetStringField(TEXT("script"), ScriptDisplayName);
		ScriptObj->SetStringField(TEXT("status"), VMData.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_UpToDate ? TEXT("up_to_date") :
			VMData.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings ? TEXT("up_to_date_warnings") :
			VMData.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Error ? TEXT("error") :
			VMData.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Dirty ? TEXT("dirty") :
			VMData.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_BeingCreated ? TEXT("being_created") : TEXT("unknown"));

		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FNiagaraCompileEvent& Ev : VMData.LastCompileEvents)
		{
			TSharedPtr<FJsonObject> EvObj = MakeShared<FJsonObject>();
			const TCHAR* Sev = Ev.Severity == FNiagaraCompileEventSeverity::Error ? TEXT("error") :
				Ev.Severity == FNiagaraCompileEventSeverity::Warning ? TEXT("warning") :
				Ev.Severity == FNiagaraCompileEventSeverity::Log ? TEXT("log") :
				Ev.Severity == FNiagaraCompileEventSeverity::Display ? TEXT("display") : TEXT("none");
			EvObj->SetStringField(TEXT("severity"), Sev);
			EvObj->SetStringField(TEXT("message"), Ev.Message);
			Events.Add(MakeShared<FJsonValueObject>(EvObj));
			if (Ev.Severity == FNiagaraCompileEventSeverity::Error) ++ErrorCount;
			else if (Ev.Severity == FNiagaraCompileEventSeverity::Warning) ++WarningCount;
		}
		ScriptObj->SetArrayField(TEXT("compile_events"), Events);
		Scripts.Add(MakeShared<FJsonValueObject>(ScriptObj));
	};

	AddScript(TEXT("System.SystemSpawnScript"), System->GetSystemSpawnScript());
	AddScript(TEXT("System.SystemUpdateScript"), System->GetSystemUpdateScript());

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FString Prefix = Handle.GetName().ToString();
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data) continue;
		Data->ForEachScript([&](UNiagaraScript* Script)
		{
			if (!Script) return;
			const FString ScriptName = FString::Printf(TEXT("%s.%s"), *Prefix, *Script->GetName());
			AddScript(ScriptName, Script);
		});
	}

	const TCHAR* Aggregate = (ErrorCount > 0) ? TEXT("error") : (WarningCount > 0) ? TEXT("warning") : TEXT("ok");

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("aggregate_status"), Aggregate);
	Result->SetNumberField(TEXT("error_count"), ErrorCount);
	Result->SetNumberField(TEXT("warning_count"), WarningCount);
	Result->SetArrayField(TEXT("scripts"), Scripts);
	OutJson = SerializeJson(Result);
}

}
