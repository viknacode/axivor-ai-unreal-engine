// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/NiagaraTools.h"
#include "Utils/MountResolver.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/EditorProfileSync.h"
#include "Managers/CapabilityProfile.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Modules/ModuleManager.h"
#include "Misc/EngineVersionComparison.h"

#include "NiagaraSystem.h"
#include "NiagaraCommon.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraTypes.h"
#include "NiagaraParameterStore.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "NiagaraScript.h"
#include "NiagaraShared.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Stateless/NiagaraStatelessDistribution.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessModule.h"
#include "UObject/UObjectIterator.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraConstants.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeInput.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptSource.h"
#include "NiagaraDataInterface.h"
#include "NiagaraVariableMetaData.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "NiagaraSystemFactoryNew.h"

namespace NiagaraTools
{

static UEdGraphPin* FindNiagaraParameterMapPin(UNiagaraNode* Node, EEdGraphPinDirection Direction);

static void BuildSuccessJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJsonString)
{
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

static void SetError(const FString& Msg, FString& OutJsonString, FString& OutError)
{
	OutError = Msg;
	TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
	Fail->SetBoolField(TEXT("success"), false);
	Fail->SetStringField(TEXT("error"), Msg);
	BuildSuccessJson(Fail, OutJsonString);
}

static TArray<FNiagaraVariable> CollectModuleInputs(UNiagaraNodeFunctionCall* FuncCall)
{
	TArray<FNiagaraVariable> Result;
	if (!FuncCall) return Result;

	UEdGraph* CalledGraph = FuncCall->GetCalledGraph();
	if (CalledGraph)
	{
		static UClass* ParamMapGetClass = FindObject<UClass>(
			nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapGet"));

		if (ParamMapGetClass)
		{
			TSet<FName> Seen;
			for (UEdGraphNode* N : CalledGraph->Nodes)
			{
				if (!N || !N->IsA(ParamMapGetClass)) continue;
				for (UEdGraphPin* Pin : N->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output) continue;
					if (Pin->PinName.IsNone()) continue;

					FNameBuilder PinNameStr(Pin->PinName);
					if (!FStringView(PinNameStr).StartsWith(TEXT("Module."))) continue;
					if (Seen.Contains(Pin->PinName)) continue;
					Seen.Add(Pin->PinName);

					const FNiagaraTypeDefinition PinType =
						UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
					if (!PinType.IsValid()) continue;
					Result.Add(FNiagaraVariable(PinType, Pin->PinName));
				}
			}
			Result.Sort([](const FNiagaraVariable& A, const FNiagaraVariable& B)
			{
				return A.GetName().LexicalLess(B.GetName());
			});
		}
	}

	if (Result.Num() == 0)
		Result.Append(FuncCall->Signature.Inputs);

	return Result;
}

struct FModuleInputState
{
	FString CurrentValue;
	FString Source;
	FString LinkedNodeTitle;
};

static FModuleInputState GetModuleInputState(UNiagaraNodeFunctionCall* FuncCall, const FNiagaraVariable& Input)
{
	FModuleInputState State;
	State.Source = TEXT("default");
	if (!FuncCall) return State;

	FString ShortName = Input.GetName().ToString();
	if (ShortName.StartsWith(TEXT("Module."))) ShortName = ShortName.Mid(7);

	UEdGraphPin* MapInputPin = nullptr;
	for (UEdGraphPin* Pin : FuncCall->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input) continue;
		if (Pin->PinType.PinSubCategoryObject == FNiagaraTypeDefinition::GetParameterMapStruct())
		{
			MapInputPin = Pin;
			break;
		}
	}
	if (!MapInputPin || MapInputPin->LinkedTo.Num() != 1) return State;

	UEdGraphPin* SrcPin = MapInputPin->LinkedTo[0];
	UEdGraphNode* OverrideNode = SrcPin ? SrcPin->GetOwningNode() : nullptr;
	if (!OverrideNode) return State;

	static UClass* ParamMapSetClass = FindObject<UClass>(
		nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
	if (!ParamMapSetClass || !OverrideNode->IsA(ParamMapSetClass)) return State;

	UEdGraphPin* OverridePin = nullptr;
	for (UEdGraphPin* Pin : OverrideNode->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input) continue;
		if (Pin->PinType.PinSubCategoryObject == FNiagaraTypeDefinition::GetParameterMapStruct()) continue;
		const FString PinNameStr = Pin->PinName.ToString();
		if (PinNameStr.EndsWith(FString(TEXT(".")) + ShortName, ESearchCase::IgnoreCase)
			|| PinNameStr.EndsWith(FString(TEXT(".Module.")) + ShortName, ESearchCase::IgnoreCase))
		{
			OverridePin = Pin;
			break;
		}
	}

	if (!OverridePin) return State;

	if (OverridePin->LinkedTo.Num() > 0)
	{
		UEdGraphPin* LinkedSrcPin = OverridePin->LinkedTo[0];
		UEdGraphNode* LinkedSrcNode = LinkedSrcPin ? LinkedSrcPin->GetOwningNode() : nullptr;
		if (LinkedSrcNode)
		{
			State.Source = FString::Printf(TEXT("linked:%s"), *LinkedSrcNode->GetClass()->GetName());
			State.LinkedNodeTitle = LinkedSrcNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		}
		else
		{
			State.Source = TEXT("linked");
		}
	}
	else if (!OverridePin->DefaultValue.IsEmpty())
	{
		State.CurrentValue = OverridePin->DefaultValue;
		State.Source = TEXT("literal");
	}

	return State;
}

struct FModuleInputMetadata
{
	FString Description;
	FString UIMin;
	FString UIMax;
	TArray<FString> EnumValues;
};

static FModuleInputMetadata GetModuleInputMetadata(UNiagaraNodeFunctionCall* FuncCall, const FNiagaraVariable& Input)
{
	FModuleInputMetadata Meta;
	if (!FuncCall) return Meta;

	if (const UEnum* En = Input.GetType().GetEnum())
	{
		const int32 Count = En->NumEnums();
		for (int32 i = 0; i < Count; i++)
		{
			const FString Name = En->GetNameStringByIndex(i);
			if (Name.EndsWith(TEXT("_MAX"))) continue;
			Meta.EnumValues.Add(Name);
		}
	}

	UNiagaraGraph* NiagaraGraph = Cast<UNiagaraGraph>(FuncCall->GetCalledGraph());
	if (!NiagaraGraph) return Meta;

	TOptional<FNiagaraVariableMetaData> MaybeMeta = NiagaraGraph->GetMetaData(Input);
	if (!MaybeMeta.IsSet()) return Meta;

	const FNiagaraVariableMetaData& VarMeta = MaybeMeta.GetValue();
	Meta.Description = VarMeta.Description.ToString();

	if (const FString* V = VarMeta.PropertyMetaData.Find(FName("UIMin")))    Meta.UIMin = *V;
	if (const FString* V = VarMeta.PropertyMetaData.Find(FName("UIMax")))    Meta.UIMax = *V;
	if (Meta.UIMin.IsEmpty()) if (const FString* V = VarMeta.PropertyMetaData.Find(FName("ClampMin"))) Meta.UIMin = *V;
	if (Meta.UIMax.IsEmpty()) if (const FString* V = VarMeta.PropertyMetaData.Find(FName("ClampMax"))) Meta.UIMax = *V;

	return Meta;
}

struct FModuleStaticSwitch
{
	FName            Name;
	FNiagaraTypeDefinition Type;
	FString          CurrentValue;
	FString          CurrentValueDisplay;
	TArray<FString>  EnumValues;
	TArray<FString>  EnumDisplayNames;
};

static UEdGraphPin* FindStaticSwitchInputPinManual(UNiagaraNodeFunctionCall* FuncCall, FName VariableName)
{
	if (!FuncCall) return nullptr;
	UNiagaraGraph* CalledGraph = Cast<UNiagaraGraph>(FuncCall->GetCalledGraph());
	if (!CalledGraph) return nullptr;

	bool bIsValidSwitchName = false;
	for (const FNiagaraVariable& Var : CalledGraph->FindStaticSwitchInputs())
	{
		if (Var.GetName().IsEqual(VariableName))
		{
			bIsValidSwitchName = true;
			break;
		}
	}
	if (!bIsValidSwitchName) return nullptr;

	for (UEdGraphPin* Pin : FuncCall->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->GetFName().IsEqual(VariableName))
			return Pin;
	}
	return nullptr;
}

static TArray<FModuleStaticSwitch> CollectModuleStaticSwitches(UNiagaraNodeFunctionCall* FuncCall)
{
	TArray<FModuleStaticSwitch> Out;
	if (!FuncCall) return Out;

	UNiagaraGraph* CalledGraph = Cast<UNiagaraGraph>(FuncCall->GetCalledGraph());
	if (!CalledGraph) return Out;

	for (const FNiagaraVariable& Var : CalledGraph->FindStaticSwitchInputs())
	{
		FModuleStaticSwitch Sw;
		Sw.Name = Var.GetName();
		Sw.Type = Var.GetType();

		if (UEdGraphPin* Pin = FindStaticSwitchInputPinManual(FuncCall, Var.GetName()))
		{
			Sw.CurrentValue = Pin->DefaultValue;
		}
		if (const UEnum* En = Sw.Type.GetEnum())
		{
			const int32 Count = En->NumEnums();
			for (int32 i = 0; i < Count; i++)
			{
				const FString Raw = En->GetNameStringByIndex(i);
				if (Raw.EndsWith(TEXT("_MAX"))) continue;
				const FString Display = En->GetDisplayNameTextByIndex(i).ToString();
				Sw.EnumValues.Add(Raw);
				Sw.EnumDisplayNames.Add(Display);
				if (Raw == Sw.CurrentValue) Sw.CurrentValueDisplay = Display;
			}
		}
		Out.Add(MoveTemp(Sw));
	}

	Out.Sort([](const FModuleStaticSwitch& A, const FModuleStaticSwitch& B)
	{
		return A.Name.LexicalLess(B.Name);
	});
	return Out;
}

static bool SetModuleStaticSwitch(UNiagaraNodeFunctionCall* FuncCall, FName VarName, const FString& NewValue)
{
	if (!FuncCall) return false;
	UEdGraphPin* Pin = FindStaticSwitchInputPinManual(FuncCall, VarName);
	if (!Pin) return false;
	if (const UEdGraphSchema* Schema = FuncCall->GetSchema())
	{
		Schema->TrySetDefaultValue(*Pin, NewValue);
	}
	else
	{
		Pin->DefaultValue = NewValue;
	}
	return true;
}

static FString StripObjectSuffix(const FString& Path)
{
	int32 DotIdx;
	if (Path.FindLastChar('.', DotIdx))
	{
		return Path.Left(DotIdx);
	}
	return Path;
}

static void RebuildStatelessEmitterCache(UNiagaraSystem* System);
static void CompileAndReport(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& ResultObj);

static UNiagaraScript* ResolveClassicEmitterScript(
    FVersionedNiagaraEmitterData* EmitterData,
    const FString& EmitterName,
    const FString& ScriptSection,
    const FString& EventName,
    FString& OutError)
{
    if (!EmitterData) { OutError = TEXT("Emitter has no data — asset may be corrupt"); return nullptr; }

    const FString SectionLower = ScriptSection.ToLower();
    if (SectionLower.IsEmpty() || SectionLower == TEXT("particle_spawn") || SectionLower == TEXT("spawn") || SectionLower == TEXT("particlespawn"))
        return EmitterData->SpawnScriptProps.Script;
    if (SectionLower == TEXT("particle_update") || SectionLower == TEXT("update") || SectionLower == TEXT("particleupdate"))
        return EmitterData->UpdateScriptProps.Script;
    if (SectionLower == TEXT("emitter_spawn") || SectionLower == TEXT("emitterspawn"))
        return EmitterData->EmitterSpawnScriptProps.Script;
    if (SectionLower == TEXT("emitter_update") || SectionLower == TEXT("emitterupdate"))
        return EmitterData->EmitterUpdateScriptProps.Script;
    if (SectionLower == TEXT("event_handler") || SectionLower == TEXT("event"))
    {
        const TArray<FNiagaraEventScriptProperties>& Handlers = EmitterData->GetEventHandlers();
        if (Handlers.Num() == 0)
        {
            OutError = FString::Printf(TEXT("Emitter '%s' has no event handlers. Call add_niagara_event_handler first."), *EmitterName);
            return nullptr;
        }
        if (!EventName.IsEmpty())
        {
            for (const FNiagaraEventScriptProperties& H : Handlers)
                if (H.SourceEventName.ToString().Equals(EventName, ESearchCase::IgnoreCase))
                    return H.Script;
            TArray<FString> EvNames;
            for (const FNiagaraEventScriptProperties& H : Handlers) EvNames.Add(H.SourceEventName.ToString());
            OutError = FString::Printf(TEXT("Event handler '%s' not found on '%s'. Available: %s"),
                *EventName, *EmitterName, *FString::Join(EvNames, TEXT(", ")));
            return nullptr;
        }
        if (Handlers.Num() == 1) return Handlers[0].Script;
        TArray<FString> EvNames;
        for (const FNiagaraEventScriptProperties& H : Handlers) EvNames.Add(H.SourceEventName.ToString());
        OutError = FString::Printf(TEXT("%d event handlers on '%s' — pass event_name. Available: %s"),
            Handlers.Num(), *EmitterName, *FString::Join(EvNames, TEXT(", ")));
        return nullptr;
    }
    OutError = FString::Printf(
        TEXT("script_section '%s' not recognised. Valid: particle_spawn, particle_update, emitter_spawn, emitter_update, event_handler."),
        *ScriptSection);
    return nullptr;
}

struct FNiagaraPreFlightIssue
{
	FString Code;
	FString Hint;
	bool    bHardFail = true;
};
static void EmitPreFlightFailureJson(const FNiagaraPreFlightIssue& Issue, FString& OutJsonString, FString& OutError);
static void RunAddModulePreFlight(const FString& ModulePath, const FString& ScriptSection,
	TArray<FNiagaraPreFlightIssue>& OutIssues);

void HandleCreateNiagaraSystem(const FString& Name, const FString& SavePath, const FString& TemplatePath, FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty())
	{
		SetError(TEXT("'name' is required."), OutJsonString, OutError);
		return;
	}

	FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/Effects") : SavePath;
	if (!UECPMountResolver::IsValidMountedPath(TargetPath))
	{
		SetError(FString::Printf(TEXT("save_path '%s' must be under a mounted content path (/Game/... or /<PluginName>/...)"), *TargetPath), OutJsonString, OutError);
		return;
	}

	FString FullPath = TargetPath / Name;
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		UNiagaraSystem* Existing = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(FullPath));
		if (Existing)
		{
			TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
			Resp->SetBoolField(TEXT("success"), true);
			Resp->SetBoolField(TEXT("existed"), true);
			Resp->SetStringField(TEXT("system_path"), Existing->GetPathName());
			Resp->SetStringField(TEXT("name"), Name);
			Resp->SetStringField(TEXT("note"), TEXT("NiagaraSystem already existed at this path — returned as-is. Existing emitters and modules are intact. To force a fresh system, delete the existing asset first via asset_management(action='delete_asset')."));
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(Resp.ToSharedRef(), Writer);
			return;
		}
		SetError(FString::Printf(TEXT("An asset of a different class already exists at: %s. Delete via asset_management(action='delete_asset') first or pick a different name."), *FullPath), OutJsonString, OutError);
		return;
	}

	UObject* Created = nullptr;
	FString CopiedFrom;

	if (TemplatePath.IsEmpty())
	{
		UPackage* Package = CreatePackage(*FullPath);
		Package->FullyLoad();
		UNiagaraSystem* NewSystem = NewObject<UNiagaraSystem>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		if (!NewSystem)
		{
			SetError(FString::Printf(TEXT("Failed to create empty NiagaraSystem at '%s'."), *FullPath), OutJsonString, OutError);
			return;
		}
		UNiagaraSystemFactoryNew::InitializeSystem(NewSystem, true);
		Created = NewSystem;
		CopiedFrom = TEXT("(empty — no template)");
	}
	else
	{
		UNiagaraSystem* SourceSystem = LoadObject<UNiagaraSystem>(nullptr, *TemplatePath);
		if (!SourceSystem)
		{
			FString SourcePackagePath = StripObjectSuffix(TemplatePath);
			SourceSystem = LoadObject<UNiagaraSystem>(nullptr, *SourcePackagePath);
		}
		if (!SourceSystem)
		{
			SetError(FString::Printf(TEXT("Could not load NiagaraSystem template: %s"), *TemplatePath), OutJsonString, OutError);
			return;
		}

		if (!SourceSystem->IsReadyToRun())
		{
			SourceSystem->WaitForCompilationComplete();
		}

		UPackage* Package = CreatePackage(*FullPath);
		Package->FullyLoad();
		UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(
			StaticDuplicateObject(SourceSystem, Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional));
		if (!NewSystem)
		{
			SetError(FString::Printf(TEXT("Failed to duplicate template '%s'."), *TemplatePath), OutJsonString, OutError);
			return;
		}

		NewSystem->TemplateAssetDescription = FText();
		NewSystem->Category = FText();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
		NewSystem->AssetTags.Empty();
#endif

		NewSystem->RequestCompile(false);

		Created = NewSystem;
		CopiedFrom = TemplatePath;
	}

	Created->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Created);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("system_path"), Created->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("copied_from"), CopiedFrom);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleCreateNiagaraEmitter(const FString& Name, const FString& SavePath, const FString& TemplatePath, FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty())
	{
		SetError(TEXT("'name' is required."), OutJsonString, OutError);
		return;
	}

	if (TemplatePath.IsEmpty())
	{
		SetError(TEXT("template_path is required. Call list_niagara_templates(emitters_only=true) to find an emitter template."), OutJsonString, OutError);
		return;
	}

	FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/Effects/Emitters") : SavePath;
	if (!UECPMountResolver::IsValidMountedPath(TargetPath))
	{
		SetError(FString::Printf(TEXT("save_path '%s' must be under a mounted content path (/Game/... or /<PluginName>/...)"), *TargetPath), OutJsonString, OutError);
		return;
	}

	FString FullPath = TargetPath / Name;
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		SetError(FString::Printf(TEXT("Asset already exists: %s"), *FullPath), OutJsonString, OutError);
		return;
	}

	FString SourcePackagePath = StripObjectSuffix(TemplatePath);
	UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourcePackagePath, FullPath);
	if (!Duplicated)
	{
		SetError(FString::Printf(TEXT("Failed to duplicate emitter template '%s'. Verify the path is correct using list_niagara_templates."), *TemplatePath), OutJsonString, OutError);
		return;
	}

	Duplicated->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Duplicated);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("emitter_path"), Duplicated->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("copied_from"), TemplatePath);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddEmitterToSystem(const FString& SystemPath, const FString& EmitterPath, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError);
		return;
	}

	UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
	if (!Emitter)
	{
		SetError(FString::Printf(TEXT("Could not load NiagaraEmitter: %s"), *EmitterPath), OutJsonString, OutError);
		return;
	}

	System->Modify();
	const FGuid EmitterVersion = Emitter->GetExposedVersion().VersionGuid;
	const FGuid NewHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, EmitterVersion,  true);

	FString NewHandleName;
	FNiagaraEmitterHandle* NewHandle = nullptr;
	for (FNiagaraEmitterHandle& H : System->GetEmitterHandles())
	{
		if (H.GetId() == NewHandleId) { NewHandleName = H.GetName().ToString(); NewHandle = &H; break; }
	}

	int32 UpgradedNodeCount = 0;
	if (NewHandle && NewHandle->GetInstance().Emitter)
	{
		if (FVersionedNiagaraEmitterData* Data = NewHandle->GetEmitterData())
		{
			for (UNiagaraScript* Script : { Data->SpawnScriptProps.Script, Data->UpdateScriptProps.Script,
				Data->EmitterSpawnScriptProps.Script, Data->EmitterUpdateScriptProps.Script })
			{
				if (!Script) continue;
				UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
				if (!Src || !Src->NodeGraph) continue;
				for (UEdGraphNode* N : Src->NodeGraph->Nodes)
				{
					UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(N);
					if (!FC || !FC->FunctionScript) continue;
					const FGuid LatestGuid = FC->FunctionScript->GetExposedVersion().VersionGuid;
					if (LatestGuid.IsValid() && FC->SelectedScriptVersion != LatestGuid)
					{
						FC->SelectedScriptVersion = LatestGuid;
						++UpgradedNodeCount;
					}
				}
			}
		}
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("system_path"), System->GetPathName());
	Result->SetStringField(TEXT("emitter_handle_name"), NewHandleName);
	Result->SetNumberField(TEXT("upgraded_module_nodes"), UpgradedNodeCount);
	CompileAndReport(System, Result);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleSetNiagaraParameter(const FString& SystemPath, const FString& ParameterName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError);
		return;
	}

	FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();

	TArray<FNiagaraVariable> UserParams;
	Store.GetUserParameters(UserParams);

	FString SearchLower = ParameterName.ToLower();
	if (SearchLower.StartsWith(TEXT("user.")))
	{
		SearchLower = SearchLower.Mid(5);
	}

	const FNiagaraVariable* FoundVar = nullptr;
	for (const FNiagaraVariable& Var : UserParams)
	{
		FString VarNameLower = Var.GetName().ToString().ToLower();
		FString VarStripped = VarNameLower;
		if (VarStripped.StartsWith(TEXT("user.")))
		{
			VarStripped = VarStripped.Mid(5);
		}
		if (VarNameLower == SearchLower || VarStripped == SearchLower)
		{
			FoundVar = &Var;
			break;
		}
	}

	if (!FoundVar)
	{
		auto IsInternalNamespace = [](const FString& Name) -> bool
		{
			static const TArray<FString> InternalPrefixes {
				TEXT("Engine."), TEXT("Particles."), TEXT("Emitter."),
				TEXT("System."), TEXT("Module."), TEXT("Local."),
				TEXT("NPC."), TEXT("MAP."), TEXT("Output."), TEXT("Constant.")
			};
			for (const FString& P : InternalPrefixes)
				if (Name.StartsWith(P, ESearchCase::IgnoreCase)) return true;
			return false;
		};
		const bool bHasUserPrefix = ParameterName.StartsWith(TEXT("User."), ESearchCase::IgnoreCase);
		const bool bIsInternalNs = !bHasUserPrefix && IsInternalNamespace(ParameterName);
		const bool bIsUserParam = bHasUserPrefix || !bIsInternalNs;
		FName CanonicalName = bHasUserPrefix
			? FName(*ParameterName)
			: FName(*FString::Printf(TEXT("User.%s"), *ParameterName));

		FNiagaraTypeDefinition InferredType;
		double NumProbe = 0.0;
		bool BoolProbe = false;
		const TArray<TSharedPtr<FJsonValue>>* ArrProbe = nullptr;
		FString StrProbe;
		if (ValueJson->TryGetNumberField(TEXT("float_value"), NumProbe))
		{
			InferredType = FNiagaraTypeDefinition::GetFloatDef();
		}
		else if (ValueJson->TryGetNumberField(TEXT("int_value"), NumProbe))
		{
			InferredType = FNiagaraTypeDefinition::GetIntDef();
		}
		else if (ValueJson->TryGetBoolField(TEXT("bool_value"), BoolProbe))
		{
			InferredType = FNiagaraTypeDefinition::GetBoolDef();
		}
		else if (ValueJson->TryGetArrayField(TEXT("vector_value"), ArrProbe) && ArrProbe)
		{
			const int32 N = ArrProbe->Num();
			if      (N >= 4) InferredType = FNiagaraTypeDefinition::GetColorDef();
			else if (N == 3) InferredType = FNiagaraTypeDefinition::GetVec3Def();
			else if (N == 2) InferredType = FNiagaraTypeDefinition::GetVec2Def();
		}
		else if (ValueJson->TryGetStringField(TEXT("string_value"), StrProbe))
		{
			const FString L = StrProbe.ToLower();
			if      (L == TEXT("float"))    InferredType = FNiagaraTypeDefinition::GetFloatDef();
			else if (L == TEXT("int") || L == TEXT("int32")) InferredType = FNiagaraTypeDefinition::GetIntDef();
			else if (L == TEXT("bool"))     InferredType = FNiagaraTypeDefinition::GetBoolDef();
			else if (L == TEXT("color") || L == TEXT("linearcolor")) InferredType = FNiagaraTypeDefinition::GetColorDef();
			else if (L == TEXT("vector3") || L == TEXT("vec3"))     InferredType = FNiagaraTypeDefinition::GetVec3Def();
			else if (L == TEXT("vector2") || L == TEXT("vec2"))     InferredType = FNiagaraTypeDefinition::GetVec2Def();
		}

		static thread_local FNiagaraVariable AutoCreatedVar;
		if (bIsUserParam && InferredType.IsValid())
		{
			AutoCreatedVar = FNiagaraVariable(InferredType, CanonicalName);
			Store.AddParameter(AutoCreatedVar, true, true);
			FoundVar = &AutoCreatedVar;
		}

		if (!FoundVar)
		{
			TArray<FString> Available;
			for (const FNiagaraVariable& Var : UserParams)
			{
				Available.Add(Var.GetName().ToString());
			}
			FString AvailStr = Available.Num() > 0 ? FString::Join(Available, TEXT(", ")) : TEXT("(none)");
			const FString Hint = bIsUserParam
				? TEXT(" Pass float_value / int_value / bool_value / vector_value (or string_value with a type name like \"color\") so we can infer the type to create.")
				: TEXT(" To auto-create, prefix with \"User.\" — internal-namespace params (Engine.*, Particles.*) cannot be created via this tool.");
			SetError(FString::Printf(TEXT("Parameter '%s' not found and not auto-created.%s Available: %s"), *ParameterName, *Hint, *AvailStr), OutJsonString, OutError);
			return;
		}
	}

	System->Modify();
	bool bSet = false;
	FString TypeName = FoundVar->GetType().GetName();

	if (FoundVar->GetType() == FNiagaraTypeDefinition::GetFloatDef())
	{
		double FloatVal = 0.0;
		if (ValueJson->TryGetNumberField(TEXT("float_value"), FloatVal)
			|| ValueJson->TryGetNumberField(TEXT("int_value"), FloatVal)
			|| ValueJson->TryGetNumberField(TEXT("value"), FloatVal))
		{
			Store.SetParameterValue<float>((float)FloatVal, *FoundVar);
			bSet = true;
		}
	}
	else if (FoundVar->GetType() == FNiagaraTypeDefinition::GetIntDef())
	{
		double IntVal = 0.0;
		if (ValueJson->TryGetNumberField(TEXT("int_value"), IntVal)
			|| ValueJson->TryGetNumberField(TEXT("float_value"), IntVal)
			|| ValueJson->TryGetNumberField(TEXT("value"), IntVal))
		{
			Store.SetParameterValue<int32>((int32)IntVal, *FoundVar);
			bSet = true;
		}
	}
	else if (FoundVar->GetType() == FNiagaraTypeDefinition::GetBoolDef())
	{
		bool BoolVal = false;
		if (ValueJson->TryGetBoolField(TEXT("bool_value"), BoolVal))
		{
			FNiagaraBool NiagaraBool;
			NiagaraBool.SetValue(BoolVal);
			Store.SetParameterValue<FNiagaraBool>(NiagaraBool, *FoundVar);
			bSet = true;
		}
	}
	else if (FoundVar->GetType() == FNiagaraTypeDefinition::GetColorDef())
	{
		const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
		if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArr) && VecArr && VecArr->Num() >= 3)
		{
			float R = (float)(*VecArr)[0]->AsNumber();
			float G = (float)(*VecArr)[1]->AsNumber();
			float B = (float)(*VecArr)[2]->AsNumber();
			float A = VecArr->Num() >= 4 ? (float)(*VecArr)[3]->AsNumber() : 1.0f;
			Store.SetParameterValue<FLinearColor>(FLinearColor(R, G, B, A), *FoundVar);
			bSet = true;
		}
	}
	else if (FoundVar->GetType() == FNiagaraTypeDefinition::GetVec3Def() ||
	         FoundVar->GetType() == FNiagaraTypeDefinition::GetPositionDef())
	{
		const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
		if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArr) && VecArr && VecArr->Num() >= 3)
		{
			float X = (float)(*VecArr)[0]->AsNumber();
			float Y = (float)(*VecArr)[1]->AsNumber();
			float Z = (float)(*VecArr)[2]->AsNumber();
			Store.SetParameterValue<FVector3f>(FVector3f(X, Y, Z), *FoundVar);
			bSet = true;
		}
	}
	else if (FoundVar->GetType() == FNiagaraTypeDefinition::GetVec2Def())
	{
		const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
		if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArr) && VecArr && VecArr->Num() >= 2)
		{
			float X = (float)(*VecArr)[0]->AsNumber();
			float Y = (float)(*VecArr)[1]->AsNumber();
			Store.SetParameterValue<FVector2f>(FVector2f(X, Y), *FoundVar);
			bSet = true;
		}
	}
	else if (FoundVar->GetType() == FNiagaraTypeDefinition::GetVec4Def())
	{
		const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
		if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArr) && VecArr && VecArr->Num() >= 4)
		{
			float X = (float)(*VecArr)[0]->AsNumber();
			float Y = (float)(*VecArr)[1]->AsNumber();
			float Z = (float)(*VecArr)[2]->AsNumber();
			float W = (float)(*VecArr)[3]->AsNumber();
			Store.SetParameterValue<FVector4f>(FVector4f(X, Y, Z, W), *FoundVar);
			bSet = true;
		}
	}

	if (!bSet)
	{
		SetError(FString::Printf(
			TEXT("Could not set parameter '%s' (type: %s). Provide float_value (float/int), bool_value (bool), or vector_value [R,G,B] or [R,G,B,A] (color/vec3/vec4)."),
			*ParameterName, *TypeName), OutJsonString, OutError);
		return;
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("parameter"), FoundVar->GetName().ToString());
	Result->SetStringField(TEXT("type"), TypeName);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleGetNiagaraSummary(const FString& SystemPath, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("system_path"), System->GetPathName());

	TArray<TSharedPtr<FJsonValue>> EmittersArray;
	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	for (const FNiagaraEmitterHandle& Handle : Handles)
	{
		TSharedPtr<FJsonObject> EmitterObj = MakeShareable(new FJsonObject);
		EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
		EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
		EmittersArray.Add(MakeShareable(new FJsonValueObject(EmitterObj)));
	}
	Result->SetArrayField(TEXT("emitters"), EmittersArray);

	TArray<TSharedPtr<FJsonValue>> ParamsArray;
	FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
	TArray<FNiagaraVariable> UserParams;
	Store.GetUserParameters(UserParams);
	for (const FNiagaraVariable& Var : UserParams)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
		ParamObj->SetStringField(TEXT("name"), Var.GetName().ToString());
		ParamObj->SetStringField(TEXT("type"), Var.GetType().GetName());
		ParamsArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
	}
	Result->SetArrayField(TEXT("user_parameters"), ParamsArray);

	Result->SetNumberField(TEXT("emitter_count"), Handles.Num());
	Result->SetNumberField(TEXT("parameter_count"), UserParams.Num());

	BuildSuccessJson(Result, OutJsonString);
}

static FVersionedNiagaraEmitterData* GetBestEmitterData(FNiagaraEmitterHandle* Handle)
{
	if (!Handle) return nullptr;
	FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
	if (!Data || (!Data->SpawnScriptProps.Script && Data->GetRenderers().Num() == 0))
	{
		FVersionedNiagaraEmitter VI = Handle->GetInstance();
		if (VI.Emitter)
		{
			Data = VI.Emitter->GetLatestEmitterData();
		}
	}
	return Data;
}

static bool OuterMatchesEmitter(UObject* Obj, const FString& EmitterName)
{
	UObject* Outer = Obj ? Obj->GetOuter() : nullptr;
	if (!Outer) return false;
	FString OuterName = Outer->GetName();
	return OuterName.Equals(EmitterName, ESearchCase::IgnoreCase) ||
	       OuterName.StartsWith(EmitterName + TEXT("_"), ESearchCase::IgnoreCase);
}

static TArray<UNiagaraRendererProperties*> CollectRenderers(UNiagaraSystem* System, const FString& EmitterName)
{
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(System->GetOutermost(), SubObjects, true);
	TArray<UNiagaraRendererProperties*> Result;
	for (UObject* Obj : SubObjects)
	{
		if (!IsValid(Obj)) continue;
		UNiagaraRendererProperties* R = Cast<UNiagaraRendererProperties>(Obj);
		if (!R) continue;
		if (!EmitterName.IsEmpty() && !OuterMatchesEmitter(R, EmitterName)) continue;
		Result.Add(R);
	}
	return Result;
}

static TArray<UObject*> CollectStatelessModules(UNiagaraSystem* System, const FString& EmitterName)
{
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(System->GetOutermost(), SubObjects, true);
	TArray<UObject*> Result;
	for (UObject* Obj : SubObjects)
	{
		if (!Obj) continue;
		if (!Obj->GetClass()->GetName().StartsWith(TEXT("NiagaraStatelessModule_"))) continue;
		if (!EmitterName.IsEmpty() && !OuterMatchesEmitter(Obj, EmitterName)) continue;
		Result.Add(Obj);
	}
	return Result;
}

static UNiagaraStatelessEmitter* FindStatelessEmitterFromHandle(UNiagaraSystem* System, const FString& EmitterName)
{
	for (FNiagaraEmitterHandle& H : System->GetEmitterHandles())
	{
		if (EmitterName.IsEmpty() || H.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			if (UNiagaraStatelessEmitter* SE = H.GetStatelessEmitter())
			{
				return SE;
			}
		}
	}
	return nullptr;
}

enum class EEmitterKind { None, Stateless, Classic };
static EEmitterKind ClassifyEmitter(UNiagaraSystem* System, const FString& EmitterName)
{
	if (!System) return EEmitterKind::None;
	for (FNiagaraEmitterHandle& H : System->GetEmitterHandles())
	{
		if (H.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			return H.GetStatelessEmitter() ? EEmitterKind::Stateless : EEmitterKind::Classic;
		}
	}
	return EEmitterKind::None;
}

static void RebuildStatelessEmitterCache(UNiagaraSystem* System)
{
	System->CacheFromCompiledData();

	FNiagaraSystemUpdateContext UpdateContext(System, true);
}

static void CompileAndReport(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& ResultObj)
{
	if (!System || !ResultObj.IsValid()) return;

#if WITH_EDITORONLY_DATA
	System->RequestCompile(false);
	System->WaitForCompilationComplete();

	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;

	auto AppendEvents = [&](UNiagaraScript* Script, const FString& EmitterName, const FString& Section)
	{
		if (!Script) return;
		const FNiagaraVMExecutableData& VMData = Script->GetVMExecutableData();
		for (const FNiagaraCompileEvent& Event : VMData.LastCompileEvents)
		{
			if (Event.Severity != FNiagaraCompileEventSeverity::Error
				&& Event.Severity != FNiagaraCompileEventSeverity::Warning) continue;

			TSharedPtr<FJsonObject> EventObj = MakeShareable(new FJsonObject);
			if (!EmitterName.IsEmpty()) EventObj->SetStringField(TEXT("emitter"), EmitterName);
			if (!Section.IsEmpty())     EventObj->SetStringField(TEXT("script_section"), Section);
			EventObj->SetStringField(TEXT("message"), Event.Message);
			if (!Event.ShortDescription.IsEmpty()) EventObj->SetStringField(TEXT("short"), Event.ShortDescription);

			if (Event.Severity == FNiagaraCompileEventSeverity::Error)
				Errors.Add(MakeShareable(new FJsonValueObject(EventObj)));
			else
				Warnings.Add(MakeShareable(new FJsonValueObject(EventObj)));
		}
	};

	AppendEvents(System->GetSystemSpawnScript(),  FString(), TEXT("system_spawn"));
	AppendEvents(System->GetSystemUpdateScript(), FString(), TEXT("system_update"));

	auto SectionForUsage = [](ENiagaraScriptUsage Usage) -> FString
	{
		switch (Usage)
		{
		case ENiagaraScriptUsage::ParticleSpawnScript:             return TEXT("particle_spawn");
		case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated: return TEXT("particle_spawn_interpolated");
		case ENiagaraScriptUsage::ParticleUpdateScript:            return TEXT("particle_update");
		case ENiagaraScriptUsage::ParticleEventScript:             return TEXT("particle_event");
		case ENiagaraScriptUsage::ParticleSimulationStageScript:   return TEXT("particle_simulation_stage");
		case ENiagaraScriptUsage::ParticleGPUComputeScript:        return TEXT("particle_gpu");
		case ENiagaraScriptUsage::EmitterSpawnScript:              return TEXT("emitter_spawn");
		case ENiagaraScriptUsage::EmitterUpdateScript:             return TEXT("emitter_update");
		case ENiagaraScriptUsage::SystemSpawnScript:               return TEXT("system_spawn");
		case ENiagaraScriptUsage::SystemUpdateScript:              return TEXT("system_update");
		default:                                                   return TEXT("unknown");
		}
	};

	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data) continue;

		const FString EmitterName = Handle.GetName().ToString();
		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts,  true,  false);
		for (UNiagaraScript* Script : Scripts)
		{
			if (!Script) continue;
			AppendEvents(Script, EmitterName, SectionForUsage(Script->GetUsage()));
		}
	}

	const bool bHasErrors   = Errors.Num()   > 0;
	const bool bHasWarnings = Warnings.Num() > 0;
	ResultObj->SetStringField(TEXT("compile_status"),
		bHasErrors ? TEXT("error") : (bHasWarnings ? TEXT("warning") : TEXT("ok")));
	if (bHasErrors)   ResultObj->SetArrayField(TEXT("compile_errors"),   Errors);
	if (bHasWarnings) ResultObj->SetArrayField(TEXT("compile_warnings"), Warnings);
