// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/NiagaraSchemaTools.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraDataInterface.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"

#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"

namespace NiagaraSchemaTools
{

namespace
{
	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> SerializeProperty(FProperty* Prop)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Prop->GetName());
		P->SetStringField(TEXT("cpp_type"), Prop->GetCPPType());
		const FString Category = Prop->HasMetaData(TEXT("Category")) ? Prop->GetMetaData(TEXT("Category")) : FString();
		if (!Category.IsEmpty()) P->SetStringField(TEXT("category"), Category);
		const FString Tooltip = Prop->HasMetaData(TEXT("ToolTip")) ? Prop->GetMetaData(TEXT("ToolTip")) : FString();
		if (!Tooltip.IsEmpty()) P->SetStringField(TEXT("tooltip"), Tooltip);
		P->SetBoolField(TEXT("edit_anywhere"), Prop->HasAnyPropertyFlags(CPF_Edit));
		P->SetBoolField(TEXT("blueprint_visible"), Prop->HasAnyPropertyFlags(CPF_BlueprintVisible));
		P->SetBoolField(TEXT("instance_editable"), !Prop->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
		return P;
	}

	void EmitClassSchema(UClass* Class, const FString& Label, FString& OutJson)
	{
		TArray<TSharedPtr<FJsonValue>> Props;
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;
			if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly)) continue;
			Props.Add(MakeShared<FJsonValueObject>(SerializeProperty(Prop)));
		}
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		R->SetStringField(TEXT("class"), Label);
		R->SetStringField(TEXT("class_path"), Class->GetPathName());
		R->SetNumberField(TEXT("count"), Props.Num());
		R->SetArrayField(TEXT("properties"), Props);
		OutJson = SerializeJson(R);
	}

	UClass* ResolveClassByName(const FString& Spec, UClass* RequiredBase)
	{
		if (UClass* Direct = LoadObject<UClass>(nullptr, *Spec))
		{
			if (!RequiredBase || Direct->IsChildOf(RequiredBase)) return Direct;
		}
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C || C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
			if (RequiredBase && !C->IsChildOf(RequiredBase)) continue;
			if (C->GetName() == Spec || C->GetName() == FString::Printf(TEXT("U%s"), *Spec)) return C;
		}
		return nullptr;
	}

	UNiagaraSystem* LoadSystem(const FString& Path, FString& OutError)
	{
		UNiagaraSystem* Sys = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(Path));
		if (!Sys) OutError = FString::Printf(TEXT("NiagaraSystem not found at '%s'"), *Path);
		return Sys;
	}

	bool ParseScriptSection(const FString& Section, ENiagaraScriptUsage& OutUsage)
	{
		if (Section.Equals(TEXT("particle_spawn"),  ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::ParticleSpawnScript;  return true; }
		if (Section.Equals(TEXT("particle_update"), ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::ParticleUpdateScript; return true; }
		if (Section.Equals(TEXT("emitter_spawn"),   ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::EmitterSpawnScript;   return true; }
		if (Section.Equals(TEXT("emitter_update"),  ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::EmitterUpdateScript;  return true; }
		if (Section.Equals(TEXT("system_spawn"),    ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::SystemSpawnScript;    return true; }
		if (Section.Equals(TEXT("system_update"),   ESearchCase::IgnoreCase)) { OutUsage = ENiagaraScriptUsage::SystemUpdateScript;   return true; }
		return false;
	}

	bool ParseTypeName(const FString& Type, FNiagaraTypeDefinition& Out)
	{
		const FString Lower = Type.ToLower();
		if (Lower == TEXT("float"))   { Out = FNiagaraTypeDefinition::GetFloatDef();  return true; }
		if (Lower == TEXT("int")
			|| Lower == TEXT("int32")) { Out = FNiagaraTypeDefinition::GetIntDef();   return true; }
		if (Lower == TEXT("bool"))    { Out = FNiagaraTypeDefinition::GetBoolDef();   return true; }
		if (Lower == TEXT("vec2")
			|| Lower == TEXT("vector2")) { Out = FNiagaraTypeDefinition::GetVec2Def(); return true; }
		if (Lower == TEXT("vec3")
			|| Lower == TEXT("vector")
			|| Lower == TEXT("vector3")) { Out = FNiagaraTypeDefinition::GetVec3Def(); return true; }
		if (Lower == TEXT("vec4")
			|| Lower == TEXT("vector4")) { Out = FNiagaraTypeDefinition::GetVec4Def(); return true; }
		if (Lower == TEXT("color")
			|| Lower == TEXT("linearcolor")) { Out = FNiagaraTypeDefinition::GetColorDef(); return true; }
		return false;
	}

	UNiagaraNodeOutput* FindOutputForUsage(UNiagaraSystem* System, const FString& EmitterName, ENiagaraScriptUsage Usage, FString& OutError)
	{
		if (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript)
		{
			UNiagaraScript* Script = (Usage == ENiagaraScriptUsage::SystemSpawnScript)
				? System->GetSystemSpawnScript() : System->GetSystemUpdateScript();
			if (!Script) { OutError = TEXT("System script missing"); return nullptr; }
			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
			if (!Src || !Src->NodeGraph) { OutError = TEXT("System script source missing"); return nullptr; }
			return Src->NodeGraph->FindEquivalentOutputNode(Usage);
		}

		if (EmitterName.IsEmpty()) { OutError = TEXT("emitter_name required for non-system script section"); return nullptr; }
		const FName EmitterFName(*EmitterName);
		const FNiagaraEmitterHandle* TargetHandle = nullptr;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (Handle.GetName() == EmitterFName) { TargetHandle = &Handle; break; }
		}
		if (!TargetHandle) { OutError = FString::Printf(TEXT("Emitter '%s' not found on system"), *EmitterName); return nullptr; }
		FVersionedNiagaraEmitterData* Data = TargetHandle->GetEmitterData();
		if (!Data) { OutError = TEXT("Emitter data missing"); return nullptr; }
		UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Data->GraphSource);
		if (!Src || !Src->NodeGraph) { OutError = TEXT("Emitter source graph missing"); return nullptr; }
		return Src->NodeGraph->FindEquivalentOutputNode(Usage);
	}
}

void HandleGetSystemSchemaFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	EmitClassSchema(UNiagaraSystem::StaticClass(), TEXT("NiagaraSystem"), OutJson);
}

void HandleGetEmitterSchemaFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	EmitClassSchema(UNiagaraEmitter::StaticClass(), TEXT("NiagaraEmitter"), OutJson);
}

void HandleGetRendererSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ClassSpec; Args->TryGetStringField(TEXT("renderer_class"), ClassSpec);
	if (ClassSpec.IsEmpty()) { OutError = TEXT("renderer_class required"); return; }
	UClass* C = ResolveClassByName(ClassSpec, UNiagaraRendererProperties::StaticClass());
	if (!C) { OutError = FString::Printf(TEXT("renderer_class '%s' did not resolve to a UNiagaraRendererProperties subclass"), *ClassSpec); return; }
	EmitClassSchema(C, C->GetName(), OutJson);
}

void HandleGetDataInterfaceSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ClassSpec; Args->TryGetStringField(TEXT("data_interface_class"), ClassSpec);
	if (ClassSpec.IsEmpty()) { OutError = TEXT("data_interface_class required"); return; }
	UClass* C = ResolveClassByName(ClassSpec, UNiagaraDataInterface::StaticClass());
	if (!C) { OutError = FString::Printf(TEXT("data_interface_class '%s' did not resolve to a UNiagaraDataInterface subclass"), *ClassSpec); return; }
	EmitClassSchema(C, C->GetName(), OutJson);
}

void HandleListRendererClassesFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C || C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
		if (!C->IsChildOf(UNiagaraRendererProperties::StaticClass())) continue;
		if (C == UNiagaraRendererProperties::StaticClass()) continue;
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("name"), C->GetName());
		E->SetStringField(TEXT("path"), C->GetPathName());
		Arr.Add(MakeShared<FJsonValueObject>(E));
	}
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("classes"), Arr);
	OutJson = SerializeJson(R);
}

void HandleListDataInterfaceClassesFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C || C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
		if (!C->IsChildOf(UNiagaraDataInterface::StaticClass())) continue;
		if (C == UNiagaraDataInterface::StaticClass()) continue;
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("name"), C->GetName());
		E->SetStringField(TEXT("path"), C->GetPathName());
		Arr.Add(MakeShared<FJsonValueObject>(E));
	}
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("classes"), Arr);
	OutJson = SerializeJson(R);
}

void HandleAddSetParametersModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SystemPath, EmitterName, Section;
	Args->TryGetStringField(TEXT("system_path"), SystemPath);
	Args->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Args->TryGetStringField(TEXT("script_section"), Section);
	int32 InsertIndex = INDEX_NONE;
	Args->TryGetNumberField(TEXT("insert_index"), InsertIndex);

	UNiagaraSystem* System = LoadSystem(SystemPath, OutError); if (!System) return;

	ENiagaraScriptUsage Usage;
	if (!ParseScriptSection(Section, Usage))
	{
		OutError = FString::Printf(TEXT("script_section '%s' invalid (particle_spawn|particle_update|emitter_spawn|emitter_update|system_spawn|system_update)"), *Section);
		return;
	}

	UNiagaraNodeOutput* Output = FindOutputForUsage(System, EmitterName, Usage, OutError);
	if (!Output) return;

	const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
	if (!Args->TryGetArrayField(TEXT("parameters"), Params) || !Params || Params->Num() == 0)
	{
		OutError = TEXT("parameters (array of {name, type, value?}) required and non-empty");
		return;
	}

	TArray<FNiagaraVariable> Variables;
	TArray<FString> Defaults;
	TArray<TSharedPtr<FJsonValue>> ResolvedEntries;
	for (const TSharedPtr<FJsonValue>& V : *Params)
	{
		const TSharedPtr<FJsonObject> Entry = V->AsObject();
		if (!Entry.IsValid()) continue;
		FString Name, Type, Value;
		Entry->TryGetStringField(TEXT("name"), Name);
		Entry->TryGetStringField(TEXT("type"), Type);
		Entry->TryGetStringField(TEXT("value"), Value);
		FNiagaraTypeDefinition TypeDef;
		if (!ParseTypeName(Type, TypeDef))
		{
			OutError = FString::Printf(TEXT("type '%s' on parameter '%s' not recognized (float|int|bool|vec2|vec3|vec4|color)"), *Type, *Name);
			return;
		}
		Variables.Emplace(TypeDef, FName(*Name));
		Defaults.Add(Value);

		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("name"), Name);
		R->SetStringField(TEXT("type"), Type);
		if (!Value.IsEmpty()) R->SetStringField(TEXT("value"), Value);
		ResolvedEntries.Add(MakeShared<FJsonValueObject>(R));
	}

	UNiagaraNodeAssignment* Added = FNiagaraStackGraphUtilities::AddParameterModuleToStack(
		Variables, *Output, InsertIndex, Defaults);
	if (!Added)
	{
		OutError = TEXT("FNiagaraStackGraphUtilities::AddParameterModuleToStack returned null");
		return;
	}

	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("emitter_name"), EmitterName);
	Result->SetStringField(TEXT("script_section"), Section);
	Result->SetNumberField(TEXT("parameter_count"), Variables.Num());
	Result->SetArrayField(TEXT("parameters"), ResolvedEntries);
	OutJson = SerializeJson(Result);
}

}
