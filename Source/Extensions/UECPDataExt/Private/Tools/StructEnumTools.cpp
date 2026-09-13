// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/StructEnumTools.h"
#include "Tools/BatchToolHelper.h"
#include "Tools/PinTypeResolver.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/Blueprint.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UnrealType.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/EngineVersionComparison.h"

namespace StructEnumTools
{
static bool UECP_SetEnums(UUserDefinedEnum* Enum, TArray<TPair<FName, int64>>& Pairs, UEnum::ECppForm Form)
{
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	return Enum->SetEnums(Pairs, Form, UEnum::EUnderlyingType::int32, EEnumFlags::None, UEnum::EAddMaxKeyIfMissing::Yes);
#else
	return Enum->SetEnums(Pairs, Form);
#endif
}

static bool StringToPinType(const FString& TypeStr, FEdGraphPinType& OutPinType)
{
	return UECPPinTypes::ResolvePinTypeFromString(TypeStr, OutPinType);
}

void HandleCreateStruct(const FString& StructName, const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& Variables, FString& OutAssetPath, FString& OutError)
{
	if (StructName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString CleanSavePath = SavePath;
	while (CleanSavePath.EndsWith(TEXT("/"))) CleanSavePath = CleanSavePath.LeftChop(1);
	if (CleanSavePath.EndsWith(TEXT("/") + StructName, ESearchCase::IgnoreCase))
		CleanSavePath = CleanSavePath.LeftChop(StructName.Len() + 1);
	while (CleanSavePath.EndsWith(TEXT("/"))) CleanSavePath = CleanSavePath.LeftChop(1);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(StructName, CleanSavePath, UUserDefinedStruct::StaticClass(), nullptr);

	if (UUserDefinedStruct* NewStruct = Cast<UUserDefinedStruct>(NewAsset))
	{
		NewStruct->EditorData = NewObject<UUserDefinedStructEditorData>(NewStruct);
		FStructureEditorUtils::ModifyStructData(NewStruct);

		for (const TSharedPtr<FJsonValue>& VarValue : Variables)
		{
			const TSharedPtr<FJsonObject>* VarObject;
			if (VarValue->TryGetObject(VarObject))
			{
				FString VarName, VarType;
				if (!(*VarObject)->TryGetStringField(TEXT("name"), VarName) || VarName.IsEmpty())
					if (!(*VarObject)->TryGetStringField(TEXT("var_name"), VarName) || VarName.IsEmpty())
						(*VarObject)->TryGetStringField(TEXT("member_name"), VarName);
				if (!(*VarObject)->TryGetStringField(TEXT("type"), VarType) || VarType.IsEmpty())
					if (!(*VarObject)->TryGetStringField(TEXT("var_type"), VarType) || VarType.IsEmpty())
						(*VarObject)->TryGetStringField(TEXT("member_type"), VarType);

				if (VarName.IsEmpty() || VarType.IsEmpty())
				{
					continue;
				}

				FEdGraphPinType PinType;
				if (StringToPinType(VarType, PinType))
				{
					FStructureEditorUtils::AddVariable(NewStruct, PinType);

					TArray<FStructVariableDescription>& VarDescriptions = FStructureEditorUtils::GetVarDesc(NewStruct);
					const FGuid VarGuid = VarDescriptions.Last().VarGuid;

					FStructureEditorUtils::RenameVariable(NewStruct, VarGuid, VarName);
				}
				else
				{
					OutError = FString::Printf(TEXT("Field '%s': type '%s' could not be resolved — field was NOT created. Use the enum/struct's ACTUAL asset name (e.g. 'EStatusEffectType', 'FMyStruct') — do NOT add an extra E_/S_ prefix if the name already starts with E/F/S, and no 'object:' prefix for user enums/structs. Struct was created with the other fields. Use add_struct_member to add the missing field."), *VarName, *VarType);
				}
			}
		}

		FStructureEditorUtils::OnStructureChanged(NewStruct);
		FStructureEditorUtils::CompileStructure(NewStruct);
		NewStruct->MarkPackageDirty();
		OutAssetPath = NewStruct->GetPathName();
	}
	else
	{
		OutError = TEXT("Failed to create User Defined Struct asset. The asset may already exist or the path is invalid.");
	}
}

void HandleCreateEnum(const FString& EnumName, const FString& SavePath, const TArray<FString>& Enumerators, FString& OutAssetPath, FString& OutError)
{
	if (EnumName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString CleanSavePath = SavePath;
	while (CleanSavePath.EndsWith(TEXT("/"))) CleanSavePath = CleanSavePath.LeftChop(1);
	if (CleanSavePath.EndsWith(TEXT("/") + EnumName, ESearchCase::IgnoreCase))
		CleanSavePath = CleanSavePath.LeftChop(EnumName.Len() + 1);
	while (CleanSavePath.EndsWith(TEXT("/"))) CleanSavePath = CleanSavePath.LeftChop(1);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(EnumName, CleanSavePath, UUserDefinedEnum::StaticClass(), nullptr);

	if (UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(NewAsset))
	{
		TArray<TPair<FName, int64>> EnumNameValuePairs;
		for (int32 i = 0; i < Enumerators.Num(); ++i)
		{
			FName InternalName = FName(*FString::Printf(TEXT("%s::%s"), *NewEnum->GetName(), *Enumerators[i]));
			EnumNameValuePairs.Add(TPair<FName, int64>(InternalName, i));
		}

		if (UECP_SetEnums(NewEnum, EnumNameValuePairs, UEnum::ECppForm::Namespaced))
		{
			for (int32 i = 0; i < Enumerators.Num(); ++i)
			{
				NewEnum->DisplayNameMap.Add(NewEnum->GetNameByIndex(i), FText::FromString(Enumerators[i]));
			}
		}

		NewEnum->MarkPackageDirty();
		OutAssetPath = NewEnum->GetPathName();
	}
	else
	{
		OutError = TEXT("Failed to create User Defined Enum asset. The asset may already exist or the path is invalid.");
	}
}

void HandleAddEnumValue(const FString& EnumPath, const FString& NewValue, FString& OutJsonString, FString& OutError)
{
	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(UEditorAssetLibrary::LoadAsset(EnumPath));
	if (!Enum) { OutError = FString::Printf(TEXT("Could not load UserDefinedEnum at: %s"), *EnumPath); return; }

	int32 ExistingCount = Enum->NumEnums() - 1;
	TArray<TPair<FName, int64>> Pairs;
	for (int32 i = 0; i < ExistingCount; ++i)
		Pairs.Add(TPair<FName, int64>(Enum->GetNameByIndex(i), Enum->GetValueByIndex(i)));

	FName NewInternalName = FName(*FString::Printf(TEXT("%s::%s"), *Enum->GetName(), *NewValue));
	for (const auto& P : Pairs)
	{
		if (P.Key == NewInternalName)
		{
			TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
			Res->SetBoolField(TEXT("success"), true);
			Res->SetStringField(TEXT("enum_path"), EnumPath);
			Res->SetStringField(TEXT("value"), NewValue);
			Res->SetStringField(TEXT("note"), TEXT("value already existed"));
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(Res.ToSharedRef(), W);
			return;
		}
	}

	Pairs.Add(TPair<FName, int64>(NewInternalName, (int64)ExistingCount));
	if (!UECP_SetEnums(Enum, Pairs, UEnum::ECppForm::Namespaced))
	{
		OutError = FString::Printf(TEXT("SetEnums failed on '%s'"), *EnumPath);
		return;
	}
	Enum->DisplayNameMap.Add(NewInternalName, FText::FromString(NewValue));
	Enum->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(EnumPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("enum_path"), EnumPath);
	Res->SetStringField(TEXT("value"), NewValue);
	Res->SetNumberField(TEXT("index"), ExistingCount);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleAddStructMember(const FString& StructPath, const FString& MemberName, const FString& MemberType, FString& OutJsonString, FString& OutError)
{
	UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(UEditorAssetLibrary::LoadAsset(StructPath));
	if (!Struct) { OutError = FString::Printf(TEXT("Could not load UserDefinedStruct at: %s"), *StructPath); return; }

	FEdGraphPinType PinType;
	if (!StringToPinType(MemberType, PinType))
	{
		OutError = FString::Printf(TEXT("Unknown or unsupported member type: '%s'. Use the enum/struct's ACTUAL asset name (e.g. 'EStatusEffectType', 'FMyStruct') — don't add an extra E_/S_ prefix if the name already starts with E/F/S; primitives are int/float/bool/string/name/vector; arrays as 'TArray<int>'."), *MemberType);
		return;
	}

	FStructureEditorUtils::AddVariable(Struct, PinType);
	TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Struct);
	const FGuid VarGuid = VarDescs.Last().VarGuid;
	FStructureEditorUtils::RenameVariable(Struct, VarGuid, MemberName);
	FStructureEditorUtils::OnStructureChanged(Struct);
	FStructureEditorUtils::CompileStructure(Struct);
	Struct->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(StructPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("struct_path"), StructPath);
	Res->SetStringField(TEXT("member_name"), MemberName);
	Res->SetStringField(TEXT("member_type"), MemberType);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetEnumValues(const FString& EnumPath, FString& OutJsonString, FString& OutError)
{

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(EnumPath);
	if (!Loaded && !EnumPath.Contains(TEXT(".")))
	{
		const FString WithSuffix = EnumPath + TEXT(".") + FPaths::GetBaseFilename(EnumPath);
		Loaded = UEditorAssetLibrary::LoadAsset(WithSuffix);
	}
	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Loaded);
	if (!Enum)
	{
		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("Asset not found at: %s"), *EnumPath);
		}
		else
		{
			OutError = FString::Printf(TEXT("Asset at '%s' is %s, not a UserDefinedEnum."),
				*EnumPath, *Loaded->GetClass()->GetName());
		}
		return;
	}

	int32 Count = Enum->NumEnums() - 1;

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("enum_path"), Enum->GetPathName());
	Res->SetStringField(TEXT("enum_name"), Enum->GetName());