#endif
}

static void EmitPreFlightFailureJson(const FNiagaraPreFlightIssue& Issue, FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
	Fail->SetBoolField(TEXT("success"), false);
	Fail->SetStringField(TEXT("error"), FString::Printf(TEXT("Pre-flight rejected: %s"), *Issue.Code));

	TSharedPtr<FJsonObject> IssueObj = MakeShareable(new FJsonObject);
	IssueObj->SetStringField(TEXT("code"), Issue.Code);
	IssueObj->SetStringField(TEXT("severity"), Issue.bHardFail ? TEXT("error") : TEXT("warning"));
	IssueObj->SetStringField(TEXT("hint"), Issue.Hint);
	TArray<TSharedPtr<FJsonValue>> Issues;
	Issues.Add(MakeShareable(new FJsonValueObject(IssueObj)));
	Fail->SetArrayField(TEXT("pre_flight_issues"), Issues);

	BuildSuccessJson(Fail, OutJsonString);
	OutError = Issue.Code;
}

static void RunAddModulePreFlight(const FString& ModulePath, const FString& ScriptSection,
	TArray<FNiagaraPreFlightIssue>& OutIssues)
{
	const FString ModuleName = FPaths::GetBaseFilename(ModulePath);
	const FString SectionLower = ScriptSection.ToLower();

	const bool bIsParticleSection =
		SectionLower.IsEmpty()
		|| SectionLower == TEXT("spawn")
		|| SectionLower == TEXT("update")
		|| SectionLower == TEXT("particle_spawn")
		|| SectionLower == TEXT("particle_update")
		|| SectionLower.Contains(TEXT("particlespawn"))
		|| SectionLower.Contains(TEXT("particleupdate"));
	const bool bIsEmitterScopeSection =
		SectionLower == TEXT("emitter_spawn")
		|| SectionLower == TEXT("emitter_update")
		|| SectionLower.Contains(TEXT("emitterspawn"))
		|| SectionLower.Contains(TEXT("emitterupdate"));
	const bool bIsEventHandler = SectionLower.Contains(TEXT("event"));

	const bool bLooksLikeEventReceiver =
		(ModuleName.Contains(TEXT("Receive")) && ModuleName.Contains(TEXT("Event")))
		|| (ModuleName.StartsWith(TEXT("Spawn")) && ModuleName.Contains(TEXT("Event")))
		|| ModuleName.Contains(TEXT("EventHandler"));

	if (bLooksLikeEventReceiver && !bIsEventHandler)
	{
		FNiagaraPreFlightIssue Issue;
		Issue.Code = TEXT("event_module_outside_event_handler");
		Issue.bHardFail = true;
		Issue.Hint = FString::Printf(
			TEXT("Module '%s' is an event-handler module but script_section is '%s'. ")
			TEXT("Event receivers and event-driven spawn modules must live in an event handler script — adding one to particle or emitter scope produces a graph the system editor can't open. ")
			TEXT("Workflow: call add_niagara_event_handler(emitter_name, source_emitter_name, event_name) FIRST to create the handler, then add modules with script_section='event_handler' (pass event_name to disambiguate when an emitter has multiple handlers)."),
			*ModuleName, *ScriptSection);
		OutIssues.Add(Issue);
	}

	const bool bLooksLikeParticleModule =
		ModuleName.StartsWith(TEXT("Initialize Particle"))
		|| ModuleName.StartsWith(TEXT("InitializeParticle"))
		|| ModuleName.Contains(TEXT("ShapeLocation"))
		|| ModuleName.Contains(TEXT("AddVelocity"))
		|| ModuleName.Contains(TEXT("AccelerationForce"))
		|| ModuleName.Contains(TEXT("DragForce"))
		|| ModuleName.Contains(TEXT("ScaleColor"))
		|| ModuleName.Contains(TEXT("ScaleSpriteSize"))
		|| ModuleName.Contains(TEXT("ScaleMeshSize"))
		|| ModuleName.Contains(TEXT("ScaleRibbonWidth"));

	if (bLooksLikeParticleModule && bIsEmitterScopeSection)
	{
		FNiagaraPreFlightIssue Issue;
		Issue.Code = TEXT("particle_module_in_emitter_scope");
		Issue.bHardFail = true;
		Issue.Hint = FString::Printf(
			TEXT("Module '%s' operates on Particles.* attributes but script_section is '%s' (emitter scope). ")
			TEXT("Use script_section=\"spawn\" for one-time particle init at spawn, or script_section=\"update\" for per-frame particle updates. ")
			TEXT("Emitter-scope scripts (emitter_spawn / emitter_update) are for emitter-level state like spawn rate, loop control, and TickRate — not particle attributes."),
			*ModuleName, *ScriptSection);
		OutIssues.Add(Issue);
	}
}

void HandleSetNiagaraRendererMaterial(const FString& SystemPath, const FString& EmitterName, const FString& MaterialPath, int32 RendererIndex, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material) { SetError(FString::Printf(TEXT("Could not load Material: %s"), *MaterialPath), OutJsonString, OutError); return; }

	TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
	if (!Renderers.IsValidIndex(RendererIndex))
	{
		SetError(FString::Printf(TEXT("Renderer index %d out of range. Found %d renderer(s) for emitter '%s'."), RendererIndex, Renderers.Num(), *EmitterName), OutJsonString, OutError);
		return;
	}

	UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];
	FObjectProperty* MatProp = CastField<FObjectProperty>(Renderer->GetClass()->FindPropertyByName(FName("Material")));
	if (!MatProp) { SetError(FString::Printf(TEXT("Renderer '%s' has no Material property."), *Renderer->GetClass()->GetName()), OutJsonString, OutError); return; }

	const FString RendererClassName = Renderer->GetClass()->GetName();
	const bool bIsSprite = RendererClassName == TEXT("NiagaraSpriteRendererProperties");
	const bool bIsMesh   = RendererClassName == TEXT("NiagaraMeshRendererProperties");
	const bool bIsRibbon = RendererClassName == TEXT("NiagaraRibbonRendererProperties");

	UMaterial* BaseMat = Material->GetMaterial();
	FString UsageFlagAutoSet;
	if (BaseMat && (bIsSprite || bIsMesh || bIsRibbon))
	{
		if (bIsSprite && !BaseMat->bUsedWithNiagaraSprites)
		{
			BaseMat->bUsedWithNiagaraSprites = true;
			UsageFlagAutoSet = TEXT("NiagaraSprites");
		}
		else if (bIsMesh && !BaseMat->bUsedWithNiagaraMeshParticles)
		{
			BaseMat->bUsedWithNiagaraMeshParticles = true;
			UsageFlagAutoSet = TEXT("NiagaraMeshParticles");
		}
		else if (bIsRibbon && !BaseMat->bUsedWithNiagaraRibbons)
		{
			BaseMat->bUsedWithNiagaraRibbons = true;
			UsageFlagAutoSet = TEXT("NiagaraRibbons");
		}

		if (!UsageFlagAutoSet.IsEmpty())
		{
			BaseMat->MarkPackageDirty();
			BaseMat->PostEditChange();
		}
	}

	Renderer->Modify();
	MatProp->SetObjectPropertyValue(MatProp->ContainerPtrToValuePtr<void>(Renderer), Material);
	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("renderer_class"), Renderer->GetClass()->GetName());
	Result->SetStringField(TEXT("material"), Material->GetPathName());
	if (!UsageFlagAutoSet.IsEmpty())
	{
		Result->SetStringField(TEXT("usage_flag_auto_set"), UsageFlagAutoSet);
		Result->SetStringField(TEXT("note"),
			FString::Printf(TEXT("Material was missing 'Used with %s' — auto-enabled so the renderer doesn't fall back to the default (pink-checker) material. The material asset has been dirtied; remember to save it."),
				*UsageFlagAutoSet));
	}
	BuildSuccessJson(Result, OutJsonString);
}

void HandleListNiagaraTemplates(const FString& Filter, bool bSystemsOnly, bool bEmittersOnly, FString& OutJsonString, FString& OutError)
{

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> AllAssets;

	auto CollectClass = [&](const FString& ClassName)
	{
		FARFilter ARFilter;
		ARFilter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Niagara"), *ClassName));
		ARFilter.bIncludeOnlyOnDiskAssets = false;
		ARFilter.bRecursivePaths = true;
		TArray<FAssetData> Found;
		AssetRegistry.GetAssets(ARFilter, Found);
		AllAssets.Append(Found);
	};

	if (!bEmittersOnly) CollectClass(TEXT("NiagaraSystem"));
	if (!bSystemsOnly)  CollectClass(TEXT("NiagaraEmitter"));

	FString FilterLower = Filter.ToLower();
	TArray<TSharedPtr<FJsonValue>> ResultArray;
	for (const FAssetData& Asset : AllAssets)
	{
		FString AssetName = Asset.AssetName.ToString();
		FString PackagePath = Asset.PackagePath.ToString();
		if (!FilterLower.IsEmpty())
		{
			FString Combined = (AssetName + TEXT(" ") + PackagePath).ToLower();
			if (!Combined.Contains(FilterLower))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("name"), AssetName);
		Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		Obj->SetStringField(TEXT("folder"), PackagePath);
		Obj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		Obj->SetBoolField(TEXT("is_engine_template"), !PackagePath.StartsWith(TEXT("/Game/")));
		ResultArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), ResultArray.Num());
	Result->SetArrayField(TEXT("templates"), ResultArray);
	BuildSuccessJson(Result, OutJsonString);
}

static FNiagaraEmitterHandle* FindEmitterHandle(UNiagaraSystem* System, const FString& EmitterName)
{
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (EmitterName.IsEmpty() || Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
			return &Handle;
	}
	return nullptr;
}

void HandleGetNiagaraDetailedSummary(const FString& SystemPath, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("system_path"), System->GetPathName());

	auto EmitScalarProp = [](TArray<TSharedPtr<FJsonValue>>& PropsOut, const FString& Name, FProperty* Prop, void* Container)
	{
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("name"), Name);
		if (FByteProperty* BP = CastField<FByteProperty>(Prop))
		{
			if (BP->Enum) {
				int64 V = BP->GetPropertyValue_InContainer(Container);
				P->SetStringField(TEXT("type"), TEXT("enum")); P->SetStringField(TEXT("enum_type"), BP->Enum->GetName());
				P->SetStringField(TEXT("value"), BP->Enum->GetNameStringByValue(V)); P->SetNumberField(TEXT("int_value"), (double)V);
				PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
			}
		}
		else if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			int64 V = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Container));
			P->SetStringField(TEXT("type"), TEXT("enum")); P->SetStringField(TEXT("enum_type"), EP->GetEnum()->GetName());
			P->SetStringField(TEXT("value"), EP->GetEnum()->GetNameStringByValue(V)); P->SetNumberField(TEXT("int_value"), (double)V);
			PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
		}
		else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			P->SetStringField(TEXT("type"), TEXT("float")); P->SetNumberField(TEXT("value"), FP->GetPropertyValue_InContainer(Container));
			PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
		}
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			P->SetStringField(TEXT("type"), TEXT("float")); P->SetNumberField(TEXT("value"), DP->GetPropertyValue_InContainer(Container));
			PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
		}
		else if (FBoolProperty* BoolP = CastField<FBoolProperty>(Prop))
		{
			P->SetStringField(TEXT("type"), TEXT("bool")); P->SetBoolField(TEXT("value"), BoolP->GetPropertyValue_InContainer(Container));
			PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
		}
		else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		{
			P->SetStringField(TEXT("type"), TEXT("int")); P->SetNumberField(TEXT("value"), IP->GetPropertyValue_InContainer(Container));
			PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
		}
	};

	auto SerializeEditProps = [&EmitScalarProp](UObject* Obj) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> PropsOut;
		if (!Obj) return PropsOut;
		for (TFieldIterator<FProperty> PropIt(Obj->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->PropertyFlags & CPF_Edit)) continue;
			FString PropName = Prop->GetName();
			if (PropName.EndsWith(TEXT("Binding"))) continue;

			if (FStructProperty* SP = CastField<FStructProperty>(Prop))
			{
				FString SN = SP->Struct->GetName();
				void* StructPtr = SP->ContainerPtrToValuePtr<void>(Obj);
				if (SN == TEXT("Vector2f")) {
					const FVector2f* V = static_cast<FVector2f*>(StructPtr);
					if (V) {
						TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
						TArray<TSharedPtr<FJsonValue>> VA;
						VA.Add(MakeShareable(new FJsonValueNumber(V->X))); VA.Add(MakeShareable(new FJsonValueNumber(V->Y)));
						P->SetStringField(TEXT("name"), PropName); P->SetStringField(TEXT("type"), TEXT("vector2")); P->SetArrayField(TEXT("value"), VA);
						PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
					}
				}
				else if (SN == TEXT("Vector3f")) {
					const FVector3f* V = static_cast<FVector3f*>(StructPtr);
					if (V) {
						TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
						TArray<TSharedPtr<FJsonValue>> VA;
						VA.Add(MakeShareable(new FJsonValueNumber(V->X))); VA.Add(MakeShareable(new FJsonValueNumber(V->Y))); VA.Add(MakeShareable(new FJsonValueNumber(V->Z)));
						P->SetStringField(TEXT("name"), PropName); P->SetStringField(TEXT("type"), TEXT("vector3")); P->SetArrayField(TEXT("value"), VA);
						PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
					}
				}
				else if (SN == TEXT("LinearColor")) {
					const FLinearColor* C = static_cast<FLinearColor*>(StructPtr);
					if (C) {
						TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
						TArray<TSharedPtr<FJsonValue>> VA;
						VA.Add(MakeShareable(new FJsonValueNumber(C->R))); VA.Add(MakeShareable(new FJsonValueNumber(C->G)));
						VA.Add(MakeShareable(new FJsonValueNumber(C->B))); VA.Add(MakeShareable(new FJsonValueNumber(C->A)));
						P->SetStringField(TEXT("name"), PropName); P->SetStringField(TEXT("type"), TEXT("color")); P->SetArrayField(TEXT("value"), VA);
						PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
					}
				}
				else
				{
					for (TFieldIterator<FProperty> SubIt(SP->Struct); SubIt; ++SubIt)
					{
						FProperty* SubProp = *SubIt;
						if (FStructProperty* SubSP = CastField<FStructProperty>(SubProp))
						{
							FString SubSN = SubSP->Struct->GetName();
							void* SubStructPtr = SubSP->ContainerPtrToValuePtr<void>(StructPtr);
							if (SubSN == TEXT("LinearColor")) {
								const FLinearColor* C = static_cast<FLinearColor*>(SubStructPtr);
								if (C) {
									TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
									TArray<TSharedPtr<FJsonValue>> VA;
									VA.Add(MakeShareable(new FJsonValueNumber(C->R))); VA.Add(MakeShareable(new FJsonValueNumber(C->G)));
									VA.Add(MakeShareable(new FJsonValueNumber(C->B))); VA.Add(MakeShareable(new FJsonValueNumber(C->A)));
									P->SetStringField(TEXT("name"), PropName + TEXT(".") + SubProp->GetName()); P->SetStringField(TEXT("type"), TEXT("color")); P->SetArrayField(TEXT("value"), VA);
									PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
								}
							}
							else if (SubSN == TEXT("Vector3f")) {
								const FVector3f* V = static_cast<FVector3f*>(SubStructPtr);
								if (V) {
									TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
									TArray<TSharedPtr<FJsonValue>> VA;
									VA.Add(MakeShareable(new FJsonValueNumber(V->X))); VA.Add(MakeShareable(new FJsonValueNumber(V->Y))); VA.Add(MakeShareable(new FJsonValueNumber(V->Z)));
									P->SetStringField(TEXT("name"), PropName + TEXT(".") + SubProp->GetName()); P->SetStringField(TEXT("type"), TEXT("vector3")); P->SetArrayField(TEXT("value"), VA);
									PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
								}
							}
							else if (SubSN == TEXT("Vector2f")) {
								const FVector2f* V = static_cast<FVector2f*>(SubStructPtr);
								if (V) {
									TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
									TArray<TSharedPtr<FJsonValue>> VA;
									VA.Add(MakeShareable(new FJsonValueNumber(V->X))); VA.Add(MakeShareable(new FJsonValueNumber(V->Y)));
									P->SetStringField(TEXT("name"), PropName + TEXT(".") + SubProp->GetName()); P->SetStringField(TEXT("type"), TEXT("vector2")); P->SetArrayField(TEXT("value"), VA);
									PropsOut.Add(MakeShareable(new FJsonValueObject(P)));
								}
							}
							else {
								for (TFieldIterator<FProperty> Sub2It(SubSP->Struct); Sub2It; ++Sub2It)
									EmitScalarProp(PropsOut, PropName + TEXT(".") + SubProp->GetName() + TEXT(".") + Sub2It->GetName(), *Sub2It, SubStructPtr);
							}
						}
						else
						{
							EmitScalarProp(PropsOut, PropName + TEXT(".") + SubProp->GetName(), SubProp, StructPtr);
						}
					}
				}
			}
			else
			{
				EmitScalarProp(PropsOut, PropName, Prop, Obj);
			}
		}
		return PropsOut;
	};

	TArray<TSharedPtr<FJsonValue>> EmittersArray;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FString HandleName = Handle.GetName().ToString();
		TSharedPtr<FJsonObject> EmitterObj = MakeShareable(new FJsonObject);
		EmitterObj->SetStringField(TEXT("name"), HandleName);
		EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

		TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, HandleName);
		TArray<TSharedPtr<FJsonValue>> RenderersArray;
		for (int32 RI = 0; RI < Renderers.Num(); RI++)
		{
			UNiagaraRendererProperties* Renderer = Renderers[RI];
			if (!Renderer) continue;

			TSharedPtr<FJsonObject> RendObj = MakeShareable(new FJsonObject);
			RendObj->SetNumberField(TEXT("index"), RI);
			RendObj->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
			RendObj->SetArrayField(TEXT("properties"), SerializeEditProps(Renderer));
			RenderersArray.Add(MakeShareable(new FJsonValueObject(RendObj)));
		}
		EmitterObj->SetArrayField(TEXT("renderers"), RenderersArray);

		TArray<UObject*> Modules = CollectStatelessModules(System, HandleName);
		TArray<TSharedPtr<FJsonValue>> ModulesArray;
		int32 SkippedDisabled = 0;
		for (UObject* Mod : Modules)
		{
			bool bModuleEnabled = true;
			if (FBoolProperty* EnableProp = FindFProperty<FBoolProperty>(Mod->GetClass(), TEXT("bModuleEnabled")))
				bModuleEnabled = EnableProp->GetPropertyValue_InContainer(Mod);
			if (!bModuleEnabled) { ++SkippedDisabled; continue; }

			TSharedPtr<FJsonObject> ModObj = MakeShareable(new FJsonObject);
			FString ClassName = Mod->GetClass()->GetName();
			FString ShortClass = ClassName.StartsWith(TEXT("NiagaraStatelessModule_")) ? ClassName.Mid(23) : ClassName;
			ModObj->SetStringField(TEXT("module"), ShortClass);
			ModObj->SetStringField(TEXT("full_class"), ClassName);

			TArray<TSharedPtr<FJsonValue>> AllProps = SerializeEditProps(Mod);
			TArray<TSharedPtr<FJsonValue>> FilteredProps;
			FilteredProps.Reserve(AllProps.Num());
			for (const TSharedPtr<FJsonValue>& V : AllProps)
			{
				TSharedPtr<FJsonObject> P = V->AsObject();
				if (!P.IsValid()) continue;
				FString PN; P->TryGetStringField(TEXT("name"), PN);
				if (PN.EndsWith(TEXT(".MaxLutSampleCount"))) continue;
				if (PN.EndsWith(TEXT(".InterpolationMode"))) continue;
				if (PN.EndsWith(TEXT(".AddressMode"))) continue;
				if (PN.EndsWith(TEXT(".ValuesTimeRange"))) continue;
				FilteredProps.Add(V);
			}
			ModObj->SetArrayField(TEXT("properties"), FilteredProps);
			ModulesArray.Add(MakeShareable(new FJsonValueObject(ModObj)));
		}
		EmitterObj->SetArrayField(TEXT("module_parameters"), ModulesArray);
		if (SkippedDisabled > 0)
			EmitterObj->SetNumberField(TEXT("disabled_modules_skipped"), SkippedDisabled);
		EmittersArray.Add(MakeShareable(new FJsonValueObject(EmitterObj)));
	}

	Result->SetArrayField(TEXT("emitters"), EmittersArray);
	Result->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());

	BuildSuccessJson(Result, OutJsonString);
	const int32 kMaxResponseChars = 40000;
	if (OutJsonString.Len() > kMaxResponseChars)
	{
		for (const TSharedPtr<FJsonValue>& EV : EmittersArray)
		{
			TSharedPtr<FJsonObject> EObj = EV->AsObject();
			if (EObj.IsValid()) EObj->RemoveField(TEXT("module_parameters"));
		}
		Result->SetArrayField(TEXT("emitters"), EmittersArray);
		Result->SetStringField(TEXT("truncated"), FString::Printf(TEXT("Module parameters omitted — full response was %d chars (limit %d). Use get_emitter_modules(emitter_name=...) for the per-emitter module list, then set_niagara_module_parameter for targeted edits. Avoid calling get_niagara_detailed_summary on systems with many emitters."), OutJsonString.Len(), kMaxResponseChars));
		OutJsonString.Empty();
		BuildSuccessJson(Result, OutJsonString);
	}
}

void HandleSetNiagaraRendererProperty(const FString& SystemPath, const FString& EmitterName, int32 RendererIndex, const FString& PropertyName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
	if (!Renderers.IsValidIndex(RendererIndex))
	{
		SetError(FString::Printf(TEXT("Renderer index %d out of range. Found %d renderer(s) for emitter '%s'."), RendererIndex, Renderers.Num(), *EmitterName), OutJsonString, OutError);
		return;
	}

	UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];

	FString ParentName = PropertyName;
	FString ComponentSuffix;
	{
		int32 DotIdx = INDEX_NONE;
		if (PropertyName.FindLastChar(TEXT('.'), DotIdx))
		{
			const FString Suffix = PropertyName.Mid(DotIdx + 1);
			if (Suffix == TEXT("X") || Suffix == TEXT("Y") || Suffix == TEXT("Z") || Suffix == TEXT("W"))
			{
				ParentName = PropertyName.Left(DotIdx);
				ComponentSuffix = Suffix;
			}
		}
	}

	FProperty* Prop = Renderer->GetClass()->FindPropertyByName(FName(*ParentName));
	if (!Prop)
	{
		TArray<FString> Available;
		for (TFieldIterator<FProperty> It(Renderer->GetClass()); It; ++It)
		{
			if (It->PropertyFlags & CPF_Edit) Available.Add(It->GetName());
		}
		SetError(FString::Printf(TEXT("Property '%s' not found on %s. Available: %s"), *PropertyName, *Renderer->GetClass()->GetName(), *FString::Join(Available, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	Renderer->Modify();
	bool bSet = false;

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		FString StrVal; double NumVal = 0.0;
		if (ValueJson->TryGetStringField(TEXT("string_value"), StrVal) && ByteProp->Enum)
		{
			int64 EnumVal = ByteProp->Enum->GetValueByNameString(StrVal);
			if (EnumVal == INDEX_NONE) EnumVal = ByteProp->Enum->GetValueByNameString(ByteProp->Enum->GetName() + TEXT("::") + StrVal);
			if (EnumVal == INDEX_NONE) { SetError(FString::Printf(TEXT("Invalid enum value '%s' for %s."), *StrVal, *ByteProp->Enum->GetName()), OutJsonString, OutError); return; }
			ByteProp->SetPropertyValue_InContainer(Renderer, (uint8)EnumVal);
			bSet = true;
		}
		else if (ValueJson->TryGetNumberField(TEXT("int_value"), NumVal))
		{
			ByteProp->SetPropertyValue_InContainer(Renderer, (uint8)NumVal);
			bSet = true;
		}
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		FString StrVal; double NumVal = 0.0;
		UEnum* Enum = EnumProp->GetEnum();
		if (ValueJson->TryGetStringField(TEXT("string_value"), StrVal))
		{
			int64 EnumVal = Enum->GetValueByNameString(StrVal);
			if (EnumVal == INDEX_NONE) EnumVal = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + StrVal);
			if (EnumVal == INDEX_NONE) { SetError(FString::Printf(TEXT("Invalid enum value '%s' for %s."), *StrVal, *Enum->GetName()), OutJsonString, OutError); return; }
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(Renderer), EnumVal);
			bSet = true;
		}
		else if (ValueJson->TryGetNumberField(TEXT("int_value"), NumVal))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(Renderer), (int64)NumVal);
			bSet = true;
		}
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		double Val = 0.0;
		if (ValueJson->TryGetNumberField(TEXT("float_value"), Val)) { FloatProp->SetPropertyValue_InContainer(Renderer, (float)Val); bSet = true; }
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		bool Val = false;
		if (ValueJson->TryGetBoolField(TEXT("bool_value"), Val)) { BoolProp->SetPropertyValue_InContainer(Renderer, Val); bSet = true; }
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
	{
		double Val = 0.0;
		if (ValueJson->TryGetNumberField(TEXT("int_value"), Val)) { IntProp->SetPropertyValue_InContainer(Renderer, (int32)Val); bSet = true; }
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		void* Ptr = StructProp->ContainerPtrToValuePtr<void>(Renderer);
		const FString SN = StructProp->Struct->GetName();

		const bool bIsVec2Float  = (SN == TEXT("Vector2f"));
		const bool bIsVec2Double = (SN == TEXT("Vector2D"));
		const bool bIsVec3Float  = (SN == TEXT("Vector3f"));
		const bool bIsVec3Double = (SN == TEXT("Vector") || SN == TEXT("Vector3d"));
		const bool bIsVec4Float  = (SN == TEXT("Vector4f"));
		const bool bIsVec4Double = (SN == TEXT("Vector4") || SN == TEXT("Vector4d"));
		const bool bIsColor      = (SN == TEXT("LinearColor"));

		if (!ComponentSuffix.IsEmpty())
		{
			double ScalarVal = 0.0;
			if (!ValueJson->TryGetNumberField(TEXT("float_value"), ScalarVal)
				&& !ValueJson->TryGetNumberField(TEXT("value"), ScalarVal))
			{
				SetError(FString::Printf(TEXT("'%s' is a struct component — pass float_value to write the .%s scalar."),
					*PropertyName, *ComponentSuffix), OutJsonString, OutError);
				return;
			}
			int32 ComponentIdx = -1;
			if      (ComponentSuffix == TEXT("X")) ComponentIdx = 0;
			else if (ComponentSuffix == TEXT("Y")) ComponentIdx = 1;
			else if (ComponentSuffix == TEXT("Z")) ComponentIdx = 2;
			else if (ComponentSuffix == TEXT("W")) ComponentIdx = 3;

			auto WriteFloatComp = [&](int32 MaxComps) -> bool {
				if (ComponentIdx >= MaxComps) return false;
				float* Floats = static_cast<float*>(Ptr);
				Floats[ComponentIdx] = (float)ScalarVal;
				return true;
			};
			auto WriteDoubleComp = [&](int32 MaxComps) -> bool {
				if (ComponentIdx >= MaxComps) return false;
				double* Doubles = static_cast<double*>(Ptr);
				Doubles[ComponentIdx] = ScalarVal;
				return true;
			};

			if (bIsVec2Float)       bSet = WriteFloatComp(2);
			else if (bIsVec2Double) bSet = WriteDoubleComp(2);
			else if (bIsVec3Float)  bSet = WriteFloatComp(3);
			else if (bIsVec3Double) bSet = WriteDoubleComp(3);
			else if (bIsVec4Float)  bSet = WriteFloatComp(4);
			else if (bIsVec4Double) bSet = WriteDoubleComp(4);
			else if (bIsColor)
			{
				float* Floats = static_cast<float*>(Ptr);
				if (ComponentIdx >= 0 && ComponentIdx < 4) { Floats[ComponentIdx] = (float)ScalarVal; bSet = true; }
			}
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
			if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArr) && VecArr)
			{
				if (bIsVec2Float && VecArr->Num() >= 2)
				{
					FVector2f V((float)(*VecArr)[0]->AsNumber(), (float)(*VecArr)[1]->AsNumber());
					FMemory::Memcpy(Ptr, &V, sizeof(FVector2f));
					bSet = true;
				}
				else if (bIsVec2Double && VecArr->Num() >= 2)
				{
					FVector2D V((double)(*VecArr)[0]->AsNumber(), (double)(*VecArr)[1]->AsNumber());
					FMemory::Memcpy(Ptr, &V, sizeof(FVector2D));
					bSet = true;
				}
				else if (bIsVec3Float && VecArr->Num() >= 3)
				{
					FVector3f V((float)(*VecArr)[0]->AsNumber(), (float)(*VecArr)[1]->AsNumber(), (float)(*VecArr)[2]->AsNumber());
					FMemory::Memcpy(Ptr, &V, sizeof(FVector3f));
					bSet = true;
				}
				else if (bIsVec3Double && VecArr->Num() >= 3)
				{
					FVector V((double)(*VecArr)[0]->AsNumber(), (double)(*VecArr)[1]->AsNumber(), (double)(*VecArr)[2]->AsNumber());
					FMemory::Memcpy(Ptr, &V, sizeof(FVector));
					bSet = true;
				}
				else if (bIsVec4Float && VecArr->Num() >= 4)
				{
					FVector4f V((float)(*VecArr)[0]->AsNumber(), (float)(*VecArr)[1]->AsNumber(),
								(float)(*VecArr)[2]->AsNumber(), (float)(*VecArr)[3]->AsNumber());
					FMemory::Memcpy(Ptr, &V, sizeof(FVector4f));
					bSet = true;
				}
				else if (bIsVec4Double && VecArr->Num() >= 4)
				{
					FVector4 V((double)(*VecArr)[0]->AsNumber(), (double)(*VecArr)[1]->AsNumber(),
							   (double)(*VecArr)[2]->AsNumber(), (double)(*VecArr)[3]->AsNumber());
					FMemory::Memcpy(Ptr, &V, sizeof(FVector4));
					bSet = true;
				}
				else if (bIsColor && VecArr->Num() >= 3)
				{
					FLinearColor C(
						(float)(*VecArr)[0]->AsNumber(),
						(float)(*VecArr)[1]->AsNumber(),
						(float)(*VecArr)[2]->AsNumber(),
						VecArr->Num() >= 4 ? (float)(*VecArr)[3]->AsNumber() : 1.f);
					FMemory::Memcpy(Ptr, &C, sizeof(FLinearColor));
					bSet = true;
				}
			}
		}
	}
	else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
	{
		FString PathStr;
		if (ValueJson->TryGetStringField(TEXT("string_value"), PathStr)
			|| ValueJson->TryGetStringField(TEXT("asset_path"), PathStr)
			|| ValueJson->TryGetStringField(TEXT("path"), PathStr))
		{
			if (UObject* Asset = UEditorAssetLibrary::LoadAsset(PathStr))
			{
				if (Asset->IsA(ObjProp->PropertyClass))
				{
					ObjProp->SetObjectPropertyValue_InContainer(Renderer, Asset);
					bSet = true;
				}
				else
				{
					SetError(FString::Printf(TEXT("Asset '%s' is a %s, but property '%s' expects a %s."),
						*PathStr, *Asset->GetClass()->GetName(), *PropertyName, *ObjProp->PropertyClass->GetName()),
						OutJsonString, OutError);
					return;
				}
			}
			else
			{
				SetError(FString::Printf(TEXT("Could not load asset '%s' for property '%s'."), *PathStr, *PropertyName), OutJsonString, OutError);
				return;
			}
		}
	}

	if (!bSet)
	{
		SetError(FString::Printf(TEXT("Could not set '%s' (type: %s). Use float_value, int_value, bool_value, string_value, vector_value, or asset_path (for object refs)."), *PropertyName, *Prop->GetClass()->GetName()), OutJsonString, OutError);
		return;
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("property"), PropertyName);
	Resp->SetStringField(TEXT("renderer"), Renderer->GetClass()->GetName());
	BuildSuccessJson(Resp, OutJsonString);
}

void HandleSetMeshRendererMesh(const FString& SystemPath, const FString& EmitterName, int32 RendererIndex,
	int32 MeshIndex, const FString& MeshPath, const FString& MaterialPath,
	FString& OutJsonString, FString& OutError)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	if (MeshPath.IsEmpty()) { SetError(TEXT("'mesh_path' is required (UStaticMesh asset path)."), OutJsonString, OutError); return; }
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh) { SetError(FString::Printf(TEXT("Could not load StaticMesh: %s"), *MeshPath), OutJsonString, OutError); return; }

	UMaterialInterface* Material = nullptr;
	if (!MaterialPath.IsEmpty())
	{
		Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (!Material) { SetError(FString::Printf(TEXT("Could not load Material: %s"), *MaterialPath), OutJsonString, OutError); return; }
	}

	TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
	if (!Renderers.IsValidIndex(RendererIndex))
	{
		SetError(FString::Printf(TEXT("Renderer index %d out of range. Found %d renderer(s) for emitter '%s'."),
			RendererIndex, Renderers.Num(), *EmitterName), OutJsonString, OutError);
		return;
	}

	UNiagaraMeshRendererProperties* MeshRenderer = Cast<UNiagaraMeshRendererProperties>(Renderers[RendererIndex]);
	if (!MeshRenderer)
	{
		SetError(FString::Printf(TEXT("Renderer %d is a %s, not a Mesh renderer. Add a Mesh renderer first via add_renderer_to_emitter(renderer_type='Mesh')."),
			RendererIndex, *Renderers[RendererIndex]->GetClass()->GetName()), OutJsonString, OutError);
		return;
	}

	if (MeshIndex < 0)
	{
		SetError(TEXT("'mesh_index' must be >= 0."), OutJsonString, OutError); return;
	}

	MeshRenderer->Modify();

	while (MeshRenderer->Meshes.Num() <= MeshIndex)
	{
		MeshRenderer->Meshes.Emplace();
	}
	MeshRenderer->Meshes[MeshIndex].Mesh = Mesh;

	FString UsageFlagAutoSet;
	if (Material)
	{
		MeshRenderer->bOverrideMaterials = true;
		if (MeshRenderer->OverrideMaterials.Num() == 0)
		{
			MeshRenderer->OverrideMaterials.Emplace();
		}
		MeshRenderer->OverrideMaterials[0].ExplicitMat = Material;

		if (UMaterial* BaseMat = Material->GetMaterial())
		{
			if (!BaseMat->bUsedWithNiagaraMeshParticles)
			{
				BaseMat->bUsedWithNiagaraMeshParticles = true;
				BaseMat->MarkPackageDirty();
				BaseMat->PostEditChange();
				UsageFlagAutoSet = TEXT("NiagaraMeshParticles");
			}
		}
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("renderer_class"), MeshRenderer->GetClass()->GetName());
	Resp->SetNumberField(TEXT("renderer_index"), RendererIndex);
	Resp->SetNumberField(TEXT("mesh_index"), MeshIndex);
	Resp->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	if (Material) Resp->SetStringField(TEXT("material"), Material->GetPathName());
	if (!UsageFlagAutoSet.IsEmpty())
	{
		Resp->SetStringField(TEXT("usage_flag_auto_set"), UsageFlagAutoSet);
		Resp->SetStringField(TEXT("usage_flag_note"),
			TEXT("Material was missing 'Used with NiagaraMeshParticles' — auto-enabled. The material asset has been dirtied; remember to save it."));
	}
	CompileAndReport(System, Resp);
	BuildSuccessJson(Resp, OutJsonString);
}

