// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/MVVMTools.h"
#include "Tools/BatchToolHelper.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "MCPToolsLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"

#include "MVVMViewModelBase.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMFieldVariant.h"

namespace MVVMTools
{

static const TCHAR* MVVMPluginNotEnabledError =
	TEXT("ModelViewViewModel plugin is not enabled. Enable it in the .uproject under Plugins → ModelViewViewModel.");

static bool IsMVVMLoaded()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("ModelViewViewModelBlueprint"));
}

static void EmitJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJson)
{
	FString S;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	OutJson = S;
}

static UClass* ResolveClassByPath(const FString& Path)
{
	if (Path.IsEmpty()) return nullptr;
	if (UClass* Direct = LoadObject<UClass>(nullptr, *Path)) return Direct;
	if (UObject* Asset = UEditorAssetLibrary::LoadAsset(Path))
	{
		if (UBlueprint* BP = Cast<UBlueprint>(Asset)) return BP->GeneratedClass;
		if (UClass* AsClass = Cast<UClass>(Asset))    return AsClass;
	}
	if (UClass* C = FindFirstObject<UClass>(*Path, EFindFirstObjectOptions::None, ELogVerbosity::NoLogging))
		return C;
	return nullptr;
}

static UWidgetBlueprint* LoadWidgetBP(const FString& Path)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
	return Cast<UWidgetBlueprint>(Asset);
}

static UMVVMBlueprintView* GetOrCreateView(UWidgetBlueprint* WBP)
{
	UMVVMWidgetBlueprintExtension_View* Ext =
		UWidgetBlueprintExtension::RequestExtension<UMVVMWidgetBlueprintExtension_View>(WBP);
	if (!Ext) return nullptr;
	if (!Ext->GetBlueprintView())
	{
		Ext->CreateBlueprintViewInstance();
	}
	return Ext->GetBlueprintView();
}

static EMVVMBlueprintViewModelContextCreationType ParseCreationType(const FString& In)
{
	const FString L = In.ToLower();
	if (L == TEXT("manual"))                       return EMVVMBlueprintViewModelContextCreationType::Manual;
	if (L == TEXT("globalviewmodelcollection") ||
		L == TEXT("global"))                       return EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection;
	if (L == TEXT("propertypath") ||
		L == TEXT("property_path"))                return EMVVMBlueprintViewModelContextCreationType::PropertyPath;
	if (L == TEXT("resolver"))                     return EMVVMBlueprintViewModelContextCreationType::Resolver;
	return EMVVMBlueprintViewModelContextCreationType::CreateInstance;
}

static EMVVMBindingMode ParseBindingMode(const FString& In)
{
	const FString L = In.ToLower();
	if (L == TEXT("onetimetodestination") ||
		L == TEXT("onetime"))                  return EMVVMBindingMode::OneTimeToDestination;
	if (L == TEXT("twoway"))                   return EMVVMBindingMode::TwoWay;
	if (L == TEXT("onewaytosource"))           return EMVVMBindingMode::OneWayToSource;
	return EMVVMBindingMode::OneWayToDestination;
}

static const TCHAR* BindingModeToString(EMVVMBindingMode M)
{
	switch (M)
	{
		case EMVVMBindingMode::OneTimeToDestination: return TEXT("OneTimeToDestination");
		case EMVVMBindingMode::OneWayToDestination:  return TEXT("OneWayToDestination");
		case EMVVMBindingMode::TwoWay:               return TEXT("TwoWay");
		case EMVVMBindingMode::OneTimeToSource:      return TEXT("OneTimeToSource");
		case EMVVMBindingMode::OneWayToSource:       return TEXT("OneWayToSource");
	}
	return TEXT("Unknown");
}

static const TCHAR* CreationTypeToString(EMVVMBlueprintViewModelContextCreationType T)
{
	switch (T)
	{
		case EMVVMBlueprintViewModelContextCreationType::Manual:                     return TEXT("Manual");
		case EMVVMBlueprintViewModelContextCreationType::CreateInstance:             return TEXT("CreateInstance");
		case EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection:  return TEXT("GlobalViewModelCollection");
		case EMVVMBlueprintViewModelContextCreationType::PropertyPath:               return TEXT("PropertyPath");
		case EMVVMBlueprintViewModelContextCreationType::Resolver:                   return TEXT("Resolver");
	}
	return TEXT("Unknown");
}