	TArray<TSharedPtr<FJsonValue>> ValuesArray;
	for (int32 i = 0; i < Count; ++i)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetNumberField(TEXT("value"), Enum->GetValueByIndex(i));
		Entry->SetStringField(TEXT("internal_name"), Enum->GetNameStringByIndex(i));

		FText DisplayName = Enum->GetDisplayNameTextByIndex(i);
		Entry->SetStringField(TEXT("display_name"), DisplayName.ToString());

		ValuesArray.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	Res->SetArrayField(TEXT("values"), ValuesArray);
	Res->SetNumberField(TEXT("count"), Count);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetStructMembers(const FString& StructPath, FString& OutJsonString, FString& OutError)
{

	UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(UEditorAssetLibrary::LoadAsset(StructPath));
	if (!Struct) { OutError = FString::Printf(TEXT("Could not load UserDefinedStruct at: %s"), *StructPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("struct_path"), Struct->GetPathName());
	Res->SetStringField(TEXT("struct_name"), Struct->GetName());

	TArray<TSharedPtr<FJsonValue>> MembersArray;
	const TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Struct);
	for (const FStructVariableDescription& Desc : VarDescs)
	{
		TSharedPtr<FJsonObject> Member = MakeShareable(new FJsonObject());
		Member->SetStringField(TEXT("name"), Desc.FriendlyName);
		Member->SetStringField(TEXT("guid"), Desc.VarGuid.ToString());

		FString TypeStr = TEXT("unknown");
		for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) continue;
			FString PropName = Prop->GetName();
			if (PropName.StartsWith(Desc.FriendlyName + TEXT("_")) || PropName.Equals(Desc.FriendlyName, ESearchCase::IgnoreCase))
			{
				if (CastField<FBoolProperty>(Prop)) TypeStr = TEXT("bool");
				else if (CastField<FByteProperty>(Prop)) TypeStr = TEXT("byte");
				else if (CastField<FIntProperty>(Prop)) TypeStr = TEXT("int");
				else if (CastField<FInt64Property>(Prop)) TypeStr = TEXT("int64");
				else if (CastField<FFloatProperty>(Prop)) TypeStr = TEXT("float");
				else if (CastField<FDoubleProperty>(Prop)) TypeStr = TEXT("double");
				else if (CastField<FNameProperty>(Prop)) TypeStr = TEXT("name");
				else if (CastField<FStrProperty>(Prop)) TypeStr = TEXT("string");
				else if (CastField<FTextProperty>(Prop)) TypeStr = TEXT("text");
				else if (FStructProperty* SP = CastField<FStructProperty>(Prop))
					TypeStr = SP->Struct ? SP->Struct->GetName() : TEXT("struct");
				else if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
					TypeStr = OP->PropertyClass ? OP->PropertyClass->GetName() : TEXT("object");
				else if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
					TypeStr = EP->GetEnum() ? EP->GetEnum()->GetName() : TEXT("enum");
				else TypeStr = Prop->GetCPPType();
				break;
			}
		}

