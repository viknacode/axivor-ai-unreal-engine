// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/GameFeaturesTools.h"
#include "Tools/BatchToolHelper.h"
#include "MCPToolsLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Interfaces/IPluginManager.h"
#include "PluginUtils.h"

#include "GameFeatureData.h"
#include "GameFeatureAction.h"
#include "GameFeatureAction_AddComponents.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureTypes.h"

namespace GameFeaturesTools
{

static const TCHAR* GFPluginNotEnabledError =
	TEXT("GameFeatures plugin is not enabled. Enable it in the .uproject under Plugins → GameFeatures.");

static bool IsGameFeaturesLoaded()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("GameFeatures"));
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
		if (UClass* AsClass = Cast<UClass>(Asset)) return AsClass;
		if (UBlueprint* BP = Cast<UBlueprint>(Asset)) return BP->GeneratedClass;
	}
	if (UClass* C = FindFirstObject<UClass>(*Path, EFindFirstObjectOptions::None, ELogVerbosity::NoLogging))
		return C;
	return nullptr;
}

static UGameFeatureData* LoadGameFeatureData(const FString& Path, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load asset at '%s'"), *Path); return nullptr; }
	UGameFeatureData* GFD = Cast<UGameFeatureData>(Asset);
	if (!GFD) { OutError = FString::Printf(TEXT("Asset at '%s' is not a UGameFeatureData"), *Path); return nullptr; }
	return GFD;
}

static FString SummarizeAction(UGameFeatureAction* Action)
{
	if (!Action) return FString();
	if (UGameFeatureAction_AddComponents* AddComps = Cast<UGameFeatureAction_AddComponents>(Action))
	{
		const int32 N = AddComps->ComponentList.Num();
		return FString::Printf(TEXT("%d component entries"), N);
	}
	return FString();
}

void HandleCreateGameFeaturePluginFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsGameFeaturesLoaded()) { OutError = GFPluginNotEnabledError; return; }

	FString Name, Description;
	bool bEnabledByDefault = true;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("description"), Description);
	Args->TryGetBoolField(TEXT("enabled_by_default"), bEnabledByDefault);

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	const FString PluginLocation = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("GameFeatures")));

	if (IPluginManager::Get().FindPlugin(Name).IsValid())
	{
		OutError = FString::Printf(TEXT("A plugin named '%s' is already registered"), *Name);
		return;
	}

	FPluginUtils::FNewPluginParamsWithDescriptor PluginParams;
	PluginParams.Descriptor.FriendlyName = Name;
	PluginParams.Descriptor.Description = Description.IsEmpty() ? FString::Printf(TEXT("%s game feature plugin"), *Name) : Description;
	PluginParams.Descriptor.Category = TEXT("Game Features");
	PluginParams.Descriptor.bCanContainContent = true;
	PluginParams.Descriptor.bExplicitlyLoaded = true;
	PluginParams.Descriptor.EnabledByDefault = bEnabledByDefault ? EPluginEnabledByDefault::Enabled : EPluginEnabledByDefault::Disabled;

	FPluginReferenceDescriptor GFRef(TEXT("GameFeatures"), true);
	FPluginReferenceDescriptor MGRef(TEXT("ModularGameplay"), true);
	PluginParams.Descriptor.Plugins.Add(GFRef);
	PluginParams.Descriptor.Plugins.Add(MGRef);

	FPluginUtils::FLoadPluginParams LoadParams;
	FText FailReason;
	LoadParams.bSelectInContentBrowser = false;
	LoadParams.bEnablePluginInProject = true;
	LoadParams.bUpdateProjectPluginSearchPath = true;
	LoadParams.OutFailReason = &FailReason;

	TSharedPtr<IPlugin> NewPlugin = FPluginUtils::CreateAndLoadNewPlugin(Name, PluginLocation, PluginParams, LoadParams);
	if (!NewPlugin.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to create plugin '%s': %s"), *Name, *FailReason.ToString());
		return;
	}

	const FString DataAssetName = FString::Printf(TEXT("GameFeatureData_%s"), *Name);
	const FString DataPackagePath = FString::Printf(TEXT("/%s/%s"), *Name, *DataAssetName);
	UGameFeatureData* GFD = nullptr;
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		const FString PackagePath = FString::Printf(TEXT("/%s"), *Name);
		UObject* Asset = AssetTools.CreateAsset(DataAssetName, PackagePath, UGameFeatureData::StaticClass(), nullptr);
		GFD = Cast<UGameFeatureData>(Asset);
	}
	if (!GFD)
	{
		OutError = FString::Printf(TEXT("Plugin '%s' was created but UGameFeatureData asset creation failed at '%s'"), *Name, *DataPackagePath);
		return;
	}
	GFD->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(GFD->GetPathName(), false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("plugin_name"), Name);
	Obj->SetStringField(TEXT("plugin_descriptor"), NewPlugin->GetDescriptorFileName());
	Obj->SetStringField(TEXT("game_feature_data_path"), GFD->GetPathName());
	Obj->SetBoolField(TEXT("enabled_by_default"), bEnabledByDefault);
	EmitJson(Obj, OutJson);
}

void HandleAddGameFeatureActionAddComponentsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsGameFeaturesLoaded()) { OutError = GFPluginNotEnabledError; return; }

	FString GFDPath;
	Args->TryGetStringField(TEXT("game_feature_data_path"), GFDPath);
	UGameFeatureData* GFD = LoadGameFeatureData(GFDPath, OutError);
	if (!GFD) return;

	const TArray<TSharedPtr<FJsonValue>>* ComponentsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("components"), ComponentsArr) || !ComponentsArr || ComponentsArr->Num() == 0)
	{
		OutError = TEXT("components=[{actor_class, component_class, client?, server?}] is required");
		return;
	}

	UGameFeatureAction_AddComponents* AddComps = NewObject<UGameFeatureAction_AddComponents>(
		GFD, NAME_None, RF_Public | RF_Transactional);
	AddComps->ComponentList.Reset();

	int32 Accepted = 0;
	TArray<TSharedPtr<FJsonValue>> Failures;
	for (int32 i = 0; i < ComponentsArr->Num(); i++)
	{
		TSharedPtr<FJsonObject> Item = (*ComponentsArr)[i]->AsObject();
		if (!Item.IsValid()) continue;
		FString ActorClassPath, CompClassPath;
		bool bClient = true, bServer = true;
		Item->TryGetStringField(TEXT("actor_class"), ActorClassPath);
		Item->TryGetStringField(TEXT("component_class"), CompClassPath);
		Item->TryGetBoolField(TEXT("client"), bClient);
		Item->TryGetBoolField(TEXT("server"), bServer);

		if (ActorClassPath.IsEmpty() || CompClassPath.IsEmpty())
		{
			TSharedPtr<FJsonObject> F = MakeShareable(new FJsonObject);
			F->SetNumberField(TEXT("index"), i);
			F->SetStringField(TEXT("error"), TEXT("actor_class and component_class are required"));
			Failures.Add(MakeShareable(new FJsonValueObject(F)));
			continue;
		}
		UClass* ActorClass = ResolveClassByPath(ActorClassPath);
		UClass* CompClass  = ResolveClassByPath(CompClassPath);
		if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			TSharedPtr<FJsonObject> F = MakeShareable(new FJsonObject);
			F->SetNumberField(TEXT("index"), i);
			F->SetStringField(TEXT("error"), FString::Printf(TEXT("actor_class '%s' did not resolve to an AActor subclass"), *ActorClassPath));
			Failures.Add(MakeShareable(new FJsonValueObject(F)));
			continue;
		}
		if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass()))
		{
			TSharedPtr<FJsonObject> F = MakeShareable(new FJsonObject);
			F->SetNumberField(TEXT("index"), i);
			F->SetStringField(TEXT("error"), FString::Printf(TEXT("component_class '%s' did not resolve to a UActorComponent subclass"), *CompClassPath));
			Failures.Add(MakeShareable(new FJsonValueObject(F)));
			continue;
		}

		FGameFeatureComponentEntry Entry;
		Entry.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(ActorClass));
		Entry.ComponentClass = TSoftClassPtr<UActorComponent>(FSoftObjectPath(CompClass));
		Entry.bClientComponent = bClient ? 1 : 0;
		Entry.bServerComponent = bServer ? 1 : 0;
		AddComps->ComponentList.Add(Entry);
		++Accepted;
	}

	if (Accepted == 0)
	{
		OutError = TEXT("No valid component entries were provided — see failures for details");
		return;
	}

	GFD->GetMutableActionsInEditor().Add(AddComps);
	GFD->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(GFD->GetPathName(), false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("game_feature_data_path"), GFDPath);
	Obj->SetStringField(TEXT("action_class"), UGameFeatureAction_AddComponents::StaticClass()->GetName());
	Obj->SetNumberField(TEXT("entries_added"), Accepted);
	if (Failures.Num() > 0) Obj->SetArrayField(TEXT("failures"), Failures);
	Obj->SetNumberField(TEXT("total_actions"), GFD->GetActions().Num());
	EmitJson(Obj, OutJson);
}

void HandleAddGameFeatureActionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsGameFeaturesLoaded()) { OutError = GFPluginNotEnabledError; return; }

	FString GFDPath, ActionClassPath;
	Args->TryGetStringField(TEXT("game_feature_data_path"), GFDPath);
	Args->TryGetStringField(TEXT("action_class"), ActionClassPath);

	UGameFeatureData* GFD = LoadGameFeatureData(GFDPath, OutError);
	if (!GFD) return;

	UClass* ActionClass = ResolveClassByPath(ActionClassPath);
	if (!ActionClass) { OutError = FString::Printf(TEXT("Could not resolve action_class '%s'"), *ActionClassPath); return; }
	if (!ActionClass->IsChildOf(UGameFeatureAction::StaticClass()))
	{
		OutError = FString::Printf(TEXT("'%s' must derive from UGameFeatureAction"), *ActionClass->GetName());
		return;
	}
	if (ActionClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutError = FString::Printf(TEXT("'%s' is abstract"), *ActionClass->GetName());
		return;
	}

	UGameFeatureAction* NewAction = NewObject<UGameFeatureAction>(GFD, ActionClass, NAME_None, RF_Public | RF_Transactional);
	if (!NewAction) { OutError = FString::Printf(TEXT("NewObject failed for '%s'"), *ActionClass->GetName()); return; }

	GFD->GetMutableActionsInEditor().Add(NewAction);
	GFD->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(GFD->GetPathName(), false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("game_feature_data_path"), GFDPath);
	Obj->SetStringField(TEXT("action_class"), ActionClass->GetName());
	Obj->SetStringField(TEXT("action_object"), NewAction->GetName());
	Obj->SetNumberField(TEXT("total_actions"), GFD->GetActions().Num());
	EmitJson(Obj, OutJson);
}

void HandleListGameFeatureActionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsGameFeaturesLoaded()) { OutError = GFPluginNotEnabledError; return; }

	FString GFDPath;
	Args->TryGetStringField(TEXT("game_feature_data_path"), GFDPath);
	UGameFeatureData* GFD = LoadGameFeatureData(GFDPath, OutError);
	if (!GFD) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	int32 Idx = 0;
	for (UGameFeatureAction* Action : GFD->GetActions())
	{
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetNumberField(TEXT("index"), Idx++);
		O->SetStringField(TEXT("class_name"), Action ? Action->GetClass()->GetName() : TEXT("<null>"));
		O->SetStringField(TEXT("instance_name"), Action ? Action->GetName() : TEXT(""));
		O->SetStringField(TEXT("summary"), SummarizeAction(Action));
		Arr.Add(MakeShareable(new FJsonValueObject(O)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("game_feature_data_path"), GFDPath);
	Root->SetNumberField(TEXT("count"), Arr.Num());
	Root->SetArrayField(TEXT("actions"), Arr);
	EmitJson(Root, OutJson);
}

void HandleListGameFeaturesFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& OutError)
{
	if (!IsGameFeaturesLoaded()) { OutError = GFPluginNotEnabledError; return; }

	UGameFeaturesSubsystem& Subsys = UGameFeaturesSubsystem::Get();

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		if (!Plugin->GetDescriptor().bExplicitlyLoaded) continue;

		const FString PluginName = Plugin->GetName();
		FString URL;
		Subsys.GetPluginURLByName(PluginName, URL);

		EGameFeaturePluginState State = EGameFeaturePluginState::Uninitialized;
		if (!URL.IsEmpty())
		{
			State = Subsys.GetPluginState(URL);
		}

		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetStringField(TEXT("plugin_name"), PluginName);
		O->SetStringField(TEXT("descriptor_path"), Plugin->GetDescriptorFileName());
		O->SetStringField(TEXT("plugin_url"), URL);
		O->SetStringField(TEXT("state"), UE::GameFeatures::ToString(State));
		O->SetBoolField(TEXT("active"), State == EGameFeaturePluginState::Active);
		Arr.Add(MakeShareable(new FJsonValueObject(O)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), Arr.Num());
	Root->SetArrayField(TEXT("plugins"), Arr);
	EmitJson(Root, OutJson);
}

void HandleSetGameFeatureStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!IsGameFeaturesLoaded()) { OutError = GFPluginNotEnabledError; return; }

	FString PluginName, StateStr;
	Args->TryGetStringField(TEXT("plugin_name"), PluginName);
	Args->TryGetStringField(TEXT("state"), StateStr);
	if (PluginName.IsEmpty()) { OutError = TEXT("plugin_name is required"); return; }
	if (StateStr.IsEmpty())   { OutError = TEXT("state is required (active|registered|unloaded)"); return; }

	UGameFeaturesSubsystem& Subsys = UGameFeaturesSubsystem::Get();
	FString URL;
	if (!Subsys.GetPluginURLByName(PluginName, URL) || URL.IsEmpty())
	{
		OutError = FString::Printf(TEXT("No game-feature plugin URL found for '%s' — is it installed and enabled?"), *PluginName);
		return;
	}

	const FString S = StateStr.ToLower();
	if (S == TEXT("active") || S == TEXT("activate") || S == TEXT("activated"))
	{
		Subsys.LoadAndActivateGameFeaturePlugin(URL, FGameFeaturePluginLoadComplete());
	}
	else if (S == TEXT("registered") || S == TEXT("deactivate") || S == TEXT("deactivated"))
	{
		Subsys.DeactivateGameFeaturePlugin(URL);
	}
	else if (S == TEXT("unloaded") || S == TEXT("unload"))
	{
		Subsys.UnloadGameFeaturePlugin(URL, false);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown state '%s'. Use: active | registered | unloaded"), *StateStr);
		return;
	}

	OutJson = FString::Printf(TEXT("{\"success\":true,\"plugin_name\":\"%s\",\"state_requested\":\"%s\",\"plugin_url\":\"%s\",\"note\":\"State transitions are async; call list_game_features to confirm.\"}"),
		*PluginName, *StateStr, *URL);
}

}