void HandleSetNiagaraModuleParameter(const FString& SystemPath, const FString& EmitterName, const FString& ScriptSection, const FString& ParameterName, const FString& ModuleName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	TArray<UObject*> Modules;
	UNiagaraStatelessEmitter* StatelessEmitter = FindStatelessEmitterFromHandle(System, EmitterName);
	if (StatelessEmitter)
	{
		for (UNiagaraStatelessModule* Mod : StatelessEmitter->GetModules())
		{
			if (Mod) Modules.Add(Mod);
		}
	}
	if (Modules.Num() == 0)
	{
		Modules = CollectStatelessModules(System, EmitterName);
	}
	FString ModuleNameLower = ModuleName.ToLower();

	FString TopPropName = ParameterName;
	FString SubPropName;
	int32 DotIdx;
	if (ParameterName.FindChar('.', DotIdx))
	{
		TopPropName = ParameterName.Left(DotIdx);
		SubPropName = ParameterName.Mid(DotIdx + 1);
	}
	FString SearchLower = TopPropName.ToLower();
	FString SubSearchLower = SubPropName.ToLower();

	UObject* FoundModule = nullptr;
	FProperty* FoundProp = nullptr;
	for (UObject* Mod : Modules)
	{
		if (!ModuleNameLower.IsEmpty())
		{
			FString ClassName = Mod->GetClass()->GetName();
			FString ShortName = ClassName.StartsWith(TEXT("NiagaraStatelessModule_")) ? ClassName.Mid(23) : ClassName;
			if (!ShortName.ToLower().Equals(ModuleNameLower) && !ClassName.ToLower().Equals(ModuleNameLower))
				continue;
		}
		for (TFieldIterator<FProperty> PropIt(Mod->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->PropertyFlags & CPF_Edit)) continue;
			if (Prop->GetName().ToLower() == SearchLower) { FoundModule = Mod; FoundProp = Prop; break; }
		}
		if (FoundModule) break;
	}

	if (!FoundModule || !FoundProp)
	{
		if (Modules.Num() == 0)
		{
			const FString ClassicScriptSection = ScriptSection;

			FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
			if (!Handle)
			{
				SetError(FString::Printf(TEXT("Emitter '%s' not found on '%s'"), *EmitterName, *SystemPath), OutJsonString, OutError);
				return;
			}
			FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
			if (!EmitterData)
			{
				SetError(FString::Printf(TEXT("Emitter '%s' has no data — asset may be corrupt"), *EmitterName), OutJsonString, OutError);
				return;
			}

			FString EventName;
			if (ValueJson.IsValid()) ValueJson->TryGetStringField(TEXT("event_name"), EventName);
			FString ResolveErr;
			UNiagaraScript* Script = ResolveClassicEmitterScript(EmitterData, EmitterName, ClassicScriptSection, EventName, ResolveErr);
			const FString SectionLower = ClassicScriptSection.ToLower();
			if (!Script)
			{
				if (ResolveErr.IsEmpty()) ResolveErr = FString::Printf(TEXT("No script found on emitter '%s' for section '%s'"), *EmitterName, *ClassicScriptSection);
				SetError(ResolveErr, OutJsonString, OutError);
				return;
			}
			UNiagaraScriptSource* ScriptSrc = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
			if (!ScriptSrc || !ScriptSrc->NodeGraph)
			{
				SetError(TEXT("Script has no graph (compiled-only runtime?)"), OutJsonString, OutError);
				return;
			}
			UNiagaraGraph* Graph = ScriptSrc->NodeGraph;

			UNiagaraNodeFunctionCall* FoundCall = nullptr;
			TArray<FString> AvailableModules;
			for (UEdGraphNode* N : Graph->Nodes)
			{
				UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(N);
				if (!FC) continue;
				AvailableModules.Add(FC->GetFunctionName());
				if (FC->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase))
				{
					FoundCall = FC;
					break;
				}
			}
			if (!FoundCall)
			{
				SetError(FString::Printf(
					TEXT("Module '%s' not found in '%s' section. Available: %s"),
					*ModuleName, *(SectionLower.IsEmpty() ? FString(TEXT("particle_spawn")) : SectionLower),
					*FString::Join(AvailableModules, TEXT(", "))),
					OutJsonString, OutError);
				return;
			}

			{
				TArray<FModuleStaticSwitch> Switches = CollectModuleStaticSwitches(FoundCall);
				const FModuleStaticSwitch* MatchedSwitch = Switches.FindByPredicate(
					[&](const FModuleStaticSwitch& Sw)
					{
						return Sw.Name.ToString().Equals(ParameterName, ESearchCase::IgnoreCase);
					});

				if (MatchedSwitch)
				{
					FString SwitchNewValue;
					FString SwitchValueType;
					bool bSwitchConverted = false;

					double SwNum = 0.0; bool SwBool = false; FString SwStr;
					if (MatchedSwitch->Type == FNiagaraTypeDefinition::GetBoolDef())
					{
						if (ValueJson->TryGetBoolField(TEXT("bool_value"), SwBool))
						{
							SwitchNewValue = SwBool ? TEXT("true") : TEXT("false");
							SwitchValueType = TEXT("bool");
							bSwitchConverted = true;
						}
					}
					else if (MatchedSwitch->Type == FNiagaraTypeDefinition::GetIntDef())
					{
						if (ValueJson->TryGetNumberField(TEXT("int_value"), SwNum) ||
							ValueJson->TryGetNumberField(TEXT("value"),     SwNum))
						{
							SwitchNewValue = FString::FromInt((int32)SwNum);
							SwitchValueType = TEXT("int");
							bSwitchConverted = true;
						}
					}

					if (!bSwitchConverted && MatchedSwitch->EnumValues.Num() > 0 &&
						ValueJson->TryGetStringField(TEXT("string_value"), SwStr))
					{
						const FString Trimmed = SwStr.TrimStartAndEnd();
						const bool bHaveDisplay = MatchedSwitch->EnumDisplayNames.Num() == MatchedSwitch->EnumValues.Num();
						const FString* Hit = nullptr;
						Hit = MatchedSwitch->EnumValues.FindByPredicate(
							[&](const FString& V) { return V.Equals(Trimmed, ESearchCase::IgnoreCase); });
						if (!Hit && bHaveDisplay)
						{
							for (int32 i = 0; i < MatchedSwitch->EnumDisplayNames.Num(); i++)
							{
								if (MatchedSwitch->EnumDisplayNames[i].Equals(Trimmed, ESearchCase::IgnoreCase))
								{
									Hit = &MatchedSwitch->EnumValues[i]; break;
								}
							}
						}
						if (!Hit)
						{
							for (int32 i = 0; i < MatchedSwitch->EnumValues.Num(); i++)
							{
								const FString& V = MatchedSwitch->EnumValues[i];
								if (V.EndsWith(TEXT("::") + Trimmed, ESearchCase::IgnoreCase) ||
									Trimmed.EndsWith(TEXT("::") + V, ESearchCase::IgnoreCase))
								{
									Hit = &V; break;
								}
							}
						}
						if (Hit)
						{
							SwitchNewValue = *Hit;
							SwitchValueType = TEXT("enum");
							bSwitchConverted = true;
						}
						else
						{
							FString DisplayList = bHaveDisplay ? FString::Join(MatchedSwitch->EnumDisplayNames, TEXT(", ")) : FString();
							SetError(FString::Printf(
								TEXT("Static switch '%s' rejected value '%s'. Display names: %s. Raw names: %s"),
								*ParameterName, *SwStr,
								*DisplayList,
								*FString::Join(MatchedSwitch->EnumValues, TEXT(", "))),
								OutJsonString, OutError);
							return;
						}
					}

					if (!bSwitchConverted && ValueJson->TryGetStringField(TEXT("string_value"), SwStr))
					{
						SwitchNewValue = SwStr;
						SwitchValueType = TEXT("string");
						bSwitchConverted = true;
					}

					if (!bSwitchConverted)
					{
						SetError(FString::Printf(
							TEXT("Static switch '%s' (type %s) couldn't be converted. Pass bool_value / int_value / string_value as appropriate."),
							*ParameterName, *MatchedSwitch->Type.GetName()),
							OutJsonString, OutError);
						return;
					}

					Graph->Modify();
					FoundCall->Modify();
					if (!SetModuleStaticSwitch(FoundCall, MatchedSwitch->Name, SwitchNewValue))
					{
						SetError(FString::Printf(
							TEXT("Static switch '%s' pin not present on module '%s' (called script may be stale — re-add the module)."),
							*ParameterName, *ModuleName),
							OutJsonString, OutError);
						return;
					}

					Graph->NotifyGraphChanged();
					Script->MarkPackageDirty();
					System->MarkPackageDirty();

					OutJsonString = FString::Printf(
						TEXT("{\"success\":true,\"emitter\":\"%s\",\"emitter_type\":\"classic\",\"section\":\"%s\","
							 "\"module\":\"%s\",\"input\":\"%s\",\"value_type\":\"%s\",\"value\":\"%s\","
							 "\"static_switch\":true}"),
						*EmitterName,
						*(SectionLower.IsEmpty() ? FString(TEXT("particle_spawn")) : SectionLower),
						*ModuleName, *ParameterName, *SwitchValueType, *SwitchNewValue);
					return;
				}
			}

			TArray<FNiagaraVariable> ModuleInputs = CollectModuleInputs(FoundCall);
			FNiagaraVariable FoundInputValue;
			bool bFoundInput = false;
			TArray<FString> AvailableInputs;
			for (const FNiagaraVariable& Input : ModuleInputs)
			{
				FString Stripped = Input.GetName().ToString();
				if (Stripped.StartsWith(TEXT("Module."))) Stripped = Stripped.Mid(7);
				AvailableInputs.Add(Stripped);
				if (!bFoundInput && Stripped.Equals(ParameterName, ESearchCase::IgnoreCase))
				{
					FoundInputValue = Input;
					bFoundInput = true;
				}
			}
			if (!bFoundInput)
			{
				TArray<FString> AvailableSwitches;
				for (const FModuleStaticSwitch& Sw : CollectModuleStaticSwitches(FoundCall))
					AvailableSwitches.Add(Sw.Name.ToString());

				FString SwitchHint;
				if (AvailableSwitches.Num() > 0)
					SwitchHint = FString::Printf(TEXT(". Static switches: %s"), *FString::Join(AvailableSwitches, TEXT(", ")));

				SetError(FString::Printf(
					TEXT("Input '%s' not found on module '%s'. Available inputs: %s%s"),
					*ParameterName, *ModuleName, *FString::Join(AvailableInputs, TEXT(", ")), *SwitchHint),
					OutJsonString, OutError);
				return;
			}
			FNiagaraVariable* FoundInputVar = &FoundInputValue;

			const FNiagaraTypeDefinition& InputType = FoundInputVar->GetType();
			FString DefaultStr;
			FString TypeUsedForResponse;
			bool bConverted = false;

			double NumProbe = 0.0;
			bool BoolProbe = false;
			FString StrProbe;
			const TArray<TSharedPtr<FJsonValue>>* ArrProbe = nullptr;

			if (InputType == FNiagaraTypeDefinition::GetFloatDef())
			{
				if (ValueJson->TryGetNumberField(TEXT("float_value"), NumProbe) ||
					ValueJson->TryGetNumberField(TEXT("value"),       NumProbe))
				{
					DefaultStr = FString::SanitizeFloat(NumProbe);
					TypeUsedForResponse = TEXT("float");
					bConverted = true;
				}
			}
			else if (InputType == FNiagaraTypeDefinition::GetIntDef())
			{
				if (ValueJson->TryGetNumberField(TEXT("int_value"), NumProbe) ||
					ValueJson->TryGetNumberField(TEXT("value"),     NumProbe))
				{
					DefaultStr = FString::FromInt((int32)NumProbe);
					TypeUsedForResponse = TEXT("int");
					bConverted = true;
				}
			}
			else if (InputType == FNiagaraTypeDefinition::GetBoolDef())
			{
				if (ValueJson->TryGetBoolField(TEXT("bool_value"), BoolProbe))
				{
					DefaultStr = BoolProbe ? TEXT("true") : TEXT("false");
					TypeUsedForResponse = TEXT("bool");
					bConverted = true;
				}
			}
			else if (InputType == FNiagaraTypeDefinition::GetVec2Def() ||
			         InputType == FNiagaraTypeDefinition::GetVec3Def() ||
			         InputType == FNiagaraTypeDefinition::GetVec4Def() ||
			         InputType == FNiagaraTypeDefinition::GetColorDef() ||
			         InputType == FNiagaraTypeDefinition::GetPositionDef())
			{
				if (ValueJson->TryGetArrayField(TEXT("vector_value"), ArrProbe) && ArrProbe)
				{
					const int32 N = ArrProbe->Num();
					auto AsF = [&](int32 I) { return I < N ? (float)(*ArrProbe)[I]->AsNumber() : 0.f; };
					if (InputType == FNiagaraTypeDefinition::GetColorDef())
					{
						DefaultStr = FString::Printf(TEXT("(R=%f,G=%f,B=%f,A=%f)"),
							AsF(0), AsF(1), AsF(2), N >= 4 ? AsF(3) : 1.f);
						TypeUsedForResponse = TEXT("color");
					}
					else if (InputType == FNiagaraTypeDefinition::GetVec2Def())
					{
						DefaultStr = FString::Printf(TEXT("(X=%f,Y=%f)"), AsF(0), AsF(1));
						TypeUsedForResponse = TEXT("vector2");
					}
					else
					{
						DefaultStr = FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), AsF(0), AsF(1), AsF(2));
						TypeUsedForResponse = (InputType == FNiagaraTypeDefinition::GetPositionDef()) ? TEXT("position") : TEXT("vector3");
					}
					bConverted = true;
				}
			}

			if (!bConverted && ValueJson->TryGetStringField(TEXT("string_value"), StrProbe))
			{
				DefaultStr = StrProbe;
				TypeUsedForResponse = TEXT("string");
				bConverted = true;
			}

			if (!bConverted)
			{
				SetError(FString::Printf(
					TEXT("Could not convert value to type '%s' for input '%s'. Pass the right field "
						 "(float_value / int_value / bool_value / vector_value=[..] / string_value)."),
					*InputType.GetName(), *ParameterName),
					OutJsonString, OutError);
				return;
			}

			const FNiagaraParameterHandle InputHandle{FoundInputVar->GetName()};
			const FNiagaraParameterHandle AliasedInputHandle =
				FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, FoundCall);

			Graph->Modify();
			FoundCall->Modify();

			UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				*FoundCall, AliasedInputHandle, InputType, FGuid(), FGuid());

			if (OverridePin.LinkedTo.Num() > 0)
			{
				TArray<UEdGraphPin*> LinkedCopy = OverridePin.LinkedTo;
				for (UEdGraphPin* Linked : LinkedCopy)
				{
					if (!Linked) continue;
					UEdGraphNode* OwningNode = Linked->GetOwningNode();
					if (OwningNode)
					{
						OwningNode->Modify();
						OwningNode->BreakAllNodeLinks();
						Graph->RemoveNode(OwningNode);
					}
				}
			}

			OverridePin.Modify();
			OverridePin.DefaultValue = DefaultStr;

			Graph->NotifyGraphChanged();
			Script->MarkPackageDirty();
			System->MarkPackageDirty();

			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"emitter\":\"%s\",\"emitter_type\":\"classic\",\"section\":\"%s\","
					 "\"module\":\"%s\",\"input\":\"%s\",\"value_type\":\"%s\",\"value\":\"%s\"}"),
				*EmitterName,
				*(SectionLower.IsEmpty() ? FString(TEXT("particle_spawn")) : SectionLower),
				*ModuleName, *ParameterName, *TypeUsedForResponse, *DefaultStr);
			return;
		}

		TArray<FString> ModuleSummaries;
		TArray<FString> AllPropNames;
		for (UObject* Mod : Modules)
		{
			FString CN = Mod->GetClass()->GetName();
			FString ShortCN = CN.StartsWith(TEXT("NiagaraStatelessModule_")) ? CN.Mid(23) : CN;
			TArray<FString> ModProps;
			for (TFieldIterator<FProperty> PropIt(Mod->GetClass()); PropIt; ++PropIt)
			{
				if (!(PropIt->PropertyFlags & CPF_Edit)) continue;
				ModProps.Add(PropIt->GetName());
				AllPropNames.AddUnique(PropIt->GetName());
			}
			if (ModProps.Num() > 0)
				ModuleSummaries.Add(FString::Printf(TEXT("%s: [%s]"), *ShortCN, *FString::Join(ModProps, TEXT(", "))));
		}

		FString Detail;
		if (!ModuleName.IsEmpty())
		{
			UObject* MatchedModule = nullptr;
			for (UObject* Mod : Modules)
			{
				FString CN = Mod->GetClass()->GetName();
				FString ShortCN = CN.StartsWith(TEXT("NiagaraStatelessModule_")) ? CN.Mid(23) : CN;
				if (ShortCN.Equals(ModuleName, ESearchCase::IgnoreCase) ||
					CN.Equals(ModuleName, ESearchCase::IgnoreCase))
				{
					MatchedModule = Mod;
					break;
				}
			}
			if (MatchedModule)
			{
				TArray<FString> ModInputs;
				for (TFieldIterator<FProperty> PropIt(MatchedModule->GetClass()); PropIt; ++PropIt)
				{
					if (!(PropIt->PropertyFlags & CPF_Edit)) continue;
					ModInputs.Add(PropIt->GetName());
				}
				Detail = FString::Printf(TEXT("Available inputs on module '%s': %s"),
					*ModuleName,
					ModInputs.Num() > 0 ? *FString::Join(ModInputs, TEXT(", ")) : TEXT("(none)"));
			}
			else
			{
				TArray<FString> ModuleNames;
				for (UObject* Mod : Modules)
				{
					FString CN = Mod->GetClass()->GetName();
					ModuleNames.AddUnique(CN.StartsWith(TEXT("NiagaraStatelessModule_")) ? CN.Mid(23) : CN);
				}
				Detail = FString::Printf(TEXT("Module '%s' not found on emitter. Available modules: %s"),
					*ModuleName, *FString::Join(ModuleNames, TEXT(", ")));
			}
		}
		else
		{
			Detail = FString::Printf(TEXT("Per-module properties: %s"), *FString::Join(ModuleSummaries, TEXT(" | ")));
		}

		SetError(FString::Printf(TEXT("Parameter '%s' not found. %s"), *ParameterName, *Detail), OutJsonString, OutError);
		return;
	}

	bool bSet = false;

	auto SetScalarProp = [&](FProperty* Prop, void* Container) -> bool
	{
		double NumVal = 0.0; bool BoolVal = false;
		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			if (ValueJson->TryGetNumberField(TEXT("float_value"), NumVal)) { FP->SetPropertyValue_InContainer(Container, (float)NumVal); return true; }
		}
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			if (ValueJson->TryGetNumberField(TEXT("float_value"), NumVal)) { DP->SetPropertyValue_InContainer(Container, NumVal); return true; }
		}
		else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		{
			if (ValueJson->TryGetNumberField(TEXT("float_value"), NumVal)) { IP->SetPropertyValue_InContainer(Container, (int32)NumVal); return true; }
		}
		else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			if (ValueJson->TryGetBoolField(TEXT("bool_value"), BoolVal)) { BP->SetPropertyValue_InContainer(Container, BoolVal); return true; }
		}
		else if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			FString StrVal;
			if (ValueJson->TryGetStringField(TEXT("string_value"), StrVal))
			{
				UEnum* Enm = EP->GetEnum();
				int64 EnumVal = Enm->GetValueByNameString(StrVal);
				if (EnumVal == INDEX_NONE) EnumVal = Enm->GetValueByNameString(Enm->GetName() + TEXT("::") + StrVal);
				if (EnumVal != INDEX_NONE) { EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Container), EnumVal); return true; }
			}
			else if (ValueJson->TryGetNumberField(TEXT("float_value"), NumVal))
			{
				EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Container), (int64)NumVal); return true;
			}
		}
		else if (FByteProperty* ByteP = CastField<FByteProperty>(Prop))
		{
			FString StrVal;
			if (ByteP->Enum && ValueJson->TryGetStringField(TEXT("string_value"), StrVal))
			{
				int64 EnumVal = ByteP->Enum->GetValueByNameString(StrVal);
				if (EnumVal == INDEX_NONE) EnumVal = ByteP->Enum->GetValueByNameString(ByteP->Enum->GetName() + TEXT("::") + StrVal);
				if (EnumVal != INDEX_NONE) { ByteP->SetPropertyValue_InContainer(Container, (uint8)EnumVal); return true; }
			}
			else if (ValueJson->TryGetNumberField(TEXT("float_value"), NumVal))
			{
				ByteP->SetPropertyValue_InContainer(Container, (uint8)NumVal); return true;
			}
		}
		return false;
	};

	if (!SubPropName.IsEmpty())
	{
		FStructProperty* SP = CastField<FStructProperty>(FoundProp);
		if (!SP) { SetError(FString::Printf(TEXT("'%s' is not a struct — cannot use dot-notation."), *TopPropName), OutJsonString, OutError); return; }

		void* StructPtr = SP->ContainerPtrToValuePtr<void>(FoundModule);

		FString SubTop = SubPropName;
		FString SubSub;
		int32 SubDotIdx;
		if (SubPropName.FindChar('.', SubDotIdx))
		{
			SubTop = SubPropName.Left(SubDotIdx);
			SubSub = SubPropName.Mid(SubDotIdx + 1);
		}

		FProperty* SubProp = SP->Struct->FindPropertyByName(FName(*SubTop));
		if (!SubProp)
		{
			TArray<FString> SubNames;
			for (TFieldIterator<FProperty> It(SP->Struct); It; ++It) SubNames.Add(It->GetName());
			SetError(FString::Printf(TEXT("Sub-property '%s' not found in struct '%s'. Fields: %s"), *SubTop, *SP->Struct->GetName(), *FString::Join(SubNames, TEXT(", "))), OutJsonString, OutError);
			return;
		}

		if (!SubSub.IsEmpty())
		{
			FStructProperty* SubSP = CastField<FStructProperty>(SubProp);
			if (!SubSP) { SetError(FString::Printf(TEXT("'%s.%s' is not a struct."), *TopPropName, *SubTop), OutJsonString, OutError); return; }
			void* SubStructPtr = SubSP->ContainerPtrToValuePtr<void>(StructPtr);
			FProperty* LeafProp = SubSP->Struct->FindPropertyByName(FName(*SubSub));
			if (!LeafProp) { SetError(FString::Printf(TEXT("Leaf property '%s' not found."), *SubSub), OutJsonString, OutError); return; }
			bSet = SetScalarProp(LeafProp, SubStructPtr);
		}
		else if (FStructProperty* SubSP = CastField<FStructProperty>(SubProp))
		{
			void* SubStructPtr = SubSP->ContainerPtrToValuePtr<void>(StructPtr);
			FString SubSN = SubSP->Struct->GetName();
			const TArray<TSharedPtr<FJsonValue>>* VA = nullptr;
			if (ValueJson->TryGetArrayField(TEXT("vector_value"), VA) && VA)
			{
				if (SubSN == TEXT("LinearColor") && VA->Num() >= 3)
				{
					FLinearColor C((float)(*VA)[0]->AsNumber(), (float)(*VA)[1]->AsNumber(), (float)(*VA)[2]->AsNumber(), VA->Num() >= 4 ? (float)(*VA)[3]->AsNumber() : 1.f);
					FMemory::Memcpy(SubStructPtr, &C, sizeof(FLinearColor)); bSet = true;
				}
				else if ((SubSN == TEXT("Vector3f")) && VA->Num() >= 3)
				{
					FVector3f V((float)(*VA)[0]->AsNumber(), (float)(*VA)[1]->AsNumber(), (float)(*VA)[2]->AsNumber());
					FMemory::Memcpy(SubStructPtr, &V, sizeof(FVector3f)); bSet = true;
				}
				else if ((SubSN == TEXT("Vector2f")) && VA->Num() >= 2)
				{
					FVector2f V((float)(*VA)[0]->AsNumber(), (float)(*VA)[1]->AsNumber());
					FMemory::Memcpy(SubStructPtr, &V, sizeof(FVector2f)); bSet = true;
				}
			}
		}
		else
		{
			bSet = SetScalarProp(SubProp, StructPtr);
		}
	}
	else if (FStructProperty* SP = CastField<FStructProperty>(FoundProp))
	{
		if (SP->Struct->IsChildOf(FNiagaraDistributionBase::StaticStruct())
			|| SP->Struct->GetName().StartsWith(TEXT("NiagaraDistribution")))
		{
			SetError(FString::Printf(
				TEXT("'%s' is a distribution (%s). Use set_niagara_distribution_mode to set Direct/Range/NonUniformRange values, or set_niagara_distribution_curve for curve modes. Do not use set_niagara_module_parameter on distribution properties."),
				*ParameterName, *SP->Struct->GetName()), OutJsonString, OutError);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* VA = nullptr;
		void* Ptr = SP->ContainerPtrToValuePtr<void>(FoundModule);
		FString SN = SP->Struct->GetName();
		if (ValueJson->TryGetArrayField(TEXT("vector_value"), VA) && VA)
		{
			if (SN == TEXT("LinearColor") && VA->Num() >= 3)
			{
				FLinearColor C((float)(*VA)[0]->AsNumber(), (float)(*VA)[1]->AsNumber(), (float)(*VA)[2]->AsNumber(), VA->Num() >= 4 ? (float)(*VA)[3]->AsNumber() : 1.f);
				FMemory::Memcpy(Ptr, &C, sizeof(FLinearColor)); bSet = true;
			}
			else if ((SN == TEXT("Vector2f") || SN.Contains(TEXT("Vector2"))) && VA->Num() >= 2)
			{
				FVector2f V((float)(*VA)[0]->AsNumber(), (float)(*VA)[1]->AsNumber());
				FMemory::Memcpy(Ptr, &V, sizeof(FVector2f)); bSet = true;
			}
			else if ((SN == TEXT("Vector3f") || SN.Contains(TEXT("Vector3"))) && VA->Num() >= 3)
			{
				FVector3f V((float)(*VA)[0]->AsNumber(), (float)(*VA)[1]->AsNumber(), (float)(*VA)[2]->AsNumber());
				FMemory::Memcpy(Ptr, &V, sizeof(FVector3f)); bSet = true;
			}
		}
	}
	else
	{
		bSet = SetScalarProp(FoundProp, FoundModule);
	}

	if (!bSet)
	{
		if (FStructProperty* SP = CastField<FStructProperty>(FoundProp))
		{
			const FString StructName = SP->Struct->GetName();
			if (SP->Struct->IsChildOf(FNiagaraDistributionBase::StaticStruct())
				|| StructName.StartsWith(TEXT("NiagaraDistribution")))
			{
				SetError(FString::Printf(TEXT("'%s' is a distribution (%s). Use set_niagara_distribution_mode to set Direct/Range/NonUniformRange values, or set_niagara_distribution_curve for curve modes. Do not use set_niagara_module_parameter on distribution properties."), *ParameterName, *StructName), OutJsonString, OutError);
				return;
			}
		}
		SetError(FString::Printf(TEXT("Could not set '%s' (prop type: %s). Use float_value, bool_value, or vector_value."), *ParameterName, *FoundProp->GetClass()->GetName()), OutJsonString, OutError);
		return;
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("parameter"), ParameterName);
	Resp->SetStringField(TEXT("module"), FoundModule->GetClass()->GetName());
	CompileAndReport(System, Resp);
	BuildSuccessJson(Resp, OutJsonString);
}

void HandleSetNiagaraDistributionCurve(const FString& SystemPath, const FString& EmitterName, const FString& ModuleName, const FString& PropertyName, const TSharedPtr<FJsonObject>& ArgsJson, FString& OutJsonString, FString& OutError)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	TArray<UObject*> Modules;
	UNiagaraStatelessEmitter* StatelessEmitter = FindStatelessEmitterFromHandle(System, EmitterName);
	if (StatelessEmitter)
	{
		for (UNiagaraStatelessModule* Mod : StatelessEmitter->GetModules())
			if (Mod) Modules.Add(Mod);
	}

	FString ModuleNameLower = ModuleName.ToLower();
	UObject* FoundModule = nullptr;
	FProperty* FoundProp = nullptr;

	for (UObject* Mod : Modules)
	{
		FString ClassName = Mod->GetClass()->GetName();
		FString ShortName = ClassName.StartsWith(TEXT("NiagaraStatelessModule_")) ? ClassName.Mid(23) : ClassName;
		if (!ModuleNameLower.IsEmpty() && !ShortName.ToLower().Equals(ModuleNameLower) && !ClassName.ToLower().Equals(ModuleNameLower))
			continue;
		for (TFieldIterator<FProperty> PropIt(Mod->GetClass()); PropIt; ++PropIt)
		{
			if (!(PropIt->PropertyFlags & CPF_Edit)) continue;
			if (PropIt->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { FoundModule = Mod; FoundProp = *PropIt; break; }
		}
		if (FoundModule) break;
	}

	if (!FoundModule || !FoundProp)
	{
		if (Modules.Num() == 0)
		{
			const EEmitterKind Kind = ClassifyEmitter(System, EmitterName);
			if (Kind == EEmitterKind::Classic)
				SetError(FString::Printf(TEXT("Emitter '%s' is a classic emitter — set_niagara_distribution_curve only works on stateless (lightweight) emitters. To get curve-driven color/scale, recreate the system from a Lightweight template (FountainLightweight, DirectionalBurstLightweight, RadialBurstLightweight, MinimalLightweight) — list with `list_niagara_templates(systems_only=True, filter='Lightweight')`. Or keep this classic emitter and set Min/Max constants via `set_niagara_module_parameter` instead (no curve animation)."), *EmitterName), OutJsonString, OutError);
			else
				SetError(FString::Printf(TEXT("Emitter '%s' not found in system. Run get_niagara_summary to list emitters."), *EmitterName), OutJsonString, OutError);
			return;
		}
		TArray<FString> DistProps;
		for (UObject* Mod : Modules)
		{
			FString CN = Mod->GetClass()->GetName();
			FString SN = CN.StartsWith(TEXT("NiagaraStatelessModule_")) ? CN.Mid(23) : CN;
			if (!ModuleNameLower.IsEmpty() && !SN.ToLower().Equals(ModuleNameLower) && !CN.ToLower().Equals(ModuleNameLower))
				continue;
			for (TFieldIterator<FProperty> It(Mod->GetClass()); It; ++It)
			{
				if (!(It->PropertyFlags & CPF_Edit)) continue;
				if (FStructProperty* MSP = CastField<FStructProperty>(*It))
				{
					if (MSP->Struct->IsChildOf(FNiagaraDistributionBase::StaticStruct()))
						DistProps.AddUnique(It->GetName());
				}
			}
		}
		const FString PropList = DistProps.Num() > 0 ? FString::Join(DistProps, TEXT(", ")) : TEXT("(none on this module)");
		SetError(FString::Printf(TEXT("Property '%s' not found on module '%s'. Distribution properties on this module: %s"), *PropertyName, *ModuleName, *PropList), OutJsonString, OutError);
		return;
	}

	FStructProperty* SP = CastField<FStructProperty>(FoundProp);
	if (!SP || !SP->Struct->IsChildOf(FNiagaraDistributionBase::StaticStruct()))
	{
		SetError(FString::Printf(TEXT("'%s' is not a distribution property."), *PropertyName), OutJsonString, OutError);
		return;
	}

	void* DistPtr = SP->ContainerPtrToValuePtr<void>(FoundModule);
	FNiagaraDistributionBase* DistBase = (FNiagaraDistributionBase*)DistPtr;

	const TArray<TSharedPtr<FJsonValue>>* CurveValues = nullptr;
	for (const TCHAR* Key : { TEXT("curve_values"), TEXT("values"), TEXT("keyframes"),
		TEXT("points"), TEXT("curve"), TEXT("curves"), TEXT("keys") })
	{
		if (ArgsJson->TryGetArrayField(Key, CurveValues) && CurveValues && CurveValues->Num() > 0)
			break;
		CurveValues = nullptr;
	}
	if (!CurveValues || CurveValues->Num() < 2)
	{
		SetError(TEXT("'curve_values' array required with at least 2 keyframes. For float: curve_values=[1.0, 0.0]. For color (RGBA per keyframe): curve_values=[[1,1,1,1], [1,1,1,0]]. Aliased field names accepted: values, keyframes, points, curve, curves, keys."), OutJsonString, OutError);
		return;
	}

	FString StructName = SP->Struct->GetName();
	const bool bIsColor   = StructName.Contains(TEXT("Color"));
	const bool bIsVector2 = StructName.Contains(TEXT("Vector2"));
	const bool bIsVector3 = StructName.Contains(TEXT("Vector3"));
	const int32 NumChannels = bIsColor ? 4 : (bIsVector2 ? 2 : (bIsVector3 ? 3 : 1));

	if (StructName.Contains(TEXT("Range")))
	{
		SetError(FString::Printf(TEXT("Distribution '%s' (%s) is a Range-only struct — it cannot hold curves. Use set_niagara_distribution_mode (mode=range/nonuniform_range) for Min/Max ranges, or `set_niagara_module_parameter` for fixed scalar values. Curve modes only work on FNiagaraDistribution[Color|Float|Vector2|Vector3] — the non-Range variants."),
			*PropertyName, *StructName), OutJsonString, OutError);
		return;
	}

	DistBase->Mode = (NumChannels > 1)
		? ENiagaraDistributionMode::NonUniformCurve
		: ENiagaraDistributionMode::UniformCurve;

	FArrayProperty* ChannelCurvesProp = FindFProperty<FArrayProperty>(SP->Struct, TEXT("ChannelCurves"));
	if (ChannelCurvesProp)
	{
		FScriptArrayHelper CurvesHelper(ChannelCurvesProp, ChannelCurvesProp->ContainerPtrToValuePtr<void>(DistPtr));
		CurvesHelper.Resize(NumChannels);

		const int32 NumKeyframes = CurveValues->Num();
		for (int32 Ch = 0; Ch < NumChannels; Ch++)
		{
			FRichCurve* RichCurve = reinterpret_cast<FRichCurve*>(CurvesHelper.GetRawPtr(Ch));
			RichCurve->Reset();

			for (int32 k = 0; k < NumKeyframes; k++)
			{
				float Time = (NumKeyframes > 1) ? (float)k / (float)(NumKeyframes - 1) : 0.0f;
				float Value = 0.0f;

				const TArray<TSharedPtr<FJsonValue>>* SubArr = nullptr;
				if ((*CurveValues)[k]->TryGetArray(SubArr) && SubArr && SubArr->Num() > Ch)
					Value = (float)(*SubArr)[Ch]->AsNumber();
				else if ((*CurveValues)[k]->Type == EJson::Number)
					Value = (float)(*CurveValues)[k]->AsNumber();

				RichCurve->AddKey(Time, Value);
			}
		}

		FStructProperty* TimeRangeProp = FindFProperty<FStructProperty>(SP->Struct, TEXT("ValuesTimeRange"));
		if (TimeRangeProp)
		{
			void* TimeRangePtr = TimeRangeProp->ContainerPtrToValuePtr<void>(DistPtr);
			const float Range[2] = { 0.0f, 1.0f };
			FMemory::Memcpy(TimeRangePtr, &Range, sizeof(Range));
		}

		DistBase->UpdateValuesFromDistribution();
	}
	else
	{
		if (bIsColor)
		{
			FArrayProperty* ValuesProp = FindFProperty<FArrayProperty>(SP->Struct, TEXT("Values"));
			if (ValuesProp)
			{
				FScriptArrayHelper ValHelper(ValuesProp, ValuesProp->ContainerPtrToValuePtr<void>(DistPtr));
				ValHelper.Resize(CurveValues->Num());
				for (int32 i = 0; i < CurveValues->Num(); i++)
				{
					const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
					if ((*CurveValues)[i]->TryGetArray(ColorArr) && ColorArr->Num() >= 3)
					{
						FLinearColor C(
							(float)(*ColorArr)[0]->AsNumber(), (float)(*ColorArr)[1]->AsNumber(),
							(float)(*ColorArr)[2]->AsNumber(),
							ColorArr->Num() >= 4 ? (float)(*ColorArr)[3]->AsNumber() : 1.0f);
						FMemory::Memcpy(ValHelper.GetRawPtr(i), &C, sizeof(FLinearColor));
					}
				}
			}
		}
		else
		{
			FArrayProperty* ValuesProp = FindFProperty<FArrayProperty>(SP->Struct, TEXT("Values"));
			if (ValuesProp)
			{
				FScriptArrayHelper ValHelper(ValuesProp, ValuesProp->ContainerPtrToValuePtr<void>(DistPtr));
				ValHelper.Resize(CurveValues->Num());
				if (FFloatProperty* FP = CastField<FFloatProperty>(ValuesProp->Inner))
					for (int32 i = 0; i < CurveValues->Num(); i++)
						FP->SetPropertyValue(ValHelper.GetRawPtr(i), (float)(*CurveValues)[i]->AsNumber());
			}
		}
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("module"), FoundModule->GetClass()->GetName());
	Resp->SetStringField(TEXT("property"), PropertyName);
	Resp->SetStringField(TEXT("mode"), TEXT("UniformCurve"));
	Resp->SetNumberField(TEXT("keyframes"), CurveValues->Num());
	CompileAndReport(System, Resp);
	BuildSuccessJson(Resp, OutJsonString);
}

namespace
{
	bool ReadChannelValues(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, int32 Channels, TArray<double>& Out)
	{
		Out.Reset(); Out.SetNumZeroed(Channels);
		double Scalar = 0;
		if (Args->TryGetNumberField(Key, Scalar))
		{
			for (int32 i = 0; i < Channels; ++i) Out[i] = Scalar;
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(Key, Arr) && Arr && Arr->Num() > 0)
		{
			const int32 N = FMath::Min(Channels, Arr->Num());
			for (int32 i = 0; i < N; ++i) (*Arr)[i]->TryGetNumber(Out[i]);
			for (int32 i = N; i < Channels; ++i) Out[i] = Out[N - 1];
			return true;
		}
		return false;
	}

