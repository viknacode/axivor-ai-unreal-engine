// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CommonUITools.h"
#include "Tools/BatchToolHelper.h"
#include "MCPToolsLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

#include "CommonButtonBase.h"
#include "CommonActivatableWidget.h"
#include "CommonTextBlock.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Input/CommonGenericInputActionDataTable.h"
#include "CommonUITypes.h"

#include "Blueprint/UserWidget.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/DataTable.h"
#include "InputAction.h"

namespace CommonUITools
{

static bool IsCommonUILoaded()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI"));
}

static void SetError(const FString& Msg, FString& OutJson, FString& OutError)
{
	OutError = Msg;
	OutJson = FString::Printf(TEXT("{\"success\":false,\"error\":\"%s\"}"),
		*Msg.Replace(TEXT("\""), TEXT("\\\"")));
}

static void BuildSuccessJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJson)
{
	Obj->SetBoolField(TEXT("success"), true);
	FString S;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	OutJson = S;
}

static bool CreateCommonUMGBlueprint(const FString& Name, const FString& SavePath,
	UClass* ParentClass, FString& OutAssetPath, FString& OutError)
{
	if (!IsCommonUILoaded())
	{
		OutError = TEXT("CommonUI module is not loaded. Enable the CommonUI plugin in your project.");
		return false;
	}

	if (Name.IsEmpty() || SavePath.IsEmpty() || !ParentClass)
	{
		OutError = TEXT("name, save_path, and parent class are required.");
		return false;
	}

	FString CleanPath = SavePath;
	while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
	FString PackagePath = CleanPath + TEXT("/") + Name;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists: %s"), *PackagePath);
		return false;
	}
	UPackage* ExistingPkg = FindPackage(nullptr, *PackagePath);
	if (ExistingPkg && FindObject<UBlueprint>(ExistingPkg, *Name))
	{
		OutAssetPath = PackagePath + TEXT(".") + Name;
		return true;
	}

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		CreatePackage(*PackagePath),
		FName(*Name),
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass());

	if (!BP)
	{
		OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed.");
		return false;
	}

	FAssetRegistryModule::AssetCreated(BP);
	BP->MarkPackageDirty();
	OutAssetPath = BP->GetPathName();
	return true;
}

void HandleCreateCommonButtonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SP   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString AssetPath, Err;
			if (CreateCommonUMGBlueprint(Name, SP, UCommonButtonBase::StaticClass(), AssetPath, Err))
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath); Batch.AddSuccess(i, E);
			}
			else { Batch.AddFailure(i, Err); }
		}
		Batch.Finalize(OutJson); return;
	}

	FString Name, SP;
	Args->TryGetStringField(TEXT("name"), Name); Args->TryGetStringField(TEXT("save_path"), SP);
	FString AssetPath;
	if (!CreateCommonUMGBlueprint(Name, SP, UCommonButtonBase::StaticClass(), AssetPath, OutError)) return;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCreateActivatableWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SP   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString AssetPath, Err;
			if (CreateCommonUMGBlueprint(Name, SP, UCommonActivatableWidget::StaticClass(), AssetPath, Err))
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath); Batch.AddSuccess(i, E);
			}
			else { Batch.AddFailure(i, Err); }
		}
		Batch.Finalize(OutJson); return;
	}

	FString Name, SP;
	Args->TryGetStringField(TEXT("name"), Name); Args->TryGetStringField(TEXT("save_path"), SP);
	FString AssetPath;
	if (!CreateCommonUMGBlueprint(Name, SP, UCommonActivatableWidget::StaticClass(), AssetPath, OutError)) return;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCreateCommonTextBlockFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SP   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString AssetPath, Err;
			if (CreateCommonUMGBlueprint(Name, SP, UUserWidget::StaticClass(), AssetPath, Err))
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath);
				E->SetStringField(TEXT("note"), TEXT("Widget Blueprint created. Add a CommonTextBlock widget inside the UMG designer."));
				Batch.AddSuccess(i, E);
			}
			else { Batch.AddFailure(i, Err); }
		}
		Batch.Finalize(OutJson); return;
	}

	FString Name, SP;
	Args->TryGetStringField(TEXT("name"), Name); Args->TryGetStringField(TEXT("save_path"), SP);
	FString AssetPath;
	if (!CreateCommonUMGBlueprint(Name, SP, UUserWidget::StaticClass(), AssetPath, OutError)) return;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("note"), TEXT("Widget Blueprint created. Add a CommonTextBlock widget inside the UMG designer."));
	BuildSuccessJson(Obj, OutJson);
}

static bool CreateCommonTextStyleBlueprint(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError)
{
	if (!IsCommonUILoaded()) { OutError = TEXT("CommonUI module is not loaded."); return false; }
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required."); return false; }

	FString CleanPath = SavePath;
	while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
	const FString PkgPath = CleanPath + TEXT("/") + Name;
	if (FPackageName::DoesPackageExist(PkgPath))
	{ OutError = FString::Printf(TEXT("Asset already exists: %s"), *PkgPath); return false; }

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		UCommonTextStyle::StaticClass(),
		CreatePackage(*PkgPath),
		FName(*Name),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (!BP) { OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed for UCommonTextStyle."); return false; }
	FAssetRegistryModule::AssetCreated(BP);
	BP->MarkPackageDirty();
	OutAssetPath = BP->GetPathName();
	return true;
}

void HandleCreateCommonTextStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			const FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			const FString SP   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString AssetPath, Err;
			if (CreateCommonTextStyleBlueprint(Name, SP, AssetPath, Err))
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath); Batch.AddSuccess(i, E);
			}
			else { Batch.AddFailure(i, Err); }
		}
		Batch.Finalize(OutJson); return;
	}

	FString Name, SP;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SP);
	FString AssetPath;
	if (!CreateCommonTextStyleBlueprint(Name, SP, AssetPath, OutError)) return;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJson);
}

