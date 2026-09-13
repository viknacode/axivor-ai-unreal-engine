// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/ObjectPropertyTools.h"
#include "Tools/PropertyWriteReport.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "JsonObjectConverter.h"

#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"

namespace ObjectPropertyTools
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

	AActor* FindActorByLabelInWorld(UWorld* World, const FString& Label)
	{
		if (!World) return nullptr;
		for (FActorIterator It(World); It; ++It)
		{
			if (*It && It->GetActorLabel() == Label) return *It;
		}
		return nullptr;
	}

	UObject* ResolveTarget(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);
		if (!AssetPath.IsEmpty())
		{
			if (UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath)) return Asset;
			OutError = FString::Printf(TEXT("asset_path '%s' did not load to a UObject"), *AssetPath);
			return nullptr;
		}

		FString ActorPath; Args->TryGetStringField(TEXT("actor_path"), ActorPath);
		if (!ActorPath.IsEmpty())
		{
			if (UObject* Obj = StaticLoadObject(AActor::StaticClass(), nullptr, *ActorPath)) return Obj;
			OutError = FString::Printf(TEXT("actor_path '%s' did not load"), *ActorPath);
			return nullptr;
		}

		FString ActorLabel; Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
		if (!ActorLabel.IsEmpty() && GEditor)
		{
			for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
			{
				if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) continue;
				if (AActor* A = FindActorByLabelInWorld(Ctx.World(), ActorLabel)) return A;
			}
			if (AActor* A = FindActorByLabelInWorld(GEditor->GetEditorWorldContext().World(), ActorLabel)) return A;
			OutError = FString::Printf(TEXT("No actor with label '%s' found in any open world"), *ActorLabel);
			return nullptr;
		}

		OutError = TEXT("asset_path, actor_path or actor_label required");
		return nullptr;
	}

	bool IsExposedProperty(FProperty* Prop, bool bEditAnywhereOnly)
	{
		if (!Prop) return false;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) return false;
		if (bEditAnywhereOnly)
		{
			if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> DescribeProperty(FProperty* Prop)
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
		P->SetBoolField(TEXT("is_array"), Prop->IsA<FArrayProperty>());
		P->SetBoolField(TEXT("is_struct"), Prop->IsA<FStructProperty>());
		P->SetBoolField(TEXT("is_object_ref"), Prop->IsA<FObjectProperty>() || Prop->IsA<FSoftObjectProperty>());
		return P;
	}

	FString TargetIdentity(UObject* Target)
	{
		if (!Target) return FString();
		if (AActor* A = Cast<AActor>(Target)) return A->GetActorLabel();
		return Target->GetPathName();
	}
}

void HandleListPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	UObject* Target = ResolveTarget(Args, OutError); if (!Target) return;
	bool bIncludeInherited = true; Args->TryGetBoolField(TEXT("include_inherited"), bIncludeInherited);
	bool bEditAnywhereOnly = true; Args->TryGetBoolField(TEXT("edit_anywhere_only"), bEditAnywhereOnly);

	UClass* Class = Target->GetClass();
	TArray<TSharedPtr<FJsonValue>> Props;
	const EFieldIterationFlags IterFlags = bIncludeInherited
		? EFieldIterationFlags::IncludeSuper
		: EFieldIterationFlags::None;
	for (TFieldIterator<FProperty> It(Class, IterFlags); It; ++It)
	{
		FProperty* Prop = *It;
		if (!IsExposedProperty(Prop, bEditAnywhereOnly)) continue;
		Props.Add(MakeShared<FJsonValueObject>(DescribeProperty(Prop)));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("target"), TargetIdentity(Target));
	R->SetStringField(TEXT("class"), Class->GetName());
	R->SetNumberField(TEXT("count"), Props.Num());
	R->SetArrayField(TEXT("properties"), Props);
	OutJson = SerializeJson(R);
}

void HandleGetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	UObject* Target = ResolveTarget(Args, OutError); if (!Target) return;
	UClass* Class = Target->GetClass();

	TArray<FString> RequestedNames;
	const TArray<TSharedPtr<FJsonValue>>* NameArr = nullptr;
	if (Args->TryGetArrayField(TEXT("names"), NameArr) && NameArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *NameArr) RequestedNames.Add(V->AsString());
	}
	const bool bAllProps = RequestedNames.Num() == 0;

	TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
	TArray<FString> Missing;
	auto VisitProperty = [&](FProperty* Prop)
	{
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);
		if (TSharedPtr<FJsonValue> J = FJsonObjectConverter::UPropertyToJsonValue(Prop, ValuePtr))
		{
			Values->SetField(Prop->GetName(), J);
		}
	};

	if (bAllProps)
	{
		for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Prop = *It;
			if (!IsExposedProperty(Prop,  true)) continue;
			VisitProperty(Prop);
		}
	}
	else
	{
		for (const FString& Name : RequestedNames)
		{
			FProperty* Prop = FindFProperty<FProperty>(Class, *Name);
			if (!Prop) { Missing.Add(Name); continue; }
			VisitProperty(Prop);
		}
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("target"), TargetIdentity(Target));
	R->SetStringField(TEXT("class"), Class->GetName());
	R->SetObjectField(TEXT("values"), Values);
	if (Missing.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MissArr;
		for (const FString& M : Missing) MissArr.Add(MakeShared<FJsonValueString>(M));
		R->SetArrayField(TEXT("missing"), MissArr);
	}
	OutJson = SerializeJson(R);
}

void HandleSetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	UObject* Target = ResolveTarget(Args, OutError); if (!Target) return;
	UClass* Class = Target->GetClass();

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (!Args->TryGetObjectField(TEXT("props"), PropsObj) || !PropsObj || !PropsObj->IsValid())
	{
		OutError = TEXT("props (object of {name: value}) required");
		return;
	}

	UECPProps::FWriteReport Report;

	Target->Modify();
	for (const auto& Pair : (*PropsObj)->Values)
	{
		const FString Name(Pair.Key);
		FProperty* Prop = FindFProperty<FProperty>(Class, *Name);
		if (!Prop) { Report.AddFailed(Name, TEXT("property not found on class")); continue; }
		if (Prop->HasAnyPropertyFlags(CPF_Deprecated)) { Report.AddFailed(Name, TEXT("property is deprecated")); continue; }

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);
		FText FailReason;
		const bool bOk = FJsonObjectConverter::JsonValueToUProperty(Pair.Value, Prop, ValuePtr, 0, 0,  false, &FailReason);
		if (!bOk)
		{
			Report.AddFailed(Name, FailReason.IsEmpty() ? TEXT("JsonValueToUProperty failed") : FailReason.ToString());
			continue;
		}

		FPropertyChangedEvent ChangeEvt(Prop, EPropertyChangeType::ValueSet);
		Target->PostEditChangeProperty(ChangeEvt);
		Report.AddApplied(Name, UECPProps::ExportPropertyValueString(Prop, ValuePtr));
	}
	Target->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	Report.FillInto(R);
	R->SetStringField(TEXT("target"), TargetIdentity(Target));
	R->SetStringField(TEXT("class"), Class->GetName());
	OutJson = SerializeJson(R);
}

}