static bool CreateViewModelInternal(const FString& Name, const FString& SavePath,
	UClass* ParentClass, FString& OutAssetPath, FString& OutError)
{
	if (!IsMVVMLoaded()) { OutError = MVVMPluginNotEnabledError; return false; }
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

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, CreatePackage(*PackagePath),
		FName(*Name), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!BP) { OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed."); return false; }

	FAssetRegistryModule::AssetCreated(BP);
	BP->MarkPackageDirty();
	OutAssetPath = BP->GetPathName();
	return true;
}

void HandleCreateViewModelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString ParentPath;
	Args->TryGetStringField(TEXT("parent_class"), ParentPath);
	UClass* ParentClass = ParentPath.IsEmpty()
		? UMVVMViewModelBase::StaticClass()
		: ResolveClassByPath(ParentPath);
	if (!ParentClass) { OutError = FString::Printf(TEXT("Could not resolve parent_class '%s'"), *ParentPath); return; }
	if (!ParentClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
	{
		OutError = FString::Printf(TEXT("parent_class '%s' must derive from UMVVMViewModelBase"), *ParentClass->GetName());
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("view_models"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString N = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SP = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			if (N.IsEmpty() || SP.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name or save_path")); continue; }
			FString AssetPath, Err;
			if (CreateViewModelInternal(N, SP, ParentClass, AssetPath, Err))
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("asset_path"), AssetPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);

	FString AssetPath;
	if (!CreateViewModelInternal(Name, SavePath, ParentClass, AssetPath, OutError)) return;

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	EmitJson(Obj, OutJson);
}

static void AddFieldNotifyToVariable(UBlueprint* BP, FName VarName)
{
	FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, nullptr, TEXT("FieldNotify"), TEXT("true"));
	for (FBPVariableDescription& Desc : BP->NewVariables)
	{
		if (Desc.VarName == VarName)
		{
			Desc.PropertyFlags |= CPF_BlueprintVisible;
			Desc.PropertyFlags &= ~CPF_BlueprintReadOnly;
			break;
		}
	}
}

void HandleAddViewModelFieldFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsMVVMLoaded()) { OutError = MVVMPluginNotEnabledError; return; }

	FString VMPath;
	Args->TryGetStringField(TEXT("view_model_path"), VMPath);
	if (VMPath.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), VMPath);

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(VMPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *VMPath); return; }
	if (!BP->ParentClass || !BP->ParentClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Blueprint at '%s' is not a UMVVMViewModelBase subclass — use create_view_model first"), *VMPath);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("fields"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString VName = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("var_name"));
			FString VType = BatchToolHelper::GetItemString(Item, TEXT("type"), TEXT("var_type"));
			FString VDef  = BatchToolHelper::GetItemString(Item, TEXT("default_value"));
			FString VCat  = BatchToolHelper::GetItemString(Item, TEXT("category"));
			if (VName.IsEmpty() || VType.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name or type")); continue; }
			TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
			AddArgs->SetStringField(TEXT("blueprint_path"),   VMPath);
			AddArgs->SetStringField(TEXT("variable_name"),    VName);
			AddArgs->SetStringField(TEXT("variable_type"),    VType);
			if (!VDef.IsEmpty()) AddArgs->SetStringField(TEXT("default_value"), VDef);
			if (!VCat.IsEmpty()) AddArgs->SetStringField(TEXT("category"), VCat);
			const FUECPToolResult AddR = IUECPCoreModule::Get().GetToolDispatcher()
				.ExecuteFromArgs(TEXT("add_variable"), AddArgs);
			if (!AddR.ErrorMessage.IsEmpty()) { Batch.AddFailure(i, AddR.ErrorMessage); continue; }
			AddFieldNotifyToVariable(BP, FName(*VName));
			auto Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("name"), VName);
			Batch.AddSuccess(i, Extra);
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		BP->MarkPackageDirty();
		Batch.Finalize(OutJson);
		return;
	}

	FString VarName, VarType, DefVal, Category;
	Args->TryGetStringField(TEXT("var_name"), VarName);
	if (VarName.IsEmpty()) Args->TryGetStringField(TEXT("name"), VarName);
	Args->TryGetStringField(TEXT("var_type"), VarType);
	if (VarType.IsEmpty()) Args->TryGetStringField(TEXT("type"), VarType);
	Args->TryGetStringField(TEXT("default_value"), DefVal);
	Args->TryGetStringField(TEXT("category"), Category);

	if (VarName.IsEmpty() || VarType.IsEmpty()) { OutError = TEXT("var_name and var_type are required"); return; }

	{
		TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
		AddArgs->SetStringField(TEXT("blueprint_path"),   VMPath);
		AddArgs->SetStringField(TEXT("variable_name"),    VarName);
		AddArgs->SetStringField(TEXT("variable_type"),    VarType);
		if (!DefVal.IsEmpty())   AddArgs->SetStringField(TEXT("default_value"), DefVal);
		if (!Category.IsEmpty()) AddArgs->SetStringField(TEXT("category"), Category);
		const FUECPToolResult AddR = IUECPCoreModule::Get().GetToolDispatcher()
			.ExecuteFromArgs(TEXT("add_variable"), AddArgs);
		OutError = AddR.ErrorMessage;
	}
	if (!OutError.IsEmpty()) return;

	AddFieldNotifyToVariable(BP, FName(*VarName));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	BP->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("view_model_path"), VMPath);
	Obj->SetStringField(TEXT("name"), VarName);
	Obj->SetStringField(TEXT("type"), VarType);
	Obj->SetBoolField(TEXT("field_notify"), true);
	EmitJson(Obj, OutJson);
}

void HandleAddWidgetViewModelContextFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsMVVMLoaded()) { OutError = MVVMPluginNotEnabledError; return; }

	FString WidgetPath, VMClassPath, VMName, CreationStr, PropPath, GlobalId;
	bool bOptional = false;
	Args->TryGetStringField(TEXT("widget_path"), WidgetPath);
	Args->TryGetStringField(TEXT("view_model_class"), VMClassPath);
	Args->TryGetStringField(TEXT("view_model_name"), VMName);
	Args->TryGetStringField(TEXT("creation_type"), CreationStr);
	Args->TryGetStringField(TEXT("property_path"), PropPath);
	Args->TryGetStringField(TEXT("global_identifier"), GlobalId);
	Args->TryGetBoolField(TEXT("optional"), bOptional);

	UWidgetBlueprint* WBP = LoadWidgetBP(WidgetPath);
	if (!WBP) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at '%s'"), *WidgetPath); return; }

	UClass* VMClass = ResolveClassByPath(VMClassPath);
	if (!VMClass) { OutError = FString::Printf(TEXT("Could not resolve view_model_class '%s'"), *VMClassPath); return; }
	if (!VMClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
	{
		OutError = FString::Printf(TEXT("'%s' must derive from UMVVMViewModelBase"), *VMClass->GetName());
		return;
	}
	if (VMName.IsEmpty()) VMName = VMClass->GetName();

	UMVVMBlueprintView* View = GetOrCreateView(WBP);
	if (!View) { OutError = TEXT("Failed to create MVVM view extension on widget"); return; }

	if (View->FindViewModel(FName(*VMName)))
	{
		OutError = FString::Printf(TEXT("Viewmodel context '%s' already exists on this widget"), *VMName);
		return;
	}

	FMVVMBlueprintViewModelContext Ctx(VMClass, FName(*VMName));
	Ctx.CreationType = ParseCreationType(CreationStr);
	Ctx.GlobalViewModelIdentifier = FName(*GlobalId);
	Ctx.ViewModelPropertyPath = PropPath;
	Ctx.bOptional = bOptional || (Ctx.CreationType == EMVVMBlueprintViewModelContextCreationType::Manual);

	View->AddViewModel(Ctx);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	WBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("view_model_name"), VMName);
	Obj->SetStringField(TEXT("view_model_class"), VMClass->GetName());
	Obj->SetStringField(TEXT("creation_type"), CreationTypeToString(Ctx.CreationType));
	EmitJson(Obj, OutJson);
}

void HandleRemoveWidgetViewModelContextFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsMVVMLoaded()) { OutError = MVVMPluginNotEnabledError; return; }

	FString WidgetPath, VMName;
	Args->TryGetStringField(TEXT("widget_path"), WidgetPath);
	Args->TryGetStringField(TEXT("view_model_name"), VMName);

	UWidgetBlueprint* WBP = LoadWidgetBP(WidgetPath);
	if (!WBP) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at '%s'"), *WidgetPath); return; }

	UMVVMWidgetBlueprintExtension_View* Ext =
		UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WBP);
	if (!Ext || !Ext->GetBlueprintView())
	{
		OutError = TEXT("This widget has no MVVM view extension — nothing to remove");
		return;
	}

	UMVVMBlueprintView* View = Ext->GetBlueprintView();
	const FMVVMBlueprintViewModelContext* Found = View->FindViewModel(FName(*VMName));
	if (!Found) { OutError = FString::Printf(TEXT("Viewmodel '%s' not found"), *VMName); return; }

	const FGuid Id = Found->GetViewModelId();
	if (!View->RemoveViewModel(Id))
	{
		OutError = FString::Printf(TEXT("Failed to remove viewmodel '%s'"), *VMName);
		return;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	WBP->MarkPackageDirty();

	OutJson = FString::Printf(TEXT("{\"success\":true,\"view_model_name\":\"%s\"}"), *VMName);
}