	bool WriteDistributionMinMax(UScriptStruct* DistStruct, void* DistPtr, const FString& FieldName,
		const TArray<double>& Values, int32 Channels)
	{
		FProperty* Prop = DistStruct->FindPropertyByName(FName(*FieldName));
		if (!Prop) return false;
		void* Ptr = Prop->ContainerPtrToValuePtr<void>(DistPtr);

		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			FP->SetPropertyValue(Ptr, (float)Values[0]); return true;
		}
		if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		{
			IP->SetPropertyValue(Ptr, (int32)Values[0]); return true;
		}
		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			const FString SN = SP->Struct->GetName();
			if (SN == TEXT("LinearColor") && Channels >= 3)
			{
				FLinearColor C((float)Values[0], (float)Values[1], (float)Values[2],
					Channels >= 4 ? (float)Values[3] : 1.f);
				FMemory::Memcpy(Ptr, &C, sizeof(FLinearColor)); return true;
			}
			if (SN.Contains(TEXT("Vector3")) && Channels >= 3)
			{
				FVector3f V((float)Values[0], (float)Values[1], (float)Values[2]);
				FMemory::Memcpy(Ptr, &V, sizeof(FVector3f)); return true;
			}
			if (SN.Contains(TEXT("Vector2")) && Channels >= 2)
			{
				FVector2f V((float)Values[0], (float)Values[1]);
				FMemory::Memcpy(Ptr, &V, sizeof(FVector2f)); return true;
			}
		}
		return false;
	}
}

void HandleSetNiagaraDistributionModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, ModuleName, PropertyName, ModeStr;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("module_name"), ModuleName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("mode"), ModeStr);

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	UNiagaraStatelessEmitter* StatelessEmitter = FindStatelessEmitterFromHandle(System, EmitterName);
	if (!StatelessEmitter)
	{
		const EEmitterKind Kind = ClassifyEmitter(System, EmitterName);
		if (Kind == EEmitterKind::Classic)
			SetError(FString::Printf(TEXT("Emitter '%s' is a classic emitter — set_niagara_distribution_mode is stateless-only (distribution Mode/Min/Max struct is a stateless-emitter concept). For classic emitters, the equivalent is to write the module's underlying float/vector inputs directly via set_niagara_module_parameter — e.g. parameter_name='LifetimeMin'/'LifetimeMax' for a range, or 'Lifetime' for a constant. Call get_emitter_modules first to see the input names for the module on this emitter."), *EmitterName), OutJsonString, OutError);
		else
			SetError(FString::Printf(TEXT("Emitter '%s' not found in system. Run get_niagara_summary to list emitters."), *EmitterName), OutJsonString, OutError);
		return;
	}

	const FString ModuleNameLower = ModuleName.ToLower();
	UObject* FoundModule = nullptr;
	FStructProperty* FoundProp = nullptr;
	for (UNiagaraStatelessModule* Mod : StatelessEmitter->GetModules())
	{
		if (!Mod) continue;
		FString ClassName = Mod->GetClass()->GetName();
		FString ShortName = ClassName.StartsWith(TEXT("NiagaraStatelessModule_")) ? ClassName.Mid(23) : ClassName;
		if (!ModuleNameLower.IsEmpty() && !ShortName.ToLower().Equals(ModuleNameLower) && !ClassName.ToLower().Equals(ModuleNameLower))
			continue;
		for (TFieldIterator<FProperty> PropIt(Mod->GetClass()); PropIt; ++PropIt)
		{
			if (!(PropIt->PropertyFlags & CPF_Edit)) continue;
			if (!PropIt->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) continue;
			FStructProperty* SP = CastField<FStructProperty>(*PropIt);
			if (SP && SP->Struct->IsChildOf(FNiagaraDistributionBase::StaticStruct()))
			{
				FoundModule = Mod; FoundProp = SP; break;
			}
		}
		if (FoundModule) break;
	}
	if (!FoundModule || !FoundProp)
	{
		SetError(FString::Printf(TEXT("Distribution property '%s' not found on module '%s'"), *PropertyName, *ModuleName), OutJsonString, OutError);
		return;
	}

	FString ModeLower = ModeStr.ToLower();
	ENiagaraDistributionMode NewMode = ENiagaraDistributionMode::UniformConstant;
	if (ModeLower == TEXT("direct") || ModeLower == TEXT("constant") || ModeLower == TEXT("uniform_constant"))
		NewMode = ENiagaraDistributionMode::UniformConstant;
	else if (ModeLower == TEXT("nonuniform_constant"))
		NewMode = ENiagaraDistributionMode::NonUniformConstant;
	else if (ModeLower == TEXT("range") || ModeLower == TEXT("uniform_range"))
		NewMode = ENiagaraDistributionMode::UniformRange;
	else if (ModeLower == TEXT("nonuniform_range") || ModeLower == TEXT("random_range"))
		NewMode = ENiagaraDistributionMode::NonUniformRange;
	else
	{
		SetError(FString::Printf(TEXT("Unknown mode '%s'. Valid: direct | range | nonuniform_range. Use set_niagara_distribution_curve for curve modes."), *ModeStr), OutJsonString, OutError);
		return;
	}

	void* DistPtr = FoundProp->ContainerPtrToValuePtr<void>(FoundModule);
	FNiagaraDistributionBase* DistBase = (FNiagaraDistributionBase*)DistPtr;

	const FString StructName = FoundProp->Struct->GetName();
	const int32 Channels = StructName.Contains(TEXT("Color")) ? 4
		: StructName.Contains(TEXT("Vector3")) ? 3
		: StructName.Contains(TEXT("Vector2")) ? 2
		: 1;

	FProperty* MinProp = FoundProp->Struct->FindPropertyByName(FName(TEXT("Min")));
	FProperty* MaxProp = FoundProp->Struct->FindPropertyByName(FName(TEXT("Max")));
	if (!MinProp || !MaxProp)
	{
		SetError(FString::Printf(TEXT("Distribution '%s' (%s) has no Min/Max fields — non-curve modes aren't supported on this distribution type. Use set_niagara_distribution_curve for curve-only distributions."), *PropertyName, *StructName), OutJsonString, OutError);
		return;
	}

	TArray<double> MinVals, MaxVals, ConstVals;
	const bool bHasMin    = ReadChannelValues(Args, TEXT("min"),   Channels, MinVals);
	const bool bHasConst  = Args->HasField(TEXT("value")) && ReadChannelValues(Args, TEXT("value"), Channels, ConstVals);
	const bool bHasValue  = bHasConst || bHasMin;
	const bool bHasMax    = ReadChannelValues(Args, TEXT("max"),   Channels, MaxVals);
	const TArray<double>& ConstSource = bHasConst ? ConstVals : MinVals;

	bool bMinWritten = false;
	bool bMaxWritten = false;
	TArray<double> ResolvedMin, ResolvedMax;
	if (NewMode == ENiagaraDistributionMode::UniformConstant
		|| NewMode == ENiagaraDistributionMode::NonUniformConstant)
	{
		if (!bHasValue)
		{
			SetError(TEXT("Constant mode requires 'value' (scalar or per-channel array)"), OutJsonString, OutError);
			return;
		}
		if (NewMode == ENiagaraDistributionMode::UniformConstant && Channels > 1 && ConstSource.Num() == Channels)
		{
			bool bAllSame = true;
			for (int32 i = 1; i < ConstSource.Num(); ++i)
			{
				if (!FMath::IsNearlyEqual(ConstSource[i], ConstSource[0]))
				{
					bAllSame = false;
					break;
				}
			}
			if (!bAllSame)
			{
				NewMode = ENiagaraDistributionMode::NonUniformConstant;
			}
		}
		bMinWritten = WriteDistributionMinMax(FoundProp->Struct, DistPtr, TEXT("Min"), ConstSource, Channels);
		bMaxWritten = WriteDistributionMinMax(FoundProp->Struct, DistPtr, TEXT("Max"), ConstSource, Channels);
		ResolvedMin = ConstSource;
		ResolvedMax = ConstSource;
	}
	else
	{
		if (!bHasMin || !bHasMax)
		{
			SetError(TEXT("Range mode requires both 'min' and 'max' (scalar or per-channel array)"), OutJsonString, OutError);
			return;
		}
		bMinWritten = WriteDistributionMinMax(FoundProp->Struct, DistPtr, TEXT("Min"), MinVals, Channels);
		bMaxWritten = WriteDistributionMinMax(FoundProp->Struct, DistPtr, TEXT("Max"), MaxVals, Channels);
		ResolvedMin = MinVals;
		ResolvedMax = MaxVals;
	}

	if (!bMinWritten || !bMaxWritten)
	{
		SetError(FString::Printf(TEXT("Failed to write Min/Max on distribution '%s' (struct: %s, channels: %d). This typically means the distribution uses a non-standard inner type the typed-setter doesn't recognise. The asset is unchanged."), *PropertyName, *StructName, Channels), OutJsonString, OutError);
		return;
	}

	DistBase->Mode = NewMode;

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	const UEnum* ModeEnum = StaticEnum<ENiagaraDistributionMode>();
	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("module"), FoundModule->GetClass()->GetName());
	Resp->SetStringField(TEXT("property"), PropertyName);
	Resp->SetStringField(TEXT("mode"), ModeEnum ? ModeEnum->GetNameStringByValue((int64)NewMode) : ModeStr);
	Resp->SetNumberField(TEXT("channels"), Channels);

	auto ToJsonArray = [](const TArray<double>& Vals)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (double V : Vals) Out.Add(MakeShareable(new FJsonValueNumber(V)));
		return Out;
	};
	Resp->SetArrayField(TEXT("min"), ToJsonArray(ResolvedMin));
	Resp->SetArrayField(TEXT("max"), ToJsonArray(ResolvedMax));

	CompileAndReport(System, Resp);
	BuildSuccessJson(Resp, OutJsonString);
}

namespace
{
	bool SetSingleModuleEnabled(UNiagaraSystem* System, const FString& EmitterName,
		const FString& ModuleName, bool bEnabled, FString& OutModuleClass)
	{
		UNiagaraStatelessEmitter* StatelessEmitter = FindStatelessEmitterFromHandle(System, EmitterName);
		if (!StatelessEmitter) return false;
		const FString ModuleNameLower = ModuleName.ToLower();
		for (UNiagaraStatelessModule* Mod : StatelessEmitter->GetModules())
		{
			if (!Mod) continue;
			const FString ClassName = Mod->GetClass()->GetName();
			const FString ShortName = ClassName.StartsWith(TEXT("NiagaraStatelessModule_")) ? ClassName.Mid(23) : ClassName;
			if (ShortName.ToLower().Equals(ModuleNameLower) || ClassName.ToLower().Equals(ModuleNameLower))
			{
				Mod->SetIsModuleEnabled(bEnabled);
				Mod->Modify();
				OutModuleClass = ClassName;
				return true;
			}
		}
		return false;
	}
}

void HandleSetNiagaraModuleEnabledFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("modules"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString MN = BatchToolHelper::GetItemString(Item, TEXT("module_name"));
			if (MN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing module_name")); continue; }
			bool bE = true; Item->TryGetBoolField(TEXT("enabled"), bE);
			FString MCls;
			if (SetSingleModuleEnabled(System, EmitterName, MN, bE, MCls))
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("module_name"), MN);
				Extra->SetStringField(TEXT("module_class"), MCls);
				Extra->SetBoolField(TEXT("enabled"), bE);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, FString::Printf(TEXT("Module '%s' not found on emitter '%s'"), *MN, *EmitterName));
		}
		Batch.Finalize(OutJsonString);
		RebuildStatelessEmitterCache(System);
		System->MarkPackageDirty();
		return;
	}

	FString ModuleName; Args->TryGetStringField(TEXT("module_name"), ModuleName);
	if (ModuleName.IsEmpty()) { OutError = TEXT("Missing module_name"); return; }
	bool bEnabled = true; Args->TryGetBoolField(TEXT("enabled"), bEnabled);

	FString ModuleClass;
	if (!SetSingleModuleEnabled(System, EmitterName, ModuleName, bEnabled, ModuleClass))
	{
		const EEmitterKind Kind = ClassifyEmitter(System, EmitterName);
		if (Kind == EEmitterKind::Classic)
		{
			SetError(FString::Printf(TEXT("Emitter '%s' is a classic emitter — set_niagara_module_enabled is stateless-only (bModuleEnabled is a stateless-emitter FProperty). For classic emitters, modules don't have a simple enable/disable toggle — to remove a module from the stack use remove_niagara_module; to add one use add_niagara_module."), *EmitterName), OutJsonString, OutError);
		}
		else if (Kind == EEmitterKind::None)
		{
			TArray<FString> Names;
			for (FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
			SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		}
		else
		{
			TArray<FString> Available;
			if (UNiagaraStatelessEmitter* SE = FindStatelessEmitterFromHandle(System, EmitterName))
			{
				for (UNiagaraStatelessModule* Mod : SE->GetModules())
				{
					if (!Mod) continue;
					const FString CN = Mod->GetClass()->GetName();
					Available.AddUnique(CN.StartsWith(TEXT("NiagaraStatelessModule_")) ? CN.Mid(23) : CN);
				}
			}
			SetError(FString::Printf(TEXT("Module '%s' not found on emitter '%s'. Available: %s"), *ModuleName, *EmitterName, *FString::Join(Available, TEXT(", "))), OutJsonString, OutError);
		}
		return;
	}
	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("module_name"), ModuleName);
	Resp->SetStringField(TEXT("module_class"), ModuleClass);
	Resp->SetBoolField(TEXT("enabled"), bEnabled);
	BuildSuccessJson(Resp, OutJsonString);
}

void HandleSetNiagaraModuleParametersBulk(const FString& SystemPath, const FString& EmitterName, const TSharedPtr<FJsonObject>& ArgsJson, FString& OutJsonString, FString& OutError)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	const TArray<TSharedPtr<FJsonValue>>* ParamsArray = nullptr;
	if (!ArgsJson->TryGetArrayField(TEXT("parameters"), ParamsArray) || !ParamsArray || ParamsArray->Num() == 0)
	{
		SetError(TEXT("'parameters' array is required. Each element: {parameter_name, module_name, float_value|vector_value|bool_value|string_value}"), OutJsonString, OutError);
		return;
	}

	TArray<FString> Succeeded;
	TArray<FString> Failed;

	for (const TSharedPtr<FJsonValue>& ParamVal : *ParamsArray)
	{
		const TSharedPtr<FJsonObject>* ParamObjPtr = nullptr;
		if (!ParamVal->TryGetObject(ParamObjPtr) || !ParamObjPtr || !(*ParamObjPtr).IsValid())
		{
			Failed.Add(TEXT("(invalid parameter object)"));
			continue;
		}
		const TSharedPtr<FJsonObject>& ParamObj = *ParamObjPtr;

		FString SingleOut, SingleErr;
		FString ParamName, ModuleName, ScriptSection;
		ParamObj->TryGetStringField(TEXT("parameter_name"), ParamName);
		ParamObj->TryGetStringField(TEXT("module_name"), ModuleName);
		ParamObj->TryGetStringField(TEXT("script_section"), ScriptSection);

		HandleSetNiagaraModuleParameter(SystemPath, EmitterName, ScriptSection, ParamName, ModuleName, ParamObj, SingleOut, SingleErr);

		if (SingleErr.IsEmpty())
		{
			Succeeded.Add(ParamName);
		}
		else
		{
			Failed.Add(FString::Printf(TEXT("%s: %s"), *ParamName, *SingleErr));
		}
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), Failed.Num() == 0);
	Resp->SetNumberField(TEXT("succeeded"), Succeeded.Num());
	Resp->SetNumberField(TEXT("failed"), Failed.Num());
	Resp->SetNumberField(TEXT("total"), ParamsArray->Num());

	TArray<TSharedPtr<FJsonValue>> SuccArr;
	for (const FString& S : Succeeded) SuccArr.Add(MakeShareable(new FJsonValueString(S)));
	Resp->SetArrayField(TEXT("succeeded_params"), SuccArr);

	if (Failed.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailArr;
		for (const FString& F : Failed) FailArr.Add(MakeShareable(new FJsonValueString(F)));
		Resp->SetArrayField(TEXT("failed_params"), FailArr);
	}
	BuildSuccessJson(Resp, OutJsonString);
}

void HandleAddNiagaraModule(const FString& SystemPath, const FString& EmitterName, const FString& ScriptSection, const FString& ModulePath, int32 InsertIndex, FString& OutJsonString, FString& OutError)
{
	HandleAddNiagaraModuleEx(SystemPath, EmitterName, ScriptSection, ModulePath, InsertIndex, FString(), OutJsonString, OutError);
}

void HandleAddNiagaraModuleEx(const FString& SystemPath, const FString& EmitterName, const FString& ScriptSection, const FString& ModulePath, int32 InsertIndex, const FString& EventName, FString& OutJsonString, FString& OutError)
{
	{
		TArray<FNiagaraPreFlightIssue> PreFlight;
		RunAddModulePreFlight(ModulePath, ScriptSection, PreFlight);
		for (const FNiagaraPreFlightIssue& Issue : PreFlight)
		{
			if (Issue.bHardFail)
			{
				EmitPreFlightFailureJson(Issue, OutJsonString, OutError);
				return;
			}
		}
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* FoundHandle = FindEmitterHandle(System, EmitterName);
	if (!FoundHandle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	FVersionedNiagaraEmitterData* EmitterData = GetBestEmitterData(FoundHandle);
	if (!EmitterData) { SetError(TEXT("Could not get emitter data."), OutJsonString, OutError); return; }

	{
		const FString ModuleNameLower = FPaths::GetBaseFilename(ModulePath).ToLower();
		const bool bIsEventWrite =
			ModuleNameLower.StartsWith(TEXT("generate")) && ModuleNameLower.Contains(TEXT("event"));
		if (bIsEventWrite && EmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			SetError(FString::Printf(
				TEXT("Cannot add event-write module '%s' to emitter '%s': it runs as GPUComputeSim, ")
				TEXT("and event write nodes are CPU-only (the GPU compute backend rejects them at compile time). ")
				TEXT("Either: (a) call set_niagara_sim_target(emitter_name='%s', sim_target='CPU') first, ")
				TEXT("or (b) drop the cross-emitter event chain — GPU emitters can't author events anyway."),
				*FPaths::GetBaseFilename(ModulePath), *EmitterName, *EmitterName),
				OutJsonString, OutError);
			return;
		}
	}

	FString SectionLower = ScriptSection.ToLower();
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage TargetUsage = ENiagaraScriptUsage::ParticleSpawnScript;
	FGuid TargetUsageId;

	if (SectionLower.IsEmpty() || SectionLower.Contains(TEXT("particlespawn")) || SectionLower == TEXT("particle_spawn") || SectionLower == TEXT("spawn"))
	{
		Script = EmitterData->SpawnScriptProps.Script;
		TargetUsage = ENiagaraScriptUsage::ParticleSpawnScript;
	}
	else if (SectionLower.Contains(TEXT("particleupdate")) || SectionLower == TEXT("particle_update") || SectionLower == TEXT("update"))
	{
		Script = EmitterData->UpdateScriptProps.Script;
		TargetUsage = ENiagaraScriptUsage::ParticleUpdateScript;
	}
	else if (SectionLower.Contains(TEXT("emitterspawn")) || SectionLower == TEXT("emitter_spawn"))
	{
		Script = EmitterData->EmitterSpawnScriptProps.Script;
		TargetUsage = ENiagaraScriptUsage::EmitterSpawnScript;
	}
	else if (SectionLower.Contains(TEXT("emitterupdate")) || SectionLower == TEXT("emitter_update"))
	{
		Script = EmitterData->EmitterUpdateScriptProps.Script;
		TargetUsage = ENiagaraScriptUsage::EmitterUpdateScript;
	}
	else if (SectionLower == TEXT("event_handler") || SectionLower == TEXT("event") || SectionLower == TEXT("particleevent") || SectionLower.Contains(TEXT("event_handler")))
	{
		const TArray<FNiagaraEventScriptProperties>& Handlers = EmitterData->GetEventHandlers();
		if (Handlers.Num() == 0)
		{
			SetError(FString::Printf(TEXT("Emitter '%s' has no event handlers. Call add_niagara_event_handler first."), *EmitterName), OutJsonString, OutError);
			return;
		}
		const FNiagaraEventScriptProperties* Match = nullptr;
		if (!EventName.IsEmpty())
		{
			for (const FNiagaraEventScriptProperties& H : Handlers)
			{
				if (H.SourceEventName.ToString().Equals(EventName, ESearchCase::IgnoreCase)) { Match = &H; break; }
			}
			if (!Match)
			{
				TArray<FString> EventNames;
				for (const FNiagaraEventScriptProperties& H : Handlers) EventNames.Add(H.SourceEventName.ToString());
				SetError(FString::Printf(TEXT("Event handler '%s' not found on emitter '%s'. Available events: %s"), *EventName, *EmitterName, *FString::Join(EventNames, TEXT(", "))), OutJsonString, OutError);
				return;
			}
		}
		else if (Handlers.Num() == 1)
		{
			Match = &Handlers[0];
		}
		else
		{
			TArray<FString> EventNames;
			for (const FNiagaraEventScriptProperties& H : Handlers) EventNames.Add(H.SourceEventName.ToString());
			SetError(FString::Printf(TEXT("Emitter '%s' has %d event handlers — pass event_name to disambiguate. Available: %s"), *EmitterName, Handlers.Num(), *FString::Join(EventNames, TEXT(", "))), OutJsonString, OutError);
			return;
		}
		Script = Match->Script;
		TargetUsage = ENiagaraScriptUsage::ParticleEventScript;
		if (Script) TargetUsageId = Script->GetUsageId();
	}

	if (!Script)
	{
		SetError(FString::Printf(TEXT("No script found for section '%s'. Use: ParticleSpawn, ParticleUpdate, EmitterSpawn, EmitterUpdate, or event_handler."), *ScriptSection), OutJsonString, OutError);
		return;
	}

	UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	if (!ScriptSource || !ScriptSource->NodeGraph)
	{
		SetError(TEXT("Could not access emitter script graph. The emitter may be using a compiled-only runtime representation."), OutJsonString, OutError);
		return;
	}
	UNiagaraGraph* Graph = ScriptSource->NodeGraph;

	UNiagaraNodeOutput* OutputNode = nullptr;
	if (TargetUsage == ENiagaraScriptUsage::ParticleEventScript && TargetUsageId.IsValid())
	{
		OutputNode = Graph->FindEquivalentOutputNode(TargetUsage, TargetUsageId);
	}
	else
	{
		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (UNiagaraNodeOutput* Node : OutputNodes)
		{
			if (Node->GetUsage() == TargetUsage) { OutputNode = Node; break; }
		}
	}
	if (!OutputNode) { SetError(FString::Printf(TEXT("Could not find output node for usage '%s'."), *ScriptSection), OutJsonString, OutError); return; }

	FString CleanModulePath = StripObjectSuffix(ModulePath);
	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, *CleanModulePath);
	if (!ModuleScript) ModuleScript = LoadObject<UNiagaraScript>(nullptr, *ModulePath);
	if (!ModuleScript)
	{
		SetError(FString::Printf(TEXT("Could not load module script: %s. Use list_niagara_modules to find valid paths."), *ModulePath), OutJsonString, OutError);
		return;
	}

	FString DeprecationChainNote;
	{
		int32 RedirectHops = 0;
		while (RedirectHops < 4)
		{
			FVersionedNiagaraScriptData* SD = ModuleScript->GetScriptData(ModuleScript->GetExposedVersion().VersionGuid);
			if (!SD || !SD->bDeprecated || SD->DeprecationRecommendation == nullptr) break;
			UNiagaraScript* Successor = SD->DeprecationRecommendation;
			if (Successor == ModuleScript) break;
			DeprecationChainNote += FString::Printf(TEXT("%s → %s; "),
				*ModuleScript->GetName(), *Successor->GetName());
			ModuleScript = Successor;
			++RedirectHops;
		}
	}

	System->Modify();
	Graph->Modify();

	UNiagaraNodeFunctionCall* NewNode = NewObject<UNiagaraNodeFunctionCall>(Graph);
	NewNode->FunctionScript = ModuleScript;
	NewNode->NodeGuid = FGuid::NewGuid();

	NewNode->SelectedScriptVersion = ModuleScript->GetExposedVersion().VersionGuid;

	Graph->AddNode(NewNode, false);
	NewNode->AllocateDefaultPins();

	NewNode->NodePosX = OutputNode->NodePosX - 350;
	NewNode->NodePosY = OutputNode->NodePosY;

	UEdGraphPin* OutputInputPin = nullptr;
	for (UEdGraphPin* Pin : OutputNode->Pins)
	{
		if (Pin->Direction == EGPD_Input) { OutputInputPin = Pin; break; }
	}

	bool bWired = false;
	if (OutputInputPin && NewNode->Pins.Num() > 0)
	{
		UEdGraphPin* NewNodeMapIn = nullptr;
		UEdGraphPin* NewNodeMapOut = nullptr;
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin->PinType == OutputInputPin->PinType)
			{
				if (Pin->Direction == EGPD_Input && !NewNodeMapIn)   NewNodeMapIn  = Pin;
				else if (Pin->Direction == EGPD_Output && !NewNodeMapOut) NewNodeMapOut = Pin;
			}
		}
		if (NewNodeMapIn && NewNodeMapOut)
		{
			if (OutputInputPin->LinkedTo.Num() > 0)
			{
				UEdGraphPin* PrevPin = OutputInputPin->LinkedTo[0];
				OutputInputPin->BreakLinkTo(PrevPin);
				PrevPin->MakeLinkTo(NewNodeMapIn);
			}
			NewNodeMapOut->MakeLinkTo(OutputInputPin);
			bWired = true;
		}
	}

	Graph->NotifyGraphChanged();
	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("module_name"), NewNode->GetFunctionName());
	Resp->SetStringField(TEXT("emitter"), FoundHandle->GetName().ToString());
	Resp->SetStringField(TEXT("section"), ScriptSection);
	Resp->SetBoolField(TEXT("wired_into_chain"), bWired);
	if (!DeprecationChainNote.IsEmpty())
	{
		Resp->SetStringField(TEXT("redirected_from_deprecated"), DeprecationChainNote);
		Resp->SetStringField(TEXT("resolved_module_path"), ModuleScript->GetPathName());
	}
	Resp->SetStringField(TEXT("note"), TEXT("Module node added. Open the asset in UE editor and save to compile."));
	CompileAndReport(System, Resp);
	BuildSuccessJson(Resp, OutJsonString);
}

void HandleListNiagaraModules(const FString& Filter, FString& OutJsonString, FString& OutError)
{

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter ARFilter;
	ARFilter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraScript")));
	ARFilter.bIncludeOnlyOnDiskAssets = false;
	ARFilter.bRecursivePaths = true;
	ARFilter.PackagePaths.Add(FName(TEXT("/Niagara/Modules")));
	ARFilter.PackagePaths.Add(FName(TEXT("/Niagara/DefaultAssets")));

	TArray<FAssetData> Found;
	AssetRegistry.GetAssets(ARFilter, Found);

	FString FilterLower = Filter.ToLower();
	TArray<TSharedPtr<FJsonValue>> ResultArray;

	TSet<FString> NamesWithV2Sibling;
	for (const FAssetData& Asset : Found)
	{
		const FString PackagePath = Asset.PackagePath.ToString();
		if (PackagePath.Contains(TEXT("/V2/")) || PackagePath.EndsWith(TEXT("/V2")))
		{
			NamesWithV2Sibling.Add(Asset.AssetName.ToString());
		}
	}

	for (const FAssetData& Asset : Found)
	{
		FString AssetName = Asset.AssetName.ToString();
		FString PackagePath = Asset.PackagePath.ToString();
		if (!FilterLower.IsEmpty())
		{
			FString Combined = (AssetName + TEXT(" ") + PackagePath).ToLower();
			if (!Combined.Contains(FilterLower)) continue;
		}

		const bool bIsV2Path = PackagePath.Contains(TEXT("/V2/")) || PackagePath.EndsWith(TEXT("/V2"));
		if (!bIsV2Path && NamesWithV2Sibling.Contains(AssetName)) continue;

		bool bDeprecated = false;
		FString DepStr;
		if (Asset.GetTagValue(GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, bDeprecated), DepStr))
		{
			bDeprecated = DepStr == TEXT("True") || DepStr == TEXT("true") || DepStr == TEXT("1");
		}
		if (bDeprecated) continue;

		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("name"), AssetName);
		Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		Obj->SetStringField(TEXT("folder"), PackagePath);
		ResultArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), ResultArray.Num());
	Result->SetArrayField(TEXT("modules"), ResultArray);
	BuildSuccessJson(Result, OutJsonString);
}

static int32 RemoveOrphanEmitterNodesInSystem(UNiagaraSystem* System, const FGuid& OnlyThisHandleId = FGuid())
{
	if (!System) return 0;

	static UClass* NiagaraNodeEmitterClass = FindObject<UClass>(
		nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeEmitter"));
	if (!NiagaraNodeEmitterClass) return 0;

	TSet<FGuid> ValidHandleIds;
	if (!OnlyThisHandleId.IsValid())
	{
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles())
		{
			ValidHandleIds.Add(H.GetId());
		}
	}

	int32 Removed = 0;
	auto Sweep = [&](UNiagaraScript* Script)
	{
		if (!Script) return;
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
		if (!Graph) return;

		FProperty* IdProp = NiagaraNodeEmitterClass->FindPropertyByName(TEXT("EmitterHandleId"));
		FStructProperty* IdStruct = CastField<FStructProperty>(IdProp);
		if (!IdStruct || IdStruct->Struct != TBaseStructure<FGuid>::Get()) return;

		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (!N || !N->IsA(NiagaraNodeEmitterClass)) continue;

			FGuid NodeHandleId;
			IdStruct->CopySingleValue(&NodeHandleId, IdStruct->ContainerPtrToValuePtr<void>(N));

			const bool bIsOrphan = OnlyThisHandleId.IsValid()
				? (NodeHandleId == OnlyThisHandleId)
				: !ValidHandleIds.Contains(NodeHandleId);
			if (bIsOrphan)
			{
				NodesToRemove.Add(N);
			}
		}

		for (UEdGraphNode* N : NodesToRemove)
		{
			UEdGraphPin* InPin = nullptr;
			UEdGraphPin* OutPin = nullptr;
			for (UEdGraphPin* Pin : N->Pins)
			{
				if (!Pin) continue;
				if (Pin->PinType.PinSubCategoryObject != FNiagaraTypeDefinition::GetParameterMapStruct()) continue;
				if (Pin->Direction == EGPD_Input && !InPin) InPin = Pin;
				else if (Pin->Direction == EGPD_Output && !OutPin) OutPin = Pin;
			}

			if (InPin && OutPin && InPin->LinkedTo.Num() > 0 && OutPin->LinkedTo.Num() > 0)
			{
				TArray<UEdGraphPin*> UpstreamPins = InPin->LinkedTo;
				TArray<UEdGraphPin*> DownstreamPins = OutPin->LinkedTo;
				for (UEdGraphPin* UpstreamPin : UpstreamPins)
				{
					for (UEdGraphPin* DownstreamPin : DownstreamPins)
					{
						if (UpstreamPin && DownstreamPin)
						{
							UpstreamPin->MakeLinkTo(DownstreamPin);
						}
					}
				}
			}

			N->Modify();
			N->BreakAllNodeLinks();
			Graph->RemoveNode(N);
			++Removed;
		}
		if (NodesToRemove.Num() > 0)
		{
			Graph->NotifyGraphChanged();
		}
	};

	Sweep(System->GetSystemSpawnScript());
	Sweep(System->GetSystemUpdateScript());

	return Removed;
}

void HandleRemoveEmitterFromSystem(const FString& SystemPath, const FString& EmitterName, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	const FGuid RemovedHandleId = Handle->GetId();
	FString HandleName = Handle->GetName().ToString();

	System->Modify();
	const int32 OrphansRemoved = RemoveOrphanEmitterNodesInSystem(System, RemovedHandleId);

	int32 OrphanHandlersRemoved = 0;
	for (const FNiagaraEmitterHandle& OtherHandle : System->GetEmitterHandles())
	{
		if (OtherHandle.GetId() == RemovedHandleId) continue;
		FVersionedNiagaraEmitterData* OtherData = OtherHandle.GetEmitterData();
		UNiagaraEmitter* OtherEmitter = OtherHandle.GetInstance().Emitter;
		if (!OtherData || !OtherEmitter) continue;

		TArray<FGuid> ToRemove;
		for (const FNiagaraEventScriptProperties& Ev : OtherData->GetEventHandlers())
		{
			if (Ev.SourceEmitterID == RemovedHandleId)
			{
				FGuid UsageId = Ev.Script ? Ev.Script->GetUsageId() : FGuid();
				if (UsageId.IsValid()) ToRemove.Add(UsageId);
			}
		}
		if (ToRemove.Num() == 0) continue;

		OtherEmitter->Modify();
		const FGuid EmitterVer = OtherData->Version.VersionGuid;
		for (const FGuid& UsageId : ToRemove)
		{
			OtherEmitter->RemoveEventHandlerByUsageId(UsageId, EmitterVer);
			OrphanHandlersRemoved++;
		}
	}

	System->RemoveEmitterHandle(*Handle);
	System->RequestCompile(false);
	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_emitter"), HandleName);
	Result->SetNumberField(TEXT("remaining_emitters"), System->GetEmitterHandles().Num());
	if (OrphansRemoved > 0)
	{
		Result->SetNumberField(TEXT("orphan_emitter_nodes_cleaned"), OrphansRemoved);
	}
	if (OrphanHandlersRemoved > 0)
	{
		Result->SetNumberField(TEXT("orphan_event_handlers_cleaned"), OrphanHandlersRemoved);
	}
	BuildSuccessJson(Result, OutJsonString);
}

void HandleCleanupNiagaraSystemOrphans(const FString& SystemPath, FString& OutJsonString, FString& OutError)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	System->Modify();
	const int32 Removed = RemoveOrphanEmitterNodesInSystem(System);

	TSet<FGuid> LiveEmitterIds;
	for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles())
		LiveEmitterIds.Add(H.GetId());

	int32 OrphanHandlersRemoved = 0;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
		if (!Data || !Emitter) continue;

		TArray<FGuid> ToRemove;
		for (const FNiagaraEventScriptProperties& Ev : Data->GetEventHandlers())
		{
			if (!Ev.SourceEmitterID.IsValid()) continue;
			if (LiveEmitterIds.Contains(Ev.SourceEmitterID)) continue;
			FGuid UsageId = Ev.Script ? Ev.Script->GetUsageId() : FGuid();
			if (UsageId.IsValid()) ToRemove.Add(UsageId);
		}
		if (ToRemove.Num() == 0) continue;

		Emitter->Modify();
		const FGuid EmitterVer = Data->Version.VersionGuid;
		for (const FGuid& UsageId : ToRemove)
		{
			Emitter->RemoveEventHandlerByUsageId(UsageId, EmitterVer);
			OrphanHandlersRemoved++;
		}
	}

	if (Removed > 0 || OrphanHandlersRemoved > 0)
	{
		System->RequestCompile(false);
		RebuildStatelessEmitterCache(System);
		System->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("system_path"), System->GetPathName());
	Result->SetNumberField(TEXT("orphan_emitter_nodes_removed"), Removed);
	Result->SetNumberField(TEXT("orphan_event_handlers_removed"), OrphanHandlersRemoved);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleCleanupNiagaraSystemOrphansFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { SetError(TEXT("Invalid args"), OutJsonString, OutError); return; }
	FString SystemPath;
	if (!Args->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		SetError(TEXT("`system_path` is required"), OutJsonString, OutError);
		return;
	}
	HandleCleanupNiagaraSystemOrphans(SystemPath, OutJsonString, OutError);
}