static bool CreateCommonButtonStyleBlueprint(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError)
{
	if (!IsCommonUILoaded()) { OutError = TEXT("CommonUI module is not loaded."); return false; }
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required."); return false; }

	FString CleanPath = SavePath;
	while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
	const FString PkgPath = CleanPath + TEXT("/") + Name;
	if (FPackageName::DoesPackageExist(PkgPath))
	{ OutError = FString::Printf(TEXT("Asset already exists: %s"), *PkgPath); return false; }

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		UCommonButtonStyle::StaticClass(),
		CreatePackage(*PkgPath),
		FName(*Name),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (!BP) { OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed for UCommonButtonStyle."); return false; }
	FAssetRegistryModule::AssetCreated(BP);
	BP->MarkPackageDirty();
	OutAssetPath = BP->GetPathName();
	return true;
}

void HandleCreateCommonButtonStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			const FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			const FString SP   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString AssetPath, Err;
			if (CreateCommonButtonStyleBlueprint(Name, SP, AssetPath, Err))
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath); Batch.AddSuccess(i, E);
			}
			else { Batch.AddFailure(i, Err); }
		}
		Batch.Finalize(OutJson); return;
	}

	FString Name, SP;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SP);
	FString AssetPath;
	if (!CreateCommonButtonStyleBlueprint(Name, SP, AssetPath, OutError)) return;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCreateInputActionDataTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded())
	{
		SetError(TEXT("CommonUI module is not loaded."), OutJson, OutError); return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SP   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			if (Name.IsEmpty() || SP.IsEmpty()) { Batch.AddFailure(i, TEXT("name and save_path required")); continue; }

			FString CleanPath = SP;
			while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
			FString PkgPath = CleanPath + TEXT("/") + Name;
			if (FPackageName::DoesPackageExist(PkgPath)) { Batch.AddFailure(i, FString::Printf(TEXT("Exists: %s"), *PkgPath)); continue; }

			UPackage* Pkg = CreatePackage(*PkgPath);
			UCommonGenericInputActionDataTable* DT = NewObject<UCommonGenericInputActionDataTable>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
			if (!DT) { Batch.AddFailure(i, TEXT("Failed to create data table")); continue; }
			FAssetRegistryModule::AssetCreated(DT);
			DT->MarkPackageDirty();
			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), DT->GetPathName()); Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}

	FString Name, SP;
	Args->TryGetStringField(TEXT("name"), Name); Args->TryGetStringField(TEXT("save_path"), SP);
	if (Name.IsEmpty() || SP.IsEmpty()) { SetError(TEXT("name and save_path are required."), OutJson, OutError); return; }

	FString CleanPath = SP;
	while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
	FString PkgPath = CleanPath + TEXT("/") + Name;
	if (FPackageName::DoesPackageExist(PkgPath)) { SetError(FString::Printf(TEXT("Asset exists: %s"), *PkgPath), OutJson, OutError); return; }

	UPackage* Pkg = CreatePackage(*PkgPath);
	UCommonGenericInputActionDataTable* DT = NewObject<UCommonGenericInputActionDataTable>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!DT) { SetError(TEXT("Failed to create UCommonGenericInputActionDataTable."), OutJson, OutError); return; }
	FAssetRegistryModule::AssetCreated(DT);
	DT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), DT->GetPathName());
	Obj->SetStringField(TEXT("row_struct"), TEXT("FCommonInputActionDataBase"));
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddInputActionRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded())
	{
		SetError(TEXT("CommonUI module is not loaded."), OutJson, OutError); return;
	}

	FString DTPath; Args->TryGetStringField(TEXT("data_table_path"), DTPath);
	if (DTPath.IsEmpty()) { SetError(TEXT("data_table_path is required."), OutJson, OutError); return; }

	UCommonGenericInputActionDataTable* DT = LoadObject<UCommonGenericInputActionDataTable>(nullptr, *DTPath);
	if (!DT)
	{
		SetError(FString::Printf(TEXT("Data table not found: %s"), *DTPath), OutJson, OutError);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString RowName = BatchToolHelper::GetItemString(Item, TEXT("row_name"), TEXT("name"));
			if (RowName.IsEmpty()) { Batch.AddFailure(i, TEXT("row_name is required")); continue; }

			FCommonInputActionDataBase NewRow;
			DT->AddRow(FName(*RowName), NewRow);

			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("row_name"), RowName); Batch.AddSuccess(i, E);
		}
		DT->MarkPackageDirty();
		Batch.Finalize(OutJson); return;
	}

	FString RowName;
	Args->TryGetStringField(TEXT("row_name"), RowName);
	if (RowName.IsEmpty()) Args->TryGetStringField(TEXT("name"), RowName);
	if (RowName.IsEmpty()) { SetError(TEXT("row_name is required."), OutJson, OutError); return; }

	FCommonInputActionDataBase NewRow;
	DT->AddRow(FName(*RowName), NewRow);
	DT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("data_table_path"), DTPath);
	Obj->SetStringField(TEXT("row_name"), RowName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetButtonStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded())
	{
		SetError(TEXT("CommonUI module is not loaded."), OutJson, OutError); return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString BPPath    = BatchToolHelper::GetItemString(Item, TEXT("button_path"));
			FString StylePath = BatchToolHelper::GetItemString(Item, TEXT("style_class"));

			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
			if (!BP || !BP->GeneratedClass) { Batch.AddFailure(i, FString::Printf(TEXT("Blueprint not found: %s"), *BPPath)); continue; }

			UCommonButtonBase* CDO = Cast<UCommonButtonBase>(BP->GeneratedClass->GetDefaultObject());
			if (!CDO) { Batch.AddFailure(i, TEXT("Blueprint CDO is not a UCommonButtonBase")); continue; }

			UClass* StyleClass = nullptr;
			if (UBlueprint* StyleBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(StylePath)))
				StyleClass = StyleBP->GeneratedClass;
			if (!StyleClass) StyleClass = LoadObject<UClass>(nullptr, *StylePath);
			if (!StyleClass) { StyleClass = FindObject<UClass>(nullptr, *StylePath); }

			FObjectProperty* StyleProp = FindFProperty<FObjectProperty>(UCommonButtonBase::StaticClass(), TEXT("Style"));
			if (StyleProp)
			{
				TSubclassOf<UCommonButtonStyle>* StylePtr = StyleProp->ContainerPtrToValuePtr<TSubclassOf<UCommonButtonStyle>>(CDO);
				if (StylePtr) *StylePtr = StyleClass;
			}
			CDO->MarkPackageDirty();
			BP->MarkPackageDirty();

			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("button_path"), BPPath); Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}

	FString BPPath, StylePath;
	Args->TryGetStringField(TEXT("button_path"), BPPath); Args->TryGetStringField(TEXT("style_class"), StylePath);
	if (BPPath.IsEmpty()) { SetError(TEXT("button_path is required."), OutJson, OutError); return; }

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP || !BP->GeneratedClass) { SetError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath), OutJson, OutError); return; }

	UCommonButtonBase* CDO = Cast<UCommonButtonBase>(BP->GeneratedClass->GetDefaultObject());
	if (!CDO) { SetError(TEXT("Blueprint CDO is not a UCommonButtonBase."), OutJson, OutError); return; }

	UClass* StyleClass = nullptr;
	if (!StylePath.IsEmpty())
	{
		if (UBlueprint* StyleBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(StylePath)))
			StyleClass = StyleBP->GeneratedClass;
		if (!StyleClass) StyleClass = LoadObject<UClass>(nullptr, *StylePath);
		if (!StyleClass) StyleClass = FindObject<UClass>(nullptr, *StylePath);
	}

	FObjectProperty* StyleProp = FindFProperty<FObjectProperty>(UCommonButtonBase::StaticClass(), TEXT("Style"));
	if (StyleProp)
	{
		TSubclassOf<UCommonButtonStyle>* StylePtr = StyleProp->ContainerPtrToValuePtr<TSubclassOf<UCommonButtonStyle>>(CDO);
		if (StylePtr) *StylePtr = StyleClass;
	}
	CDO->MarkPackageDirty();
	BP->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("button_path"), BPPath);
	Obj->SetStringField(TEXT("style_class"), StylePath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCreateWidgetStackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	auto CreateOne = [](const FString& Name, const FString& SP, FString& OutAssetPath, FString& Err) -> bool
	{
		if (!CreateCommonUMGBlueprint(Name, SP, UCommonActivatableWidget::StaticClass(), OutAssetPath, Err))
			return false;

		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(
			StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *OutAssetPath));
		if (WBP && WBP->WidgetTree && !WBP->WidgetTree->RootWidget)
		{
			FName StackName = FName(*(Name + TEXT("_Stack")));
			UCommonActivatableWidgetStack* Stack =
				WBP->WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
					UCommonActivatableWidgetStack::StaticClass(), StackName);
			if (Stack)
			{
				WBP->WidgetTree->RootWidget = Stack;
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
				WBP->MarkPackageDirty();
			}
		}
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SP   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString AssetPath, Err;
			if (CreateOne(Name, SP, AssetPath, Err))
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath); Batch.AddSuccess(i, E);
			}
			else { Batch.AddFailure(i, Err); }
		}
		Batch.Finalize(OutJson); return;
	}

	FString Name, SP;
	Args->TryGetStringField(TEXT("name"), Name); Args->TryGetStringField(TEXT("save_path"), SP);
	FString AssetPath;
	if (!CreateOne(Name, SP, AssetPath, OutError)) return;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleGetCommonUISummaryFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& OutError)
{
	HandleGetCommonUISummary(OutJson, OutError);
}

