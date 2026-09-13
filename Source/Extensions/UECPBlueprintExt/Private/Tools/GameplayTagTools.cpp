// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/GameplayTagTools.h"
#include "Tools/BatchToolHelper.h"

#include "GameplayTagsManager.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphSchema.h"
#include "K2Node_Variable.h"
#include "EdGraphSchema_K2.h"

namespace GameplayTagTools
{

void HandleAddGameplayTag(const FString& TagName, const FString& DevComment,
	FString& OutJsonString, FString& OutError)
{

	if (TagName.IsEmpty())
	{
		OutError = TEXT("tag_name is required");
		return;
	}

	if (TagName.Contains(TEXT(" ")))
	{
		OutError = TEXT("Tag names cannot contain spaces. Use dots to separate hierarchy (e.g. Combat.Attack.Melee)");
		return;
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	FGameplayTag ExistingTag = Manager.RequestGameplayTag(FName(*TagName), false);
	if (ExistingTag.IsValid())
	{
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"tag\":\"%s\",\"message\":\"Tag already exists\"}"),
			*TagName);
		return;
	}

	IGameplayTagsEditorModule& EditorModule = IGameplayTagsEditorModule::Get();
	bool bAdded = EditorModule.AddNewGameplayTagToINI(TagName, DevComment);

	if (!bAdded)
	{
		OutError = FString::Printf(TEXT("Failed to add gameplay tag '%s'. Check Output Log for details."), *TagName);
		return;
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"tag\":\"%s\",\"message\":\"Tag registered in DefaultGameplayTags.ini\"}"),
		*TagName);
}

void HandleGetGameplayTags(FString& OutJsonString, FString& OutError)
{

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	FGameplayTagContainer AllTags;
	Manager.RequestAllGameplayTags(AllTags, false);

	FString TagArray = TEXT("[");
	bool bFirst = true;
	for (const FGameplayTag& Tag : AllTags)
	{
		if (!bFirst) TagArray += TEXT(",");
		TagArray += FString::Printf(TEXT("\"%s\""), *Tag.ToString());
		bFirst = false;
	}
	TagArray += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"tag_count\":%d,\"tags\":%s}"),
		AllTags.Num(), *TagArray);
}

void HandleAssignGameplayTagToBlueprint(const FString& BlueprintPath,
	const FString& VariableName, const TArray<FString>& DefaultTags,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	FString VarName = VariableName.IsEmpty() ? TEXT("GameplayTags") : VariableName;

	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == FName(*VarName))
		{
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"variable\":\"%s\",\"message\":\"Variable already exists\"}"),
				*VarName);
			return;
		}
	}

	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	PinType.PinSubCategoryObject = FGameplayTagContainer::StaticStruct();

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VarName), PinType);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	FString DefaultTagList = TEXT("[");
	bool bFirst = true;
	for (const FString& T : DefaultTags) { if (!bFirst) DefaultTagList += TEXT(","); DefaultTagList += FString::Printf(TEXT("\"%s\""), *T); bFirst = false; }
	DefaultTagList += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"variable\":\"%s\",\"type\":\"FGameplayTagContainer\",\"default_tags\":%s,\"message\":\"FGameplayTagContainer variable added. Set default tags in Blueprint editor Details panel.\"}"),
		*VarName, *DefaultTagList);
}

void HandleRemoveGameplayTag(const FString& TagName,
	FString& OutJsonString, FString& OutError)
{

	if (TagName.IsEmpty()) { OutError = TEXT("tag_name is required"); return; }

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	FGameplayTag ExistingTag = Manager.RequestGameplayTag(FName(*TagName), false);
	if (!ExistingTag.IsValid())
	{
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"tag\":\"%s\",\"message\":\"Tag does not exist (nothing to remove)\"}"),
			*TagName);
		return;
	}

	TSharedPtr<FGameplayTagNode> TagNode = Manager.FindTagNode(ExistingTag.GetTagName());
	IGameplayTagsEditorModule& EditorModule = IGameplayTagsEditorModule::Get();
	bool bRemoved = TagNode.IsValid() && EditorModule.DeleteTagFromINI(TagNode);

	if (!bRemoved)
	{
		OutError = FString::Printf(
			TEXT("Failed to remove gameplay tag '%s'. Tag may be defined in a plugin or C++ and cannot be deleted via INI."),
			*TagName);
		return;
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"tag\":\"%s\",\"message\":\"Tag removed from DefaultGameplayTags.ini\"}"),
		*TagName);
}