static UWidget* FindWidgetByName(UWidgetBlueprint* WBP, FName Name)
{
	if (!WBP || !WBP->WidgetTree) return nullptr;
	UWidget* Found = nullptr;
	WBP->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		if (W && W->GetFName() == Name) Found = W;
	});
	return Found;
}

static bool AppendPropertyChain(FMVVMBlueprintPropertyPath& OutPath, UStruct* StartContext,
	const FString& DottedPath, const UBlueprint* BPCtx, FString& OutError)
{
	if (DottedPath.IsEmpty())
	{
		OutError = TEXT("Property path is empty");
		return false;
	}
	if (!StartContext)
	{
		OutError = TEXT("Cannot resolve property path against a null class");
		return false;
	}

	TArray<FString> Segments;
	DottedPath.ParseIntoArray(Segments, TEXT("."), true);

	UStruct* Current = StartContext;
	for (int32 i = 0; i < Segments.Num(); i++)
	{
		const FString& Seg = Segments[i];
		if (!Current)
		{
			OutError = FString::Printf(
				TEXT("Cannot continue '%s' past segment %d — previous segment is not a struct/object property"),
				*DottedPath, i);
			return false;
		}

		FProperty* Prop = FindFProperty<FProperty>(Current, FName(*Seg));
		UFunction* Func = nullptr;
		if (!Prop)
		{
			if (UClass* AsClass = Cast<UClass>(Current))
			{
				Func = AsClass->FindFunctionByName(FName(*Seg));
			}
		}
		if (!Prop && !Func)
		{
			OutError = FString::Printf(
				TEXT("Segment '%s' not found on %s (path '%s')"),
				*Seg, *Current->GetName(), *DottedPath);
			return false;
		}

		OutPath.AppendPropertyPath(BPCtx,
			Prop ? UE::MVVM::FMVVMConstFieldVariant(Prop)
			     : UE::MVVM::FMVVMConstFieldVariant(Func));

		if (i + 1 < Segments.Num())
		{
			UStruct* Next = nullptr;
			FProperty* AdvanceFrom = Prop;
			if (!AdvanceFrom && Func)
			{
				AdvanceFrom = Func->GetReturnProperty();
			}
			if (FStructProperty* SP = CastField<FStructProperty>(AdvanceFrom))
			{
				Next = SP->Struct;
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(AdvanceFrom))
			{
				Next = OP->PropertyClass;
			}
			if (!Next)
			{
				OutError = FString::Printf(
					TEXT("Cannot chain through '%s' (segment %d of '%s') — only struct, object, or class-returning function segments support chaining"),
					*Seg, i, *DottedPath);
				return false;
			}
			Current = Next;
		}
	}
	return true;
}

void HandleAddWidgetBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsMVVMLoaded()) { OutError = MVVMPluginNotEnabledError; return; }

	FString WidgetPath, WidgetName, WidgetProp, VMName, VMField, ModeStr;
	Args->TryGetStringField(TEXT("widget_path"), WidgetPath);
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	Args->TryGetStringField(TEXT("widget_property"), WidgetProp);
	Args->TryGetStringField(TEXT("view_model_name"), VMName);
	Args->TryGetStringField(TEXT("view_model_field"), VMField);
	Args->TryGetStringField(TEXT("binding_mode"), ModeStr);

	UWidgetBlueprint* WBP = LoadWidgetBP(WidgetPath);
	if (!WBP) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at '%s'"), *WidgetPath); return; }

	UMVVMBlueprintView* View = GetOrCreateView(WBP);
	if (!View) { OutError = TEXT("Failed to access MVVM view extension"); return; }

	UWidget* TargetWidget = nullptr;
	UClass* WidgetClass = WBP->GeneratedClass ? WBP->GeneratedClass : WBP->ParentClass;
	if (!WidgetName.IsEmpty())
	{
		TargetWidget = FindWidgetByName(WBP, FName(*WidgetName));
		if (!TargetWidget) { OutError = FString::Printf(TEXT("Widget '%s' not found in '%s'"), *WidgetName, *WidgetPath); return; }
		WidgetClass = TargetWidget->GetClass();
	}

	const FMVVMBlueprintViewModelContext* VMCtx = View->FindViewModel(FName(*VMName));
	if (!VMCtx) { OutError = FString::Printf(TEXT("Viewmodel '%s' not found on widget — call add_widget_view_model_context first"), *VMName); return; }
	UClass* VMClass = VMCtx->GetViewModelClass();
	if (!VMClass) { OutError = TEXT("Viewmodel context has no class"); return; }

	FMVVMBlueprintViewBinding& B = View->AddDefaultBinding();
	B.BindingType = ParseBindingMode(ModeStr);
	B.bEnabled = true;
	B.bCompile = true;

	if (TargetWidget) B.DestinationPath.SetWidgetName(TargetWidget->GetFName());
	else              B.DestinationPath.SetSelfContext();
	if (!AppendPropertyChain(B.DestinationPath, WidgetClass, WidgetProp, WBP, OutError))
	{
		View->RemoveBinding(&B);
		return;
	}

	B.SourcePath.SetViewModelId(VMCtx->GetViewModelId());
	if (!AppendPropertyChain(B.SourcePath, VMClass, VMField, WBP, OutError))
	{
		View->RemoveBinding(&B);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	WBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("binding_id"), B.BindingId.ToString());
	Obj->SetStringField(TEXT("binding_mode"), BindingModeToString(B.BindingType));
	Obj->SetStringField(TEXT("widget"), TargetWidget ? TargetWidget->GetName() : TEXT("<self>"));
	Obj->SetStringField(TEXT("widget_property"), WidgetProp);
	Obj->SetStringField(TEXT("view_model_name"), VMName);
	Obj->SetStringField(TEXT("view_model_field"), VMField);
	EmitJson(Obj, OutJson);
}

void HandleListWidgetBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsMVVMLoaded()) { OutError = MVVMPluginNotEnabledError; return; }

	FString WidgetPath;
	Args->TryGetStringField(TEXT("widget_path"), WidgetPath);

	UWidgetBlueprint* WBP = LoadWidgetBP(WidgetPath);
	if (!WBP) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at '%s'"), *WidgetPath); return; }

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("widget_path"), WidgetPath);

	UMVVMWidgetBlueprintExtension_View* Ext =
		UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WBP);
	if (!Ext || !Ext->GetBlueprintView())
	{
		Root->SetNumberField(TEXT("view_model_count"), 0);
		Root->SetNumberField(TEXT("binding_count"), 0);
		Root->SetArrayField(TEXT("view_models"), {});
		Root->SetArrayField(TEXT("bindings"), {});
		EmitJson(Root, OutJson);
		return;
	}

	UMVVMBlueprintView* View = Ext->GetBlueprintView();
	UClass* SelfClass = WBP->GeneratedClass ? WBP->GeneratedClass : WBP->ParentClass;

	TArray<TSharedPtr<FJsonValue>> VMArr;
	for (const FMVVMBlueprintViewModelContext& Ctx : View->GetViewModels())
	{
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetStringField(TEXT("view_model_name"), Ctx.GetViewModelName().ToString());
		O->SetStringField(TEXT("view_model_class"), Ctx.GetViewModelClass() ? Ctx.GetViewModelClass()->GetName() : TEXT(""));
		O->SetStringField(TEXT("creation_type"), CreationTypeToString(Ctx.CreationType));
		O->SetStringField(TEXT("view_model_id"), Ctx.GetViewModelId().ToString());
		VMArr.Add(MakeShareable(new FJsonValueObject(O)));
	}
	Root->SetNumberField(TEXT("view_model_count"), VMArr.Num());
	Root->SetArrayField(TEXT("view_models"), VMArr);

	TArray<TSharedPtr<FJsonValue>> BArr;
	for (const FMVVMBlueprintViewBinding& B : View->GetBindings())
	{
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetStringField(TEXT("binding_id"), B.BindingId.ToString());
		O->SetStringField(TEXT("binding_mode"), BindingModeToString(B.BindingType));
		O->SetBoolField(TEXT("enabled"), B.bEnabled);
		O->SetStringField(TEXT("display_name"), B.GetDisplayNameString(WBP, false));
		O->SetStringField(TEXT("source_path"), B.SourcePath.GetPropertyPath(SelfClass));
		O->SetStringField(TEXT("destination_path"), B.DestinationPath.GetPropertyPath(SelfClass));
		BArr.Add(MakeShareable(new FJsonValueObject(O)));
	}
	Root->SetNumberField(TEXT("binding_count"), BArr.Num());
	Root->SetArrayField(TEXT("bindings"), BArr);

	EmitJson(Root, OutJson);
}

}