		Member->SetStringField(TEXT("type"), TypeStr);
		if (!Desc.DefaultValue.IsEmpty())
			Member->SetStringField(TEXT("default_value"), Desc.DefaultValue);

		MembersArray.Add(MakeShareable(new FJsonValueObject(Member)));
	}

	Res->SetArrayField(TEXT("members"), MembersArray);
	Res->SetNumberField(TEXT("count"), VarDescs.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleRemoveEnumValue(const FString& EnumPath, const FString& ValueName, FString& OutJsonString, FString& OutError)
{

	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(UEditorAssetLibrary::LoadAsset(EnumPath));
	if (!Enum) { OutError = FString::Printf(TEXT("Could not load UserDefinedEnum at: %s"), *EnumPath); return; }

	int32 ExistingCount = Enum->NumEnums() - 1;
	if (ExistingCount <= 1)
	{
		OutError = TEXT("Cannot remove the last enum value — enum must have at least one value");
		return;
	}

	int32 RemoveIndex = -1;
	for (int32 i = 0; i < ExistingCount; ++i)
	{
		FText DisplayName = Enum->GetDisplayNameTextByIndex(i);
		FString InternalName = Enum->GetNameStringByIndex(i);
		if (DisplayName.ToString().Equals(ValueName, ESearchCase::IgnoreCase) ||
			InternalName.EndsWith(TEXT("::") + ValueName))
		{
			RemoveIndex = i;
			break;
		}
	}

	if (RemoveIndex < 0)
	{
		OutError = FString::Printf(TEXT("Enum value '%s' not found in '%s'"), *ValueName, *EnumPath);
		return;
	}

	TArray<TPair<FName, int64>> NewPairs;
	int64 NewValue = 0;
	for (int32 i = 0; i < ExistingCount; ++i)
	{
		if (i == RemoveIndex) continue;
		NewPairs.Add(TPair<FName, int64>(Enum->GetNameByIndex(i), NewValue++));
	}

	FName RemovedInternalName = Enum->GetNameByIndex(RemoveIndex);

	if (!UECP_SetEnums(Enum, NewPairs, UEnum::ECppForm::Namespaced))
	{
		OutError = FString::Printf(TEXT("SetEnums failed when removing '%s'"), *ValueName);
		return;
	}

	Enum->DisplayNameMap.Remove(RemovedInternalName);
	Enum->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(EnumPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("enum_path"), EnumPath);
	Res->SetStringField(TEXT("removed_value"), ValueName);
	Res->SetNumberField(TEXT("remaining_count"), NewPairs.Num());
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleRemoveStructMember(const FString& StructPath, const FString& MemberName, FString& OutJsonString, FString& OutError)
{

	UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(UEditorAssetLibrary::LoadAsset(StructPath));
	if (!Struct) { OutError = FString::Printf(TEXT("Could not load UserDefinedStruct at: %s"), *StructPath); return; }

	TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Struct);

	FGuid FoundGuid;
	bool bFound = false;
	for (const FStructVariableDescription& Desc : VarDescs)
	{
		if (Desc.FriendlyName.Equals(MemberName, ESearchCase::IgnoreCase))
		{
			FoundGuid = Desc.VarGuid;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		OutError = FString::Printf(TEXT("Member '%s' not found in struct '%s'"), *MemberName, *StructPath);
		return;
	}

	if (!FStructureEditorUtils::RemoveVariable(Struct, FoundGuid))
	{
		OutError = FString::Printf(TEXT("Failed to remove member '%s' from struct"), *MemberName);
		return;
	}

	FStructureEditorUtils::OnStructureChanged(Struct);
	Struct->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(StructPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("struct_path"), StructPath);
	Res->SetStringField(TEXT("removed_member"), MemberName);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleCreateStructFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("structs"), ItemsArray))
	{
		FString OuterSavePath;
		if (!Args->TryGetStringField(TEXT("save_path"), OuterSavePath) || OuterSavePath.IsEmpty())
			OuterSavePath = TEXT("/Game");
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("struct_name"), TEXT("name"));
			if (Name.IsEmpty()) Name = BatchToolHelper::GetItemString(Item, TEXT("asset_name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			if (SavePath.IsEmpty()) SavePath = OuterSavePath;
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing struct_name")); continue; }
			const TArray<TSharedPtr<FJsonValue>>* Vars = nullptr;
			if (!Item->TryGetArrayField(TEXT("variables"), Vars))
				if (!Item->TryGetArrayField(TEXT("fields"), Vars))
					if (!Item->TryGetArrayField(TEXT("members"), Vars))
						if (!Item->TryGetArrayField(TEXT("struct_members"), Vars))
							Item->TryGetArrayField(TEXT("properties"), Vars);
			TArray<TSharedPtr<FJsonValue>> EmptyVars;
			FString AssetPath, Err;
			HandleCreateStruct(Name, SavePath, Vars ? *Vars : EmptyVars, AssetPath, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Extra->SetStringField(TEXT("asset_path"), AssetPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Name;
	if (!Args->TryGetStringField(TEXT("struct_name"), Name))
		if (!Args->TryGetStringField(TEXT("name"), Name))
			if (!Args->TryGetStringField(TEXT("asset_name"), Name))
				Name = FString();
	FString SavePath;
	if (!Args->TryGetStringField(TEXT("save_path"), SavePath) || SavePath.IsEmpty())
		SavePath = TEXT("/Game");
	const TArray<TSharedPtr<FJsonValue>>* Vars = nullptr;
	if (!Args->TryGetArrayField(TEXT("variables"), Vars))
		if (!Args->TryGetArrayField(TEXT("fields"), Vars))
			if (!Args->TryGetArrayField(TEXT("members"), Vars))
				if (!Args->TryGetArrayField(TEXT("struct_members"), Vars))
					Args->TryGetArrayField(TEXT("properties"), Vars);
	TArray<TSharedPtr<FJsonValue>> ParsedVars;
	if (!Vars)
	{
		FString VarStr;
		if (!Args->TryGetStringField(TEXT("variables"), VarStr))
			if (!Args->TryGetStringField(TEXT("fields"), VarStr))
				if (!Args->TryGetStringField(TEXT("members"), VarStr))
					Args->TryGetStringField(TEXT("struct_members"), VarStr);
		if (!VarStr.IsEmpty())
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(VarStr);
			if (FJsonSerializer::Deserialize(Reader, ParsedVars))
				Vars = &ParsedVars;
		}
	}
	TArray<TSharedPtr<FJsonValue>> EmptyVars;
	FString AssetPath;
	HandleCreateStruct(Name, SavePath, Vars ? *Vars : EmptyVars, AssetPath, OutError);
	if (OutError.IsEmpty())
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *AssetPath);
}

void HandleAddStructMemberFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString OuterStructPath;
	Args->TryGetStringField(TEXT("struct_path"), OuterStructPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("members"), ItemsArray))
	{
		if (OuterStructPath.IsEmpty() && ItemsArray->Num() > 0)
		{
			TSharedPtr<FJsonObject> First = (*ItemsArray)[0]->AsObject();
			if (First.IsValid()) First->TryGetStringField(TEXT("struct_path"), OuterStructPath);
		}
		if (OuterStructPath.IsEmpty()) { OutError = TEXT("Missing required parameter: struct_path"); return; }
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString StructPath = OuterStructPath;
			if (Item.IsValid()) { FString PerItem; if (Item->TryGetStringField(TEXT("struct_path"), PerItem) && !PerItem.IsEmpty()) StructPath = PerItem; }
			FString MemberName = BatchToolHelper::GetItemString(Item, TEXT("member_name"), TEXT("name"));
			FString MemberType = BatchToolHelper::GetItemString(Item, TEXT("member_type"), TEXT("type"));
			if (MemberName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing member_name")); continue; }
			if (MemberType.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing member_type")); continue; }
			FString Json, Err;
			HandleAddStructMember(StructPath, MemberName, MemberType, Json, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("member_name"), MemberName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	if (OuterStructPath.IsEmpty()) { OutError = TEXT("Missing required parameter: struct_path"); return; }
	FString MemberName, MemberType;
	if (!Args->TryGetStringField(TEXT("member_name"), MemberName))
		Args->TryGetStringField(TEXT("name"), MemberName);
	if (!Args->TryGetStringField(TEXT("member_type"), MemberType))
		Args->TryGetStringField(TEXT("type"), MemberType);
	HandleAddStructMember(OuterStructPath, MemberName, MemberType, OutJsonString, OutError);
}

void HandleRemoveStructMemberFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString OuterStructPath;
	Args->TryGetStringField(TEXT("struct_path"), OuterStructPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("members"), ItemsArray))
	{
		if (OuterStructPath.IsEmpty() && ItemsArray->Num() > 0)
		{
			TSharedPtr<FJsonObject> First = (*ItemsArray)[0]->AsObject();
			if (First.IsValid()) First->TryGetStringField(TEXT("struct_path"), OuterStructPath);
		}
		if (OuterStructPath.IsEmpty()) { OutError = TEXT("Missing required parameter: struct_path"); return; }
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString StructPath = OuterStructPath;
			FString MemberName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				MemberName = (*ItemsArray)[i]->AsString();
			else if (auto Obj = (*ItemsArray)[i]->AsObject())
			{
				FString PerItem; if (Obj->TryGetStringField(TEXT("struct_path"), PerItem) && !PerItem.IsEmpty()) StructPath = PerItem;
				MemberName = BatchToolHelper::GetItemString(Obj, TEXT("member_name"), TEXT("name"));
			}
			if (MemberName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing member_name")); continue; }
			FString Json, Err;
			HandleRemoveStructMember(StructPath, MemberName, Json, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("member_name"), MemberName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	if (OuterStructPath.IsEmpty()) { OutError = TEXT("Missing required parameter: struct_path"); return; }
	FString MemberName;
	if (!Args->TryGetStringField(TEXT("member_name"), MemberName))
		Args->TryGetStringField(TEXT("name"), MemberName);
	HandleRemoveStructMember(OuterStructPath, MemberName, OutJsonString, OutError);
}

void HandleCreateEnumFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("enums"), ItemsArray))
	{
		FString OuterSavePath;
		if (!Args->TryGetStringField(TEXT("save_path"), OuterSavePath) || OuterSavePath.IsEmpty())
			OuterSavePath = TEXT("/Game");
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("enum_name"), TEXT("name"));
			if (Name.IsEmpty()) Name = BatchToolHelper::GetItemString(Item, TEXT("asset_name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			if (SavePath.IsEmpty()) SavePath = OuterSavePath;
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing enum_name")); continue; }
			const TArray<TSharedPtr<FJsonValue>>* EnumsArr = nullptr;
			Item->TryGetArrayField(TEXT("enumerators"), EnumsArr);
			if (!EnumsArr) Item->TryGetArrayField(TEXT("values"), EnumsArr);
			if (!EnumsArr) Item->TryGetArrayField(TEXT("enum_values"), EnumsArr);
			TArray<FString> Enumerators;
			if (EnumsArr)
			{
				for (const auto& V : *EnumsArr)
					Enumerators.Add(V->AsString());
			}
			FString AssetPath, Err;
			HandleCreateEnum(Name, SavePath, Enumerators, AssetPath, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Extra->SetStringField(TEXT("asset_path"), AssetPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Name;
	if (!Args->TryGetStringField(TEXT("enum_name"), Name))
		if (!Args->TryGetStringField(TEXT("name"), Name))
			if (!Args->TryGetStringField(TEXT("asset_name"), Name))
				Name = FString();
	FString SavePath;
	if (!Args->TryGetStringField(TEXT("save_path"), SavePath) || SavePath.IsEmpty())
		SavePath = TEXT("/Game");
	const TArray<TSharedPtr<FJsonValue>>* EnumsArr = nullptr;
	Args->TryGetArrayField(TEXT("enumerators"), EnumsArr);
	if (!EnumsArr) Args->TryGetArrayField(TEXT("values"), EnumsArr);
	if (!EnumsArr) Args->TryGetArrayField(TEXT("enum_values"), EnumsArr);
	if (!EnumsArr) Args->TryGetArrayField(TEXT("entries"), EnumsArr);
	TArray<FString> Enumerators;
	if (EnumsArr)
	{
		for (const auto& V : *EnumsArr)
			Enumerators.Add(V->AsString());
	}
	if (Enumerators.IsEmpty())
	{
		FString EnumStr;
		if (!Args->TryGetStringField(TEXT("enumerators"), EnumStr))
			if (!Args->TryGetStringField(TEXT("values"), EnumStr))
				if (!Args->TryGetStringField(TEXT("enum_values"), EnumStr))
					Args->TryGetStringField(TEXT("entries"), EnumStr);
		if (!EnumStr.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Parsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EnumStr);
			if (FJsonSerializer::Deserialize(Reader, Parsed))
				for (const auto& V : Parsed) Enumerators.Add(V->AsString());
		}
	}
	if (Enumerators.IsEmpty())
	{
		OutError = TEXT("No enumerators provided — use 'enumerators': [\"Value1\", \"Value2\"] (also accepts 'values', 'entries', or 'enum_values' as aliases)");
		return;
	}
	FString AssetPath;
	HandleCreateEnum(Name, SavePath, Enumerators, AssetPath, OutError);
	if (OutError.IsEmpty())
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *AssetPath);
}

void HandleAddEnumValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString EnumPath;
	if (!Args->TryGetStringField(TEXT("enum_path"), EnumPath) || EnumPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: enum_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("values"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString DisplayName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				DisplayName = (*ItemsArray)[i]->AsString();
			else if (auto Obj = (*ItemsArray)[i]->AsObject())
				DisplayName = BatchToolHelper::GetItemString(Obj, TEXT("display_name"), TEXT("value"));
			if (DisplayName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing display_name")); continue; }
			FString Json, Err;
			HandleAddEnumValue(EnumPath, DisplayName, Json, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("display_name"), DisplayName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString NewValue;
	if (!Args->TryGetStringField(TEXT("display_name"), NewValue))
		if (!Args->TryGetStringField(TEXT("value"), NewValue))
			NewValue = FString();
	HandleAddEnumValue(EnumPath, NewValue, OutJsonString, OutError);
}

void HandleRemoveEnumValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString EnumPath;
	if (!Args->TryGetStringField(TEXT("enum_path"), EnumPath) || EnumPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: enum_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("values"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString DisplayName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				DisplayName = (*ItemsArray)[i]->AsString();
			else if (auto Obj = (*ItemsArray)[i]->AsObject())
				DisplayName = BatchToolHelper::GetItemString(Obj, TEXT("display_name"), TEXT("value"));
			if (DisplayName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing display_name")); continue; }
			FString Json, Err;
			HandleRemoveEnumValue(EnumPath, DisplayName, Json, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("display_name"), DisplayName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString ValueName;
	if (!Args->TryGetStringField(TEXT("display_name"), ValueName))
		if (!Args->TryGetStringField(TEXT("value"), ValueName))
			Args->TryGetStringField(TEXT("value_name"), ValueName);
	HandleRemoveEnumValue(EnumPath, ValueName, OutJsonString, OutError);
}

void HandleGetEnumValuesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString EnumPath;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("enum_path"), EnumPath);
	HandleGetEnumValues(EnumPath, OutJsonString, OutError);
}

void HandleGetStructMembersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString StructPath;
	if (Args.IsValid())
	{
		if (!Args->TryGetStringField(TEXT("struct_path"), StructPath) || StructPath.IsEmpty())
			Args->TryGetStringField(TEXT("asset_path"), StructPath);
	}
	HandleGetStructMembers(StructPath, OutJsonString, OutError);
}

}