void HandleGetCommonUISummary(FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded())
	{
		SetError(TEXT("CommonUI module is not loaded."), OutJson, OutError); return;
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> AllWidgetBPs;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint")), AllWidgetBPs, true);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& AD : AllWidgetBPs)
	{
		FString ParentClass;
		AD.GetTagValue(FName(TEXT("ParentClass")), ParentClass);
		if (ParentClass.Contains(TEXT("CommonButton")) ||
			ParentClass.Contains(TEXT("CommonActivatableWidget")) ||
			ParentClass.Contains(TEXT("CommonActivatableWidgetStack")) ||
			ParentClass.Contains(TEXT("CommonTextBlock")))
		{
			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetStringField(TEXT("asset_path"), AD.GetObjectPathString());
			Entry->SetStringField(TEXT("parent_class"), ParentClass);
			Results.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
	}

	TArray<FAssetData> StyleBPs, StyleInstances, DTAssets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")), StyleBPs, true);
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/CommonUI"), TEXT("CommonTextStyle")), StyleInstances, true);
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/CommonUI"), TEXT("CommonGenericInputActionDataTable")), DTAssets, true);

	for (const FAssetData& AD : StyleBPs)
	{
		FString ParentClass;
		AD.GetTagValue(FName(TEXT("ParentClass")), ParentClass);
		const TCHAR* TypeLabel = nullptr;
		if      (ParentClass.Contains(TEXT("CommonTextStyle")))   TypeLabel = TEXT("CommonTextStyle");
		else if (ParentClass.Contains(TEXT("CommonButtonStyle"))) TypeLabel = TEXT("CommonButtonStyle");
		if (!TypeLabel) continue;
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("asset_path"), AD.GetObjectPathString());
		Entry->SetStringField(TEXT("type"), TypeLabel);
		Entry->SetStringField(TEXT("parent_class"), ParentClass);
		Results.Add(MakeShareable(new FJsonValueObject(Entry)));
	}
	for (const FAssetData& AD : StyleInstances)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("asset_path"), AD.GetObjectPathString());
		Entry->SetStringField(TEXT("type"), TEXT("CommonTextStyle"));
		Entry->SetStringField(TEXT("note"), TEXT("legacy instance asset — recreate via create_common_text_style for full editing support"));
		Results.Add(MakeShareable(new FJsonValueObject(Entry)));
	}
	for (const FAssetData& AD : DTAssets)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("asset_path"), AD.GetObjectPathString());
		Entry->SetStringField(TEXT("type"), TEXT("CommonGenericInputActionDataTable"));
		Results.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), (double)Results.Num());
	Root->SetArrayField(TEXT("assets"), Results);
	BuildSuccessJson(Root, OutJson);
}