void HandleRemoveNiagaraModule(const FString& SystemPath, const FString& EmitterName, const FString& ModuleName, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	TArray<UObject*> Modules = CollectStatelessModules(System, EmitterName);
	FString ModuleNameLower = ModuleName.ToLower();

	UObject* FoundModule = nullptr;
	for (UObject* Mod : Modules)
	{
		FString ClassName = Mod->GetClass()->GetName();
		FString ShortName = ClassName.StartsWith(TEXT("NiagaraStatelessModule_")) ? ClassName.Mid(23) : ClassName;
		if (ShortName.ToLower() == ModuleNameLower || ClassName.ToLower() == ModuleNameLower)
		{
			FoundModule = Mod;
			break;
		}
	}

	if (!FoundModule)
	{
		if (Modules.Num() == 0)
		{
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
			FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
			if (Data)
			{
				auto TryRemoveFromScript = [&](UNiagaraScript* Script) -> UNiagaraNodeFunctionCall*
				{
					if (!Script) return nullptr;
					UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
					UNiagaraGraph* Graph = Src ? Src->NodeGraph : nullptr;
					if (!Graph) return nullptr;

					UNiagaraNodeFunctionCall* Found = nullptr;
					for (UEdGraphNode* N : Graph->Nodes)
					{
						UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(N);
						if (FC && FC->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase))
						{
							Found = FC;
							break;
						}
					}
					if (!Found) return nullptr;

					Graph->Modify();

					UEdGraphPin* InPin = nullptr;
					UEdGraphPin* OutPin = nullptr;
					for (UEdGraphPin* Pin : Found->Pins)
					{
						if (!Pin) continue;
						if (Pin->PinType.PinSubCategoryObject != FNiagaraTypeDefinition::GetParameterMapStruct()) continue;
						if (Pin->Direction == EGPD_Input && !InPin) InPin = Pin;
						else if (Pin->Direction == EGPD_Output && !OutPin) OutPin = Pin;
					}
					if (InPin && OutPin && InPin->LinkedTo.Num() > 0 && OutPin->LinkedTo.Num() > 0)
					{
						TArray<UEdGraphPin*> Up = InPin->LinkedTo;
						TArray<UEdGraphPin*> Down = OutPin->LinkedTo;
						for (UEdGraphPin* U : Up)
						{
							for (UEdGraphPin* D : Down)
							{
								if (U && D) U->MakeLinkTo(D);
							}
						}
					}
					Found->Modify();
					Found->BreakAllNodeLinks();
					Graph->RemoveNode(Found);
					Graph->NotifyGraphChanged();
					return Found;
				};

				FString FoundSection;
				if (TryRemoveFromScript(Data->SpawnScriptProps.Script))         FoundSection = TEXT("particle_spawn");
				else if (TryRemoveFromScript(Data->UpdateScriptProps.Script))   FoundSection = TEXT("particle_update");
				else if (TryRemoveFromScript(Data->EmitterSpawnScriptProps.Script))  FoundSection = TEXT("emitter_spawn");
				else if (TryRemoveFromScript(Data->EmitterUpdateScriptProps.Script)) FoundSection = TEXT("emitter_update");

				if (!FoundSection.IsEmpty())
				{
					System->RequestCompile(false);
					System->MarkPackageDirty();

					TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
					Resp->SetBoolField(TEXT("success"), true);
					Resp->SetStringField(TEXT("removed_module"), ModuleName);
					Resp->SetStringField(TEXT("emitter"), EmitterName);
					Resp->SetStringField(TEXT("emitter_type"), TEXT("classic"));
					Resp->SetStringField(TEXT("script_section"), FoundSection);
					CompileAndReport(System, Resp);
					BuildSuccessJson(Resp, OutJsonString);
					return;
				}

				SetError(FString::Printf(TEXT("Module '%s' not found on classic emitter '%s' in any script section. Use get_emitter_modules to see the exact module names available."), *ModuleName, *EmitterName), OutJsonString, OutError);
				return;
			}
			SetError(FString::Printf(TEXT("Emitter '%s' not found on system."), *EmitterName), OutJsonString, OutError);
			return;
		}
		TArray<FString> Available;
		for (UObject* Mod : Modules)
		{
			FString CN = Mod->GetClass()->GetName();
			Available.AddUnique(CN.StartsWith(TEXT("NiagaraStatelessModule_")) ? CN.Mid(23) : CN);
		}
		SetError(FString::Printf(TEXT("Module '%s' not found. Available on emitter '%s': %s"), *ModuleName, *EmitterName, *FString::Join(Available, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	FString RemovedClass = FoundModule->GetClass()->GetName();
	UObject* EmitterObj = FoundModule->GetOuter();
	bool bRemoved = false;

	if (EmitterObj)
	{
		EmitterObj->Modify();
		for (TFieldIterator<FArrayProperty> ArrayIt(EmitterObj->GetClass()); ArrayIt; ++ArrayIt)
		{
			FArrayProperty* AP = *ArrayIt;
			FObjectProperty* Inner = CastField<FObjectProperty>(AP->Inner);
			if (!Inner) continue;
			FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(EmitterObj));
			for (int32 i = 0; i < Helper.Num(); i++)
			{
				UObject* Elem = Inner->GetObjectPropertyValue(Helper.GetRawPtr(i));
				if (Elem == FoundModule)
				{
					Helper.RemoveValues(i, 1);
					FoundModule->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
					bRemoved = true;
					break;
				}
			}
			if (bRemoved) break;
		}
	}

	if (!bRemoved)
	{
		FoundModule->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_module"), RemovedClass);
	BuildSuccessJson(Result, OutJsonString);
}

static UObject* FindStatelessEmitterObject(UNiagaraSystem* System, const FString& EmitterName)
{
	TArray<UObject*> Modules = CollectStatelessModules(System, EmitterName);
	if (Modules.Num() > 0) return Modules[0]->GetOuter();
	TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
	if (Renderers.Num() > 0) return Renderers[0]->GetOuter();
	return nullptr;
}

void HandleSetEmitterProperties(const FString& SystemPath, const FString& EmitterName, const TSharedPtr<FJsonObject>& ValueJsonRaw, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> ValueJson = ValueJsonRaw;
	{
		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (ValueJsonRaw->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
		{
			ValueJson = *PropsObj;
		}
	}

	System->Modify();
	TArray<FString> SetProps;

	bool bEnabledVal = false;
	if (ValueJson->TryGetBoolField(TEXT("enabled"), bEnabledVal))
	{
		Handle->SetIsEnabled(bEnabledVal, *System, false);
		SetProps.Add(TEXT("enabled"));
	}

	auto TryWriteOneField = [&ValueJson, &SetProps](FProperty* Prop, void* Container) -> bool
	{
		if (!(Prop->PropertyFlags & CPF_Edit)) return false;
		const FString PropName = Prop->GetName();
		double NumVal = 0.0; bool BoolVal = false;
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			if (ValueJson->TryGetBoolField(PropName, BoolVal))
			{ BP->SetPropertyValue_InContainer(Container, BoolVal); SetProps.Add(PropName); return true; }
		}
		else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		{
			if (ValueJson->TryGetNumberField(PropName, NumVal))
			{ IP->SetPropertyValue_InContainer(Container, (int32)NumVal); SetProps.Add(PropName); return true; }
		}
		else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			if (ValueJson->TryGetNumberField(PropName, NumVal))
			{ FP->SetPropertyValue_InContainer(Container, (float)NumVal); SetProps.Add(PropName); return true; }
		}
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			if (ValueJson->TryGetNumberField(PropName, NumVal))
			{ DP->SetPropertyValue_InContainer(Container, NumVal); SetProps.Add(PropName); return true; }
		}
		else if (FByteProperty* ByP = CastField<FByteProperty>(Prop))
		{
			if (ValueJson->TryGetNumberField(PropName, NumVal))
			{ ByP->SetPropertyValue_InContainer(Container, (uint8)NumVal); SetProps.Add(PropName); return true; }
		}
		else if (FEnumProperty* EnP = CastField<FEnumProperty>(Prop))
		{
			FString EnumStr;
			if (ValueJson->TryGetStringField(PropName, EnumStr))
			{
				UEnum* Enum = EnP->GetEnum();
				int64 EnumVal = Enum ? Enum->GetValueByNameString(EnumStr) : INDEX_NONE;
				if (EnumVal == INDEX_NONE && Enum) EnumVal = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + EnumStr);
				if (EnumVal != INDEX_NONE)
				{
					EnP->GetUnderlyingProperty()->SetIntPropertyValue(EnP->ContainerPtrToValuePtr<void>(Container), EnumVal);
					SetProps.Add(PropName);
					return true;
				}
			}
		}
		return false;
	};

	UObject* EmitterObj = FindStatelessEmitterObject(System, EmitterName);
	if (!EmitterObj) EmitterObj = Handle->GetInstance().Emitter;
	if (EmitterObj)
	{
		EmitterObj->Modify();
		for (TFieldIterator<FProperty> PropIt(EmitterObj->GetClass()); PropIt; ++PropIt)
		{
			TryWriteOneField(*PropIt, EmitterObj);
		}
	}

	if (FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData())
	{
		UScriptStruct* DataStruct = FVersionedNiagaraEmitterData::StaticStruct();
		for (TFieldIterator<FProperty> PropIt(DataStruct); PropIt; ++PropIt)
		{
			TryWriteOneField(*PropIt, Data);
		}
	}

	if (SetProps.Num() == 0)
	{
		TArray<FString> Available = { TEXT("enabled") };
		if (EmitterObj)
		{
			for (TFieldIterator<FProperty> PropIt(EmitterObj->GetClass()); PropIt; ++PropIt)
				if (PropIt->PropertyFlags & CPF_Edit) Available.Add(PropIt->GetName());
		}
		if (Handle->GetEmitterData())
		{
			for (TFieldIterator<FProperty> PropIt(FVersionedNiagaraEmitterData::StaticStruct()); PropIt; ++PropIt)
				if (PropIt->PropertyFlags & CPF_Edit) Available.Add(PropIt->GetName());
		}
		FString Hint;
		if (ValueJsonRaw.IsValid())
		{
			for (const auto& KV : ValueJsonRaw->Values)
			{
				const FString K(*KV.Key);
				if (K.Equals(TEXT("SimTarget"), ESearchCase::IgnoreCase))
				{
					Hint = TEXT(" Hint: SimTarget is set via the dedicated `set_niagara_sim_target` action (CPU/GPU); on classic emitters it lives on EmitterState, not the emitter root.");
					break;
				}
				if (K.Equals(TEXT("bFixedBounds"), ESearchCase::IgnoreCase))
				{
					Hint = TEXT(" Hint: the field is `FixedBounds` (FBox struct), not `bFixedBounds`. Pass `properties: { \"FixedBounds\": \"(Min=(X=-100,Y=-100,Z=-100),Max=(X=100,Y=100,Z=100))\" }`.");
					break;
				}
				if (K.StartsWith(TEXT("EmitterState."), ESearchCase::IgnoreCase) || K.Equals(TEXT("EmitterState"), ESearchCase::IgnoreCase)
					|| K.Equals(TEXT("LoopCount"), ESearchCase::IgnoreCase) || K.Equals(TEXT("LoopBehavior"), ESearchCase::IgnoreCase)
					|| K.Equals(TEXT("LoopDuration"), ESearchCase::IgnoreCase) || K.Equals(TEXT("bInfiniteLoop"), ESearchCase::IgnoreCase)
					|| K.Equals(TEXT("MaxAllocation"), ESearchCase::IgnoreCase))
				{
					Hint = TEXT(" Hint: EmitterState fields (LoopCount, LoopBehavior, LoopDuration, bInfiniteLoop, MaxAllocation) live on the EmitterState MODULE, not on the emitter root. Set via `set_niagara_module_parameter(module_name='EmitterState', parameter_name='LoopCount', int_value=N)`. Stateless: also a module of the same name.");
					break;
				}
				if (K.Contains(TEXT(".")))
				{
					Hint = FString::Printf(TEXT(" Hint: dot-notation (`%s`) isn't supported here. Set the parent struct in one go via UE export string syntax."), *K);
					break;
				}
			}
		}
		SetError(FString::Printf(TEXT("No matching properties found.%s Available: %s"), *Hint, *FString::Join(Available, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	if (UNiagaraEmitter* ClassicEmitter = Handle->GetInstance().Emitter)
	{
		ClassicEmitter->PostEditChange();
	}
	if (EmitterObj && EmitterObj != Handle->GetInstance().Emitter)
	{
		EmitterObj->PostEditChange();
	}
	if (FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData())
	{
		const FGuid Version = Data->Version.VersionGuid;
		auto MarkDirty = [Version](UNiagaraScript* S) { if (S) S->MarkScriptAndSourceDesynchronized(TEXT("UECP set_emitter_properties"), Version); };
		MarkDirty(Data->SpawnScriptProps.Script);
		MarkDirty(Data->UpdateScriptProps.Script);
		MarkDirty(Data->EmitterSpawnScriptProps.Script);
		MarkDirty(Data->EmitterUpdateScriptProps.Script);
		MarkDirty(Data->GetGPUComputeScript());
		for (const FNiagaraEventScriptProperties& EvProps : Data->GetEventHandlers())
			MarkDirty(EvProps.Script);
	}
	System->RequestCompile(true );

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("set_properties"), FString::Join(SetProps, TEXT(", ")));
	Result->SetStringField(TEXT("emitter"), Handle->GetName().ToString());
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddRendererToEmitter(const FString& SystemPath, const FString& EmitterName, const FString& RendererType, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	UObject* EmitterObj = nullptr;
	if (FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName))
	{
		if (Handle->GetEmitterMode() == ENiagaraEmitterMode::Stateless)
		{
			if (UNiagaraStatelessEmitter* SE = Handle->GetStatelessEmitter())
			{
				EmitterObj = SE;
			}
		}
		else
		{
			if (UNiagaraEmitter* ClassicEmitter = Handle->GetInstance().Emitter)
			{
				EmitterObj = ClassicEmitter;
			}
		}
	}
	if (!EmitterObj)
	{
		EmitterObj = FindStatelessEmitterObject(System, EmitterName);
	}
	if (!EmitterObj)
	{
		TArray<UObject*> SubObjs;
		GetObjectsWithOuter(System->GetOutermost(), SubObjs, true);
		for (UObject* Obj : SubObjs)
		{
			if (Obj->GetClass()->GetName().Contains(TEXT("StatelessEmitter")))
			{
				FString ObjName = Obj->GetName();
				if (ObjName.Equals(EmitterName, ESearchCase::IgnoreCase) ||
				    ObjName.StartsWith(EmitterName + TEXT("_"), ESearchCase::IgnoreCase))
				{
					EmitterObj = Obj; break;
				}
			}
		}
	}
	if (!EmitterObj) { SetError(FString::Printf(TEXT("Could not find emitter object for '%s'."), *EmitterName), OutJsonString, OutError); return; }

	FString RendererTypeLower = RendererType.ToLower();
	FString RendererClassName;
	if (RendererTypeLower == TEXT("sprite")) RendererClassName = TEXT("NiagaraSpriteRendererProperties");
	else if (RendererTypeLower == TEXT("mesh")) RendererClassName = TEXT("NiagaraMeshRendererProperties");
	else if (RendererTypeLower == TEXT("ribbon") || RendererTypeLower == TEXT("beam")) RendererClassName = TEXT("NiagaraRibbonRendererProperties");
	else if (RendererTypeLower == TEXT("light")) RendererClassName = TEXT("NiagaraLightRendererProperties");
	else if (RendererTypeLower == TEXT("component")) RendererClassName = TEXT("NiagaraComponentRendererProperties");
	else if (RendererTypeLower == TEXT("decal")) RendererClassName = TEXT("NiagaraDecalRendererProperties");
	else if (RendererTypeLower == TEXT("volumetric") || RendererTypeLower == TEXT("volume")) RendererClassName = TEXT("NiagaraVolumetricRendererProperties");
	else RendererClassName = RendererType;

	UClass* RendererClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == RendererClassName && It->IsChildOf(UNiagaraRendererProperties::StaticClass()))
		{
			RendererClass = *It;
			break;
		}
	}
	if (!RendererClass) { SetError(FString::Printf(TEXT("Unknown renderer type: '%s'. Use: Sprite, Mesh, Ribbon (or Beam — alias for Ribbon), Light, Component, Decal."), *RendererType), OutJsonString, OutError); return; }

	if (Cast<UNiagaraStatelessEmitter>(EmitterObj) != nullptr)
	{
		const bool bRendererIsCPUOnly =
			RendererClassName == TEXT("NiagaraLightRendererProperties") ||
			RendererClassName == TEXT("NiagaraComponentRendererProperties") ||
			RendererClassName == TEXT("NiagaraDecalRendererProperties");
		if (bRendererIsCPUOnly)
		{
			SetError(FString::Printf(TEXT("'%s' renderers are CPU-sim only and cannot be added to stateless emitters (which run as GPUComputeSim). Adding it now would crash the editor on next asset open (NiagaraSystemRenderData assertion). Either: (a) split this into a separate emitter using a classic template (use add_emitter_to_system with a classic UNiagaraEmitter template path — list_niagara_templates(emitters_only=true) to find one), or (b) drop this renderer if the visual isn't load-bearing."), *RendererType), OutJsonString, OutError);
			return;
		}
	}

	EmitterObj->Modify();
	UNiagaraRendererProperties* NewRenderer = NewObject<UNiagaraRendererProperties>(EmitterObj, RendererClass, NAME_None, RF_Transactional);

	{
		auto SetMaterialByPath = [&](const TCHAR* PropName, const TCHAR* MaterialPath)
		{
			if (FObjectProperty* MatProp = CastField<FObjectProperty>(RendererClass->FindPropertyByName(FName(PropName))))
			{
				if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
				{
					MatProp->SetObjectPropertyValue(MatProp->ContainerPtrToValuePtr<void>(NewRenderer), Mat);
				}
			}
		};
		if (RendererClassName == TEXT("NiagaraSpriteRendererProperties"))
		{
			SetMaterialByPath(TEXT("Material"), TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial"));
		}
		else if (RendererClassName == TEXT("NiagaraRibbonRendererProperties"))
		{
			SetMaterialByPath(TEXT("Material"), TEXT("/Niagara/DefaultAssets/DefaultRIbbonMaterial.DefaultRIbbonMaterial"));
		}
		else if (RendererClassName == TEXT("NiagaraDecalRendererProperties"))
		{
			SetMaterialByPath(TEXT("Material"), TEXT("/Niagara/DefaultAssets/DefaultDecalMaterial.DefaultDecalMaterial"));
		}
		else if (RendererClassName == TEXT("NiagaraMeshRendererProperties"))
		{
			if (FArrayProperty* MeshesArray = CastField<FArrayProperty>(RendererClass->FindPropertyByName(FName(TEXT("Meshes")))))
			{
				FScriptArrayHelper Helper(MeshesArray, MeshesArray->ContainerPtrToValuePtr<void>(NewRenderer));
				if (Helper.Num() > 0)
				{
					if (FStructProperty* SP = CastField<FStructProperty>(MeshesArray->Inner))
					{
						if (FObjectProperty* MeshFieldProp = CastField<FObjectProperty>(SP->Struct->FindPropertyByName(FName(TEXT("Mesh")))))
						{
							if (UStaticMesh* DefaultMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Niagara/DefaultAssets/S_Gnomon.S_Gnomon")))
							{
								void* SlotPtr = Helper.GetRawPtr(0);
								MeshFieldProp->SetObjectPropertyValue(MeshFieldProp->ContainerPtrToValuePtr<void>(SlotPtr), DefaultMesh);
							}
						}
					}
				}
			}
		}
	}

	bool bAdded = false;

	int32 DiagClassicMissingAttrs = -1;
	int32 DiagClassicPreCount = -1, DiagClassicPostAddCount = -1, DiagClassicPostRebuildCount = -1;
	if (UNiagaraEmitter* ClassicEmitter = Cast<UNiagaraEmitter>(EmitterObj))
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
		FGuid VersionGuid = Handle ? Handle->GetInstance().Version : FGuid();
		if (FVersionedNiagaraEmitterData* PreData = ClassicEmitter->GetEmitterData(VersionGuid))
		{
			DiagClassicPreCount = PreData->GetRenderers().Num();
		}
		ClassicEmitter->AddRenderer(NewRenderer, VersionGuid);
		if (FVersionedNiagaraEmitterData* PostData = ClassicEmitter->GetEmitterData(VersionGuid))
		{
			DiagClassicPostAddCount = PostData->GetRenderers().Num();
		}

		if (FVersionedNiagaraEmitterData* Data = ClassicEmitter->GetEmitterData(VersionGuid))
		{
			if (UNiagaraScript* SpawnScript = Data->SpawnScriptProps.Script)
			{
				UNiagaraScriptSource* SpawnSrc = Cast<UNiagaraScriptSource>(SpawnScript->GetLatestSource());
				UNiagaraGraph* SpawnGraph = SpawnSrc ? SpawnSrc->NodeGraph : nullptr;
				UNiagaraNodeOutput* SpawnOutputNode = SpawnGraph
					? SpawnGraph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleSpawnScript, FGuid())
					: nullptr;
				if (SpawnOutputNode)
				{
					const TArray<FNiagaraVariable>& RequiredAttrs = NewRenderer->GetRequiredAttributes();
					TArray<FNiagaraVariable> MissingAttrs;
					TArray<FString> MissingDefaults;
					for (FNiagaraVariable Attr : RequiredAttrs)
					{
						FNiagaraVariable OriginalAttr = Attr;
						FString AttrName = Attr.GetName().ToString();
						if (AttrName.RemoveFromStart(TEXT("Particles.")))
						{
							Attr.SetName(*AttrName);
						}
						const bool bExists = SpawnScript->GetVMExecutableData().Attributes.ContainsByPredicate(
							[&Attr](const FNiagaraVariable& V) { return V.GetName() == Attr.GetName(); });
						if (!bExists)
						{
							MissingAttrs.Add(OriginalAttr);
							MissingDefaults.Add(FNiagaraConstants::GetAttributeDefaultValue(OriginalAttr));
						}
					}
					DiagClassicMissingAttrs = MissingAttrs.Num();
					if (MissingAttrs.Num() > 0)
					{
						FNiagaraStackGraphUtilities::AddParameterModuleToStack(
							MissingAttrs, *SpawnOutputNode, INDEX_NONE, MissingDefaults);
					}
				}
			}
			Data->RebuildRendererBindings(*ClassicEmitter);
		}

		bAdded = true;
	}
	int32 DiagPreCount = -1, DiagPostCount = -1, DiagPostRebuildCount = -1;
	bool bDiagSEMatchesHandle = false;
	if (false) {}

	else if (UNiagaraStatelessEmitter* StatelessEmitter = Cast<UNiagaraStatelessEmitter>(EmitterObj))
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
		DiagPreCount = StatelessEmitter->GetRenderers().Num();
		bDiagSEMatchesHandle = (Handle && Handle->GetStatelessEmitter() == StatelessEmitter);
		StatelessEmitter->AddRenderer(NewRenderer, FGuid());
		DiagPostCount = StatelessEmitter->GetRenderers().Num();
		bAdded = true;
	}

	if (!bAdded) { SetError(TEXT("Could not find renderer array on emitter. The emitter may use a different storage layout."), OutJsonString, OutError); return; }

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	if (UNiagaraStatelessEmitter* SEPostRebuild = Cast<UNiagaraStatelessEmitter>(EmitterObj))
	{
		DiagPostRebuildCount = SEPostRebuild->GetRenderers().Num();
	}
	if (UNiagaraEmitter* CEPostRebuild = Cast<UNiagaraEmitter>(EmitterObj))
	{
		FNiagaraEmitterHandle* HandleAfter = FindEmitterHandle(System, EmitterName);
		FGuid VersionAfter = HandleAfter ? HandleAfter->GetInstance().Version : FGuid();
		if (FVersionedNiagaraEmitterData* PostRebuildData = CEPostRebuild->GetEmitterData(VersionAfter))
		{
			DiagClassicPostRebuildCount = PostRebuildData->GetRenderers().Num();
		}
	}

	TArray<UNiagaraRendererProperties*> AllRenderers = CollectRenderers(System, EmitterName);
	int32 NewRendererIdx = AllRenderers.IndexOfByKey(NewRenderer);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("renderer_class"), RendererClass->GetName());
	Result->SetNumberField(TEXT("renderer_index"), NewRendererIdx);
	Result->SetStringField(TEXT("note"), TEXT("Use set_niagara_renderer_material to assign a material, set_niagara_renderer_property to configure."));
	CompileAndReport(System, Result);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleSetNiagaraSystemProperties(const FString& SystemPath, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	TSharedPtr<FJsonObject> PropsJson = ValueJson;
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (ValueJson->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
		PropsJson = *PropsObj;

	System->Modify();
	TArray<FString> SetProps;

	for (TFieldIterator<FProperty> PropIt(System->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!(Prop->PropertyFlags & CPF_Edit)) continue;
		FString PropName = Prop->GetName();
		double NumVal = 0.0; bool BoolVal = false;

		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			if (PropsJson->TryGetBoolField(PropName, BoolVal)) { BP->SetPropertyValue_InContainer(System, BoolVal); SetProps.Add(PropName); }
		}
		else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		{
			if (PropsJson->TryGetNumberField(PropName, NumVal)) { IP->SetPropertyValue_InContainer(System, (int32)NumVal); SetProps.Add(PropName); }
		}
		else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			if (PropsJson->TryGetNumberField(PropName, NumVal)) { FP->SetPropertyValue_InContainer(System, (float)NumVal); SetProps.Add(PropName); }
		}
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			if (PropsJson->TryGetNumberField(PropName, NumVal)) { DP->SetPropertyValue_InContainer(System, NumVal); SetProps.Add(PropName); }
		}
	}

	if (SetProps.Num() == 0)
	{
		TArray<FString> Available;
		for (TFieldIterator<FProperty> PropIt(System->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->PropertyFlags & CPF_Edit)) continue;
			if (CastField<FBoolProperty>(Prop) || CastField<FIntProperty>(Prop) ||
			    CastField<FFloatProperty>(Prop) || CastField<FDoubleProperty>(Prop))
				Available.Add(Prop->GetName());
		}
		SetError(FString::Printf(TEXT("No matching system properties found. Common: WarmupTime, WarmupTickCount, bAutoDeactivate. All available: %s"),
			*FString::Join(Available, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("set_properties"), FString::Join(SetProps, TEXT(", ")));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleDuplicateEmitterInSystem(const FString& SystemPath, const FString& SourceEmitterName, const FString& NewEmitterName, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* SourceHandle = FindEmitterHandle(System, SourceEmitterName);
	if (!SourceHandle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Source emitter '%s' not found. Available: %s"), *SourceEmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	FString TargetName = NewEmitterName.IsEmpty() ? SourceEmitterName + TEXT("_Copy") : NewEmitterName;

	System->Modify();

	FNiagaraEmitterHandle NewHandle = System->DuplicateEmitterHandle(*SourceHandle, FName(*TargetName));
	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("source_emitter"), SourceEmitterName);
	Result->SetStringField(TEXT("new_emitter"), NewHandle.GetName().ToString());
	Result->SetBoolField(TEXT("stateless_duplicated"), false);
	Result->SetNumberField(TEXT("total_emitters"), System->GetEmitterHandles().Num());
	BuildSuccessJson(Result, OutJsonString);
}

void HandleSetEmitterScalability(const FString& SystemPath, const FString& EmitterName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	System->Modify();
	TArray<FString> SetProps;

	auto TrySetOnObject = [&ValueJson, &SetProps](UObject* Obj)
	{
		for (TFieldIterator<FProperty> PropIt(Obj->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->PropertyFlags & CPF_Edit)) continue;
			FString PropName = Prop->GetName();
			double NumVal = 0.0; bool BoolVal = false;
			if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
			{
				if (ValueJson->TryGetBoolField(PropName, BoolVal)) { BP->SetPropertyValue_InContainer(Obj, BoolVal); SetProps.Add(PropName); }
			}
			else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
			{
				if (ValueJson->TryGetNumberField(PropName, NumVal)) { IP->SetPropertyValue_InContainer(Obj, (int32)NumVal); SetProps.Add(PropName); }
			}
			else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
			{
				if (ValueJson->TryGetNumberField(PropName, NumVal)) { FP->SetPropertyValue_InContainer(Obj, (float)NumVal); SetProps.Add(PropName); }
			}
			else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
			{
				if (ValueJson->TryGetNumberField(PropName, NumVal)) { DP->SetPropertyValue_InContainer(Obj, NumVal); SetProps.Add(PropName); }
			}
		}
	};

	TrySetOnObject(System);

	if (!EmitterName.IsEmpty())
	{
		UObject* EmitterObj = FindStatelessEmitterObject(System, EmitterName);
		if (EmitterObj)
		{
			EmitterObj->Modify();
			TrySetOnObject(EmitterObj);
		}
	}

	if (SetProps.Num() == 0)
	{
		SetError(TEXT("No matching properties found. Common system properties: bAutoDeactivate. "
			"Pass property names as JSON keys, e.g. {\"bAutoDeactivate\": true, \"WarmupTime\": 1.0}. "
			"For emitter bounds/LOD: check get_niagara_detailed_summary for available emitter properties."),
			OutJsonString, OutError);
		return;
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("set_properties"), FString::Join(SetProps, TEXT(", ")));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleRemoveNiagaraRenderer(const FString& SystemPath, const FString& EmitterName, int32 RendererIndex, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
	if (!Renderers.IsValidIndex(RendererIndex))
	{
		SetError(FString::Printf(TEXT("Renderer index %d out of range. Found %d renderer(s) for emitter '%s'."), RendererIndex, Renderers.Num(), *EmitterName), OutJsonString, OutError);
		return;
	}

	if (Renderers.Num() <= 1)
	{
		SetError(FString::Printf(
			TEXT("Refusing to remove the last renderer on emitter '%s' — the asset would render nothing. ")
			TEXT("If you're swapping renderer types: call add_renderer_to_emitter(renderer_type='<new>') ")
			TEXT("FIRST, then remove the old one by index. If you genuinely want zero renderers, that's ")
			TEXT("not a state Niagara supports — pick or add a renderer instead."),
			*EmitterName),
			OutJsonString, OutError);
		return;
	}

	UNiagaraRendererProperties* RendererToRemove = Renderers[RendererIndex];
	FString RendererClass = RendererToRemove->GetClass()->GetName();

	bool bRemoved = false;

	if (UNiagaraEmitter* ClassicEmitter = Cast<UNiagaraEmitter>(RendererToRemove->GetOuter()))
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
		FGuid VersionGuid = Handle ? Handle->GetInstance().Version : FGuid();
		ClassicEmitter->Modify();
		ClassicEmitter->RemoveRenderer(RendererToRemove, VersionGuid);
		bRemoved = true;
	}
	else if (UNiagaraStatelessEmitter* StatelessEmitter = Cast<UNiagaraStatelessEmitter>(RendererToRemove->GetOuter()))
	{
		StatelessEmitter->RemoveRenderer(RendererToRemove, FGuid());
		bRemoved = true;
	}

	if (!bRemoved)
	{
		SetError(FString::Printf(TEXT("Could not remove renderer at index %d from emitter '%s'. Storage layout may differ."), RendererIndex, *EmitterName), OutJsonString, OutError);
		return;
	}

	RendererToRemove->MarkAsGarbage();
	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_renderer_class"), RendererClass);
	Result->SetNumberField(TEXT("removed_index"), RendererIndex);
	Result->SetNumberField(TEXT("remaining_renderers"), Renderers.Num() - 1);
	CompileAndReport(System, Result);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleGetEmitterModules(const FString& SystemPath, const FString& EmitterName, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	TArray<UObject*> StatelessMods = CollectStatelessModules(System, EmitterName);
	bool bIsStateless = StatelessMods.Num() > 0;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("emitter_name"), Handle->GetName().ToString());
	Result->SetStringField(TEXT("emitter_type"), bIsStateless ? TEXT("stateless") : TEXT("classic"));

	TArray<TSharedPtr<FJsonValue>> ModulesArray;

	if (bIsStateless)
	{
		for (UObject* Mod : StatelessMods)
		{
			TSharedPtr<FJsonObject> ModObj = MakeShareable(new FJsonObject);
			FString ClassName = Mod->GetClass()->GetName();
			FString ShortClass = ClassName.StartsWith(TEXT("NiagaraStatelessModule_")) ? ClassName.Mid(23) : ClassName;
			ModObj->SetStringField(TEXT("module"), ShortClass);
			ModObj->SetStringField(TEXT("full_class"), ClassName);

			TArray<TSharedPtr<FJsonValue>> PropNames;
			for (TFieldIterator<FProperty> PropIt(Mod->GetClass()); PropIt; ++PropIt)
			{
				if (PropIt->PropertyFlags & CPF_Edit)
					PropNames.Add(MakeShareable(new FJsonValueString(PropIt->GetName())));
			}
			ModObj->SetArrayField(TEXT("settable_properties"), PropNames);
			ModulesArray.Add(MakeShareable(new FJsonValueObject(ModObj)));
		}

		TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
		TArray<TSharedPtr<FJsonValue>> RenderersArray;
		for (int32 RI = 0; RI < Renderers.Num(); RI++)
		{
			TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
			R->SetNumberField(TEXT("index"), RI);
			R->SetStringField(TEXT("class"), Renderers[RI]->GetClass()->GetName());
			RenderersArray.Add(MakeShareable(new FJsonValueObject(R)));
		}
		Result->SetArrayField(TEXT("renderers"), RenderersArray);
		Result->SetStringField(TEXT("note"), TEXT("Stateless emitter. Edit modules via set_niagara_module_parameter(parameter_name=<property>). Use get_niagara_detailed_summary for current values."));
	}
	else
	{
		FVersionedNiagaraEmitterData* Data = GetBestEmitterData(Handle);
		if (Data)
		{
			TMap<FGuid, FName> UsageIdToEventName;
			const TArray<FNiagaraEventScriptProperties>& Handlers = Data->GetEventHandlers();
			for (const FNiagaraEventScriptProperties& H : Handlers)
			{
				if (H.Script) UsageIdToEventName.Add(H.Script->GetUsageId(), H.SourceEventName);
			}

			UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
			if (!Source && Data->SpawnScriptProps.Script)
				Source = Cast<UNiagaraScriptSource>(Data->SpawnScriptProps.Script->GetLatestSource());

			if (Source && Source->NodeGraph)
			{
				UNiagaraGraph* EGraph = Source->NodeGraph;
				TArray<UNiagaraNodeFunctionCall*> FuncNodes;
				EGraph->GetNodesOfClass<UNiagaraNodeFunctionCall>(FuncNodes);

				auto FindOwningOutput = [](UNiagaraNodeFunctionCall* Func) -> UNiagaraNodeOutput*
				{
					if (!Func) return nullptr;
					UEdGraphPin* MapOut = FindNiagaraParameterMapPin(Func, EGPD_Output);
					int32 Hops = 0;
					while (MapOut && MapOut->LinkedTo.Num() > 0 && Hops < 256)
					{
						UEdGraphNode* Next = MapOut->LinkedTo[0]->GetOwningNode();
						if (UNiagaraNodeOutput* Out = Cast<UNiagaraNodeOutput>(Next)) return Out;
						UNiagaraNode* NN = Cast<UNiagaraNode>(Next);
						if (!NN) return nullptr;
						MapOut = FindNiagaraParameterMapPin(NN, EGPD_Output);
						++Hops;
					}
					return nullptr;
				};

				auto UsageToSection = [](ENiagaraScriptUsage Usage) -> FString
				{
					switch (Usage)
					{
						case ENiagaraScriptUsage::ParticleSpawnScript:        return TEXT("particle_spawn");
						case ENiagaraScriptUsage::ParticleUpdateScript:       return TEXT("particle_update");
						case ENiagaraScriptUsage::EmitterSpawnScript:         return TEXT("emitter_spawn");
						case ENiagaraScriptUsage::EmitterUpdateScript:        return TEXT("emitter_update");
						case ENiagaraScriptUsage::ParticleEventScript:        return TEXT("event_handler");
						case ENiagaraScriptUsage::ParticleSimulationStageScript: return TEXT("simulation_stage");
						default: return TEXT("");
					}
				};

				for (UNiagaraNodeFunctionCall* Node : FuncNodes)
				{
					UNiagaraNodeOutput* OwningOutput = FindOwningOutput(Node);
					FString Section = OwningOutput ? UsageToSection(OwningOutput->GetUsage()) : FString();
					if (Section.IsEmpty()) continue;

					TSharedPtr<FJsonObject> ModObj = MakeShareable(new FJsonObject);
					ModObj->SetStringField(TEXT("module"), Node->GetFunctionName());
					ModObj->SetStringField(TEXT("section"), Section);
					if (Node->FunctionScript)
						ModObj->SetStringField(TEXT("script_path"), Node->FunctionScript->GetPathName());

					if (OwningOutput->GetUsage() == ENiagaraScriptUsage::ParticleEventScript)
					{
						const FGuid HandlerUsageId = OwningOutput->GetUsageId();
						if (HandlerUsageId.IsValid())
							ModObj->SetStringField(TEXT("event_handler_usage_id"), HandlerUsageId.ToString());
						if (const FName* EvName = UsageIdToEventName.Find(HandlerUsageId))
							ModObj->SetStringField(TEXT("event_name"), EvName->ToString());
					}

					TArray<TSharedPtr<FJsonValue>> InputsArr;
					for (const FNiagaraVariable& Input : CollectModuleInputs(Node))
					{
						TSharedPtr<FJsonObject> InpObj = MakeShareable(new FJsonObject);
						FString InputName = Input.GetName().ToString();
						if (InputName.StartsWith(TEXT("Module."))) InputName = InputName.Mid(7);
						InpObj->SetStringField(TEXT("name"), InputName);
						InpObj->SetStringField(TEXT("type"), Input.GetType().GetName());

						const FModuleInputState St = GetModuleInputState(Node, Input);
						InpObj->SetStringField(TEXT("source"), St.Source);
						if (!St.CurrentValue.IsEmpty())
							InpObj->SetStringField(TEXT("current_value"), St.CurrentValue);
						if (!St.LinkedNodeTitle.IsEmpty())
							InpObj->SetStringField(TEXT("linked_node_title"), St.LinkedNodeTitle);

						const FModuleInputMetadata Md = GetModuleInputMetadata(Node, Input);
						if (!Md.Description.IsEmpty())
							InpObj->SetStringField(TEXT("description"), Md.Description);
						if (!Md.UIMin.IsEmpty())
							InpObj->SetStringField(TEXT("ui_min"), Md.UIMin);
						if (!Md.UIMax.IsEmpty())
							InpObj->SetStringField(TEXT("ui_max"), Md.UIMax);
						if (Md.EnumValues.Num() > 0)
						{
							TArray<TSharedPtr<FJsonValue>> EnumsJson;
							for (const FString& V : Md.EnumValues)
								EnumsJson.Add(MakeShareable(new FJsonValueString(V)));
							InpObj->SetArrayField(TEXT("enum_values"), EnumsJson);
						}

						InputsArr.Add(MakeShareable(new FJsonValueObject(InpObj)));
					}
					ModObj->SetArrayField(TEXT("inputs"), InputsArr);

					TArray<TSharedPtr<FJsonValue>> SwitchesArr;
					for (const FModuleStaticSwitch& Sw : CollectModuleStaticSwitches(Node))
					{
						TSharedPtr<FJsonObject> SwObj = MakeShareable(new FJsonObject);
						SwObj->SetStringField(TEXT("name"), Sw.Name.ToString());
						SwObj->SetStringField(TEXT("type"), Sw.Type.GetName());
						SwObj->SetStringField(TEXT("current_value"), Sw.CurrentValue);
						if (!Sw.CurrentValueDisplay.IsEmpty() && Sw.CurrentValueDisplay != Sw.CurrentValue)
							SwObj->SetStringField(TEXT("current_value_display"), Sw.CurrentValueDisplay);
						SwObj->SetBoolField  (TEXT("static_switch"), true);
						if (Sw.EnumValues.Num() > 0)
						{
							TArray<TSharedPtr<FJsonValue>> EnumsJson;
							const bool bHaveDisplay = Sw.EnumDisplayNames.Num() == Sw.EnumValues.Num();
							for (int32 i = 0; i < Sw.EnumValues.Num(); i++)
							{
								const FString& Out = bHaveDisplay && !Sw.EnumDisplayNames[i].IsEmpty() ? Sw.EnumDisplayNames[i] : Sw.EnumValues[i];
								EnumsJson.Add(MakeShareable(new FJsonValueString(Out)));
							}
							SwObj->SetArrayField(TEXT("enum_values"), EnumsJson);
						}
						SwitchesArr.Add(MakeShareable(new FJsonValueObject(SwObj)));
					}
					if (SwitchesArr.Num() > 0)
						ModObj->SetArrayField(TEXT("static_switches"), SwitchesArr);

					ModulesArray.Add(MakeShareable(new FJsonValueObject(ModObj)));
				}
			}

			TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
			TArray<TSharedPtr<FJsonValue>> RenderersArray;
			for (int32 RI = 0; RI < Renderers.Num(); RI++)
			{
				TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
				R->SetNumberField(TEXT("index"), RI);
				R->SetStringField(TEXT("class"), Renderers[RI]->GetClass()->GetName());
				RenderersArray.Add(MakeShareable(new FJsonValueObject(R)));
			}
			Result->SetArrayField(TEXT("renderers"), RenderersArray);

			TArray<TSharedPtr<FJsonValue>> EventHandlersArray;
			for (const FNiagaraEventScriptProperties& H : Handlers)
			{
				TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
				E->SetStringField(TEXT("event_name"), H.SourceEventName.ToString());
				E->SetStringField(TEXT("source_emitter_id"), H.SourceEmitterID.ToString());
				if (H.SourceEmitterID.IsValid())
				{
					for (const FNiagaraEmitterHandle& Other : System->GetEmitterHandles())
					{
						if (Other.GetId() == H.SourceEmitterID)
						{
							E->SetStringField(TEXT("source_emitter"), Other.GetName().ToString());
							break;
						}
					}
				}
				const TCHAR* ModeStr = TEXT("SpawnedParticles");
				switch (H.ExecutionMode)
				{
					case EScriptExecutionMode::EveryParticle:    ModeStr = TEXT("EveryParticle");    break;
					case EScriptExecutionMode::SpawnedParticles: ModeStr = TEXT("SpawnedParticles"); break;
					case EScriptExecutionMode::SingleParticle:   ModeStr = TEXT("SingleParticle");   break;
				}
				E->SetStringField(TEXT("execution_mode"), ModeStr);
				E->SetNumberField(TEXT("spawn_number"), H.SpawnNumber);
				E->SetNumberField(TEXT("max_events_per_frame"), H.MaxEventsPerFrame);
				if (H.Script) E->SetStringField(TEXT("usage_id"), H.Script->GetUsageId().ToString());
				EventHandlersArray.Add(MakeShareable(new FJsonValueObject(E)));
			}
			Result->SetArrayField(TEXT("event_handlers"), EventHandlersArray);
		}
		Result->SetStringField(TEXT("note"),
			TEXT("Classic emitter. Each module's `inputs[]` lists the names you pass as parameter_name to "
				 "`set_niagara_module_parameter` and `bind_niagara_module_input`. `script_section` defaults "
				 "to `particle_spawn`; pass it explicitly when the module lives elsewhere. "
				 "Event handlers (if any) are listed under `event_handlers[]`; modules inside one report "
				 "`section: \"event_handler\"` plus their `event_name` so add_niagara_module can target them."));
	}

	Result->SetArrayField(TEXT("modules"), ModulesArray);
	Result->SetNumberField(TEXT("module_count"), ModulesArray.Num());
	BuildSuccessJson(Result, OutJsonString);
}

void HandleSetEmitterSpawnRate(const FString& SystemPath, const FString& EmitterName, float SpawnRate, int32 BurstCount, float BurstTime, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	UNiagaraStatelessEmitter* StatelessEmitter = FindStatelessEmitterFromHandle(System, EmitterName);
	UObject* EmitterObj = StatelessEmitter;
	if (!EmitterObj)
	{
		EmitterObj = FindStatelessEmitterObject(System, EmitterName);
	}
	if (!EmitterObj)
	{
		SetError(FString::Printf(TEXT("Could not find stateless emitter object for '%s'. "
			"This action is for stateless emitters (FountainLightweight, RadialBurst, etc.). "
			"For classic emitters, use set_niagara_module_parameter on spawn script rate modules."), *EmitterName), OutJsonString, OutError);
		return;
	}

	FArrayProperty* SpawnInfosProp = FindFProperty<FArrayProperty>(EmitterObj->GetClass(), TEXT("SpawnInfos"));
	if (!SpawnInfosProp)
	{
		TArray<FString> Available;
		for (TFieldIterator<FProperty> PropIt(EmitterObj->GetClass()); PropIt; ++PropIt)
			Available.Add(FString::Printf(TEXT("%s(%s)"), *PropIt->GetName(), *PropIt->GetClass()->GetName()));
		SetError(FString::Printf(TEXT("SpawnInfos property not found on '%s'. Available: %s"),
			*EmitterObj->GetClass()->GetName(), *FString::Join(Available, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	FStructProperty* ElemProp = CastField<FStructProperty>(SpawnInfosProp->Inner);
	if (!ElemProp) { SetError(TEXT("SpawnInfos inner type is not a struct."), OutJsonString, OutError); return; }
	UScriptStruct* InfoStruct = ElemProp->Struct;

	FScriptArrayHelper SpawnHelper(SpawnInfosProp, SpawnInfosProp->ContainerPtrToValuePtr<void>(EmitterObj));
	if (SpawnHelper.Num() == 0) SpawnHelper.AddValue();

	TArray<FString> Changed;

	void* ElemPtr = SpawnHelper.GetRawPtr(0);

	if (SpawnRate >= 0.0f)
	{
		FStructProperty* RateSP = CastField<FStructProperty>(InfoStruct->FindPropertyByName(FName(TEXT("Rate"))));
		if (RateSP)
		{
			FNiagaraDistributionRangeFloat* RateDist = RateSP->ContainerPtrToValuePtr<FNiagaraDistributionRangeFloat>(ElemPtr);
			if (RateDist)
			{
				RateDist->InitConstant(SpawnRate);
				Changed.Add(TEXT("Rate (InitConstant)"));
			}
		}
	}

	if (BurstCount >= 0)
	{
		FStructProperty* AmtSP = CastField<FStructProperty>(InfoStruct->FindPropertyByName(FName(TEXT("Amount"))));
		if (AmtSP)
		{
			void* AmtPtr = AmtSP->ContainerPtrToValuePtr<void>(ElemPtr);
			if (FArrayProperty* CRP = FindFProperty<FArrayProperty>(AmtSP->Struct, TEXT("ChannelConstantsAndRanges")))
			{
				FScriptArrayHelper CRH(CRP, CRP->ContainerPtrToValuePtr<void>(AmtPtr));
				if (CRH.Num() < 1) CRH.Resize(1);
				if (FIntProperty* IP = CastField<FIntProperty>(CRP->Inner))
					IP->SetPropertyValue(CRH.GetRawPtr(0), BurstCount);
				else if (FFloatProperty* FP = CastField<FFloatProperty>(CRP->Inner))
					FP->SetPropertyValue(CRH.GetRawPtr(0), (float)BurstCount);
				Changed.Add(TEXT("Amount.ChannelConstantsAndRanges[0]"));
			}
		}
		if (BurstTime >= 0.0f)
		{
			if (FProperty* TimeProp = InfoStruct->FindPropertyByName(FName(TEXT("SpawnTime"))))
			{
				if (FFloatProperty* FP = CastField<FFloatProperty>(TimeProp))
				{
					FP->SetPropertyValue_InContainer(ElemPtr, BurstTime);
					Changed.Add(TEXT("SpawnTime"));
				}
			}
		}
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("emitter"), EmitterName);
	if (SpawnRate >= 0.0f) R->SetNumberField(TEXT("spawn_rate"), SpawnRate);
	if (BurstCount >= 0) R->SetNumberField(TEXT("burst_count"), BurstCount);
	if (Changed.Num() > 0)
		R->SetStringField(TEXT("set_fields"), FString::Join(Changed, TEXT(", ")));
	else
		R->SetStringField(TEXT("warning"), TEXT("SpawnInfos structure may differ — verify spawn rate in editor"));

	R->SetStringField(TEXT("_debug_emitter_class"), EmitterObj->GetClass()->GetName());
	R->SetStringField(TEXT("_debug_emitter_path"), EmitterObj->GetPathName());
	R->SetBoolField(TEXT("_debug_found_via_handle"), StatelessEmitter != nullptr);
	for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles())
	{
		if (!H.GetStatelessEmitter()) continue;
		if (H.GetStatelessEmitter() == EmitterObj)
		{
			R->SetStringField(TEXT("_debug_handle_name"), H.GetName().ToString());
			R->SetStringField(TEXT("_debug_handle_se_path"), H.GetStatelessEmitter()->GetPathName());
			R->SetBoolField(TEXT("_debug_same_object"), true);
			break;
		}
	}
	CompileAndReport(System, R);
	BuildSuccessJson(R, OutJsonString);
}

void HandleSetNiagaraSimTarget(const FString& SystemPath, const FString& EmitterName, const FString& SimTarget, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	TArray<UObject*> StatelessMods = CollectStatelessModules(System, EmitterName);
	if (StatelessMods.Num() > 0)
	{
		SetError(TEXT("Stateless emitters (FountainLightweight, RadialBurst, etc.) are always CPU — they do not support GPU sim target. "
			"Use list_niagara_templates to find a classic (non-Lightweight) template for GPU simulation."), OutJsonString, OutError);
		return;
	}

	FVersionedNiagaraEmitterData* Data = GetBestEmitterData(Handle);
	if (!Data) { SetError(TEXT("Could not get emitter data. The emitter may be stateless or not fully loaded."), OutJsonString, OutError); return; }

	bool bGPU = SimTarget.ToLower().Contains(TEXT("gpu"));
	ENiagaraSimTarget NewTarget = bGPU ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim;

	System->Modify();
	Data->SimTarget = NewTarget;
	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("emitter"), Handle->GetName().ToString());
	Resp->SetStringField(TEXT("sim_target"), bGPU ? TEXT("GPUComputeSim") : TEXT("CPUSim"));
	Resp->SetStringField(TEXT("note"), bGPU
		? TEXT("GPU sim enabled. Compile emitter shaders in UE editor before use.")
		: TEXT("CPU sim enabled."));
	BuildSuccessJson(Resp, OutJsonString);
}

void HandleRenameEmitterInSystem(const FString& SystemPath, const FString& OldEmitterName, const FString& NewEmitterName, FString& OutJsonString, FString& OutError)
{

	if (NewEmitterName.IsEmpty()) { SetError(TEXT("'new_emitter_name' is required."), OutJsonString, OutError); return; }

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, OldEmitterName);
	if (!Handle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *OldEmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	FString OldName = Handle->GetName().ToString();
	System->Modify();
	Handle->SetName(FName(*NewEmitterName), *System);

	UObject* StatelessObj = FindStatelessEmitterObject(System, OldName);
	if (StatelessObj)
	{
		StatelessObj->Rename(*NewEmitterName, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("old_name"), OldName);
	Result->SetStringField(TEXT("new_name"), Handle->GetName().ToString());
	BuildSuccessJson(Result, OutJsonString);
}

static UEdGraphPin* FindNiagaraParameterMapPin(UNiagaraNode* Node, EEdGraphPinDirection Direction)
{
	if (!Node) return nullptr;
	const UEdGraphSchema_Niagara* Schema = Cast<UEdGraphSchema_Niagara>(Node->GetSchema());
	if (!Schema) return nullptr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != Direction) continue;
		if (UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			return Pin;
		}
	}
	return nullptr;
}

static UNiagaraNodeOutput* CreateEventScriptOutputInGraph(UNiagaraGraph& Graph, FGuid UsageId)
{
	Graph.Modify();

	UNiagaraNodeOutput* OutputNode = Graph.FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleEventScript, UsageId);
	UEdGraphPin* OutputNodeInputPin = OutputNode ? FindNiagaraParameterMapPin(OutputNode, EGPD_Input) : nullptr;
	if (OutputNode && !OutputNodeInputPin)
	{
		Graph.RemoveNode(OutputNode);
		OutputNode = nullptr;
	}

	if (!OutputNode)
	{
		FGraphNodeCreator<UNiagaraNodeOutput> OutputCreator(Graph);
		OutputNode = OutputCreator.CreateNode();
		OutputNode->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
		OutputNode->SetUsageId(UsageId);
		OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Out")));
		OutputCreator.Finalize();
		OutputNodeInputPin = FindNiagaraParameterMapPin(OutputNode, EGPD_Input);
	}
	else
	{
		OutputNode->Modify();
	}

	FGraphNodeCreator<UNiagaraNodeInput> InputCreator(Graph);
	UNiagaraNodeInput* InputNode = InputCreator.CreateNode();
	InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
	InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
	InputCreator.Finalize();

	UEdGraphPin* InputNodeOutputPin = FindNiagaraParameterMapPin(InputNode, EGPD_Output);
	if (OutputNodeInputPin && InputNodeOutputPin)
	{
		OutputNodeInputPin->BreakAllPinLinks();
		OutputNodeInputPin->MakeLinkTo(InputNodeOutputPin);
	}

	return OutputNode;
}

void HandleAddNiagaraEventHandler(const FString& SystemPath, const FString& EmitterName, const FString& SourceEmitterName, const FString& EventName, const FString& , FString& OutJsonString, FString& OutError)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
		SetError(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	FVersionedNiagaraEmitter VI = Handle->GetInstance();
	UNiagaraEmitter* Emitter = VI.Emitter;
	if (!Emitter)
	{
		SetError(TEXT("Event handlers are not supported on stateless emitters (FountainLightweight, RadialBurst, etc.). Use a classic emitter template."),
			OutJsonString, OutError);
		return;
	}

	FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
	if (!Data) { SetError(TEXT("Could not get emitter data for event handler setup."), OutJsonString, OutError); return; }

	UNiagaraScriptSourceBase* EmitterSourceBase = Data->GraphSource;
	UNiagaraScriptSource* EmitterSource = Cast<UNiagaraScriptSource>(EmitterSourceBase);
	if (!EmitterSource || !EmitterSource->NodeGraph)
	{
		SetError(TEXT("Emitter has no editable script graph. The asset may have been imported without editor data."), OutJsonString, OutError);
		return;
	}
	UNiagaraGraph* EmitterGraph = EmitterSource->NodeGraph;

	FGuid SourceEmitterID;
	FString ResolvedSourceEmitterName;
	if (!SourceEmitterName.IsEmpty())
	{
		FNiagaraEmitterHandle* SourceHandle = FindEmitterHandle(System, SourceEmitterName);
		if (!SourceHandle)
		{
			TArray<FString> Names;
			for (const FNiagaraEmitterHandle& H : System->GetEmitterHandles()) Names.Add(H.GetName().ToString());
			SetError(FString::Printf(TEXT("Source emitter '%s' not found. Available: %s"), *SourceEmitterName, *FString::Join(Names, TEXT(", "))), OutJsonString, OutError);
			return;
		}
		SourceEmitterID = SourceHandle->GetId();
		ResolvedSourceEmitterName = SourceHandle->GetName().ToString();
	}

	for (const FNiagaraEventScriptProperties& Existing : Data->GetEventHandlers())
	{
		if (Existing.SourceEmitterID == SourceEmitterID && Existing.SourceEventName == FName(*EventName))
		{
			TSharedPtr<FJsonObject> ExistingResult = MakeShareable(new FJsonObject);
			ExistingResult->SetBoolField(TEXT("success"), true);
			ExistingResult->SetBoolField(TEXT("already_exists"), true);
			ExistingResult->SetStringField(TEXT("emitter"), Handle->GetName().ToString());
			ExistingResult->SetStringField(TEXT("event_name"), EventName);
			if (!ResolvedSourceEmitterName.IsEmpty()) ExistingResult->SetStringField(TEXT("source_emitter"), ResolvedSourceEmitterName);
			ExistingResult->SetStringField(TEXT("note"), TEXT("Event handler with this source+event already exists. Reusing — no duplicate created."));
			BuildSuccessJson(ExistingResult, OutJsonString);
			return;
		}
	}

	System->Modify();
	Emitter->Modify();

	UNiagaraScript* EventScript = NewObject<UNiagaraScript>(
		Emitter,
		MakeUniqueObjectName(Emitter, UNiagaraScript::StaticClass(), TEXT("EventScript")),
		RF_Transactional);
	EventScript->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
	const FGuid EventUsageId = FGuid::NewGuid();
	EventScript->SetUsageId(EventUsageId);
	EventScript->SetLatestSource(EmitterSourceBase);

	CreateEventScriptOutputInGraph(*EmitterGraph, EventUsageId);

	FNiagaraEventScriptProperties Handler;
	Handler.Script = EventScript;
	Handler.SourceEmitterID = SourceEmitterID;
	Handler.SourceEventName = FName(*EventName);
	Handler.ExecutionMode = EScriptExecutionMode::SpawnedParticles;
	Handler.SpawnNumber = 1;
	Handler.MaxEventsPerFrame = 32;

	Emitter->AddEventHandler(Handler, VI.Version);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("emitter"), Handle->GetName().ToString());
	Result->SetStringField(TEXT("event_name"), EventName);
	if (!ResolvedSourceEmitterName.IsEmpty()) Result->SetStringField(TEXT("source_emitter"), ResolvedSourceEmitterName);
	Result->SetStringField(TEXT("usage_id"), EventUsageId.ToString());
	Result->SetStringField(TEXT("script"), EventScript->GetPathName());
	Result->SetStringField(TEXT("note"),
		TEXT("Event handler created with empty graph. Add modules via add_niagara_module(emitter_name, script_section='event_handler', module_path=...). "
			"For a spawn-on-event handler, you also need a generator module (e.g. GenerateLocationEvent) on the SOURCE emitter that emits the event."));
	CompileAndReport(System, Result);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleSetNiagaraDataInterface(const FString& SystemPath, const FString& EmitterName, const FString& DataInterfaceName, const FString& PropertyName, const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(System->GetOutermost(), SubObjects, true);

	auto StripDIPrefix = [](const FString& In) -> FString
	{
		static const FString Prefix = TEXT("NiagaraDataInterface");
		return In.StartsWith(Prefix) ? In.RightChop(Prefix.Len()) : In;
	};
	const FString DINameLower = DataInterfaceName.ToLower();

	TArray<UObject*> AllDIs;
	for (UObject* Obj : SubObjects)
	{
		if (!Obj || !Obj->IsA(UNiagaraDataInterface::StaticClass())) continue;
		if (!EmitterName.IsEmpty() && !OuterMatchesEmitter(Obj, EmitterName)) continue;
		if (!DataInterfaceName.IsEmpty())
		{
			const FString ObjName = Obj->GetName();
			const FString ObjClass = Obj->GetClass()->GetName();
			const FString LogicalClass = StripDIPrefix(ObjClass);
			const FString ObjNameLower = ObjName.ToLower();
			const FString ObjClassLower = ObjClass.ToLower();
			const FString LogicalLower = LogicalClass.ToLower();
			const bool bMatch =
				ObjNameLower.Contains(DINameLower) ||
				ObjClassLower.Contains(DINameLower) ||
				LogicalLower.Contains(DINameLower) ||
				DINameLower.Contains(LogicalLower);
			if (!bMatch) continue;
		}
		AllDIs.Add(Obj);
	}

	bool bEmitterFilterFallback = false;
	if (AllDIs.Num() == 0 && !EmitterName.IsEmpty())
	{
		for (UObject* Obj : SubObjects)
		{
			if (!Obj || !Obj->IsA(UNiagaraDataInterface::StaticClass())) continue;
			if (!DataInterfaceName.IsEmpty())
			{
				const FString ObjNameLower = Obj->GetName().ToLower();
				const FString ObjClassLower = Obj->GetClass()->GetName().ToLower();
				const FString LogicalLower = StripDIPrefix(Obj->GetClass()->GetName()).ToLower();
				const bool bMatch =
					ObjNameLower.Contains(DINameLower) ||
					ObjClassLower.Contains(DINameLower) ||
					LogicalLower.Contains(DINameLower) ||
					DINameLower.Contains(LogicalLower);
				if (!bMatch) continue;
			}
			AllDIs.Add(Obj);
			bEmitterFilterFallback = true;
		}
	}

	if (AllDIs.Num() == 0)
	{
		TArray<FString> FoundDIs;
		for (UObject* Obj : SubObjects)
		{
			if (Obj && Obj->IsA(UNiagaraDataInterface::StaticClass()))
				FoundDIs.AddUnique(FString::Printf(TEXT("%s (class: %s)"), *Obj->GetName(), *Obj->GetClass()->GetName()));
		}
		FString DIList = FoundDIs.Num() > 0 ? FString::Join(FoundDIs, TEXT(", ")) : TEXT("none found");
		SetError(FString::Printf(TEXT("Data interface '%s' not found. All DIs in system: %s. Pass data_interface_name matching name or class."), *DataInterfaceName, *DIList), OutJsonString, OutError);
		return;
	}

	if (PropertyName.IsEmpty())
	{
		TArray<FString> PropList;
		for (UObject* DI : AllDIs)
		{
			for (TFieldIterator<FProperty> PropIt(DI->GetClass()); PropIt; ++PropIt)
			{
				if (PropIt->PropertyFlags & CPF_Edit)
					PropList.AddUnique(FString::Printf(TEXT("%s.%s"), *DI->GetClass()->GetName(), *PropIt->GetName()));
			}
		}
		SetError(FString::Printf(TEXT("property_name required. Available: %s"), *FString::Join(PropList, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	UObject* TargetDI = AllDIs[0];
	FProperty* Prop = TargetDI->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		TArray<FString> Available;
		for (TFieldIterator<FProperty> PropIt(TargetDI->GetClass()); PropIt; ++PropIt)
			if (PropIt->PropertyFlags & CPF_Edit) Available.Add(PropIt->GetName());
		SetError(FString::Printf(TEXT("Property '%s' not found on %s. Available: %s"), *PropertyName, *TargetDI->GetClass()->GetName(), *FString::Join(Available, TEXT(", "))), OutJsonString, OutError);
		return;
	}

	TargetDI->Modify();
	bool bSet = false;

	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *PropertyValue);
		if (!Asset) Asset = LoadObject<UObject>(nullptr, *StripObjectSuffix(PropertyValue));
		if (Asset && (ObjProp->PropertyClass == nullptr || Asset->IsA(ObjProp->PropertyClass)))
		{
			ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(TargetDI), Asset);
			bSet = true;
		}
		else
		{
			SetError(FString::Printf(TEXT("Could not load '%s' as %s."), *PropertyValue,
				ObjProp->PropertyClass ? *ObjProp->PropertyClass->GetName() : TEXT("UObject")), OutJsonString, OutError);
			return;
		}
	}
	else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
	{
		FSoftObjectPath SoftPath(PropertyValue);
		if (!SoftPath.IsValid())
		{
			SoftPath = FSoftObjectPath(StripObjectSuffix(PropertyValue));
		}
		if (!SoftPath.IsValid())
		{
			SetError(FString::Printf(TEXT("Could not parse '%s' as asset path for SoftObjectProperty '%s'."), *PropertyValue, *PropertyName), OutJsonString, OutError);
			return;
		}
		FSoftObjectPtr SoftPtr(SoftPath);
		SoftObjProp->SetPropertyValue_InContainer(TargetDI, SoftPtr);
		bSet = true;
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		BoolProp->SetPropertyValue_InContainer(TargetDI, PropertyValue.ToBool()); bSet = true;
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
	{
		IntProp->SetPropertyValue_InContainer(TargetDI, FCString::Atoi(*PropertyValue)); bSet = true;
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		FloatProp->SetPropertyValue_InContainer(TargetDI, FCString::Atof(*PropertyValue)); bSet = true;
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		DoubleProp->SetPropertyValue_InContainer(TargetDI, FCString::Atod(*PropertyValue)); bSet = true;
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		if (ByteProp->Enum)
		{
			int64 EnumVal = ByteProp->Enum->GetValueByNameString(PropertyValue);
			if (EnumVal == INDEX_NONE) { SetError(FString::Printf(TEXT("Invalid enum '%s'."), *PropertyValue), OutJsonString, OutError); return; }
			ByteProp->SetPropertyValue_InContainer(TargetDI, (uint8)EnumVal); bSet = true;
		}
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(PropertyValue);
		if (EnumVal == INDEX_NONE) { SetError(FString::Printf(TEXT("Invalid enum '%s'."), *PropertyValue), OutJsonString, OutError); return; }
		EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(TargetDI), EnumVal);
		bSet = true;
	}

	if (!bSet)
	{
		SetError(FString::Printf(TEXT("Could not set '%s' (type: %s). Pass property_value as string."), *PropertyName, *Prop->GetClass()->GetName()), OutJsonString, OutError);
		return;
	}

	RebuildStatelessEmitterCache(System);
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("data_interface"), TargetDI->GetClass()->GetName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetStringField(TEXT("value"), PropertyValue);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddNiagaraModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("modules"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemSystemPath = BatchToolHelper::GetItemString(Item, TEXT("system_path"));
			if (ItemSystemPath.IsEmpty()) ItemSystemPath = SystemPath;
			FString ItemEmitterName = BatchToolHelper::GetItemString(Item, TEXT("emitter_name"));
			if (ItemEmitterName.IsEmpty()) ItemEmitterName = EmitterName;
			FString ModulePath = BatchToolHelper::GetItemString(Item, TEXT("module_path"), TEXT("path"));
			FString ScriptSection; Item->TryGetStringField(TEXT("script_section"), ScriptSection);
			FString ItemEventName; Item->TryGetStringField(TEXT("event_name"), ItemEventName);
			double InsertIdx = -1.0; Item->TryGetNumberField(TEXT("insert_index"), InsertIdx);
			if (ModulePath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing module_path")); continue; }
			FString ItemOut, ItemErr;
			HandleAddNiagaraModuleEx(ItemSystemPath, ItemEmitterName, ScriptSection, ModulePath, (int32)InsertIdx, ItemEventName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("module_path"), ModulePath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ScriptSection, ModulePath, EventName;
	Args->TryGetStringField(TEXT("script_section"), ScriptSection);
	Args->TryGetStringField(TEXT("module_path"), ModulePath);
	Args->TryGetStringField(TEXT("event_name"), EventName);
	double InsertIdx = -1.0; Args->TryGetNumberField(TEXT("insert_index"), InsertIdx);
	HandleAddNiagaraModuleEx(SystemPath, EmitterName, ScriptSection, ModulePath, (int32)InsertIdx, EventName, OutJsonString, OutError);
}

void HandleRemoveNiagaraModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("modules"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString ModuleName;
			FString ItemSystemPath = SystemPath, ItemEmitterName = EmitterName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				ModuleName = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
			{
				ModuleName = BatchToolHelper::GetItemString(Item, TEXT("module_name"), TEXT("name"));
				FString S = BatchToolHelper::GetItemString(Item, TEXT("system_path"));
				if (!S.IsEmpty()) ItemSystemPath = S;
				FString E = BatchToolHelper::GetItemString(Item, TEXT("emitter_name"));
				if (!E.IsEmpty()) ItemEmitterName = E;
			}
			if (ModuleName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing module_name")); continue; }
			FString ItemOut, ItemErr;
			HandleRemoveNiagaraModule(ItemSystemPath, ItemEmitterName, ModuleName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("module_name"), ModuleName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ModuleName;
	Args->TryGetStringField(TEXT("module_name"), ModuleName);
	HandleRemoveNiagaraModule(SystemPath, EmitterName, ModuleName, OutJsonString, OutError);
}

void HandleSetNiagaraModuleParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("parameters"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemSystemPath = BatchToolHelper::GetItemString(Item, TEXT("system_path"));
			if (ItemSystemPath.IsEmpty()) ItemSystemPath = SystemPath;
			FString ItemEmitter = BatchToolHelper::GetItemString(Item, TEXT("emitter_name"));
			if (ItemEmitter.IsEmpty()) ItemEmitter = EmitterName;
			FString ModuleName = BatchToolHelper::GetItemString(Item, TEXT("module_name"));
			FString ParamName = BatchToolHelper::GetItemString(Item, TEXT("parameter_name"), TEXT("param_name"));
			FString ScriptSection; Item->TryGetStringField(TEXT("script_section"), ScriptSection);
			if (ParamName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing parameter_name")); continue; }
			FString ItemOut, ItemErr;
			HandleSetNiagaraModuleParameter(ItemSystemPath, ItemEmitter, ScriptSection, ParamName, ModuleName, Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("parameter_name"), ParamName);
				if (!ModuleName.IsEmpty()) Extra->SetStringField(TEXT("module_name"), ModuleName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ScriptSection, ParameterName, ModuleName;
	Args->TryGetStringField(TEXT("script_section"), ScriptSection);
	Args->TryGetStringField(TEXT("parameter_name"), ParameterName);
	if (ParameterName.IsEmpty()) Args->TryGetStringField(TEXT("param_name"), ParameterName);
	Args->TryGetStringField(TEXT("module_name"), ModuleName);
	HandleSetNiagaraModuleParameter(SystemPath, EmitterName, ScriptSection, ParameterName, ModuleName, Args, OutJsonString, OutError);
}

void HandleAddEmitterToSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("emitters"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString EmitterPath;
			if ((*ItemsArray)[i]->Type == EJson::String)
				EmitterPath = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
				EmitterPath = BatchToolHelper::GetItemString(Item, TEXT("emitter_path"), TEXT("path"));
			if (EmitterPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing emitter_path")); continue; }
			FString ItemOut, ItemErr;
			HandleAddEmitterToSystem(SystemPath, EmitterPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("emitter_path"), EmitterPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString EmitterPath;
	Args->TryGetStringField(TEXT("emitter_path"), EmitterPath);
	if (EmitterPath.IsEmpty()) Args->TryGetStringField(TEXT("emitter_name"), EmitterPath);
	HandleAddEmitterToSystem(SystemPath, EmitterPath, OutJsonString, OutError);
}

void HandleDuplicateEmitterInSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("emitters"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SourceName = BatchToolHelper::GetItemString(Item, TEXT("source_name"), TEXT("source_emitter_name"));
			FString TargetName = BatchToolHelper::GetItemString(Item, TEXT("target_name"), TEXT("new_emitter_name"));
			if (SourceName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing source_name")); continue; }
			if (TargetName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing target_name")); continue; }
			FString ItemOut, ItemErr;
			HandleDuplicateEmitterInSystem(SystemPath, SourceName, TargetName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("source_name"), SourceName);
				Extra->SetStringField(TEXT("target_name"), TargetName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SourceEmitterName, NewEmitterName;
	Args->TryGetStringField(TEXT("source_emitter_name"), SourceEmitterName);
	if (SourceEmitterName.IsEmpty()) Args->TryGetStringField(TEXT("source_name"), SourceEmitterName);
	if (SourceEmitterName.IsEmpty()) Args->TryGetStringField(TEXT("emitter_name"), SourceEmitterName);
	Args->TryGetStringField(TEXT("new_emitter_name"), NewEmitterName);
	if (NewEmitterName.IsEmpty()) Args->TryGetStringField(TEXT("target_name"), NewEmitterName);
	if (NewEmitterName.IsEmpty()) Args->TryGetStringField(TEXT("new_name"), NewEmitterName);
	HandleDuplicateEmitterInSystem(SystemPath, SourceEmitterName, NewEmitterName, OutJsonString, OutError);
}

void HandleCreateNiagaraSystemFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x0427) == 0
			|| !FCapabilityProfile::Get().IsEnabled(ECapability::AssetProvisioning))
			{ OutError = TEXT("Niagara editor module not initialised"); return; }
	}
	FString Name, SavePath, TemplatePath;
	Args->TryGetStringField(TEXT("name"), Name);
	if (Name.IsEmpty()) Args->TryGetStringField(TEXT("system_name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("template_path"), TemplatePath);
	HandleCreateNiagaraSystem(Name, SavePath, TemplatePath, OutJsonString, OutError);
}

void HandleCreateNiagaraEmitterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, TemplatePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("template_path"), TemplatePath);
	HandleCreateNiagaraEmitter(Name, SavePath, TemplatePath, OutJsonString, OutError);
}

void HandleSetNiagaraParameterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, ParameterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("parameter_name"), ParameterName);
	HandleSetNiagaraParameter(SystemPath, ParameterName, Args, OutJsonString, OutError);
}

void HandleGetNiagaraSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	HandleGetNiagaraSummary(SystemPath, OutJsonString, OutError);
}

void HandleListNiagaraTemplatesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString Filter;
	bool bSystemsOnly = false, bEmittersOnly = false;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("filter"), Filter);
		Args->TryGetBoolField(TEXT("systems_only"), bSystemsOnly);
		Args->TryGetBoolField(TEXT("emitters_only"), bEmittersOnly);
	}
	HandleListNiagaraTemplates(Filter, bSystemsOnly, bEmittersOnly, OutJsonString, OutError);
}

void HandleSetNiagaraRendererMaterialFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, MaterialPath;
	int32 RendererIndex = 0;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	double IdxVal = 0.0;
	if (Args->TryGetNumberField(TEXT("renderer_index"), IdxVal)) RendererIndex = (int32)IdxVal;
	HandleSetNiagaraRendererMaterial(SystemPath, EmitterName, MaterialPath, RendererIndex, OutJsonString, OutError);
}

void HandleGetNiagaraDetailedSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	HandleGetNiagaraDetailedSummary(SystemPath, OutJsonString, OutError);
}

void HandleSetNiagaraRendererPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, PropertyName;
	int32 RendererIndex = 0;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	double IdxVal = 0.0;
	if (Args->TryGetNumberField(TEXT("renderer_index"), IdxVal)) RendererIndex = (int32)IdxVal;
	HandleSetNiagaraRendererProperty(SystemPath, EmitterName, RendererIndex, PropertyName, Args, OutJsonString, OutError);
}

void HandleSetNiagaraDistributionCurveFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, ModuleName, PropertyName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("module_name"), ModuleName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	if (SystemPath.IsEmpty())
	{
		OutError = TEXT("Missing system_path. set_niagara_distribution_curve needs the full /Game/Path/NS_X.NS_X path of the NiagaraSystem to find the module on.");
		return;
	}
	if (EmitterName.IsEmpty())
	{
		OutError = TEXT("Missing emitter_name. Pass the emitter the module lives on (run get_niagara_summary to list the emitters in the system).");
		return;
	}
	HandleSetNiagaraDistributionCurve(SystemPath, EmitterName, ModuleName, PropertyName, Args, OutJsonString, OutError);
}

void HandleSetMeshRendererMeshFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, MeshPath, MaterialPath;
	int32 RendererIndex = 0;
	int32 MeshIndex = 0;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	double IdxVal = 0.0;
	if (Args->TryGetNumberField(TEXT("renderer_index"), IdxVal)) RendererIndex = (int32)IdxVal;
	if (Args->TryGetNumberField(TEXT("mesh_index"), IdxVal)) MeshIndex = (int32)IdxVal;

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("meshes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemMeshPath = BatchToolHelper::GetItemString(Item, TEXT("mesh_path"), TEXT("mesh"));
			FString ItemMaterialPath = BatchToolHelper::GetItemString(Item, TEXT("material_path"), TEXT("material"));
			int32 ItemMeshIdx = MeshIndex + i;
			double ItemIdx = 0.0;
			if (Item->TryGetNumberField(TEXT("mesh_index"), ItemIdx)) ItemMeshIdx = (int32)ItemIdx;
			FString ItemOut, ItemErr;
			HandleSetMeshRendererMesh(SystemPath, EmitterName, RendererIndex, ItemMeshIdx, ItemMeshPath, ItemMaterialPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto E = MakeShared<FJsonObject>();
				E->SetNumberField(TEXT("mesh_index"), ItemMeshIdx);
				if (!ItemMeshPath.IsEmpty()) E->SetStringField(TEXT("mesh"), ItemMeshPath);
				Batch.AddSuccess(i, E);
			}
			else { Batch.AddFailure(i, ItemErr); }
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	HandleSetMeshRendererMesh(SystemPath, EmitterName, RendererIndex, MeshIndex, MeshPath, MaterialPath, OutJsonString, OutError);
}

