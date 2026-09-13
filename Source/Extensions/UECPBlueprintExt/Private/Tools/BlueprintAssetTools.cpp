// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/BlueprintAssetTools.h"
#include "Tools/BatchToolHelper.h"
#include "Tools/PinTypeResolver.h"

#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UObjectIterator.h"

namespace BlueprintAssetTools
{
namespace
{
	FEdGraphPinType ResolveMacroPinType(const FString& TypeStr)
	{
		FEdGraphPinType PinType;
		if (!UECPPinTypes::ResolvePinTypeFromString(TypeStr, PinType))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		}
		return PinType;
	}

	UEdGraph* FindMacroGraph(UBlueprint* BP, const FString& MacroName)
	{
		for (UEdGraph* G : BP->MacroGraphs)
			if (G && G->GetName() == MacroName) return G;
		return nullptr;
	}

	UClass* ResolveInterfaceClass(const FString& InterfacePath)
	{
		FString NormalizedPath = InterfacePath;
		if (NormalizedPath.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
		{
			NormalizedPath.LeftChopInline(2, EAllowShrinking::No);
		}

		UClass* InterfaceClass = nullptr;
		UBlueprint* InterfaceBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(NormalizedPath));
		if (InterfaceBP && InterfaceBP->GeneratedClass)
		{
			InterfaceClass = InterfaceBP->GeneratedClass;
		}
		else
		{
			InterfaceClass = FindObject<UClass>(nullptr, *InterfacePath);
			if (!InterfaceClass)
			{
				InterfaceClass = FindObject<UClass>(nullptr, *NormalizedPath);
			}
			if (!InterfaceClass)
			{
				FString ShortName = FPackageName::GetShortName(NormalizedPath);
				if (ShortName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
				{
					ShortName.LeftChopInline(2, EAllowShrinking::No);
				}
				for (TObjectIterator<UClass> It; It; ++It)
					if (It->GetName().Equals(ShortName, ESearchCase::IgnoreCase) && It->HasAnyClassFlags(CLASS_Interface))
					{ InterfaceClass = *It; break; }
			}
		}
		return InterfaceClass;
	}
}

void HandleAddMacroFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString BpPath; Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	if (BpPath.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path"); return; }

	auto CreateMacroOnce = [](UBlueprint* BP, const FString& MacroName) -> UEdGraph*
	{
		UEdGraph* MacroGraph = FBlueprintEditorUtils::CreateNewGraph(BP, FName(*MacroName),
			UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (!MacroGraph) return nullptr;
		FBlueprintEditorUtils::AddMacroGraph(BP, MacroGraph, true, nullptr);
		return MacroGraph;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("macros"), ItemsArray))
	{
		UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
		if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BpPath); return; }
		if (BP->BlueprintType != BPTYPE_MacroLibrary) { OutError = TEXT("Blueprint is not a Macro Library (BPTYPE_MacroLibrary)"); return; }

		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString MacroName = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("macro_name"));
			if (MacroName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing 'name'")); continue; }

			if (!CreateMacroOnce(BP, MacroName))
			{
				Batch.AddFailure(i, FString::Printf(TEXT("Failed to create graph '%s'"), *MacroName));
				continue;
			}

			TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
			E->SetStringField(TEXT("name"), MacroName);
			Batch.AddSuccess(i, E);
		}
		FKismetEditorUtilities::CompileBlueprint(BP);
		UEditorAssetLibrary::SaveAsset(BpPath, false);
		Batch.Finalize(OutJson);
		return;
	}

	FString MacroName;
	Args->TryGetStringField(TEXT("name"), MacroName);
	if (MacroName.IsEmpty()) Args->TryGetStringField(TEXT("macro_name"), MacroName);
	if (MacroName.IsEmpty()) { OutError = TEXT("Missing required parameter: name"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BpPath); return; }
	if (BP->BlueprintType != BPTYPE_MacroLibrary) { OutError = TEXT("Blueprint is not a Macro Library"); return; }

	if (!CreateMacroOnce(BP, MacroName))
	{
		OutError = FString::Printf(TEXT("Failed to create macro graph '%s'"), *MacroName);
		return;
	}
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"macro_name\":\"%s\"}"), *BpPath, *MacroName);
}

void HandleDeleteMacroFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString BpPath; Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	if (BpPath.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("macros"), ItemsArray))
	{
		UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
		if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BpPath); return; }

		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString MacroName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				MacroName = (*ItemsArray)[i]->AsString();
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (Item.IsValid()) Item->TryGetStringField(TEXT("name"), MacroName);
			}
			if (MacroName.IsEmpty()) { Batch.AddFailure(i, TEXT("Empty macro name")); continue; }

			UEdGraph* G = FindMacroGraph(BP, MacroName);
			if (!G) { Batch.AddFailure(i, FString::Printf(TEXT("Macro '%s' not found"), *MacroName)); continue; }

			FBlueprintEditorUtils::RemoveGraph(BP, G);
			TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
			E->SetStringField(TEXT("name"), MacroName);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString MacroName;
	Args->TryGetStringField(TEXT("name"), MacroName);
	if (MacroName.IsEmpty()) Args->TryGetStringField(TEXT("macro_name"), MacroName);
	if (MacroName.IsEmpty()) { OutError = TEXT("Missing required parameter: name"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BpPath); return; }

	UEdGraph* G = FindMacroGraph(BP, MacroName);
	if (!G) { OutError = FString::Printf(TEXT("Macro '%s' not found in '%s'"), *MacroName, *BpPath); return; }

	FBlueprintEditorUtils::RemoveGraph(BP, G);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"deleted\":\"%s\"}"), *MacroName);
}

void HandleAddMacroParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString BpPath, MacroName;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	Args->TryGetStringField(TEXT("macro_name"), MacroName);
	if (BpPath.IsEmpty() || MacroName.IsEmpty()) { OutError = TEXT("Missing required parameters: blueprint_path and macro_name"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BpPath); return; }

	UEdGraph* MacroGraph = FindMacroGraph(BP, MacroName);
	if (!MacroGraph) { OutError = FString::Printf(TEXT("Macro '%s' not found"), *MacroName); return; }

	UK2Node_Tunnel* EntryTunnel = nullptr;
	UK2Node_Tunnel* ExitTunnel  = nullptr;
	for (UEdGraphNode* Node : MacroGraph->Nodes)
	{
		UK2Node_Tunnel* T = Cast<UK2Node_Tunnel>(Node);
		if (!T) continue;
		if (!T->bCanHaveInputs)  EntryTunnel = T;
		else if (!T->bCanHaveOutputs) ExitTunnel = T;
	}
	if (!EntryTunnel || !ExitTunnel) { OutError = TEXT("Macro graph is missing entry/exit tunnel nodes. Call add_macro first."); return; }

	if (!EntryTunnel->IsEditable()) EntryTunnel->bIsEditable = true;
	if (!ExitTunnel->IsEditable())  ExitTunnel->bIsEditable  = true;

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("parameters"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ParamName = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("param_name"));
			FString PinTypeStr = BatchToolHelper::GetItemString(Item, TEXT("pin_type"), TEXT("type"));
			if (PinTypeStr.IsEmpty()) PinTypeStr = BatchToolHelper::GetItemString(Item, TEXT("param_type"), nullptr);
			FString Direction  = BatchToolHelper::GetItemString(Item, TEXT("direction"), TEXT("dir"));
			if (ParamName.IsEmpty() || PinTypeStr.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing 'name' or 'pin_type'")); continue; }

			bool bIsInput = !Direction.ToLower().Contains(TEXT("out"));
			UK2Node_Tunnel* TargetTunnel = bIsInput ? EntryTunnel : ExitTunnel;
			EEdGraphPinDirection PinDir   = bIsInput ? EGPD_Output : EGPD_Input;

			FEdGraphPinType PinType = ResolveMacroPinType(PinTypeStr);
			UEdGraphPin* NewPin = TargetTunnel->CreateUserDefinedPin(FName(*ParamName), PinType, PinDir);
			if (!NewPin) { Batch.AddFailure(i, FString::Printf(TEXT("Failed to create pin '%s'"), *ParamName)); continue; }

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
			E->SetStringField(TEXT("name"), ParamName);
			E->SetStringField(TEXT("direction"), bIsInput ? TEXT("input") : TEXT("output"));
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString ParamName, PinTypeStr, Direction;
	Args->TryGetStringField(TEXT("name"), ParamName);
	if (ParamName.IsEmpty()) Args->TryGetStringField(TEXT("param_name"), ParamName);
	Args->TryGetStringField(TEXT("pin_type"), PinTypeStr);
	if (PinTypeStr.IsEmpty()) Args->TryGetStringField(TEXT("type"), PinTypeStr);
	if (PinTypeStr.IsEmpty()) Args->TryGetStringField(TEXT("param_type"), PinTypeStr);
	Args->TryGetStringField(TEXT("direction"), Direction);
	if (ParamName.IsEmpty() || PinTypeStr.IsEmpty()) { OutError = TEXT("Missing required parameters: name and pin_type"); return; }

	bool bIsInput = !Direction.ToLower().Contains(TEXT("out"));
	UK2Node_Tunnel* TargetTunnel = bIsInput ? EntryTunnel : ExitTunnel;
	EEdGraphPinDirection PinDir   = bIsInput ? EGPD_Output : EGPD_Input;

	FEdGraphPinType PinType = ResolveMacroPinType(PinTypeStr);
	UEdGraphPin* NewPin = TargetTunnel->CreateUserDefinedPin(FName(*ParamName), PinType, PinDir);
	if (!NewPin) { OutError = FString::Printf(TEXT("Failed to create pin '%s' on macro '%s'"), *ParamName, *MacroName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"macro_name\":\"%s\",\"param_name\":\"%s\",\"direction\":\"%s\"}"),
		*MacroName, *ParamName, bIsInput ? TEXT("input") : TEXT("output"));
}

void HandleGetMacroSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BpPath; Args->TryGetStringField(TEXT("blueprint_path"), BpPath);

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BpPath); return; }

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("blueprint_path"), BpPath);
	Root->SetStringField(TEXT("blueprint_type"), BP->BlueprintType == BPTYPE_MacroLibrary ? TEXT("MacroLibrary") : TEXT("Other"));
	Root->SetNumberField(TEXT("macro_count"), BP->MacroGraphs.Num());

	TArray<TSharedPtr<FJsonValue>> MacrosArr;
	for (UEdGraph* G : BP->MacroGraphs)
	{
		if (!G) continue;
		TSharedPtr<FJsonObject> MacroObj = MakeShareable(new FJsonObject);
		MacroObj->SetStringField(TEXT("name"), G->GetName());

		TArray<TSharedPtr<FJsonValue>> InputPins, OutputPins;
		for (UEdGraphNode* Node : G->Nodes)
		{
			UK2Node_Tunnel* T = Cast<UK2Node_Tunnel>(Node);
			if (!T) continue;
			bool bEntry = !T->bCanHaveInputs;
			for (UEdGraphPin* Pin : T->Pins)
			{
				if (Pin->PinName == UEdGraphSchema_K2::PN_Execute || Pin->PinName == UEdGraphSchema_K2::PN_Then) continue;
				TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
				TSharedPtr<FJsonValue> PinVal = MakeShareable(new FJsonValueObject(PinObj));
				if (bEntry) InputPins.Add(PinVal);
				else        OutputPins.Add(PinVal);
			}
		}
		MacroObj->SetArrayField(TEXT("input_params"), InputPins);
		MacroObj->SetArrayField(TEXT("output_params"), OutputPins);
		MacrosArr.Add(MakeShareable(new FJsonValueObject(MacroObj)));
	}
	Root->SetArrayField(TEXT("macros"), MacrosArr);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

static bool ImplementInterfaceSingle(const FString& BlueprintPath, const FString& InterfacePath, FString& OutInterfaceName, FString& OutError)
{
	UBlueprint* TargetBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!TargetBP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BlueprintPath); return false; }

	UClass* InterfaceClass = ResolveInterfaceClass(InterfacePath);
	if (!InterfaceClass) { OutError = FString::Printf(TEXT("Could not resolve interface class from '%s'"), *InterfacePath); return false; }
	if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
	{
		OutError = FString::Printf(TEXT("'%s' (class '%s') is not a Blueprint Interface. Only Blueprint Interfaces can be implemented."), *InterfacePath, *InterfaceClass->GetName());
		return false;
	}

	for (const FBPInterfaceDescription& Existing : TargetBP->ImplementedInterfaces)
	{
		if (Existing.Interface == InterfaceClass)
		{
			OutInterfaceName = InterfaceClass->GetName();
			return true;
		}
	}
	if (TargetBP->ParentClass && TargetBP->ParentClass->ImplementsInterface(InterfaceClass))
	{
		OutInterfaceName = InterfaceClass->GetName();
		return true;
	}

	const FTopLevelAssetPath ClassPath(InterfaceClass->GetPackage()->GetFName(), InterfaceClass->GetFName());
	if (!FBlueprintEditorUtils::ImplementNewInterface(TargetBP, ClassPath))
	{
		FString Reason;
		if (InterfaceClass == TargetBP->GeneratedClass)
		{
			Reason = TEXT("can't implement self");
		}
		else if (TargetBP->ParentClass && InterfaceClass->IsChildOf(TargetBP->ParentClass))
		{
			Reason = TEXT("interface class is an ancestor of this Blueprint");
		}
		else if (InterfaceClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract) &&
				 !InterfaceClass->HasAnyClassFlags(CLASS_Interface))
		{
			Reason = TEXT("class is not flagged as an interface");
		}
		else
		{
			Reason = TEXT("FBlueprintEditorUtils::ImplementNewInterface returned false — check the editor Output Log for the engine-side reason");
		}
		OutError = FString::Printf(TEXT("Failed to implement interface '%s' on '%s' — %s"),
			*InterfaceClass->GetName(), *BlueprintPath, *Reason);
		return false;
	}

	FBlueprintEditorUtils::ConformImplementedInterfaces(TargetBP);
	FBlueprintEditorUtils::MarkBlueprintAsModified(TargetBP);
	OutInterfaceName = InterfaceClass->GetName();
	return true;
}

void HandleImplementBlueprintInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("interfaces"), ItemsArray))
	{
		FString OuterBp; Args->TryGetStringField(TEXT("blueprint_path"), OuterBp);
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); ++i)
		{
			const TSharedPtr<FJsonValue>& Val = (*ItemsArray)[i];

			FString ItemBp = OuterBp;
			FString ItemInterface;
			if (Val.IsValid() && Val->Type == EJson::String)
			{
				ItemInterface = Val->AsString();
			}
			else
			{
				TSharedPtr<FJsonObject> Item = Val.IsValid() ? Val->AsObject() : nullptr;
				if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				FString PerItemBp; if (Item->TryGetStringField(TEXT("blueprint_path"), PerItemBp) && !PerItemBp.IsEmpty()) ItemBp = PerItemBp;
				if (!Item->TryGetStringField(TEXT("interface_path"), ItemInterface) || ItemInterface.IsEmpty())
				{
					if (!Item->TryGetStringField(TEXT("interface"), ItemInterface) || ItemInterface.IsEmpty())
					{
						Item->TryGetStringField(TEXT("path"), ItemInterface);
					}
				}
			}

			if (ItemBp.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing blueprint_path")); continue; }
			if (ItemInterface.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing interface_path")); continue; }

			FString IfaceName, ItemErr;
			if (ImplementInterfaceSingle(ItemBp, ItemInterface, IfaceName, ItemErr))
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("interface"), IfaceName);
				Extra->SetStringField(TEXT("blueprint"), ItemBp);
				Batch.AddSuccess(i, Extra);
			}
			else
			{
				Batch.AddFailure(i, ItemErr);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString BlueprintPath, InterfacePath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("interface_path"), InterfacePath);

	FString IfaceName;
	if (!ImplementInterfaceSingle(BlueprintPath, InterfacePath, IfaceName, OutError))
	{
		return;
	}
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"blueprint\":\"%s\",\"interface\":\"%s\"}"),
		*BlueprintPath, *IfaceName);
}

void HandleUnimplementBlueprintInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, InterfacePath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("interface_path"), InterfacePath);
	bool bPreserveFunctions = false;
	Args->TryGetBoolField(TEXT("preserve_functions"), bPreserveFunctions);

	UBlueprint* TargetBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!TargetBP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BlueprintPath); return; }

	UClass* InterfaceClass = ResolveInterfaceClass(InterfacePath);
	if (!InterfaceClass) { OutError = FString::Printf(TEXT("Could not resolve interface class from '%s'"), *InterfacePath); return; }

	const FTopLevelAssetPath ClassPath(InterfaceClass->GetPackage()->GetFName(), InterfaceClass->GetFName());
	bool bWasImplemented = false;
	for (const FBPInterfaceDescription& Desc : TargetBP->ImplementedInterfaces)
	{
		if (Desc.Interface && Desc.Interface->GetClassPathName() == ClassPath) { bWasImplemented = true; break; }
	}
	if (!bWasImplemented)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' does not implement interface '%s'."), *BlueprintPath, *InterfaceClass->GetName());
		return;
	}

	FBlueprintEditorUtils::RemoveInterface(TargetBP, ClassPath, bPreserveFunctions);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBP);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"blueprint\":\"%s\",\"interface\":\"%s\",\"preserved_functions\":%s}"),
		*BlueprintPath, *InterfaceClass->GetName(), bPreserveFunctions ? TEXT("true") : TEXT("false"));
}

void HandleRenameFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BpPath, OldName, NewName;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	Args->TryGetStringField(TEXT("old_name"), OldName);
	Args->TryGetStringField(TEXT("new_name"), NewName);

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at: %s"), *BpPath); return; }

	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G && G->GetName().Equals(OldName, ESearchCase::IgnoreCase)) { TargetGraph = G; break; }
	}
	if (!TargetGraph)
	{
		for (UEdGraph* G : BP->MacroGraphs)
		{
			if (G && G->GetName().Equals(OldName, ESearchCase::IgnoreCase)) { TargetGraph = G; break; }
		}
	}
	if (!TargetGraph) { OutError = FString::Printf(TEXT("Function '%s' not found on '%s'"), *OldName, *BpPath); return; }

	FBlueprintEditorUtils::RenameGraph(TargetGraph, NewName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("old_name"), OldName);
	Res->SetStringField(TEXT("new_name"), NewName);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

}