void HandleConfigureCommonButton(const FString& ButtonPath,
	const FString& IsEnabled, const FString& IsSelectable, const FString& IsToggleable,
	const FString& IsLocked, int32 MinWidth, int32 MinHeight, bool bRequiresHold,
	FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded()) { SetError(TEXT("CommonUI module is not loaded"), OutJson, OutError); return; }

	UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(ButtonPath));
	if (!BP || !BP->GeneratedClass)
	{ SetError(TEXT("Could not load button blueprint — compile it first with compile_blueprint"), OutJson, OutError); return; }

	UCommonButtonBase* CDO = Cast<UCommonButtonBase>(BP->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{ SetError(TEXT("Blueprint is not a CommonButtonBase subclass"), OutJson, OutError); return; }

	UClass* BtnClass = UCommonButtonBase::StaticClass();
	auto SetBoolProp = [&](UClass* C, FName Name, bool Val)
	{
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(C, Name))
			*P->ContainerPtrToValuePtr<bool>(CDO) = Val;
	};
	auto SetUInt8Prop = [&](FName Name, uint8 Val)
	{
		if (FByteProperty* P = FindFProperty<FByteProperty>(BtnClass, Name))
			*P->ContainerPtrToValuePtr<uint8>(CDO) = Val;
	};

	if (!IsEnabled.IsEmpty())    SetBoolProp(UWidget::StaticClass(), TEXT("bIsEnabled"),   IsEnabled.ToBool());
	if (!IsSelectable.IsEmpty()) SetBoolProp(BtnClass, TEXT("bSelectable"),                IsSelectable.ToBool());
	if (!IsToggleable.IsEmpty()) SetBoolProp(BtnClass, TEXT("bToggleable"),                IsToggleable.ToBool());
	if (!IsLocked.IsEmpty())     SetBoolProp(BtnClass, TEXT("bIsLocked"),                  IsLocked.ToBool());
	if (MinWidth  >= 0) SetUInt8Prop(TEXT("MinWidth"),  (uint8)FMath::Clamp(MinWidth,  0, 255));
	if (MinHeight >= 0) SetUInt8Prop(TEXT("MinHeight"), (uint8)FMath::Clamp(MinHeight, 0, 255));
	if (bRequiresHold)  SetBoolProp(BtnClass, TEXT("bRequiresHold"), true);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("button_path"), ButtonPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleConfigureCommonButtonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString ButtonPath;
	Args->TryGetStringField(TEXT("button_path"), ButtonPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString BPPath = BatchToolHelper::GetItemString(Item, TEXT("button_path")); if (BPPath.IsEmpty()) BPPath = ButtonPath;
			FString IsEn, IsSel, IsTog, IsLk; int32 MW = -1, MH = -1; bool bRH = false;
			Item->TryGetStringField(TEXT("is_enabled"), IsEn);
			Item->TryGetStringField(TEXT("is_selectable"), IsSel);
			Item->TryGetStringField(TEXT("is_toggleable"), IsTog);
			Item->TryGetStringField(TEXT("is_locked"), IsLk);
			double Tmp; if (Item->TryGetNumberField(TEXT("min_width"), Tmp)) MW = (int32)Tmp;
			if (Item->TryGetNumberField(TEXT("min_height"), Tmp)) MH = (int32)Tmp;
			Item->TryGetBoolField(TEXT("requires_hold"), bRH);
			FString ItemOut, ItemErr;
			HandleConfigureCommonButton(BPPath, IsEn, IsSel, IsTog, IsLk, MW, MH, bRH, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("button_path"), BPPath); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson); return;
	}

	FString IsEn, IsSel, IsTog, IsLk;
	int32 MW = -1, MH = -1; bool bRH = false;
	Args->TryGetStringField(TEXT("is_enabled"), IsEn);
	Args->TryGetStringField(TEXT("is_selectable"), IsSel);
	Args->TryGetStringField(TEXT("is_toggleable"), IsTog);
	Args->TryGetStringField(TEXT("is_locked"), IsLk);
	double Tmp; if (Args->TryGetNumberField(TEXT("min_width"), Tmp)) MW = (int32)Tmp;
	if (Args->TryGetNumberField(TEXT("min_height"), Tmp)) MH = (int32)Tmp;
	Args->TryGetBoolField(TEXT("requires_hold"), bRH);
	HandleConfigureCommonButton(ButtonPath, IsEn, IsSel, IsTog, IsLk, MW, MH, bRH, OutJson, OutError);
}