namespace
{
	struct FUnmetDep
	{
		FString EmitterName;
		ENiagaraScriptUsage Usage;
		FGuid UsageId;
		FString Section;
		FName MissingId;
		ENiagaraModuleDependencyScriptConstraint Constraint;
		FString RequiringModuleName;
	};

	void GatherProvidedAndRequired(UNiagaraGraph* Graph, TSet<FName>& OutProvided, TArray<TPair<UNiagaraNodeFunctionCall*, const FNiagaraModuleDependency*>>& OutRequired)
	{
		if (!Graph) return;
		TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
		Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(ModuleNodes);
		for (UNiagaraNodeFunctionCall* Module : ModuleNodes)
		{
			if (!Module || !Module->FunctionScript) continue;
			FVersionedNiagaraScriptData* MScriptData = Module->GetScriptData();
			if (!MScriptData) continue;
			for (FName Id : MScriptData->ProvidedDependencies) OutProvided.Add(Id);
			for (const FNiagaraModuleDependency& Req : MScriptData->RequiredDependencies)
			{
				OutRequired.Add(TPair<UNiagaraNodeFunctionCall*, const FNiagaraModuleDependency*>(Module, &Req));
			}
		}
	}

	void GetModuleStackOrder(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage, FGuid UsageId,
		TArray<UNiagaraNodeFunctionCall*>& OutStack)
	{
		OutStack.Reset();
		if (!Graph) return;
		UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage, UsageId);
		if (!OutputNode) return;

		UEdGraphPin* OutputInputPin = nullptr;
		for (UEdGraphPin* Pin : OutputNode->Pins)
		{
			if (Pin->Direction == EGPD_Input) { OutputInputPin = Pin; break; }
		}
		if (!OutputInputPin || OutputInputPin->LinkedTo.Num() == 0) return;

		TArray<UNiagaraNodeFunctionCall*> Reversed;
		UEdGraphPin* CurrentInput = OutputInputPin;
		TSet<UNiagaraNodeFunctionCall*> Seen;
		while (CurrentInput && CurrentInput->LinkedTo.Num() > 0)
		{
			UEdGraphPin* PrevOutput = CurrentInput->LinkedTo[0];
			if (!PrevOutput || !PrevOutput->GetOwningNode()) break;
			UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(PrevOutput->GetOwningNode());
			if (!FC) break;
			if (Seen.Contains(FC)) break;
			Seen.Add(FC);
			Reversed.Add(FC);
			UEdGraphPin* FCMapIn = nullptr;
			for (UEdGraphPin* Pin : FC->Pins)
			{
				if (Pin->Direction == EGPD_Input && Pin->PinType == PrevOutput->PinType)
				{
					FCMapIn = Pin; break;
				}
			}
			CurrentInput = FCMapIn;
		}

		OutStack.Reserve(Reversed.Num());
		for (int32 i = Reversed.Num() - 1; i >= 0; --i) OutStack.Add(Reversed[i]);
	}

	int32 ReorderStackForDependencies(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage, FGuid UsageId)
	{
		TArray<UNiagaraNodeFunctionCall*> Stack;
		GetModuleStackOrder(Graph, Usage, UsageId, Stack);
		if (Stack.Num() < 2) return 0;

		struct FModInfo
		{
			TSet<FName> Provides;
			TArray<TPair<FName, ENiagaraModuleDependencyType>> Required;
		};
		TArray<FModInfo> Info;
		Info.SetNum(Stack.Num());
		for (int32 i = 0; i < Stack.Num(); ++i)
		{
			FVersionedNiagaraScriptData* SD = Stack[i]->GetScriptData();
			if (!SD) continue;
			for (FName Id : SD->ProvidedDependencies) Info[i].Provides.Add(Id);
			for (const FNiagaraModuleDependency& R : SD->RequiredDependencies)
			{
				Info[i].Required.Add(TPair<FName, ENiagaraModuleDependencyType>(R.Id, R.Type));
			}
		}

		auto IsViolation = [&](int32 LeftIdx, int32 RightIdx) -> bool
		{
			for (const auto& Req : Info[LeftIdx].Required)
			{
				if (Req.Value == ENiagaraModuleDependencyType::PreDependency &&
					Info[RightIdx].Provides.Contains(Req.Key))
				{
					return true;
				}
			}
			for (const auto& Req : Info[RightIdx].Required)
			{
				if (Req.Value == ENiagaraModuleDependencyType::PostDependency &&
					Info[LeftIdx].Provides.Contains(Req.Key))
				{
					return true;
				}
			}
			return false;
		};

		int32 SwapCount = 0;
		bool bChanged = true;
		const int32 MaxPasses = Stack.Num() * 2;
		int32 Pass = 0;
		while (bChanged && Pass < MaxPasses)
		{
			bChanged = false;
			++Pass;
			for (int32 i = 0; i + 1 < Stack.Num(); ++i)
			{
				if (IsViolation(i, i + 1))
				{
					Swap(Stack[i], Stack[i + 1]);
					Swap(Info[i], Info[i + 1]);
					bChanged = true;
					++SwapCount;
				}
			}
		}
		if (SwapCount == 0) return 0;

		UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage, UsageId);
		if (!OutputNode) return SwapCount;
		UEdGraphPin* OutputInputPin = nullptr;
		for (UEdGraphPin* Pin : OutputNode->Pins)
		{
			if (Pin->Direction == EGPD_Input) { OutputInputPin = Pin; break; }
		}
		if (!OutputInputPin) return SwapCount;

		auto FindMapIn  = [&](UNiagaraNodeFunctionCall* FC) -> UEdGraphPin*
		{
			for (UEdGraphPin* Pin : FC->Pins)
				if (Pin->Direction == EGPD_Input && Pin->PinType == OutputInputPin->PinType)
					return Pin;
			return nullptr;
		};
		auto FindMapOut = [&](UNiagaraNodeFunctionCall* FC) -> UEdGraphPin*
		{
			for (UEdGraphPin* Pin : FC->Pins)
				if (Pin->Direction == EGPD_Output && Pin->PinType == OutputInputPin->PinType)
					return Pin;
			return nullptr;
		};

		UEdGraphPin* HeadSourcePin = nullptr;
		{
			UNiagaraNodeFunctionCall* OldFirst = nullptr;
			TArray<UNiagaraNodeFunctionCall*> CurrentOrder;
			GetModuleStackOrder(Graph, Usage, UsageId, CurrentOrder);
			if (CurrentOrder.Num() > 0)
			{
				OldFirst = CurrentOrder[0];
				if (UEdGraphPin* OldFirstIn = FindMapIn(OldFirst))
				{
					if (OldFirstIn->LinkedTo.Num() > 0) HeadSourcePin = OldFirstIn->LinkedTo[0];
				}
			}
		}

		Graph->Modify();

		for (UNiagaraNodeFunctionCall* FC : Stack)
		{
			if (UEdGraphPin* P = FindMapIn(FC))  P->BreakAllPinLinks();
			if (UEdGraphPin* P = FindMapOut(FC)) P->BreakAllPinLinks();
		}
		OutputInputPin->BreakAllPinLinks();

		UEdGraphPin* PrevOut = HeadSourcePin;
		for (UNiagaraNodeFunctionCall* FC : Stack)
		{
			UEdGraphPin* In  = FindMapIn(FC);
			UEdGraphPin* Out = FindMapOut(FC);
			if (!In || !Out) continue;
			if (PrevOut) PrevOut->MakeLinkTo(In);
			PrevOut = Out;
		}
		if (PrevOut) PrevOut->MakeLinkTo(OutputInputPin);
		Graph->NotifyGraphChanged();
		return SwapCount;
	}

	bool FindModuleAssetProvidingDependency(FName DepId, ENiagaraScriptUsage TargetUsage, FAssetData& OutAsset)
	{
		FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Options;
		Options.bIncludeDeprecatedScripts = false;
		Options.bIncludeNonLibraryScripts = true;
		Options.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
		Options.TargetUsageToMatch = TargetUsage;

		TArray<FAssetData> ModuleAssets;
		FNiagaraEditorUtilities::GetFilteredScriptAssets(Options, ModuleAssets);

		for (const FAssetData& Asset : ModuleAssets)
		{
			FString ProvidedString;
			if (!Asset.GetTagValue(GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, ProvidedDependencies), ProvidedString)) continue;
			if (ProvidedString.IsEmpty()) continue;
			TArray<FString> Parts;
			ProvidedString.ParseIntoArray(Parts, TEXT(","));
			for (const FString& Part : Parts)
			{
				if (FName(*Part) == DepId) { OutAsset = Asset; return true; }
			}
		}
		return false;
	}
}

void HandleResolveNiagaraDependenciesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	if (SystemPath.IsEmpty()) { SetError(TEXT("'system_path' is required."), OutJsonString, OutError); return; }

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	auto SectionForUsage = [](ENiagaraScriptUsage Usage) -> FString
	{
		switch (Usage)
		{
		case ENiagaraScriptUsage::ParticleSpawnScript:             return TEXT("particle_spawn");
		case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated: return TEXT("particle_spawn_interpolated");
		case ENiagaraScriptUsage::ParticleUpdateScript:            return TEXT("particle_update");
		case ENiagaraScriptUsage::ParticleEventScript:             return TEXT("particle_event");
		case ENiagaraScriptUsage::EmitterSpawnScript:              return TEXT("emitter_spawn");
		case ENiagaraScriptUsage::EmitterUpdateScript:             return TEXT("emitter_update");
		case ENiagaraScriptUsage::SystemSpawnScript:               return TEXT("system_spawn");
		case ENiagaraScriptUsage::SystemUpdateScript:              return TEXT("system_update");
		default:                                                   return TEXT("unknown");
		}
	};

	TSet<FName> SystemProvided;
	TMap<FString, TSet<FName>> EmitterProvided;
	TMap<UNiagaraGraph*, FString> GraphToEmitterName;
	TArray<FUnmetDep> AllRequired;

	auto WalkOneScript = [&](UNiagaraScript* Script, const FString& EmitterName)
	{
		if (!Script) return;
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		if (!Source || !Source->NodeGraph) return;

		TArray<TPair<UNiagaraNodeFunctionCall*, const FNiagaraModuleDependency*>> Required;
		TSet<FName> Provided;
		GatherProvidedAndRequired(Source->NodeGraph, Provided, Required);

		const ENiagaraScriptUsage Usage = Script->GetUsage();
		const bool bIsSystemScript = Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript;
		if (bIsSystemScript)
			for (FName Id : Provided) SystemProvided.Add(Id);
		else if (!EmitterName.IsEmpty())
			for (FName Id : Provided) EmitterProvided.FindOrAdd(EmitterName).Add(Id);

		for (const auto& ReqPair : Required)
		{
			FUnmetDep U;
			U.EmitterName = EmitterName;
			U.Usage = Usage;
			U.UsageId = Script->GetUsageId();
			U.Section = SectionForUsage(Usage);
			U.MissingId = ReqPair.Value->Id;
			U.Constraint = ReqPair.Value->ScriptConstraint;
			U.RequiringModuleName = ReqPair.Key->GetFunctionName();
			AllRequired.Add(MoveTemp(U));
		}
	};

	WalkOneScript(System->GetSystemSpawnScript(),  FString());
	WalkOneScript(System->GetSystemUpdateScript(), FString());

	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data) continue;
		const FString EmitterName = Handle.GetName().ToString();
		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts,  true,  false);
		for (UNiagaraScript* Script : Scripts)
		{
			WalkOneScript(Script, EmitterName);
		}
	}

	auto IsSatisfied = [&](const FUnmetDep& Req) -> bool
	{
		if (Req.Constraint == ENiagaraModuleDependencyScriptConstraint::SameScript)
		{
			UNiagaraScript* ReqScript = nullptr;
			if (Req.EmitterName.IsEmpty())
			{
				ReqScript = (Req.Usage == ENiagaraScriptUsage::SystemSpawnScript)
					? System->GetSystemSpawnScript()
					: System->GetSystemUpdateScript();
			}
			else
			{
				FNiagaraEmitterHandle* H = FindEmitterHandle(System, Req.EmitterName);
				if (!H || !H->GetEmitterData()) return false;
				TArray<UNiagaraScript*> Scripts;
				H->GetEmitterData()->GetScripts(Scripts, true, false);
				for (UNiagaraScript* S : Scripts)
				{
					if (S->GetUsage() == Req.Usage && S->GetUsageId() == Req.UsageId) { ReqScript = S; break; }
				}
			}
			if (!ReqScript) return false;
			UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(ReqScript->GetLatestSource());
			if (!Source || !Source->NodeGraph) return false;
			TSet<FName> P; TArray<TPair<UNiagaraNodeFunctionCall*, const FNiagaraModuleDependency*>> R;
			GatherProvidedAndRequired(Source->NodeGraph, P, R);
			return P.Contains(Req.MissingId);
		}
		if (SystemProvided.Contains(Req.MissingId)) return true;
		if (!Req.EmitterName.IsEmpty())
		{
			if (TSet<FName>* PerEmitter = EmitterProvided.Find(Req.EmitterName))
				if (PerEmitter->Contains(Req.MissingId)) return true;
		}
		return false;
	};

	TArray<FUnmetDep> Unmet;
	for (const FUnmetDep& Req : AllRequired)
	{
		if (!IsSatisfied(Req)) Unmet.Add(Req);
	}

	TArray<TSharedPtr<FJsonValue>> Added;
	TArray<TSharedPtr<FJsonValue>> Unresolved;
	TSet<TPair<UNiagaraScript*, FName>> AlreadyResolved;

	for (const FUnmetDep& U : Unmet)
	{
		UNiagaraScript* TargetScript = nullptr;
		ENiagaraScriptUsage TargetUsage = U.Usage;
		FGuid TargetUsageId = U.UsageId;
		FString TargetSection = U.Section;
		FString TargetEmitterName = U.EmitterName;

		if (U.Constraint == ENiagaraModuleDependencyScriptConstraint::SameScript)
		{
		}
		else
		{
			if (UNiagaraScript* SysUpdate = System->GetSystemUpdateScript())
			{
				TargetScript = SysUpdate;
				TargetUsage = ENiagaraScriptUsage::SystemUpdateScript;
				TargetUsageId = SysUpdate->GetUsageId();
				TargetSection = TEXT("system_update");
				TargetEmitterName = FString();
			}
		}

		if (!TargetScript)
		{
			if (TargetEmitterName.IsEmpty())
			{
				TargetScript = (TargetUsage == ENiagaraScriptUsage::SystemSpawnScript)
					? System->GetSystemSpawnScript()
					: System->GetSystemUpdateScript();
			}
			else
			{
				FNiagaraEmitterHandle* H = FindEmitterHandle(System, TargetEmitterName);
				if (H && H->GetEmitterData())
				{
					TArray<UNiagaraScript*> Scripts;
					H->GetEmitterData()->GetScripts(Scripts, true, false);
					for (UNiagaraScript* S : Scripts)
					{
						if (S->GetUsage() == TargetUsage && S->GetUsageId() == TargetUsageId) { TargetScript = S; break; }
					}
				}
			}
		}

		auto ReportUnresolved = [&](const TCHAR* Reason)
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("missing_id"), U.MissingId.ToString());
			Obj->SetStringField(TEXT("requiring_module"), U.RequiringModuleName);
			if (!U.EmitterName.IsEmpty()) Obj->SetStringField(TEXT("emitter"), U.EmitterName);
			Obj->SetStringField(TEXT("script_section"), U.Section);
			Obj->SetStringField(TEXT("reason"), Reason);
			Unresolved.Add(MakeShareable(new FJsonValueObject(Obj)));
		};

		if (!TargetScript) { ReportUnresolved(TEXT("Could not resolve target script for the missing dependency.")); continue; }

		const TPair<UNiagaraScript*, FName> ResolvedKey(TargetScript, U.MissingId);
		if (AlreadyResolved.Contains(ResolvedKey)) continue;
		AlreadyResolved.Add(ResolvedKey);

		UNiagaraScriptSource* TargetSource = Cast<UNiagaraScriptSource>(TargetScript->GetLatestSource());
		UNiagaraGraph* TargetGraph = TargetSource ? TargetSource->NodeGraph : nullptr;
		if (!TargetGraph) { ReportUnresolved(TEXT("Target script has no node graph.")); continue; }
		UNiagaraNodeOutput* TargetOutput = TargetGraph->FindEquivalentOutputNode(TargetUsage, TargetUsageId);
		if (!TargetOutput) { ReportUnresolved(TEXT("Target script graph has no output node for the requested usage.")); continue; }

		FAssetData ProviderAsset;
		if (!FindModuleAssetProvidingDependency(U.MissingId, TargetUsage, ProviderAsset))
		{
			ReportUnresolved(TEXT("No module asset provides this dependency for the target usage."));
			continue;
		}

		UNiagaraScript* ProviderScript = Cast<UNiagaraScript>(ProviderAsset.GetAsset());
		if (!ProviderScript) { ReportUnresolved(TEXT("Could not load provider module script asset.")); continue; }

		UNiagaraNodeFunctionCall* AddedNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
			ProviderScript, *TargetOutput, INDEX_NONE, FString(),
			ProviderScript->GetExposedVersion().VersionGuid);
		if (!AddedNode) { ReportUnresolved(TEXT("AddScriptModuleToStack returned nullptr.")); continue; }

		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("missing_id"), U.MissingId.ToString());
		Obj->SetStringField(TEXT("provider_module"), ProviderAsset.AssetName.ToString());
		Obj->SetStringField(TEXT("provider_path"), ProviderAsset.GetObjectPathString());
		Obj->SetStringField(TEXT("requiring_module"), U.RequiringModuleName);
		if (!TargetEmitterName.IsEmpty()) Obj->SetStringField(TEXT("added_to_emitter"), TargetEmitterName);
		Obj->SetStringField(TEXT("added_to_section"), TargetSection);
		Added.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	int32 TotalSwaps = 0;
	auto ReorderForScript = [&](UNiagaraScript* Script)
	{
		if (!Script) return;
		UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		if (!Src || !Src->NodeGraph) return;
		TotalSwaps += ReorderStackForDependencies(Src->NodeGraph, Script->GetUsage(), Script->GetUsageId());
	};
	ReorderForScript(System->GetSystemSpawnScript());
	ReorderForScript(System->GetSystemUpdateScript());
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data) continue;
		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts,  true,  false);
		for (UNiagaraScript* S : Scripts) ReorderForScript(S);
	}

	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("scanned_required_count"), AllRequired.Num());
	Result->SetNumberField(TEXT("unmet_count"), Unmet.Num());
	Result->SetNumberField(TEXT("added_count"), Added.Num());
	Result->SetNumberField(TEXT("unresolved_count"), Unresolved.Num());
	Result->SetNumberField(TEXT("reorder_swap_count"), TotalSwaps);
	if (Added.Num() > 0)      Result->SetArrayField(TEXT("added"),      Added);
	if (Unresolved.Num() > 0) Result->SetArrayField(TEXT("unresolved"), Unresolved);
	CompileAndReport(System, Result);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleValidateNiagaraSystemFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	if (SystemPath.IsEmpty()) { SetError(TEXT("'system_path' is required."), OutJsonString, OutError); return; }

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;

	auto AddIssue = [&](TArray<TSharedPtr<FJsonValue>>& Bucket, const TCHAR* Code,
		const FString& EmitterName, const FString& Section, const FString& Message)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("code"), Code);
		if (!EmitterName.IsEmpty()) Obj->SetStringField(TEXT("emitter"), EmitterName);
		if (!Section.IsEmpty())     Obj->SetStringField(TEXT("script_section"), Section);
		Obj->SetStringField(TEXT("message"), Message);
		Bucket.Add(MakeShareable(new FJsonValueObject(Obj)));
	};

	auto SectionForUsage = [](ENiagaraScriptUsage Usage) -> FString
	{
		switch (Usage)
		{
		case ENiagaraScriptUsage::ParticleSpawnScript:             return TEXT("particle_spawn");
		case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated: return TEXT("particle_spawn_interpolated");
		case ENiagaraScriptUsage::ParticleUpdateScript:            return TEXT("particle_update");
		case ENiagaraScriptUsage::ParticleEventScript:             return TEXT("particle_event");
		case ENiagaraScriptUsage::ParticleSimulationStageScript:   return TEXT("particle_simulation_stage");
		case ENiagaraScriptUsage::ParticleGPUComputeScript:        return TEXT("particle_gpu");
		case ENiagaraScriptUsage::EmitterSpawnScript:              return TEXT("emitter_spawn");
		case ENiagaraScriptUsage::EmitterUpdateScript:             return TEXT("emitter_update");
		case ENiagaraScriptUsage::SystemSpawnScript:               return TEXT("system_spawn");
		case ENiagaraScriptUsage::SystemUpdateScript:              return TEXT("system_update");
		default:                                                   return TEXT("unknown");
		}
	};

	auto WalkScriptGraph = [&](UNiagaraScript* Script, const FString& EmitterName)
	{
		if (!Script) return;
		const FString Section = SectionForUsage(Script->GetUsage());

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		if (!Source)
		{
			AddIssue(Errors, TEXT("missing_script_source"), EmitterName, Section,
				TEXT("Script has no editor source. The asset may have been imported without editor data."));
			return;
		}
		UNiagaraGraph* Graph = Source->NodeGraph;
		if (!Graph)
		{
			AddIssue(Errors, TEXT("missing_node_graph"), EmitterName, Section,
				TEXT("Script source has no node graph."));
			return;
		}

		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		bool bHasValidOutput = false;
		for (UNiagaraNodeOutput* OutNode : OutputNodes)
		{
			if (IsValid(OutNode)) { bHasValidOutput = true; break; }
		}
		if (!bHasValidOutput)
		{
			AddIssue(Errors, TEXT("missing_output_node"), EmitterName, Section,
				TEXT("Script graph has no valid UNiagaraNodeOutput. The system editor's stack walker requires one to terminate."));
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node))
			{
				AddIssue(Errors, TEXT("garbage_node"), EmitterName, Section,
					TEXT("Graph contains a node marked as garbage but still in Nodes[]. Reload or repair the asset."));
				continue;
			}

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				for (int32 i = 0; i < Pin->LinkedTo.Num(); ++i)
				{
					UEdGraphPin* LinkedPin = Pin->LinkedTo[i];
					if (!LinkedPin)
					{
						AddIssue(Errors, TEXT("null_pin_link"), EmitterName, Section,
							FString::Printf(TEXT("Node '%s' pin '%s' has a null entry at LinkedTo[%d]."),
								*Node->GetName(), *Pin->PinName.ToString(), i));
						continue;
					}
					UEdGraphNode* OwningNode = LinkedPin->GetOwningNodeUnchecked();
					if (!OwningNode)
					{
						AddIssue(Errors, TEXT("dangling_pin_link"), EmitterName, Section,
							FString::Printf(TEXT("Node '%s' pin '%s' links to a pin whose owning node was destroyed."),
								*Node->GetName(), *Pin->PinName.ToString()));
						continue;
					}
					if (!IsValid(OwningNode))
					{
						AddIssue(Errors, TEXT("garbage_pin_link"), EmitterName, Section,
							FString::Printf(TEXT("Node '%s' pin '%s' links to a pin on a node marked as garbage ('%s')."),
								*Node->GetName(), *Pin->PinName.ToString(), *OwningNode->GetName()));
					}
				}
			}
		}
	};

	int32 EmitterCount = 0;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		++EmitterCount;
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data) continue;

		const FString EmitterName = Handle.GetName().ToString();
		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts,  true,  false);
		for (UNiagaraScript* Script : Scripts)
		{
			WalkScriptGraph(Script, EmitterName);
		}

		const FGuid EmitterVersion = Handle.GetInstance().Version;
		UNiagaraScriptSourceBase* ExpectedSource = Data->GraphSource;
		for (UNiagaraScript* Script : Scripts)
		{
			if (!Script) continue;
			UNiagaraScriptSourceBase* ScriptSource = Script->GetSource(EmitterVersion);
			if (ScriptSource != ExpectedSource)
			{
				const FString Section = SectionForUsage(Script->GetUsage());
				AddIssue(Errors, TEXT("script_source_version_mismatch"), EmitterName, Section,
					FString::Printf(TEXT("Script's source for emitter version doesn't match emitter GraphSource. ")
						TEXT("This crashes FNiagaraScriptViewModel::SetScripts on editor open. ")
						TEXT("Common cause: create_niagara_emitter + add_emitter_to_system double-duplicates the template; ")
						TEXT("call add_emitter_to_system with the engine template path directly instead of going through a /Game/ copy.")));
			}
		}
	}

	WalkScriptGraph(System->GetSystemSpawnScript(),  FString());
	WalkScriptGraph(System->GetSystemUpdateScript(), FString());

	auto SweepScriptCompileStatus = [&](UNiagaraScript* Script, const FString& EmitterName)
	{
		if (!Script) return;
		const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
		if (Status != ENiagaraScriptCompileStatus::NCS_Error
			&& Status != ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings)
		{
			return;
		}
		const FString Section = SectionForUsage(Script->GetUsage());
		const FNiagaraVMExecutableData& VMData = Script->GetVMExecutableData();
		for (const FNiagaraCompileEvent& Evt : VMData.LastCompileEvents)
		{
			const bool bIsError = Evt.Severity == FNiagaraCompileEventSeverity::Error;
			AddIssue(bIsError ? Errors : Warnings,
				bIsError ? TEXT("compile_error") : TEXT("compile_warning"),
				EmitterName, Section, Evt.Message);
		}
		if (Status == ENiagaraScriptCompileStatus::NCS_Error && VMData.LastCompileEvents.Num() == 0)
		{
			AddIssue(Errors, TEXT("compile_error"), EmitterName, Section,
				TEXT("Script compile status is Error but no compile events were captured. Open the asset in the editor to see the full error log."));
		}
	};
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data) continue;
		const FString EmitterName = Handle.GetName().ToString();
		TArray<UNiagaraScript*> EmitterScripts;
		Data->GetScripts(EmitterScripts,  true,  false);
		for (UNiagaraScript* Script : EmitterScripts) SweepScriptCompileStatus(Script, EmitterName);
	}
	SweepScriptCompileStatus(System->GetSystemSpawnScript(),  FString());
	SweepScriptCompileStatus(System->GetSystemUpdateScript(), FString());

	const bool bWouldOpen = Errors.Num() == 0;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("would_open"), bWouldOpen);
	Result->SetNumberField(TEXT("emitter_count"), EmitterCount);
	Result->SetNumberField(TEXT("error_count"),   Errors.Num());
	Result->SetNumberField(TEXT("warning_count"), Warnings.Num());
	if (Errors.Num()   > 0) Result->SetArrayField(TEXT("errors"),   Errors);
	if (Warnings.Num() > 0) Result->SetArrayField(TEXT("warnings"), Warnings);
	if (!bWouldOpen)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("System has structural issues that crash the editor on open. Fix the listed errors before declaring done. Common cause: an event-handler module placed outside an event handler script (use add_niagara_event_handler), or a module added then partially deleted."));
	}
	BuildSuccessJson(Result, OutJsonString);
}

void HandleListNiagaraModulesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString Filter;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("filter"), Filter);
	HandleListNiagaraModules(Filter, OutJsonString, OutError);
}

void HandleRemoveEmitterFromSystemFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	HandleRemoveEmitterFromSystem(SystemPath, EmitterName, OutJsonString, OutError);
}

void HandleSetEmitterPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	HandleSetEmitterProperties(SystemPath, EmitterName, Args, OutJsonString, OutError);
}

void HandleAddRendererToEmitterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, RendererType;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("renderer_type"), RendererType);
	HandleAddRendererToEmitter(SystemPath, EmitterName, RendererType, OutJsonString, OutError);
}

void HandleSetNiagaraSystemPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	HandleSetNiagaraSystemProperties(SystemPath, Args, OutJsonString, OutError);
}

void HandleSetEmitterScalabilityFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	HandleSetEmitterScalability(SystemPath, EmitterName, Args, OutJsonString, OutError);
}

void HandleRemoveNiagaraRendererFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName;
	int32 RendererIndex = 0;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	double IdxVal = 0.0;
	if (Args->TryGetNumberField(TEXT("renderer_index"), IdxVal)) RendererIndex = (int32)IdxVal;
	HandleRemoveNiagaraRenderer(SystemPath, EmitterName, RendererIndex, OutJsonString, OutError);
}

void HandleGetEmitterModulesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	HandleGetEmitterModules(SystemPath, EmitterName, OutJsonString, OutError);
}

void HandleSetEmitterSpawnRateFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	double SR = -1.0, BT = -1.0;
	int32 BC = -1;
	Args->TryGetNumberField(TEXT("spawn_rate"), SR);
	double BCDouble = -1.0;
	if (Args->TryGetNumberField(TEXT("burst_count"), BCDouble)) BC = (int32)BCDouble;
	Args->TryGetNumberField(TEXT("burst_time"), BT);
	HandleSetEmitterSpawnRate(SystemPath, EmitterName, (float)SR, BC, (float)BT, OutJsonString, OutError);
}

void HandleSetNiagaraSimTargetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, SimTarget;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("sim_target"), SimTarget);
	HandleSetNiagaraSimTarget(SystemPath, EmitterName, SimTarget, OutJsonString, OutError);
}

void HandleRenameEmitterInSystemFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("emitters"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemSystemPath = BatchToolHelper::GetItemString(Item, TEXT("system_path"));
			if (ItemSystemPath.IsEmpty()) ItemSystemPath = SystemPath;
			FString OldName = BatchToolHelper::GetItemString(Item, TEXT("emitter_name"), TEXT("old_emitter_name"));
			if (OldName.IsEmpty()) OldName = BatchToolHelper::GetItemString(Item, TEXT("old_name"));
			FString NewName = BatchToolHelper::GetItemString(Item, TEXT("new_emitter_name"), TEXT("new_name"));
			if (OldName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing emitter_name")); continue; }
			if (NewName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing new_emitter_name (or new_name)")); continue; }
			FString ItemOut, ItemErr;
			HandleRenameEmitterInSystem(ItemSystemPath, OldName, NewName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("emitter_name"), NewName);
				Extra->SetStringField(TEXT("old_emitter_name"), OldName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString OldName, NewName;
	Args->TryGetStringField(TEXT("old_emitter_name"), OldName);
	if (OldName.IsEmpty()) Args->TryGetStringField(TEXT("emitter_name"), OldName);
	Args->TryGetStringField(TEXT("new_emitter_name"), NewName);
	if (NewName.IsEmpty()) Args->TryGetStringField(TEXT("new_name"), NewName);
	HandleRenameEmitterInSystem(SystemPath, OldName, NewName, OutJsonString, OutError);
}

void HandleAddNiagaraEventHandlerFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, SourceEmitter, EventName, ScriptPath;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("source_emitter_name"), SourceEmitter);
	Args->TryGetStringField(TEXT("event_name"), EventName);
	Args->TryGetStringField(TEXT("script_path"), ScriptPath);
	HandleAddNiagaraEventHandler(SystemPath, EmitterName, SourceEmitter, EventName, ScriptPath, OutJsonString, OutError);
}

void HandleSetNiagaraDataInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, DataInterfaceName, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("data_interface_name"), DataInterfaceName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetNiagaraDataInterface(SystemPath, EmitterName, DataInterfaceName, PropertyName, PropertyValue, OutJsonString, OutError);
}

namespace
{
	bool FindStatelessDistributionProperty(UNiagaraStatelessEmitter* StatelessEmitter,
		const FString& ModuleName, const FString& PropertyName,
		UObject*& OutModule, FStructProperty*& OutProp, FString& OutError)
	{
		const FString ModuleNameLower = ModuleName.ToLower();
		for (UNiagaraStatelessModule* Mod : StatelessEmitter->GetModules())
		{
			if (!Mod) continue;
			if (!ModuleNameLower.IsEmpty())
			{
				FString ClassName = Mod->GetClass()->GetName();
				FString ShortName = ClassName.StartsWith(TEXT("NiagaraStatelessModule_"))
					? ClassName.Mid(23) : ClassName;
				if (!ShortName.ToLower().Equals(ModuleNameLower) &&
					!ClassName.ToLower().Equals(ModuleNameLower)) continue;
			}
			for (TFieldIterator<FProperty> PropIt(Mod->GetClass()); PropIt; ++PropIt)
			{
				if (!(PropIt->PropertyFlags & CPF_Edit)) continue;
				if (!PropIt->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) continue;
				FStructProperty* SP = CastField<FStructProperty>(*PropIt);
				if (SP && SP->Struct->IsChildOf(FNiagaraDistributionBase::StaticStruct()))
				{
					OutModule = Mod;
					OutProp   = SP;
					return true;
				}
			}
		}
		OutError = FString::Printf(
			TEXT("Distribution property '%s' not found on module '%s'"),
			*PropertyName, *ModuleName);
		return false;
	}

	FString CanonicalizeBindingSourceName(const FString& Source)
	{
		if (Source.IsEmpty()) return Source;
		static const TArray<FString> KnownPrefixes {
			TEXT("User."), TEXT("Engine."), TEXT("Particles."), TEXT("Emitter."),
			TEXT("System."), TEXT("Module."), TEXT("Local."), TEXT("NPC."),
			TEXT("MAP."), TEXT("Output."), TEXT("Constant.")
		};
		for (const FString& P : KnownPrefixes)
			if (Source.StartsWith(P, ESearchCase::IgnoreCase)) return Source;
		return FString::Printf(TEXT("User.%s"), *Source);
	}
}

void HandleBindNiagaraModuleInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, ModuleName, PropertyName, SourceParameter, ScriptSection, EventName;
	Args->TryGetStringField(TEXT("system_path"),      SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"),     EmitterName);
	Args->TryGetStringField(TEXT("module_name"),      ModuleName);
	Args->TryGetStringField(TEXT("parameter_name"),   PropertyName);
	Args->TryGetStringField(TEXT("source_parameter"), SourceParameter);
	Args->TryGetStringField(TEXT("script_section"),   ScriptSection);
	Args->TryGetStringField(TEXT("event_name"),       EventName);

	if (SourceParameter.IsEmpty()) { OutError = TEXT("source_parameter is required"); return; }

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { OutError = FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath); return; }

	UNiagaraStatelessEmitter* StatelessEmitter = FindStatelessEmitterFromHandle(System, EmitterName);
	if (!StatelessEmitter)
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
		if (!Handle)
		{
			OutError = FString::Printf(TEXT("Emitter '%s' not found on '%s'"), *EmitterName, *SystemPath);
			return;
		}
		FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
		if (!EmitterData)
		{
			OutError = FString::Printf(TEXT("Emitter '%s' has no data — asset may be corrupt"), *EmitterName);
			return;
		}

		const FString SectionLower = ScriptSection.ToLower();
		FString ResolveErr;
		UNiagaraScript* Script = ResolveClassicEmitterScript(EmitterData, EmitterName, ScriptSection, EventName, ResolveErr);
		if (!Script)
		{
			if (ResolveErr.IsEmpty()) ResolveErr = FString::Printf(TEXT("No script found on emitter '%s' for section '%s'"), *EmitterName, *ScriptSection);
			OutError = ResolveErr;
			return;
		}

		UNiagaraScriptSource* ScriptSrc = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		if (!ScriptSrc || !ScriptSrc->NodeGraph)
		{
			OutError = TEXT("Script has no graph (compiled-only runtime?)");
			return;
		}
		UNiagaraGraph* Graph = ScriptSrc->NodeGraph;

		UNiagaraNodeFunctionCall* FoundCall = nullptr;
		TArray<FString> AvailableModules;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(N);
			if (!FC) continue;
			AvailableModules.Add(FC->GetFunctionName());
			if (FC->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase))
			{
				FoundCall = FC;
				break;
			}
		}
		if (!FoundCall)
		{
			OutError = FString::Printf(
				TEXT("Module '%s' not found in '%s' section. Available: %s"),
				*ModuleName, *SectionLower, *FString::Join(AvailableModules, TEXT(", ")));
			return;
		}

		TArray<FNiagaraVariable> ModuleInputs = CollectModuleInputs(FoundCall);
		FNiagaraVariable FoundInputValue;
		bool bFoundInput = false;
		TArray<FString> AvailableInputs;
		for (const FNiagaraVariable& Input : ModuleInputs)
		{
			FString Stripped = Input.GetName().ToString();
			if (Stripped.StartsWith(TEXT("Module."))) Stripped = Stripped.Mid(7);
			AvailableInputs.Add(Stripped);
			if (!bFoundInput && Stripped.Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				FoundInputValue = Input;
				bFoundInput = true;
			}
		}
		if (!bFoundInput)
		{
			OutError = FString::Printf(
				TEXT("Input '%s' not found on module '%s'. Available inputs: %s"),
				*PropertyName, *ModuleName, *FString::Join(AvailableInputs, TEXT(", ")));
			return;
		}
		FNiagaraVariable* FoundInput = &FoundInputValue;

		const FNiagaraParameterHandle InputHandle{FoundInput->GetName()};
		const FNiagaraParameterHandle AliasedInputHandle =
			FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, FoundCall);

		Graph->Modify();
		FoundCall->Modify();

		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*FoundCall, AliasedInputHandle, FoundInput->GetType(), FGuid(), FGuid());

		if (OverridePin.LinkedTo.Num() > 0)
		{
			TArray<UEdGraphPin*> LinkedCopy = OverridePin.LinkedTo;
			for (UEdGraphPin* Linked : LinkedCopy)
			{
				if (!Linked) continue;
				UEdGraphNode* OwningNode = Linked->GetOwningNode();
				if (OwningNode)
				{
					OwningNode->Modify();
					OwningNode->BreakAllNodeLinks();
					Graph->RemoveNode(OwningNode);
				}
			}
		}

		FString CanonicalSource = SourceParameter;
		bool bHasNamespace = false;
		for (const TCHAR* NS : { TEXT("User."), TEXT("Engine."), TEXT("Particles."),
			TEXT("Emitter."), TEXT("System."), TEXT("Module."), TEXT("Local."),
			TEXT("NPC."), TEXT("MAP."), TEXT("Output."), TEXT("Constant.") })
		{
			if (CanonicalSource.StartsWith(NS, ESearchCase::IgnoreCase)) { bHasNamespace = true; break; }
		}
		if (!bHasNamespace) CanonicalSource = FString::Printf(TEXT("Particles.%s"), *SourceParameter);

		const FName LinkedFName(*CanonicalSource);
		const FNiagaraParameterHandle LinkedHandle{LinkedFName};

		bool bAutoCreated = false;
		if (LinkedHandle.IsUserHandle())
		{
			FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
			TArray<FNiagaraVariable> Existing;
			Store.GetUserParameters(Existing);
			bool bExists = false;
			for (const FNiagaraVariable& V : Existing)
			{
				if (V.GetName() == FName(*CanonicalSource)) { bExists = true; break; }
			}
			if (!bExists)
			{
				FNiagaraVariable NewVar(FoundInput->GetType(), FName(*CanonicalSource));
				Store.AddParameter(NewVar, true, true);
				bAutoCreated = true;
			}
		}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		const FNiagaraVariableBase LinkedVar(FoundInput->GetType(), LinkedFName);
		TSet<FNiagaraVariableBase> KnownParams;
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(
			OverridePin, LinkedVar, KnownParams,
			ENiagaraDefaultMode::Value, FGuid());