void HandleAddGameplayTagsBulk(const TArray<FString>& TagNames, const FString& DevComment, FString& OutJsonString, FString& OutError)
{
	if (TagNames.Num() == 0) { OutError = TEXT("tag_names array is required and must not be empty"); return; }

	int32 Added = 0;
	TArray<FString> Errors;
	for (const FString& TagName : TagNames)
	{
		FString SubJson, SubError;
		HandleAddGameplayTag(TagName, DevComment, SubJson, SubError);
		if (SubError.IsEmpty()) ++Added;
		else Errors.Add(FString::Printf(TEXT("%s: %s"), *TagName, *SubError));
	}

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"tags_added\":%d,\"total_requested\":%d}"), Added, TagNames.Num());
	if (Errors.Num() > 0)
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"tags_added\":%d,\"total_requested\":%d,\"errors\":%d}"), Added, TagNames.Num(), Errors.Num());
	}
}

void HandleAddGameplayTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tags"), ItemsArray))
	{
		FString DevComment;
		Args->TryGetStringField(TEXT("dev_comment"), DevComment);
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString TagName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				TagName = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
			{
				TagName = BatchToolHelper::GetItemString(Item, TEXT("tag_name"), TEXT("name"));
				if (TagName.IsEmpty()) TagName = BatchToolHelper::GetItemString(Item, TEXT("tag"), TEXT("tag_value"));
			}
			if (TagName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing tag_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddGameplayTag(TagName, DevComment, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("tag_name"), TagName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString TagName, DevComment;
	Args->TryGetStringField(TEXT("tag_name"), TagName);
	Args->TryGetStringField(TEXT("dev_comment"), DevComment);
	HandleAddGameplayTag(TagName, DevComment, OutJsonString, OutError);
}

void HandleRemoveGameplayTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tags"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString TagName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				TagName = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
				TagName = BatchToolHelper::GetItemString(Item, TEXT("tag_name"), TEXT("name"));
			if (TagName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing tag_name")); continue; }
			FString ItemOut, ItemErr;
			HandleRemoveGameplayTag(TagName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("tag_name"), TagName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString TagName;
	Args->TryGetStringField(TEXT("tag_name"), TagName);
	HandleRemoveGameplayTag(TagName, OutJsonString, OutError);
}

void HandleGetGameplayTagsFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetGameplayTags(OutJsonString, OutError);
}

void HandleAssignGameplayTagToBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, VariableName;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("variable_name"), VariableName);

	TArray<FString> DefaultTags;
	const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;
	if (Args->TryGetArrayField(TEXT("default_tags"), TagArray) && TagArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *TagArray)
		{
			FString TagStr;
			if (Val->TryGetString(TagStr)) DefaultTags.Add(TagStr);
		}
	}
	HandleAssignGameplayTagToBlueprint(BlueprintPath, VariableName, DefaultTags, OutJsonString, OutError);
}

void HandleAddGameplayTagsBulkFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	TArray<FString> TagNames;
	FString DevComment;
	const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("tag_names"), TagArray))
		Args->TryGetArrayField(TEXT("tags"), TagArray);
	if (TagArray)
	{
		for (const auto& Val : *TagArray)
		{
			FString T;
			if (Val->TryGetString(T)) TagNames.Add(T);
		}
	}
	Args->TryGetStringField(TEXT("dev_comment"), DevComment);
	HandleAddGameplayTagsBulk(TagNames, DevComment, OutJsonString, OutError);
}

void HandleFindReferencersByTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TagName; Args->TryGetStringField(TEXT("tag_name"), TagName);
	if (TagName.IsEmpty())
	{
		OutError = TEXT("tag_name is required");
		return;
	}

	const FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = Registry.Get();

	if (AR.IsLoadingAssets())
	{
		OutError = TEXT("AssetRegistry still scanning — wait for it to finish then retry");
		return;
	}

	const FAssetIdentifier TagIdentifier(FGameplayTag::StaticStruct(), FName(*TagName));
	TArray<FAssetIdentifier> Referencers;
	AR.GetReferencers(TagIdentifier, Referencers,
		UE::AssetRegistry::EDependencyCategory::SearchableName);

	TArray<TSharedPtr<FJsonValue>> Hits;
	for (const FAssetIdentifier& Ref : Referencers)
	{
		const FName PackageName = Ref.PackageName;
		if (PackageName.IsNone()) continue;

		TArray<FAssetData> PackageAssets;
		AR.GetAssetsByPackageName(PackageName, PackageAssets, true);
		for (const FAssetData& Data : PackageAssets)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("asset_path"), Data.GetSoftObjectPath().ToString());
			Entry->SetStringField(TEXT("class"), Data.AssetClassPath.GetAssetName().ToString());
			Hits.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("tag"), TagName);
	Result->SetNumberField(TEXT("hit_count"), Hits.Num());
	Result->SetArrayField(TEXT("references"), Hits);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	OutJsonString = Out;
}

}