void HandleSetButtonInputAction(const FString& ButtonPath, const FString& InputActionPath,
	const FString& DataTablePath, const FString& RowName,
	FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded()) { SetError(TEXT("CommonUI module is not loaded"), OutJson, OutError); return; }

	UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(ButtonPath));
	if (!BP || !BP->GeneratedClass)
	{ SetError(TEXT("Could not load button blueprint — compile it first"), OutJson, OutError); return; }

	UCommonButtonBase* CDO = Cast<UCommonButtonBase>(BP->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{ SetError(TEXT("Blueprint is not a CommonButtonBase subclass"), OutJson, OutError); return; }

	UClass* BtnClass = UCommonButtonBase::StaticClass();
	if (!InputActionPath.IsEmpty())
	{
		UInputAction* IA = LoadObject<UInputAction>(nullptr, *InputActionPath);
		if (!IA) { SetError(TEXT("Could not load InputAction asset at: ") + InputActionPath, OutJson, OutError); return; }
		if (FObjectProperty* P = FindFProperty<FObjectProperty>(BtnClass, TEXT("TriggeringEnhancedInputAction")))
			P->SetObjectPropertyValue(P->ContainerPtrToValuePtr<void>(CDO), IA);
	}
	else if (!DataTablePath.IsEmpty() && !RowName.IsEmpty())
	{
		UDataTable* DT = LoadObject<UDataTable>(nullptr, *DataTablePath);
		if (!DT) { SetError(TEXT("Could not load DataTable at: ") + DataTablePath, OutJson, OutError); return; }
		if (FStructProperty* P = FindFProperty<FStructProperty>(BtnClass, TEXT("TriggeringInputAction")))
		{
			FDataTableRowHandle* Handle = P->ContainerPtrToValuePtr<FDataTableRowHandle>(CDO);
			if (Handle) { Handle->DataTable = DT; Handle->RowName = FName(*RowName); }
		}
	}
	else
	{
		SetError(TEXT("Provide input_action_path OR (data_table_path + row_name)"), OutJson, OutError); return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("button_path"), ButtonPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetButtonInputActionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString ButtonPath, IAPath, DTPath, RowName;
	Args->TryGetStringField(TEXT("button_path"), ButtonPath);
	Args->TryGetStringField(TEXT("input_action_path"), IAPath);
	Args->TryGetStringField(TEXT("data_table_path"), DTPath);
	Args->TryGetStringField(TEXT("row_name"), RowName);
	HandleSetButtonInputAction(ButtonPath, IAPath, DTPath, RowName, OutJson, OutError);
}

void HandleSetCommonTextStyleProperties(const FString& StylePath,
	float FontSize, float R, float G, float B, float A,
	float ShadowOffsetX, float ShadowOffsetY,
	float ShadowR, float ShadowG, float ShadowB, float ShadowA,
	bool bSetUsesDropShadow, bool bUsesDropShadow,
	float LineHeightPercentage,
	float MarginLeft, float MarginTop, float MarginRight, float MarginBottom,
	FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded()) { SetError(TEXT("CommonUI module is not loaded"), OutJson, OutError); return; }

	UBlueprint* StyleBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(StylePath));
	if (!StyleBP || !StyleBP->GeneratedClass)
	{ SetError(FString::Printf(TEXT("Could not load CommonTextStyle blueprint at: %s — compile it first if just created"), *StylePath), OutJson, OutError); return; }
	UCommonTextStyle* Style = Cast<UCommonTextStyle>(StyleBP->GeneratedClass->GetDefaultObject());
	if (!Style) { SetError(TEXT("Asset is not a UCommonTextStyle subclass"), OutJson, OutError); return; }

	UClass* StyleClass = UCommonTextStyle::StaticClass();

	if (FontSize > 0.0f)
	{
		if (FProperty* P = StyleClass->FindPropertyByName(TEXT("Font")))
		{
			FSlateFontInfo* FontPtr = P->ContainerPtrToValuePtr<FSlateFontInfo>(Style);
			if (FontPtr) FontPtr->Size = FontSize;
		}
	}
	if (R >= 0.0f)
	{
		if (FProperty* P = StyleClass->FindPropertyByName(TEXT("Color")))
			*P->ContainerPtrToValuePtr<FLinearColor>(Style) = FLinearColor(R, G, B, A >= 0.0f ? A : 1.0f);
	}
	if (bSetUsesDropShadow)
	{
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(StyleClass, TEXT("bUsesDropShadow")))
			*P->ContainerPtrToValuePtr<bool>(Style) = bUsesDropShadow;
	}
	if (ShadowOffsetX >= 0.0f)
	{
		if (FProperty* P = StyleClass->FindPropertyByName(TEXT("ShadowOffset")))
			*P->ContainerPtrToValuePtr<FVector2D>(Style) = FVector2D(ShadowOffsetX, ShadowOffsetY >= 0.0f ? ShadowOffsetY : 0.0f);
	}
	if (ShadowR >= 0.0f)
	{
		if (FProperty* P = StyleClass->FindPropertyByName(TEXT("ShadowColor")))
			*P->ContainerPtrToValuePtr<FLinearColor>(Style) = FLinearColor(ShadowR, ShadowG, ShadowB, ShadowA >= 0.0f ? ShadowA : 1.0f);
	}
	if (LineHeightPercentage >= 0.0f)
	{
		if (FFloatProperty* P = FindFProperty<FFloatProperty>(StyleClass, TEXT("LineHeightPercentage")))
			*P->ContainerPtrToValuePtr<float>(Style) = LineHeightPercentage;
	}
	if (MarginLeft >= 0.0f)
	{
		if (FProperty* P = StyleClass->FindPropertyByName(TEXT("Margin")))
			*P->ContainerPtrToValuePtr<FMargin>(Style) = FMargin(MarginLeft, MarginTop, MarginRight, MarginBottom);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(StyleBP);
	StyleBP->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("style_path"), StylePath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetCommonTextStylePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString StylePath;
	Args->TryGetStringField(TEXT("style_path"), StylePath);

	double FontSizeD = -1, RD = -1, GD = -1, BD = -1, AD = -1;
	double SOX = -1, SOY = -1, SRD = -1, SGD = -1, SBD = -1, SAD = -1;
	double LHP = -1, ML = -1, MT = -1, MR = -1, MB = -1;
	bool bSetDrop = false, bDrop = false;

	Args->TryGetNumberField(TEXT("font_size"), FontSizeD);
	Args->TryGetNumberField(TEXT("color_r"), RD);
	Args->TryGetNumberField(TEXT("color_g"), GD);
	Args->TryGetNumberField(TEXT("color_b"), BD);
	Args->TryGetNumberField(TEXT("color_a"), AD);
	Args->TryGetNumberField(TEXT("shadow_offset_x"), SOX);
	Args->TryGetNumberField(TEXT("shadow_offset_y"), SOY);
	Args->TryGetNumberField(TEXT("shadow_r"), SRD);
	Args->TryGetNumberField(TEXT("shadow_g"), SGD);
	Args->TryGetNumberField(TEXT("shadow_b"), SBD);
	Args->TryGetNumberField(TEXT("shadow_a"), SAD);
	Args->TryGetNumberField(TEXT("line_height_percentage"), LHP);

	FString MarginStr;
	if (Args->TryGetStringField(TEXT("margin"), MarginStr) && !MarginStr.IsEmpty())
	{
		TArray<FString> Parts;
		MarginStr.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() >= 4) { ML = FCString::Atof(*Parts[0]); MT = FCString::Atof(*Parts[1]); MR = FCString::Atof(*Parts[2]); MB = FCString::Atof(*Parts[3]); }
		else if (Parts.Num() == 1) { ML = MT = MR = MB = FCString::Atof(*Parts[0]); }
	}

	if (Args->TryGetBoolField(TEXT("uses_drop_shadow"), bDrop)) bSetDrop = true;

	HandleSetCommonTextStyleProperties(StylePath,
		(float)FontSizeD, (float)RD, (float)GD, (float)BD, (float)AD,
		(float)SOX, (float)SOY, (float)SRD, (float)SGD, (float)SBD, (float)SAD,
		bSetDrop, bDrop, (float)LHP,
		(float)ML, (float)MT, (float)MR, (float)MB,
		OutJson, OutError);
}