#else
		TSet<FNiagaraVariable> KnownParams;
		FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(
			OverridePin, LinkedHandle, KnownParams,
			ENiagaraDefaultMode::Value, FGuid());
#endif

		Graph->NotifyGraphChanged();
		Script->MarkPackageDirty();
		System->MarkPackageDirty();

		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"emitter\":\"%s\",\"emitter_type\":\"classic\",\"section\":\"%s\","
				 "\"module\":\"%s\",\"input\":\"%s\",\"source\":\"%s\",\"binding_type\":\"%s\",\"auto_created\":%s}"),
			*EmitterName,
			*(SectionLower.IsEmpty() ? FString(TEXT("particle_spawn")) : SectionLower),
			*ModuleName, *PropertyName, *CanonicalSource,
			*FoundInput->GetType().GetName(),
			bAutoCreated ? TEXT("true") : TEXT("false"));
		return;
	}

	UObject* FoundModule = nullptr;
	FStructProperty* FoundProp = nullptr;
	if (!FindStatelessDistributionProperty(StatelessEmitter, ModuleName, PropertyName,
		FoundModule, FoundProp, OutError))
		return;

	void* DistPtr = FoundProp->ContainerPtrToValuePtr<void>(FoundModule);
	FNiagaraDistributionBase* DistBase = (FNiagaraDistributionBase*)DistPtr;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 7
	if (!DistBase->AllowBinding())
	{
		OutError = FString::Printf(
			TEXT("Distribution '%s' on '%s' does not allow parameter binding (AllowBinding() returned false). "
				 "Some distributions are constant-only by design."),
			*PropertyName, *ModuleName);
		return;
	}
#endif

	const FNiagaraTypeDefinition BindingType = DistBase->GetBindingTypeDef();
	if (!BindingType.IsValid())
	{
		OutError = FString::Printf(
			TEXT("Distribution '%s' on '%s' has no binding type definition — cannot determine what type the source parameter should be."),
			*PropertyName, *ModuleName);
		return;
	}

	const FString CanonicalSource = CanonicalizeBindingSourceName(SourceParameter);
	const FName SourceFName(*CanonicalSource);

	const bool bIsUserParam = CanonicalSource.StartsWith(TEXT("User."), ESearchCase::IgnoreCase);
	bool bAutoCreated = false;
	if (bIsUserParam)
	{
		FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
		TArray<FNiagaraVariable> Existing;
		Store.GetUserParameters(Existing);
		bool bExists = false;
		for (const FNiagaraVariable& V : Existing)
		{
			if (V.GetName() == SourceFName) { bExists = true; break; }
		}
		if (!bExists)
		{
			FNiagaraVariable NewVar(BindingType, SourceFName);
			Store.AddParameter(NewVar, true, true);
			bAutoCreated = true;
		}
	}

	DistBase->Mode = ENiagaraDistributionMode::Binding;
	DistBase->ParameterBinding = FNiagaraVariableBase(BindingType, SourceFName);

#if WITH_EDITORONLY_DATA
	DistBase->UpdateValuesFromDistribution();
#endif

	System->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"emitter\":\"%s\",\"module\":\"%s\",\"parameter\":\"%s\","
			 "\"source\":\"%s\",\"binding_type\":\"%s\",\"auto_created\":%s}"),
		*EmitterName, *ModuleName, *PropertyName, *CanonicalSource,
		*BindingType.GetName(),
		bAutoCreated ? TEXT("true") : TEXT("false"));
}

void HandleSetNiagaraRendererBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, BindingName, SourceParameter;
	int32 RendererIndex = 0;
	double TmpIdx = 0.0;
	Args->TryGetStringField(TEXT("system_path"),      SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"),     EmitterName);
	Args->TryGetStringField(TEXT("binding_name"),     BindingName);
	Args->TryGetStringField(TEXT("source_parameter"), SourceParameter);
	if (Args->TryGetNumberField(TEXT("renderer_index"), TmpIdx)) RendererIndex = (int32)TmpIdx;

	if (BindingName.IsEmpty())     { OutError = TEXT("binding_name is required (e.g. ColorBinding, PositionBinding, SpriteSizeBinding)"); return; }
	if (SourceParameter.IsEmpty()) { OutError = TEXT("source_parameter is required (e.g. Particles.Color, User.MyTint)"); return; }

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { OutError = FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath); return; }

	TArray<UNiagaraRendererProperties*> Renderers = CollectRenderers(System, EmitterName);
	if (!Renderers.IsValidIndex(RendererIndex))
	{
		OutError = FString::Printf(
			TEXT("Renderer index %d out of range. Found %d renderer(s) on emitter '%s'."),
			RendererIndex, Renderers.Num(), *EmitterName);
		return;
	}
	UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];

	auto FindBindingProp = [Renderer](const FString& Name) -> FStructProperty*
	{
		const FName Target(*Name);
		for (TFieldIterator<FProperty> It(Renderer->GetClass()); It; ++It)
		{
			FStructProperty* SP = CastField<FStructProperty>(*It);
			if (!SP || SP->Struct != FNiagaraVariableAttributeBinding::StaticStruct()) continue;
			if (SP->GetName().Equals(Name, ESearchCase::IgnoreCase)) return SP;
		}
		return nullptr;
	};

	FStructProperty* BindingProp = FindBindingProp(BindingName);
	if (!BindingProp && !BindingName.EndsWith(TEXT("Binding"), ESearchCase::IgnoreCase))
	{
		BindingProp = FindBindingProp(BindingName + TEXT("Binding"));
	}
	if (!BindingProp)
	{
		TArray<FString> Available;
		for (TFieldIterator<FProperty> It(Renderer->GetClass()); It; ++It)
		{
			FStructProperty* SP = CastField<FStructProperty>(*It);
			if (SP && SP->Struct == FNiagaraVariableAttributeBinding::StaticStruct())
				Available.Add(SP->GetName());
		}
		OutError = FString::Printf(
			TEXT("Binding '%s' not found on %s. Available bindings: %s"),
			*BindingName, *Renderer->GetClass()->GetName(),
			Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("(none)"));
		return;
	}

	void* BindingPtr = BindingProp->ContainerPtrToValuePtr<void>(Renderer);
	FNiagaraVariableAttributeBinding* Binding = (FNiagaraVariableAttributeBinding*)BindingPtr;

	const FString OldName = Binding->GetParamMapBindableVariable().GetName().ToString();

	FString CanonicalSource = SourceParameter;
	bool bHasNamespace = false;
	for (const TCHAR* NS : {TEXT("Particles."), TEXT("User."), TEXT("Emitter."), TEXT("System."), TEXT("Engine.")})
	{
		if (CanonicalSource.StartsWith(NS, ESearchCase::IgnoreCase)) { bHasNamespace = true; break; }
	}
	if (!bHasNamespace) CanonicalSource = FString::Printf(TEXT("Particles.%s"), *SourceParameter);

	Renderer->Modify();
#if UE_VERSION_OLDER_THAN(5,7,0)
	Binding->SetValue(FName(*CanonicalSource), FVersionedNiagaraEmitter(), ENiagaraRendererSourceDataMode::Particles);
#else
	Binding->SetValue(FName(*CanonicalSource), FVersionedNiagaraEmitterBase(), ENiagaraRendererSourceDataMode::Particles);
#endif

	System->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"emitter\":\"%s\",\"renderer\":\"%s\",\"renderer_index\":%d,"
			 "\"binding\":\"%s\",\"old_source\":\"%s\",\"source\":\"%s\"}"),
		*EmitterName, *Renderer->GetClass()->GetName(), RendererIndex,
		*BindingProp->GetName(), *OldName, *CanonicalSource);
}

void HandleUnbindNiagaraModuleInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, ModuleName, PropertyName, ScriptSection, EventName;
	Args->TryGetStringField(TEXT("system_path"),    SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"),   EmitterName);
	Args->TryGetStringField(TEXT("module_name"),    ModuleName);
	Args->TryGetStringField(TEXT("parameter_name"), PropertyName);
	Args->TryGetStringField(TEXT("script_section"), ScriptSection);
	Args->TryGetStringField(TEXT("event_name"),     EventName);

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { OutError = FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath); return; }

	UNiagaraStatelessEmitter* StatelessEmitter = FindStatelessEmitterFromHandle(System, EmitterName);
	if (!StatelessEmitter)
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
		if (!Handle)
		{
			OutError = FString::Printf(TEXT("Emitter '%s' not found on '%s'"), *EmitterName, *SystemPath);
			return;
		}
		FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
		if (!EmitterData)
		{
			OutError = FString::Printf(TEXT("Emitter '%s' has no data — asset may be corrupt"), *EmitterName);
			return;
		}

		const FString SectionLower = ScriptSection.ToLower();
		FString ResolveErr;
		UNiagaraScript* Script = ResolveClassicEmitterScript(EmitterData, EmitterName, ScriptSection, EventName, ResolveErr);
		if (!Script)
		{
			if (ResolveErr.IsEmpty()) ResolveErr = FString::Printf(TEXT("No script found on emitter '%s' for section '%s'"), *EmitterName, *ScriptSection);
			OutError = ResolveErr;
			return;
		}

		UNiagaraScriptSource* ScriptSrc = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		if (!ScriptSrc || !ScriptSrc->NodeGraph)
		{
			OutError = TEXT("Script has no graph (compiled-only runtime?)");
			return;
		}
		UNiagaraGraph* Graph = ScriptSrc->NodeGraph;

		UNiagaraNodeFunctionCall* FoundCall = nullptr;
		TArray<FString> AvailableModules;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			UNiagaraNodeFunctionCall* FC = Cast<UNiagaraNodeFunctionCall>(N);
			if (!FC) continue;
			AvailableModules.Add(FC->GetFunctionName());
			if (FC->GetFunctionName().Equals(ModuleName, ESearchCase::IgnoreCase))
			{
				FoundCall = FC;
				break;
			}
		}
		if (!FoundCall)
		{
			OutError = FString::Printf(
				TEXT("Module '%s' not found in '%s' section. Available: %s"),
				*ModuleName, *SectionLower, *FString::Join(AvailableModules, TEXT(", ")));
			return;
		}

		TArray<FNiagaraVariable> ModuleInputs = CollectModuleInputs(FoundCall);
		FNiagaraVariable FoundInputValue;
		bool bFoundInput = false;
		TArray<FString> AvailableInputs;
		for (const FNiagaraVariable& Input : ModuleInputs)
		{
			FString Stripped = Input.GetName().ToString();
			if (Stripped.StartsWith(TEXT("Module."))) Stripped = Stripped.Mid(7);
			AvailableInputs.Add(Stripped);
			if (!bFoundInput && Stripped.Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				FoundInputValue = Input;
				bFoundInput = true;
			}
		}
		if (!bFoundInput)
		{
			OutError = FString::Printf(
				TEXT("Input '%s' not found on module '%s'. Available inputs: %s"),
				*PropertyName, *ModuleName, *FString::Join(AvailableInputs, TEXT(", ")));
			return;
		}
		FNiagaraVariable* FoundInput = &FoundInputValue;

		const FNiagaraParameterHandle InputHandle{FoundInput->GetName()};
		const FNiagaraParameterHandle AliasedInputHandle =
			FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, FoundCall);

		Graph->Modify();
		FoundCall->Modify();

		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*FoundCall, AliasedInputHandle, FoundInput->GetType(), FGuid(), FGuid());

		if (OverridePin.LinkedTo.Num() == 0)
		{
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"emitter\":\"%s\",\"emitter_type\":\"classic\",\"section\":\"%s\","
					 "\"module\":\"%s\",\"parameter\":\"%s\",\"already_unbound\":true}"),
				*EmitterName,
				*(SectionLower.IsEmpty() ? FString(TEXT("particle_spawn")) : SectionLower),
				*ModuleName, *PropertyName);
			return;
		}

		FString OldSource;
		TArray<UEdGraphPin*> LinkedCopy = OverridePin.LinkedTo;
		for (UEdGraphPin* Linked : LinkedCopy)
		{
			if (!Linked) continue;
			UEdGraphNode* OwningNode = Linked->GetOwningNode();
			if (OwningNode)
			{
				if (OldSource.IsEmpty())
				{
					OldSource = Linked->PinName.ToString();
				}
				OwningNode->Modify();
				OwningNode->BreakAllNodeLinks();
				Graph->RemoveNode(OwningNode);
			}
		}

		Graph->NotifyGraphChanged();
		Script->MarkPackageDirty();
		System->MarkPackageDirty();

		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"emitter\":\"%s\",\"emitter_type\":\"classic\",\"section\":\"%s\","
				 "\"module\":\"%s\",\"parameter\":\"%s\",\"old_source\":\"%s\",\"reverted_to\":\"ModuleDefault\"}"),
			*EmitterName,
			*(SectionLower.IsEmpty() ? FString(TEXT("particle_spawn")) : SectionLower),
			*ModuleName, *PropertyName, *OldSource);
		return;
	}

	UObject* FoundModule = nullptr;
	FStructProperty* FoundProp = nullptr;
	if (!FindStatelessDistributionProperty(StatelessEmitter, ModuleName, PropertyName,
		FoundModule, FoundProp, OutError))
		return;

	void* DistPtr = FoundProp->ContainerPtrToValuePtr<void>(FoundModule);
	FNiagaraDistributionBase* DistBase = (FNiagaraDistributionBase*)DistPtr;

	if (DistBase->Mode != ENiagaraDistributionMode::Binding)
	{
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"emitter\":\"%s\",\"module\":\"%s\",\"parameter\":\"%s\",\"already_unbound\":true}"),
			*EmitterName, *ModuleName, *PropertyName);
		return;
	}

	const FString OldSource = DistBase->ParameterBinding.GetName().ToString();
	DistBase->Mode = ENiagaraDistributionMode::UniformConstant;
	DistBase->ParameterBinding = FNiagaraVariableBase();

#if WITH_EDITORONLY_DATA
	DistBase->UpdateValuesFromDistribution();
#endif

	System->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"emitter\":\"%s\",\"module\":\"%s\",\"parameter\":\"%s\",\"old_source\":\"%s\",\"reverted_to\":\"UniformConstant\"}"),
		*EmitterName, *ModuleName, *PropertyName, *OldSource);
}

namespace
{
	bool ResolveScratchPinType(const FString& Raw, FNiagaraTypeDefinition& Out)
	{
		const FString T = Raw.ToLower().TrimStartAndEnd();
		if (T == TEXT("float")  || T == TEXT("scalar") || T == TEXT("real"))                          { Out = FNiagaraTypeDefinition::GetFloatDef(); return true; }
		if (T == TEXT("int")    || T == TEXT("int32") || T == TEXT("integer"))                        { Out = FNiagaraTypeDefinition::GetIntDef();   return true; }
		if (T == TEXT("bool")   || T == TEXT("boolean"))                                              { Out = FNiagaraTypeDefinition::GetBoolDef();  return true; }
		if (T == TEXT("vec2")   || T == TEXT("vector2") || T == TEXT("vector2d") || T == TEXT("v2"))  { Out = FNiagaraTypeDefinition::GetVec2Def();  return true; }
		if (T == TEXT("vec3")   || T == TEXT("vector")  || T == TEXT("vector3")  || T == TEXT("v3"))  { Out = FNiagaraTypeDefinition::GetVec3Def();  return true; }
		if (T == TEXT("vec4")   || T == TEXT("vector4") || T == TEXT("v4"))                           { Out = FNiagaraTypeDefinition::GetVec4Def();  return true; }
		if (T == TEXT("color")  || T == TEXT("linearcolor") || T == TEXT("rgba"))                     { Out = FNiagaraTypeDefinition::GetColorDef(); return true; }
		if (T == TEXT("quat")   || T == TEXT("quaternion") || T == TEXT("rotation"))                  { Out = FNiagaraTypeDefinition::GetQuatDef();  return true; }
		return false;
	}

	UClass* FindCustomHlslClass()
	{
		return FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeCustomHlsl"));
	}
}

void HandleAddNiagaraScratchModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString SystemPath, EmitterName, Section, ModuleName, HlslCode, EventName;
	Args->TryGetStringField(TEXT("system_path"),    SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"),   EmitterName);
	Args->TryGetStringField(TEXT("script_section"), Section);
	Args->TryGetStringField(TEXT("name"),           ModuleName);
	Args->TryGetStringField(TEXT("hlsl_code"),      HlslCode);
	Args->TryGetStringField(TEXT("event_name"),     EventName);

	if (ModuleName.IsEmpty())
	{
		OutError = TEXT("`name` is required (used for the module's display name and signature).");
		return;
	}
	if (HlslCode.IsEmpty())
	{
		OutError = TEXT("`hlsl_code` is required (the HLSL body — reference your input/output pin names directly, e.g. `OutResult = InVelocity * InStrength;`).");
		return;
	}

	UClass* CustomHlslClass = FindCustomHlslClass();
	if (!CustomHlslClass)
	{
		OutError = TEXT("Could not resolve UNiagaraNodeCustomHlsl class — is the NiagaraEditor module loaded?");
		return;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System) { OutError = FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath); return; }

	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (!Handle)
	{
		OutError = FString::Printf(TEXT("Emitter '%s' not found on '%s'"), *EmitterName, *SystemPath);
		return;
	}
	FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
	if (!EmitterData)
	{
		OutError = FString::Printf(TEXT("Emitter '%s' has no data — is it stateless? Custom HLSL is classic-only."), *EmitterName);
		return;
	}

	const FString SectionLower = Section.ToLower();
	UNiagaraScript* Script = nullptr;
	ENiagaraScriptUsage TargetUsage = ENiagaraScriptUsage::ParticleSpawnScript;
	FGuid TargetUsageId;

	if (SectionLower.IsEmpty() || SectionLower == TEXT("particle_spawn") || SectionLower == TEXT("particlespawn") || SectionLower == TEXT("spawn"))
	{
		Script = EmitterData->SpawnScriptProps.Script;  TargetUsage = ENiagaraScriptUsage::ParticleSpawnScript;
	}
	else if (SectionLower == TEXT("particle_update") || SectionLower == TEXT("particleupdate") || SectionLower == TEXT("update"))
	{
		Script = EmitterData->UpdateScriptProps.Script; TargetUsage = ENiagaraScriptUsage::ParticleUpdateScript;
	}
	else if (SectionLower == TEXT("emitter_spawn") || SectionLower == TEXT("emitterspawn"))
	{
		Script = EmitterData->EmitterSpawnScriptProps.Script; TargetUsage = ENiagaraScriptUsage::EmitterSpawnScript;
	}
	else if (SectionLower == TEXT("emitter_update") || SectionLower == TEXT("emitterupdate"))
	{
		Script = EmitterData->EmitterUpdateScriptProps.Script; TargetUsage = ENiagaraScriptUsage::EmitterUpdateScript;
	}
	else if (SectionLower == TEXT("event_handler") || SectionLower == TEXT("event"))
	{
		const TArray<FNiagaraEventScriptProperties>& Handlers = EmitterData->GetEventHandlers();
		if (Handlers.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Emitter '%s' has no event handlers. Call add_niagara_event_handler first."), *EmitterName); return;
		}
		const FNiagaraEventScriptProperties* Match = nullptr;
		if (!EventName.IsEmpty())
		{
			for (const FNiagaraEventScriptProperties& H : Handlers)
				if (H.SourceEventName.ToString().Equals(EventName, ESearchCase::IgnoreCase)) { Match = &H; break; }
			if (!Match)
			{
				TArray<FString> EvNames;
				for (const FNiagaraEventScriptProperties& H : Handlers) EvNames.Add(H.SourceEventName.ToString());
				OutError = FString::Printf(TEXT("Event handler '%s' not found. Available: %s"), *EventName, *FString::Join(EvNames, TEXT(", "))); return;
			}
		}
		else if (Handlers.Num() == 1) Match = &Handlers[0];
		else
		{
			TArray<FString> EvNames;
			for (const FNiagaraEventScriptProperties& H : Handlers) EvNames.Add(H.SourceEventName.ToString());
			OutError = FString::Printf(TEXT("%d event handlers on '%s' — pass event_name. Available: %s"),
				Handlers.Num(), *EmitterName, *FString::Join(EvNames, TEXT(", "))); return;
		}
		Script = Match->Script;
		TargetUsage = ENiagaraScriptUsage::ParticleEventScript;
		if (Script) TargetUsageId = Script->GetUsageId();
	}
	else
	{
		OutError = FString::Printf(TEXT("script_section '%s' not recognised. Valid: particle_spawn, particle_update, emitter_spawn, emitter_update, event_handler."), *Section);
		return;
	}

	if (!Script) { OutError = TEXT("Target script is null — emitter may be in a stale state."); return; }

	UNiagaraScriptSource* ScriptSrc = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	if (!ScriptSrc || !ScriptSrc->NodeGraph) { OutError = TEXT("Script has no editable graph (compiled-only?)"); return; }
	UNiagaraGraph* Graph = ScriptSrc->NodeGraph;

	UNiagaraNodeOutput* OutputNode = nullptr;
	if (TargetUsage == ENiagaraScriptUsage::ParticleEventScript && TargetUsageId.IsValid())
	{
		OutputNode = Graph->FindEquivalentOutputNode(TargetUsage, TargetUsageId);
	}
	else
	{
		TArray<UNiagaraNodeOutput*> Outs;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(Outs);
		for (UNiagaraNodeOutput* O : Outs)
			if (O->GetUsage() == TargetUsage) { OutputNode = O; break; }
	}
	if (!OutputNode) { OutError = FString::Printf(TEXT("No Output node found for section '%s'."), *SectionLower); return; }

	struct FPlannedPin { FString Name; FNiagaraTypeDefinition Type; };
	TArray<FPlannedPin> InputPlan;
	TArray<FPlannedPin> OutputPlan;
	auto BuildPlan = [](const TArray<TSharedPtr<FJsonValue>>& Items, TArray<FPlannedPin>& Plan, const TCHAR* Side, FString& Err) -> bool
	{
		for (int32 i = 0; i < Items.Num(); i++)
		{
			TSharedPtr<FJsonObject> Obj = Items[i]->AsObject();
			if (!Obj.IsValid()) { Err = FString::Printf(TEXT("%s[%d] is not an object."), Side, i); return false; }
			FString Name, TypeStr;
			Obj->TryGetStringField(TEXT("name"), Name);
			Obj->TryGetStringField(TEXT("type"), TypeStr);
			if (Name.IsEmpty()) { Err = FString::Printf(TEXT("%s[%d].name is required."), Side, i); return false; }
			if (TypeStr.IsEmpty()) { Err = FString::Printf(TEXT("%s[%d].type is required."), Side, i); return false; }
			FNiagaraTypeDefinition Resolved;
			if (!ResolveScratchPinType(TypeStr, Resolved))
			{
				Err = FString::Printf(TEXT("%s[%d].type '%s' not supported. Valid: float, int, bool, vec2, vec3, vec4, color, quat."), Side, i, *TypeStr);
				return false;
			}
			Plan.Add({Name, Resolved});
		}
		return true;
	};
	const TArray<TSharedPtr<FJsonValue>>* InputsArr = nullptr;
	if (Args->TryGetArrayField(TEXT("inputs"), InputsArr))
	{
		FString Err;
		if (!BuildPlan(*InputsArr, InputPlan, TEXT("inputs"), Err)) { OutError = Err; return; }
	}
	const TArray<TSharedPtr<FJsonValue>>* OutputsArr = nullptr;
	if (Args->TryGetArrayField(TEXT("outputs"), OutputsArr))
	{
		FString Err;
		if (!BuildPlan(*OutputsArr, OutputPlan, TEXT("outputs"), Err)) { OutError = Err; return; }
	}

	System->Modify();
	Graph->Modify();
	UEdGraphNode* RawNode = NewObject<UEdGraphNode>(Graph, CustomHlslClass);
	UNiagaraNodeFunctionCall* HlslNode = Cast<UNiagaraNodeFunctionCall>(RawNode);
	if (!HlslNode) { OutError = TEXT("CustomHlsl class did not derive from UNiagaraNodeFunctionCall as expected — engine version mismatch?"); return; }
	HlslNode->NodeGuid = FGuid::NewGuid();

	if (FProperty* UsageProp = CustomHlslClass->FindPropertyByName(TEXT("ScriptUsage")))
	{
		if (FByteProperty* BP = CastField<FByteProperty>(UsageProp))
			BP->SetIntPropertyValue(BP->ContainerPtrToValuePtr<void>(HlslNode), (int64)TargetUsage);
		else if (FEnumProperty* EP = CastField<FEnumProperty>(UsageProp))
			EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(HlslNode), (int64)TargetUsage);
	}

	HlslNode->Signature.Name = FName(*ModuleName);
	if (FStrProperty* DisplayProp = CastField<FStrProperty>(HlslNode->GetClass()->FindPropertyByName(TEXT("FunctionDisplayName"))))
	{
		DisplayProp->SetPropertyValue(DisplayProp->ContainerPtrToValuePtr<void>(HlslNode), ModuleName);
	}

	Graph->AddNode(HlslNode, false);
	const FEdGraphPinType ParamMapPinType =
		UEdGraphSchema_Niagara::TypeDefinitionToPinType(FNiagaraTypeDefinition::GetParameterMapDef());
	HlslNode->CreatePin(EGPD_Input,  ParamMapPinType, TEXT("InputMap"));
	HlslNode->CreatePin(EGPD_Output, ParamMapPinType, TEXT("OutputMap"));

	const UEdGraphSchema_Niagara* NSchema = GetDefault<UEdGraphSchema_Niagara>();
	for (const FPlannedPin& P : InputPlan)
	{
		FEdGraphPinType PT = UEdGraphSchema_Niagara::TypeDefinitionToPinType(P.Type);
		HlslNode->CreatePin(EGPD_Input, PT, FName(*P.Name));
	}
	for (const FPlannedPin& P : OutputPlan)
	{
		FEdGraphPinType PT = UEdGraphSchema_Niagara::TypeDefinitionToPinType(P.Type);
		HlslNode->CreatePin(EGPD_Output, PT, FName(*P.Name));
	}

	{
		FEdGraphPinType AddPinType;
		AddPinType.PinCategory    = UEdGraphSchema_Niagara::PinCategoryMisc;
		AddPinType.PinSubCategory = TEXT("DynamicAddPin");

		UEdGraphPin* AddIn  = HlslNode->CreatePin(EGPD_Input,  AddPinType, TEXT("Add"));
		if (AddIn)  { AddIn->bNotConnectable  = true; }
		UEdGraphPin* AddOut = HlslNode->CreatePin(EGPD_Output, AddPinType, TEXT("Add"));
		if (AddOut) { AddOut->bNotConnectable = true; }
	}

	if (FStrProperty* HlslProp = CastField<FStrProperty>(CustomHlslClass->FindPropertyByName(TEXT("CustomHlsl"))))
	{
		HlslProp->SetPropertyValue(HlslProp->ContainerPtrToValuePtr<void>(HlslNode), HlslCode);
	}

	auto LooksLikeAddPin = [](const UEdGraphPin* Pin)
	{
		return Pin
			&& Pin->PinType.PinCategory    == UEdGraphSchema_Niagara::PinCategoryMisc
			&& Pin->PinType.PinSubCategory == FName(TEXT("DynamicAddPin"));
	};
	{
		FNiagaraFunctionSignature Sig = HlslNode->Signature;
		Sig.Inputs.Empty();
		Sig.Outputs.Empty();
		for (UEdGraphPin* Pin : HlslNode->Pins)
		{
			if (!Pin || LooksLikeAddPin(Pin)) continue;
			if (Pin->Direction == EGPD_Input)  Sig.Inputs.Add(UEdGraphSchema_Niagara::PinToNiagaraVariable(Pin, true));
			else                                Sig.Outputs.Add(UEdGraphSchema_Niagara::PinToNiagaraVariable(Pin, false));
		}
		HlslNode->Signature = Sig;
	}

	HlslNode->NodePosX = OutputNode->NodePosX - 350;
	HlslNode->NodePosY = OutputNode->NodePosY - 60;

	UEdGraphPin* OutputInputPin = FindNiagaraParameterMapPin(OutputNode, EGPD_Input);
	bool bWired = false;
	FString WireDiag;
	if (!OutputInputPin)
	{
		WireDiag = TEXT("output_node_param_map_pin_missing");
	}
	else
	{
		UEdGraphPin* MyMapIn  = FindNiagaraParameterMapPin(HlslNode, EGPD_Input);
		UEdGraphPin* MyMapOut = FindNiagaraParameterMapPin(HlslNode, EGPD_Output);
		if (!MyMapIn || !MyMapOut)
		{
			WireDiag = FString::Printf(TEXT("hlsl_node_param_map_pins_missing (in=%s out=%s)"),
				MyMapIn ? TEXT("ok") : TEXT("null"),
				MyMapOut ? TEXT("ok") : TEXT("null"));
		}
		else
		{
			if (OutputInputPin->LinkedTo.Num() > 0)
			{
				UEdGraphPin* PrevPin = OutputInputPin->LinkedTo[0];
				OutputInputPin->BreakLinkTo(PrevPin);
				PrevPin->MakeLinkTo(MyMapIn);
			}
			MyMapOut->MakeLinkTo(OutputInputPin);
			bWired = true;
		}
	}

	Graph->NotifyGraphChanged();
	Script->MarkPackageDirty();
	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("emitter"), Handle->GetName().ToString());
	Resp->SetStringField(TEXT("section"), SectionLower.IsEmpty() ? FString(TEXT("particle_spawn")) : SectionLower);
	Resp->SetStringField(TEXT("module_name"), ModuleName);
	Resp->SetNumberField(TEXT("input_count"), InputPlan.Num());
	Resp->SetNumberField(TEXT("output_count"), OutputPlan.Num());
	Resp->SetBoolField(TEXT("wired_into_chain"), bWired);
	if (!bWired && !WireDiag.IsEmpty())
	{
		Resp->SetStringField(TEXT("wire_diagnostic"), WireDiag);
	}
	Resp->SetStringField(TEXT("note"),
		TEXT("Custom HLSL scratch module added. Reference each declared input/output by its `name` directly in your HLSL body — the translator substitutes them in. "
		     "If compile fails, read compile_errors[] and either edit the HLSL or call remove_niagara_module to remove this scratch and try again."));

	if (bWired)
	{
		CompileAndReport(System, Resp);
	}
	else
	{
		Resp->SetStringField(TEXT("compile_status"), TEXT("skipped_wiring_failed"));
		Resp->SetStringField(TEXT("recovery_hint"),
			TEXT("Wiring into the parameter-map exec chain failed (see wire_diagnostic). The compile + traversal cycle has been SKIPPED to avoid an editor-side crash inside UNiagaraNodeCustomHlsl::BuildParameterMapHistory. The scratch node sits in the graph orphaned. Either: (a) call remove_niagara_module(module_name=<this>) to drop it, then retry once the scratch helper is fixed; (b) open the system in the Niagara editor and wire the param-map pins manually; or (c) accept the orphan node as a noop and move on. The system is NOT corrupted — only the scratch node itself."));
	}

	BuildSuccessJson(Resp, OutJsonString);
}

void HandleGetNiagaraRuntimeState(const FString& SystemPath, FString& OutJsonString, FString& OutError)
{
	UNiagaraSystem* Target = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!Target) { SetError(FString::Printf(TEXT("Could not load NiagaraSystem: %s"), *SystemPath), OutJsonString, OutError); return; }

	UNiagaraComponent* FoundComp = nullptr;
	int32 ComponentCount = 0;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Comp = *It;
		if (!Comp || Comp->IsTemplate() || Comp->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Comp->GetAsset() != Target) continue;
		ComponentCount++;
		if (!FoundComp && Comp->GetSystemInstanceController().IsValid())
		{
			FoundComp = Comp;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("system_path"), Target->GetPathName());
	Result->SetNumberField(TEXT("active_component_count"), ComponentCount);

	if (!FoundComp)
	{
		if (ComponentCount > 0)
		{
			Result->SetStringField(TEXT("note"),
				FString::Printf(TEXT("Found %d UNiagaraComponent(s) for this system but none have an initialized SystemInstanceController yet (likely a timing issue — asset was just opened or recompiled). Retry in a moment, or trigger Activate on the component."), ComponentCount));
		}
		else
		{
			Result->SetStringField(TEXT("note"),
				TEXT("No active UNiagaraComponent found for this system. Either open the asset in the Niagara System editor (its preview viewport spawns a NiagaraComponent), or drop the system into a level actor."));
		}
		BuildSuccessJson(Result, OutJsonString);
		return;
	}

	if (AActor* Owner = FoundComp->GetOwner())
	{
		Result->SetStringField(TEXT("component_actor_name"), Owner->GetActorNameOrLabel());
		Result->SetStringField(TEXT("component_world"), Owner->GetWorld() ? Owner->GetWorld()->GetName() : FString());
	}
	else
	{
		Result->SetStringField(TEXT("component_actor_name"), TEXT("(preview/standalone — no AActor owner)"));
	}

	Result->SetBoolField(TEXT("is_active"), FoundComp->IsActive());
	Result->SetBoolField(TEXT("is_paused"), FoundComp->IsPaused());

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FNiagaraSystemInstance* SystemInst = FoundComp->GetSystemInstance();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!SystemInst)
	{
		Result->SetStringField(TEXT("note"), TEXT("Component is registered for the system but has no live FNiagaraSystemInstance yet (compile pending, deferred init, or asset was just opened). Re-call after the editor finishes initializing."));
		BuildSuccessJson(Result, OutJsonString);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> EmittersJson;
	for (const FNiagaraEmitterInstanceRef& EmRef : SystemInst->GetEmitters())
	{
		const FNiagaraEmitterInstance& EmInst = *EmRef;
		TSharedPtr<FJsonObject> EmObj = MakeShareable(new FJsonObject);
		EmObj->SetStringField(TEXT("name"), EmInst.GetEmitterHandle().GetName().ToString());
		EmObj->SetNumberField(TEXT("total_spawned_particles"), EmInst.GetTotalSpawnedParticles());

		const UEnum* StateEnum = StaticEnum<ENiagaraExecutionState>();
		if (StateEnum)
		{
			EmObj->SetStringField(TEXT("execution_state"),
				StateEnum->GetNameStringByValue((int64)EmInst.GetExecutionState()));
		}
		else
		{
			EmObj->SetNumberField(TEXT("execution_state"), (int32)EmInst.GetExecutionState());
		}

		EmittersJson.Add(MakeShareable(new FJsonValueObject(EmObj)));
	}
	Result->SetArrayField(TEXT("emitters"), EmittersJson);

	BuildSuccessJson(Result, OutJsonString);
}

void HandleGetNiagaraRuntimeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { SetError(TEXT("Invalid args"), OutJsonString, OutError); return; }
	FString SystemPath;
	if (!Args->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		SetError(TEXT("`system_path` is required"), OutJsonString, OutError);
		return;
	}
	HandleGetNiagaraRuntimeState(SystemPath, OutJsonString, OutError);
}

}