void HandleConfigureActivatableWidget(const FString& WidgetPath,
	const FString& AutoActivate, const FString& IsBackHandler,
	const FString& IsModal, const FString& AutoRestoreFocus,
	const FString& ActivatedVisibility, const FString& DeactivatedVisibility,
	bool bSetActivatedVis, bool bSetDeactivatedVis,
	FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded()) { SetError(TEXT("CommonUI module is not loaded"), OutJson, OutError); return; }

	UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!BP || !BP->GeneratedClass)
	{ SetError(TEXT("Could not load widget blueprint — compile it first"), OutJson, OutError); return; }

	UCommonActivatableWidget* CDO = Cast<UCommonActivatableWidget>(BP->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{ SetError(TEXT("Blueprint is not a CommonActivatableWidget subclass"), OutJson, OutError); return; }

	UClass* WAClass = UCommonActivatableWidget::StaticClass();

	auto SetBoolProp = [&](const TCHAR* PropName, bool Val)
	{
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(WAClass, PropName))
			*P->ContainerPtrToValuePtr<bool>(CDO) = Val;
	};

	if (!AutoActivate.IsEmpty())     SetBoolProp(TEXT("bAutoActivate"),     AutoActivate.ToBool());
	if (!IsBackHandler.IsEmpty())    SetBoolProp(TEXT("bIsBackHandler"),    IsBackHandler.ToBool());
	if (!IsModal.IsEmpty())          SetBoolProp(TEXT("bIsModal"),          IsModal.ToBool());
	if (!AutoRestoreFocus.IsEmpty()) SetBoolProp(TEXT("bAutoRestoreFocus"), AutoRestoreFocus.ToBool());

	auto ParseVis = [](const FString& S) -> ESlateVisibility
	{
		FString L = S.ToLower();
		if (L == TEXT("collapsed")) return ESlateVisibility::Collapsed;
		if (L == TEXT("hidden"))    return ESlateVisibility::Hidden;
		return ESlateVisibility::Visible;
	};

	if (bSetActivatedVis && !ActivatedVisibility.IsEmpty())
	{
		SetBoolProp(TEXT("bSetVisibilityOnActivated"), true);
		if (FByteProperty* P = FindFProperty<FByteProperty>(WAClass, TEXT("ActivatedVisibility")))
			P->SetPropertyValue_InContainer(CDO, (uint8)ParseVis(ActivatedVisibility));
	}
	if (bSetDeactivatedVis && !DeactivatedVisibility.IsEmpty())
	{
		SetBoolProp(TEXT("bSetVisibilityOnDeactivated"), true);
		if (FByteProperty* P = FindFProperty<FByteProperty>(WAClass, TEXT("DeactivatedVisibility")))
			P->SetPropertyValue_InContainer(CDO, (uint8)ParseVis(DeactivatedVisibility));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("widget_path"), WidgetPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleConfigureActivatableWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString WidgetPath;
	Args->TryGetStringField(TEXT("widget_path"), WidgetPath);

	FString AutoAct, IsBack, IsMod, AutoRF, ActVis, DeactVis;
	Args->TryGetStringField(TEXT("auto_activate"),          AutoAct);
	Args->TryGetStringField(TEXT("is_back_handler"),        IsBack);
	Args->TryGetStringField(TEXT("is_modal"),               IsMod);
	Args->TryGetStringField(TEXT("auto_restore_focus"),     AutoRF);
	Args->TryGetStringField(TEXT("activated_visibility"),   ActVis);
	Args->TryGetStringField(TEXT("deactivated_visibility"), DeactVis);

	bool bSetAct  = !ActVis.IsEmpty();
	bool bSetDeact = !DeactVis.IsEmpty();

	HandleConfigureActivatableWidget(WidgetPath, AutoAct, IsBack, IsMod, AutoRF,
		ActVis, DeactVis, bSetAct, bSetDeact, OutJson, OutError);
}

void HandleSetInputActionRowDataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded()) { SetError(TEXT("CommonUI module is not loaded"), OutJson, OutError); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString DTPath, RowName;
	Args->TryGetStringField(TEXT("data_table_path"), DTPath);
	Args->TryGetStringField(TEXT("row_name"), RowName);
	if (DTPath.IsEmpty() || RowName.IsEmpty())
	{ SetError(TEXT("data_table_path and row_name are required"), OutJson, OutError); return; }

	UCommonGenericInputActionDataTable* DT = LoadObject<UCommonGenericInputActionDataTable>(nullptr, *DTPath);
	if (!DT) { SetError(FString::Printf(TEXT("Data table not found: %s"), *DTPath), OutJson, OutError); return; }

	FCommonInputActionDataBase* Row = DT->FindRow<FCommonInputActionDataBase>(FName(*RowName), TEXT("set_input_action_row_data"));
	if (!Row)
	{ SetError(FString::Printf(TEXT("Row '%s' not found. Add it via add_input_action_row first."), *RowName), OutJson, OutError); return; }

	UScriptStruct* Struct = FCommonInputActionDataBase::StaticStruct();
	TArray<FString> Applied;

	FString DisplayName, HoldDisplayName;
	double NavBarPriority = 0;
	if (Args->TryGetStringField(TEXT("display_name"), DisplayName))
	{ Row->DisplayName = FText::FromString(DisplayName); Applied.Add(TEXT("display_name")); }
	if (Args->TryGetStringField(TEXT("hold_display_name"), HoldDisplayName))
	{ Row->HoldDisplayName = FText::FromString(HoldDisplayName); Applied.Add(TEXT("hold_display_name")); }
	if (Args->TryGetNumberField(TEXT("nav_bar_priority"), NavBarPriority))
	{ Row->NavBarPriority = (int32)NavBarPriority; Applied.Add(TEXT("nav_bar_priority")); }

	auto WriteFKey = [Struct, Row](const TCHAR* SubStructName, const FString& KeyName) -> bool
	{
		FStructProperty* SubP = FindFProperty<FStructProperty>(Struct, SubStructName);
		if (!SubP) return false;
		void* SubPtr = SubP->ContainerPtrToValuePtr<void>(Row);
		FProperty* KeyProp = FindFProperty<FProperty>(SubP->Struct, TEXT("Key"));
		if (!KeyProp) return false;
		FKey* KeyFieldPtr = (FKey*)KeyProp->ContainerPtrToValuePtr<void>(SubPtr);
		*KeyFieldPtr = FKey(*KeyName);
		return true;
	};
	auto WriteFloat = [Struct, Row](const TCHAR* SubStructName, const TCHAR* FieldName, float Value) -> bool
	{
		FStructProperty* SubP = FindFProperty<FStructProperty>(Struct, SubStructName);
		if (!SubP) return false;
		void* SubPtr = SubP->ContainerPtrToValuePtr<void>(Row);
		FFloatProperty* FP = FindFProperty<FFloatProperty>(SubP->Struct, FieldName);
		if (!FP) return false;
		*FP->ContainerPtrToValuePtr<float>(SubPtr) = Value;
		return true;
	};

	FString KeyboardKey, GamepadKey;
	double HoldTime = 0, HoldRollback = 0;
	if (Args->TryGetStringField(TEXT("keyboard_key"), KeyboardKey))
	{ if (WriteFKey(TEXT("KeyboardInputTypeInfo"), KeyboardKey)) Applied.Add(TEXT("keyboard_key")); }
	if (Args->TryGetStringField(TEXT("gamepad_key"), GamepadKey))
	{ if (WriteFKey(TEXT("DefaultGamepadInputTypeInfo"), GamepadKey)) Applied.Add(TEXT("gamepad_key")); }
	if (Args->TryGetNumberField(TEXT("hold_time"), HoldTime))
	{
		bool bAny = false;
		bAny |= WriteFloat(TEXT("KeyboardInputTypeInfo"),       TEXT("HoldTime"), (float)HoldTime);
		bAny |= WriteFloat(TEXT("DefaultGamepadInputTypeInfo"), TEXT("HoldTime"), (float)HoldTime);
		if (bAny) Applied.Add(TEXT("hold_time"));
	}
	if (Args->TryGetNumberField(TEXT("hold_rollback_time"), HoldRollback))
	{
		bool bAny = false;
		bAny |= WriteFloat(TEXT("KeyboardInputTypeInfo"),       TEXT("HoldRollbackTime"), (float)HoldRollback);
		bAny |= WriteFloat(TEXT("DefaultGamepadInputTypeInfo"), TEXT("HoldRollbackTime"), (float)HoldRollback);
		if (bAny) Applied.Add(TEXT("hold_rollback_time"));
	}

	if (Applied.Num() == 0)
	{
		SetError(TEXT("No fields supplied. Pass any of: display_name, hold_display_name, nav_bar_priority, keyboard_key, gamepad_key, hold_time, hold_rollback_time."), OutJson, OutError);
		return;
	}

	DT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("data_table_path"), DTPath);
	Obj->SetStringField(TEXT("row_name"), RowName);
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FString& K : Applied) AppliedJson.Add(MakeShared<FJsonValueString>(K));
	Obj->SetArrayField(TEXT("applied"), AppliedJson);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetButtonTextStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsCommonUILoaded()) { SetError(TEXT("CommonUI module is not loaded"), OutJson, OutError); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString StylePath, TextStylePath, State = TEXT("Normal");
	if (!Args->TryGetStringField(TEXT("button_style_path"), StylePath))
		Args->TryGetStringField(TEXT("style_path"), StylePath);
	Args->TryGetStringField(TEXT("text_style_class"), TextStylePath);
	Args->TryGetStringField(TEXT("state"), State);
	if (StylePath.IsEmpty() || TextStylePath.IsEmpty())
	{ SetError(TEXT("button_style_path and text_style_class are required (button_style_path is a UCommonButtonStyle BP, not the button widget)"), OutJson, OutError); return; }

	UBlueprint* StyleBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(StylePath));
	if (!StyleBP || !StyleBP->GeneratedClass)
	{ SetError(TEXT("Could not load button style blueprint — compile it first"), OutJson, OutError); return; }
	UCommonButtonStyle* CDO = Cast<UCommonButtonStyle>(StyleBP->GeneratedClass->GetDefaultObject());
	if (!CDO) { SetError(TEXT("Blueprint is not a UCommonButtonStyle subclass"), OutJson, OutError); return; }

	UClass* StyleClass = nullptr;
	if (UBlueprint* TextStyleBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(TextStylePath)))
		StyleClass = TextStyleBP->GeneratedClass;
	if (!StyleClass) StyleClass = LoadObject<UClass>(nullptr, *TextStylePath);
	if (!StyleClass) StyleClass = FindObject<UClass>(nullptr, *TextStylePath);
	if (!StyleClass)
	{ SetError(FString::Printf(TEXT("Text style class not found: %s — pass the UCommonTextStyle Blueprint asset path"), *TextStylePath), OutJson, OutError); return; }

	UClass* BtnStyleClass = UCommonButtonStyle::StaticClass();
	auto SetSlot = [&](const TCHAR* PropName) -> bool
	{
		FObjectProperty* P = FindFProperty<FObjectProperty>(BtnStyleClass, PropName);
		if (!P) return false;
		TSubclassOf<UCommonTextStyle>* Ptr = P->ContainerPtrToValuePtr<TSubclassOf<UCommonTextStyle>>(CDO);
		if (Ptr) *Ptr = StyleClass;
		return true;
	};

	const FString S = State.ToLower();
	TArray<FString> Applied;
	if (S == TEXT("all"))
	{
		if (SetSlot(TEXT("NormalTextStyle")))           Applied.Add(TEXT("Normal"));
		if (SetSlot(TEXT("NormalHoveredTextStyle")))    Applied.Add(TEXT("Hovered"));
		if (SetSlot(TEXT("SelectedTextStyle")))         Applied.Add(TEXT("Selected"));
		if (SetSlot(TEXT("SelectedHoveredTextStyle")))  Applied.Add(TEXT("SelectedHovered"));
		if (SetSlot(TEXT("DisabledTextStyle")))         Applied.Add(TEXT("Disabled"));
	}
	else
	{
		const TCHAR* Prop = nullptr;
		if      (S == TEXT("normal"))           Prop = TEXT("NormalTextStyle");
		else if (S == TEXT("hovered"))          Prop = TEXT("NormalHoveredTextStyle");
		else if (S == TEXT("selected"))         Prop = TEXT("SelectedTextStyle");
		else if (S == TEXT("selectedhovered"))  Prop = TEXT("SelectedHoveredTextStyle");
		else if (S == TEXT("disabled"))         Prop = TEXT("DisabledTextStyle");
		else
		{
			SetError(TEXT("state must be one of: Normal | Hovered | Selected | SelectedHovered | Disabled | all"), OutJson, OutError);
			return;
		}
		if (SetSlot(Prop)) Applied.Add(State);
	}

	if (Applied.Num() == 0)
	{ SetError(TEXT("No text style slot updated — property not found on UCommonButtonStyle"), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(StyleBP);
	StyleBP->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("button_style_path"), StylePath);
	Obj->SetStringField(TEXT("text_style_class"), TextStylePath);
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FString& K : Applied) AppliedJson.Add(MakeShared<FJsonValueString>(K));
	Obj->SetArrayField(TEXT("applied"), AppliedJson);
	BuildSuccessJson(Obj, OutJson);
}

}
