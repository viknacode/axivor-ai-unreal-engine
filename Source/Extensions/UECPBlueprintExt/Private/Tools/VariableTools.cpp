// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/VariableTools.h"
#include "Tools/PinTypeResolver.h"
#include "Utils/EditorRuntime.h"
#include "Managers/CapabilityProfile.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Engine/UserDefinedEnum.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_EditablePinBase.h"
#include "UObject/Script.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Tools/BatchToolHelper.h"

namespace VariableTools
{

static FString DescribeObjectResolveFailure(const FString& InnerName)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Hits;
	AR.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Hits, true);
	AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Hits, true);
	const FAssetData* Match = nullptr;
	for (const FAssetData& A : Hits)
	{
		if (A.AssetName.ToString().Equals(InnerName, ESearchCase::IgnoreCase)) { Match = &A; break; }
	}
	if (!Match)
	{
		return FString::Printf(
			TEXT("Could not resolve 'object:%s' — no Blueprint asset named '%s' was found. "
			     "Use the actual Blueprint asset name (e.g. 'object:BP_InventoryItem'), ensure it is compiled, and check spelling."),
			*InnerName, *InnerName);
	}
	UBlueprint* BP = Cast<UBlueprint>(Match->GetAsset());
	const bool bHasClass = BP && (BP->GeneratedClass || BP->SkeletonGeneratedClass);
	if (!bHasClass)
	{
		return FString::Printf(
			TEXT("'%s' exists at %s but has no compiled class yet. Run "
			     "blueprint(action='compile_blueprint', blueprint_path='%s') first, then retry add_variable."),
			*InnerName, *Match->GetObjectPathString(), *Match->GetObjectPathString());
	}
	return FString::Printf(
		TEXT("'%s' exists at %s but the resolver couldn't bind to its class. "
		     "Run blueprint(action='compile_blueprint', blueprint_path='%s') and retry."),
		*InnerName, *Match->GetObjectPathString(), *Match->GetObjectPathString());
}

static UBlueprint* LoadBPFromPath(const FString& BpPath)
{
	if (BpPath.Equals(TEXT("@level_blueprint"), ESearchCase::IgnoreCase))
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World && World->PersistentLevel)
			return Cast<UBlueprint>(World->PersistentLevel->GetLevelScriptBlueprint(false));
		return nullptr;
	}
	return Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
}

static bool ResolveVarType(const FString& TypeStrRaw, FEdGraphPinType& OutPinType)
{
	return UECPPinTypes::ResolvePinTypeFromString(TypeStrRaw, OutPinType);
}

void HandleAddVariable(const FString& BpPath, const FString& VarName, const FString& VarType, const FString& DefaultValue, const FString& Category, FString& OutError)
{
	UBlueprint* TargetBlueprint = LoadBPFromPath(BpPath);
	if (!TargetBlueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath);
		return;
	}

	if (TargetBlueprint->ParentClass)
	{
		for (TFieldIterator<FProperty> PropIt(TargetBlueprint->ParentClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
		{
			if (PropIt->GetName().Equals(VarName, ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(TEXT("Variable name '%s' conflicts with a parent class property of the same name. Choose a different name to avoid shadowing (e.g. '%s2' or 'My%s')."), *VarName, *VarName, *VarName);
				return;
			}
		}
	}

	FGuid ExistingVarGuid = FBlueprintEditorUtils::FindMemberVariableGuidByName(TargetBlueprint, FName(*VarName));
	if (ExistingVarGuid.IsValid())
	{
		return;
	}

	FEdGraphPinType PinType;
	if (!ResolveVarType(VarType, PinType))
	{
		if (VarType.StartsWith(TEXT("object:"), ESearchCase::IgnoreCase))
		{
			OutError = DescribeObjectResolveFailure(VarType.RightChop(7));
		}
		else
		{
			const FString VarTypeLower = VarType.ToLower();
			if (VarTypeLower == TEXT("event_dispatcher") ||
			    VarTypeLower == TEXT("eventdispatcher") ||
			    VarTypeLower == TEXT("delegate") ||
			    VarTypeLower == TEXT("multicastdelegate") ||
			    VarTypeLower == TEXT("dispatcher") ||
			    VarTypeLower.StartsWith(TEXT("dispatcher:")) ||
			    VarTypeLower.StartsWith(TEXT("delegate:")) ||
			    VarTypeLower.StartsWith(TEXT("multicastdelegate:")))
			{
				OutError = FString::Printf(TEXT("'%s' is not a variable type in Blueprint. Event dispatchers are created via a separate action — NOT add_variable. "
				                                "Use: blueprint(action='add_event_dispatcher', blueprint_path='...', dispatcher_name='%s'), then for each parameter call "
				                                "blueprint(action='add_dispatcher_param', dispatcher_name='%s', param_name='...', param_type='...'). "
				                                "Don't try to encode params in the type string. "
				                                "To call/bind in a graph use ev.Dispatcher.<Name>, ev.DispatcherBind.<Name>, or ev.DispatcherAssign.<Name>."),
				                                *VarType, *VarName, *VarName);
			}
			else if (VarTypeLower == TEXT("map") || VarTypeLower == TEXT("tmap"))
			{
				OutError = FString::Printf(TEXT("'%s' is a container — specify key and value types. Use 'Map:KeyType,ValueType' "
				                                "or 'TMap<KeyType,ValueType>' (e.g. 'Map:Name,Actor' or 'TMap<String,int>')."), *VarType);
			}
			else if (VarTypeLower == TEXT("array") || VarTypeLower == TEXT("tarray"))
			{
				OutError = FString::Printf(TEXT("'%s' is a container — specify the element type. Use 'Array:ElementType' "
				                                "or 'TArray<ElementType>' (e.g. 'Array:Actor' or 'TArray<int>')."), *VarType);
			}
			else if (VarTypeLower == TEXT("set") || VarTypeLower == TEXT("tset"))
			{
				OutError = FString::Printf(TEXT("'%s' is a container — specify the element type. Use 'Set:ElementType' "
				                                "or 'TSet<ElementType>' (e.g. 'Set:Name' or 'TSet<int>')."), *VarType);
			}
			else if (VarType.Contains(TEXT("<")) && VarType.Contains(TEXT(">")))
			{
				if (VarTypeLower.StartsWith(TEXT("tsubclassof")))
				{
					FString Inner = VarType;
					int32 Lt, Gt;
					if (Inner.FindChar(TEXT('<'), Lt) && Inner.FindChar(TEXT('>'), Gt) && Gt > Lt)
						Inner = Inner.Mid(Lt + 1, Gt - Lt - 1).TrimStartAndEnd();
					OutError = FString::Printf(
						TEXT("'%s' is a C++ template — use the 'class:ClassName' shorthand instead. "
						     "Example: 'class:%s' creates a TSubclassOf<%s> variable. "
						     "For a Blueprint-class variable (e.g. ability class slots), use 'class:GameplayAbility'."),
						*VarType, *Inner, *Inner);
				}
				else
				{
					OutError = FString::Printf(
						TEXT("'%s' looks like a C++ template — Blueprint variables don't use angle-bracket syntax. "
						     "Use: 'Array:ElementType', 'Map:Key,Value', 'Set:ElementType', 'class:ClassName', or a plain type name like 'Float', 'Bool', 'Actor'."),
						*VarType);
				}
			}
			else if (!VarType.IsEmpty() && (VarType.StartsWith(TEXT("/Game/")) || VarType.StartsWith(TEXT("/Script/"))))
			{
				OutError = FString::Printf(
					TEXT("'%s' is an asset path, not a type name. Use the PARENT CLASS name instead. "
					     "For Blueprint assets: use their parent class (e.g. 'GameplayAbility' for a GA_* Blueprint, "
					     "'DataAsset' for a DA_* Blueprint). For actor references: use the actor's class name (e.g. 'Actor', 'Pawn', 'Character'). "
					     "To reference a specific Blueprint class type: use 'class:ParentClassName'."),
					*VarType);
			}
			else if (!VarType.IsEmpty() && (
				VarType.StartsWith(TEXT("GA_")) || VarType.StartsWith(TEXT("BP_")) ||
				VarType.StartsWith(TEXT("DA_")) || VarType.StartsWith(TEXT("WBP_")) ||
				VarType.StartsWith(TEXT("IMC_")) || VarType.StartsWith(TEXT("IA_"))))
			{
				const TCHAR* Suggestion =
					VarType.StartsWith(TEXT("GA_"))  ? TEXT("GameplayAbility") :
					VarType.StartsWith(TEXT("DA_"))  ? TEXT("DataAsset") :
					VarType.StartsWith(TEXT("WBP_")) ? TEXT("UserWidget") :
					VarType.StartsWith(TEXT("IMC_")) ? TEXT("InputMappingContext") :
					VarType.StartsWith(TEXT("IA_"))  ? TEXT("InputAction") :
					TEXT("Actor");
				OutError = FString::Printf(
					TEXT("'%s' is a Blueprint asset name, not a type. Use the PARENT CLASS name instead: '%s'. "
					     "Blueprint variables reference types by their C++ parent class, not the asset name. "
					     "To constrain to a specific Blueprint subclass: use 'class:%s' (creates a TSubclassOf slot)."),
					*VarType, Suggestion, Suggestion);
			}
			else
			{
				OutError = FString::Printf(TEXT("Unknown or unsupported variable type: '%s'. "
					"Common types: Bool, Int, Float, String, Name, Text, Vector, Rotator, Transform, Actor, Pawn, Character, Object. "
					"For arrays: 'Array:ElementType'. For enums: use the enum name. "
					"For Blueprint object refs: use the C++ parent class (e.g. 'GameplayAbility', not 'GA_MyAbility')."),
					*VarType);
			}
		}
		return;
	}

	FString SafeDefaultValue = DefaultValue;
	{
		if (!DefaultValue.IsEmpty() && PinType.PinSubCategoryObject.IsValid())
		{
			if (UEnum* ResolvedEnum = Cast<UEnum>(PinType.PinSubCategoryObject.Get()))
			{
				FString TestName = DefaultValue;
				int32 ColonPos = INDEX_NONE;
				if (TestName.FindLastChar(TEXT(':'), ColonPos) && ColonPos > 0)
					TestName = TestName.Mid(ColonPos + 1);
				int64 Val = ResolvedEnum->GetValueByNameString(TestName, EGetByNameFlags::None);
				if (Val == INDEX_NONE)
				{
					FString Qualified = FString::Printf(TEXT("%s::%s"), *ResolvedEnum->GetName(), *TestName);
					Val = ResolvedEnum->GetValueByNameString(Qualified, EGetByNameFlags::None);
				}
				if (Val != INDEX_NONE)
					SafeDefaultValue = TestName;
			}
		}

		const bool bIsObjectPath = SafeDefaultValue.StartsWith(TEXT("/Game/"))
		                        || SafeDefaultValue.StartsWith(TEXT("/Engine/"))
		                        || SafeDefaultValue.StartsWith(TEXT("/Script/"));

		bool bIsExpression = !bIsObjectPath
		                  && (SafeDefaultValue.Contains(TEXT("->"))
		                      || SafeDefaultValue.Contains(TEXT("["))
		                      || SafeDefaultValue.Contains(TEXT("::"))
		                      || (SafeDefaultValue.Contains(TEXT("(")) && SafeDefaultValue.Contains(TEXT(")"))));
		bool bIsContainer = (PinType.ContainerType != EPinContainerType::None);
		if (bIsExpression || bIsContainer)
		{
			SafeDefaultValue = TEXT("");
		}
	}

	const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(TargetBlueprint, FName(*VarName), PinType, SafeDefaultValue);
	if (!bAdded)
	{
		OutError = FString::Printf(TEXT("Failed to add variable '%s'. Name likely conflicts with a parent class property (e.g. AActor::Tags, AActor::bHidden). Choose a different name."), *VarName);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
}

void HandleAddLocalVariable(const FString& BpPath, const FString& FunctionName, const FString& VarName, const FString& VarType, const FString& DefaultValue, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath);
		return;
	}

	UEdGraph* FuncGraph = nullptr;
	{
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* G : AllGraphs)
		{
			if (G && G->GetFName().ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
			{ FuncGraph = G; break; }
		}
	}
	if (!FuncGraph)
	{
		OutError = FString::Printf(TEXT("Function '%s' not found in Blueprint"), *FunctionName);
		return;
	}

	TArray<UK2Node_FunctionEntry*> EntryNodes;
	FuncGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
	if (EntryNodes.Num() == 0)
	{
		OutError = TEXT("Function entry node not found (local variables can only be added to a function graph, not an event).");
		return;
	}
	UK2Node_FunctionEntry* EntryNode = EntryNodes[0];

	for (const FBPVariableDescription& LV : EntryNode->LocalVariables)
	{
		if (LV.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("success"), true);
			R->SetStringField(TEXT("message"), FString::Printf(TEXT("Local variable '%s' already exists in '%s'"), *VarName, *FunctionName));
			FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out); FJsonSerializer::Serialize(R.ToSharedRef(), W);
			OutJsonString = Out;
			return;
		}
	}

	if (VarType.IsEmpty())
	{
		OutError = TEXT("Missing 'type' for the local variable (e.g. type='int' or type='TArray<FItemData>'). For a batch pass items=[{name, type}].");
		return;
	}

	FEdGraphPinType PinType;
	if (!ResolveVarType(VarType, PinType))
	{
		if (VarType.StartsWith(TEXT("object:"), ESearchCase::IgnoreCase))
		{
			OutError = DescribeObjectResolveFailure(VarType.RightChop(7));
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown or unsupported type: %s"), *VarType);
		}
		return;
	}

	if (!FBlueprintEditorUtils::AddLocalVariable(BP, FuncGraph, FName(*VarName), PinType, DefaultValue))
	{
		OutError = FString::Printf(TEXT("Failed to add local variable '%s' to '%s' (target is not a function graph)."), *VarName, *FunctionName);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("var_name"), VarName);
	Result->SetStringField(TEXT("function_name"), FunctionName);
	Result->SetStringField(TEXT("var_type"), VarType);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Local variable '%s' added to function '%s'. Read/write it in this function's graph with var.get.%s / var.set.%s."), *VarName, *FunctionName, *VarName, *VarName));
	FString Output; TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output); FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	OutJsonString = Output;
}

void HandleAddVariablesBulk(const FString& BpPath, const TArray<TSharedPtr<FJsonValue>>& Variables, FString& OutJsonString, FString& OutError)
{
	int32 Added = 0, Failed = 0;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& Val : Variables)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr))
		{
			Errors.Add(TEXT("Skipped entry: not a JSON object"));
			++Failed;
			continue;
		}

		FString VarName, VarType, Default, Category;
		if (!(*ObjPtr)->TryGetStringField(TEXT("name"), VarName))
			if (!(*ObjPtr)->TryGetStringField(TEXT("var_name"), VarName))
				(*ObjPtr)->TryGetStringField(TEXT("variable_name"), VarName);
		if (!(*ObjPtr)->TryGetStringField(TEXT("type"), VarType))
			if (!(*ObjPtr)->TryGetStringField(TEXT("var_type"), VarType))
				(*ObjPtr)->TryGetStringField(TEXT("variable_type"), VarType);
		if (!(*ObjPtr)->TryGetStringField(TEXT("default_value"), Default))
			(*ObjPtr)->TryGetStringField(TEXT("default"), Default);
		(*ObjPtr)->TryGetStringField(TEXT("category"), Category);

		if (VarType.Equals(TEXT("object"), ESearchCase::IgnoreCase))
		{
			FString SubType;
			if (!(*ObjPtr)->TryGetStringField(TEXT("sub_type"), SubType))
				(*ObjPtr)->TryGetStringField(TEXT("object_class"), SubType);
			if (!SubType.IsEmpty())
				VarType = TEXT("object:") + SubType;
		}
		bool bIsArray = false;
		(*ObjPtr)->TryGetBoolField(TEXT("is_array"), bIsArray);
		if (bIsArray && !VarType.IsEmpty()
			&& !VarType.StartsWith(TEXT("TArray<"), ESearchCase::IgnoreCase)
			&& !VarType.StartsWith(TEXT("Array<"), ESearchCase::IgnoreCase))
		{
			VarType = TEXT("TArray<") + VarType + TEXT(">");
		}

		FString MapKeyType, MapValueType;
		const bool bHasKey = (*ObjPtr)->TryGetStringField(TEXT("map_key_type"), MapKeyType) && !MapKeyType.IsEmpty();
		const bool bHasVal = (*ObjPtr)->TryGetStringField(TEXT("map_value_type"), MapValueType) && !MapValueType.IsEmpty();
		if (bHasKey && bHasVal)
		{
			VarType = FString::Printf(TEXT("TMap<%s,%s>"), *MapKeyType, *MapValueType);
		}
		else if (bHasKey != bHasVal)
		{
			Errors.Add(FString::Printf(TEXT("%s: map needs BOTH map_key_type and map_value_type, or use type:'TMap<K,V>'"), *VarName));
			++Failed;
			continue;
		}
		FString SetElementType, ArrayElementType;
		if ((*ObjPtr)->TryGetStringField(TEXT("set_element_type"), SetElementType) && !SetElementType.IsEmpty())
			VarType = FString::Printf(TEXT("TSet<%s>"), *SetElementType);
		if ((*ObjPtr)->TryGetStringField(TEXT("array_element_type"), ArrayElementType) && !ArrayElementType.IsEmpty())
			VarType = FString::Printf(TEXT("TArray<%s>"), *ArrayElementType);

		if (VarName.IsEmpty() || VarType.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("Skipped entry: missing name or type (name='%s', type='%s')"), *VarName, *VarType));
			++Failed;
			continue;
		}

		FString VarErr;
		HandleAddVariable(BpPath, VarName, VarType, Default, Category, VarErr);
		if (VarErr.IsEmpty())
		{
			++Added;
			const FString IE = BatchToolHelper::GetStringOrBool(*ObjPtr, TEXT("instance_editable"));
			const FString EOS = BatchToolHelper::GetStringOrBool(*ObjPtr, TEXT("expose_on_spawn"));
			const FString BRO = BatchToolHelper::GetStringOrBool(*ObjPtr, TEXT("blueprint_read_only"));
			const FString SG = BatchToolHelper::GetStringOrBool(*ObjPtr, TEXT("save_game"));
			const FString AD = BatchToolHelper::GetStringOrBool(*ObjPtr, TEXT("advanced_display"));
			FString Rep; (*ObjPtr)->TryGetStringField(TEXT("replication"), Rep);
			FString TT; (*ObjPtr)->TryGetStringField(TEXT("tooltip"), TT);
			if (!IE.IsEmpty() || !EOS.IsEmpty() || !BRO.IsEmpty() || !SG.IsEmpty()
				|| !AD.IsEmpty() || !Rep.IsEmpty() || !TT.IsEmpty())
			{
				FString FlagOut, FlagErr;
				HandleSetVariableFlags(BpPath, VarName, IE, EOS, TT, BRO, Rep, SG, AD, FlagOut, FlagErr);
				if (!FlagErr.IsEmpty())
					Errors.Add(FString::Printf(TEXT("%s flags: %s"), *VarName, *FlagErr));
			}
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("%s: %s"), *VarName, *VarErr));
			++Failed;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), Added > 0 || Failed == 0);
	Result->SetNumberField(TEXT("added"), Added);
	Result->SetNumberField(TEXT("failed"), Failed);
	if (Errors.Num() > 0)
		Result->SetStringField(TEXT("errors"), FString::Join(Errors, TEXT("; ")));

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	OutJsonString = Output;
}

void HandleSetBlueprintVariableDefault(const FString& BpPath, const FString& VarName, const FString& NewDefault, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at: %s"), *BpPath);
		return;
	}

	bool bFound = false;
	for (FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase))
		{
			const bool bIsContainer = (Var.VarType.ContainerType != EPinContainerType::None);
			if (bIsContainer && NewDefault.IsEmpty())
			{
				const TCHAR* ContainerKind = TEXT("array");
				switch (Var.VarType.ContainerType)
				{
					case EPinContainerType::Array: ContainerKind = TEXT("array"); break;
					case EPinContainerType::Set:   ContainerKind = TEXT("set");   break;
					case EPinContainerType::Map:   ContainerKind = TEXT("map");   break;
					default: break;
				}
				OutError = FString::Printf(
					TEXT("Variable '%s' is a %s — empty defaults are no-ops (the engine constructs an empty container automatically). ")
					TEXT("If you wanted to seed entries, pass a UE export string like '(value1,value2)' for arrays, '((Key1,Val1),(Key2,Val2))' for maps. ")
					TEXT("To leave the container empty, just omit this call."),
					*VarName, ContainerKind);
				return;
			}
			Var.DefaultValue = NewDefault;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		UClass* GenClass = BP->GeneratedClass ? BP->GeneratedClass : BP->SkeletonGeneratedClass;
		FProperty* InhProp = GenClass ? FindFProperty<FProperty>(GenClass, *VarName) : nullptr;
		if (!InhProp && GenClass)
		{
			for (TFieldIterator<FProperty> It(GenClass); It; ++It)
				if (It->GetName().Equals(VarName, ESearchCase::IgnoreCase)) { InhProp = *It; break; }
		}

		if (InhProp && GenClass)
		{
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { OutError = FString::Printf(TEXT("Could not access class defaults for '%s'"), *BpPath); return; }

			const bool bIsContainerProp = InhProp->IsA(FArrayProperty::StaticClass())
				|| InhProp->IsA(FSetProperty::StaticClass()) || InhProp->IsA(FMapProperty::StaticClass());
			if (bIsContainerProp && NewDefault.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Variable '%s' is a container — empty defaults are no-ops. Pass a UE export string to seed entries, or omit this call to leave it empty."),
					*VarName);
				return;
			}

			void* ValuePtr = InhProp->ContainerPtrToValuePtr<void>(CDO);
			bool bSet = false;

			if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(InhProp))
			{
				if (NewDefault.IsEmpty()) { ObjProp->SetObjectPropertyValue(ValuePtr, nullptr); bSet = true; }
				else
				{
					UObject* AssetObj = UEditorAssetLibrary::LoadAsset(NewDefault);
					if (ObjProp->IsA(FClassProperty::StaticClass()) || ObjProp->IsA(FSoftClassProperty::StaticClass()))
					{
						UClass* AsClass = Cast<UClass>(AssetObj);
						if (!AsClass) if (UBlueprint* RefBP = Cast<UBlueprint>(AssetObj)) AsClass = RefBP->GeneratedClass;
						if (!AsClass)
						{
							UObject* Loaded2 = UEditorAssetLibrary::LoadAsset(NewDefault.EndsWith(TEXT("_C")) ? NewDefault : NewDefault + TEXT("_C"));
							AsClass = Cast<UClass>(Loaded2);
							if (!AsClass) if (UBlueprint* RefBP2 = Cast<UBlueprint>(Loaded2)) AsClass = RefBP2->GeneratedClass;
						}
						if (AsClass) { ObjProp->SetObjectPropertyValue(ValuePtr, AsClass); bSet = true; }
					}
					else if (AssetObj) { ObjProp->SetObjectPropertyValue(ValuePtr, AssetObj); bSet = true; }
				}
				if (!bSet) bSet = (InhProp->ImportText_Direct(*NewDefault, ValuePtr, CDO, PPF_None) != nullptr);
			}
			else
			{
				bSet = (InhProp->ImportText_Direct(*NewDefault, ValuePtr, CDO, PPF_None) != nullptr);
			}

			if (!bSet) { OutError = FString::Printf(TEXT("Failed to set inherited variable '%s' to '%s' on the class defaults"), *VarName, *NewDefault); return; }

			CDO->PostEditChange();
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
			Res->SetBoolField(TEXT("success"), true);
			Res->SetStringField(TEXT("blueprint_path"), BpPath);
			Res->SetStringField(TEXT("variable_name"), VarName);
			Res->SetStringField(TEXT("new_default"), NewDefault);
			Res->SetBoolField(TEXT("inherited"), true);
			Res->SetStringField(TEXT("note"), TEXT("Inherited variable — default set as a class-default override on this child's CDO."));
			FString InhResult;
			TSharedRef<TJsonWriter<>> InhWriter = TJsonWriterFactory<>::Create(&InhResult);
			FJsonSerializer::Serialize(Res.ToSharedRef(), InhWriter);
			OutJsonString = InhResult;
			return;
		}

		OutError = FString::Printf(TEXT("Variable '%s' not found on Blueprint '%s'"), *VarName, *BpPath);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("variable_name"), VarName);
	Res->SetStringField(TEXT("new_default"), NewDefault);
	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
	OutJsonString = ResultString;
}

void HandleCategorizeVariables(const FString& BpPath, const TSharedPtr<FJsonObject>* Categories, FString& OutJsonString, FString& OutError)
{
	UBlueprint* TargetBlueprint = LoadBPFromPath(BpPath);
	if (!TargetBlueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath);
		return;
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());

	if (Categories && Categories->IsValid())
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Categorize Variables")));
		TargetBlueprint->Modify();

		int32 UpdatedCount = 0;
		int32 SkippedCount = 0;
		for (FBPVariableDescription& VarDesc : TargetBlueprint->NewVariables)
		{
			FString VarName = VarDesc.VarName.ToString();
			if ((*Categories)->HasField(VarName))
			{
				if (VarDesc.Category.ToString() == TEXT("Default") || VarDesc.Category.IsEmpty())
				{
					FString NewCategory = (*Categories)->GetStringField(VarName);
					VarDesc.Category = FText::FromString(NewCategory);
					UpdatedCount++;
				}
				else
				{
					SkippedCount++;
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);

		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetNumberField(TEXT("updated_count"), UpdatedCount);
		ResultObject->SetNumberField(TEXT("skipped_count"), SkippedCount);
		ResultObject->SetStringField(TEXT("blueprint_path"), BpPath);
		if (SkippedCount > 0)
		{
			ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully categorized %d variables. Skipped %d variables that were already in custom categories."), UpdatedCount, SkippedCount));
		}
		else
		{
			ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully categorized %d variables."), UpdatedCount));
		}
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> VariablesArray;
		for (const FBPVariableDescription& VarDesc : TargetBlueprint->NewVariables)
		{
			if (VarDesc.Category.ToString() == TEXT("Default") || VarDesc.Category.IsEmpty())
			{
				TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject());
				VarObj->SetStringField(TEXT("name"), VarDesc.VarName.ToString());
				VarObj->SetStringField(TEXT("type"), VarDesc.VarType.PinCategory.ToString());
				VarObj->SetStringField(TEXT("category"), VarDesc.Category.ToString());
				VariablesArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
			}
		}

		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetArrayField(TEXT("variables"), VariablesArray);
		ResultObject->SetStringField(TEXT("blueprint_path"), BpPath);
		ResultObject->SetStringField(TEXT("blueprint_name"), TargetBlueprint->GetName());
		ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Found %d uncategorized variables. Provide a 'categories' object to organize them. Already-categorized variables will be preserved."), VariablesArray.Num()));
	}

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	OutJsonString = ResultString;
}

void HandleRenameVariable(const FString& BpPath, const FString& OldName, const FString& NewName, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at: %s"), *BpPath); return; }

	FGuid VarGuid = FBlueprintEditorUtils::FindMemberVariableGuidByName(BP, FName(*OldName));
	if (!VarGuid.IsValid()) { OutError = FString::Printf(TEXT("Variable '%s' not found on '%s'"), *OldName, *BpPath); return; }

	FBlueprintEditorUtils::RenameMemberVariable(BP, FName(*OldName), FName(*NewName));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("old_name"), OldName);
	Res->SetStringField(TEXT("new_name"), NewName);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetVariableFlags(const FString& BpPath, const FString& VarName, const FString& InstanceEditable, const FString& ExposeOnSpawn, const FString& Tooltip, const FString& BlueprintReadOnly, const FString& Replication, const FString& SaveGame, const FString& AdvancedDisplay, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at: %s"), *BpPath); return; }

	bool bFound = false;
	for (FBPVariableDescription& Var : BP->NewVariables)
	{
		if (!Var.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase)) continue;
		bFound = true;

		if (!InstanceEditable.IsEmpty())
		{
			bool bEditable = InstanceEditable.Equals(TEXT("true"), ESearchCase::IgnoreCase) || InstanceEditable.Equals(TEXT("1"));
			if (bEditable)
			{
				Var.PropertyFlags |= CPF_Edit | CPF_BlueprintVisible;
				Var.PropertyFlags &= ~CPF_DisableEditOnInstance;
			}
			else
			{
				Var.PropertyFlags &= ~CPF_Edit;
				Var.PropertyFlags |= CPF_DisableEditOnInstance;
			}
		}
		if (!ExposeOnSpawn.IsEmpty())
		{
			bool bExpose = ExposeOnSpawn.Equals(TEXT("true"), ESearchCase::IgnoreCase) || ExposeOnSpawn.Equals(TEXT("1"));
			if (bExpose)
				Var.PropertyFlags |= CPF_ExposeOnSpawn | CPF_BlueprintVisible;
			else
				Var.PropertyFlags &= ~CPF_ExposeOnSpawn;
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Var.VarName, nullptr, TEXT("ExposeOnSpawn"), bExpose ? TEXT("true") : TEXT("false"));
		}
		if (!BlueprintReadOnly.IsEmpty())
		{
			bool bReadOnly = BlueprintReadOnly.Equals(TEXT("true"), ESearchCase::IgnoreCase) || BlueprintReadOnly.Equals(TEXT("1"));
			if (bReadOnly)
			{
				Var.PropertyFlags |= CPF_BlueprintVisible | CPF_BlueprintReadOnly;
			}
			else
			{
				Var.PropertyFlags |= CPF_BlueprintVisible;
				Var.PropertyFlags &= ~CPF_BlueprintReadOnly;
			}
		}
		if (!Replication.IsEmpty())
		{
			if (Replication.Equals(TEXT("replicated"), ESearchCase::IgnoreCase))
			{
				Var.PropertyFlags |= CPF_Net;
				Var.PropertyFlags &= ~CPF_RepNotify;
				Var.RepNotifyFunc = NAME_None;
			}
			else if (Replication.Equals(TEXT("repnotify"), ESearchCase::IgnoreCase))
			{
				Var.PropertyFlags |= CPF_Net | CPF_RepNotify;
				FString NotifyName = FString::Printf(TEXT("OnRep_%s"), *Var.VarName.ToString());
				Var.RepNotifyFunc = FName(*NotifyName);
			}
			else
			{
				Var.PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
				Var.RepNotifyFunc = NAME_None;
			}
		}
		if (!SaveGame.IsEmpty())
		{
			bool bSave = SaveGame.Equals(TEXT("true"), ESearchCase::IgnoreCase) || SaveGame.Equals(TEXT("1"));
			if (bSave)
				Var.PropertyFlags |= CPF_SaveGame;
			else
				Var.PropertyFlags &= ~CPF_SaveGame;
		}
		if (!AdvancedDisplay.IsEmpty())
		{
			bool bAdv = AdvancedDisplay.Equals(TEXT("true"), ESearchCase::IgnoreCase) || AdvancedDisplay.Equals(TEXT("1"));
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Var.VarName, nullptr, TEXT("AdvancedDisplay"), bAdv ? TEXT("true") : TEXT("false"));
		}
		if (!Tooltip.IsEmpty())
		{
			bool bSet = false;
			for (FBPVariableMetaDataEntry& Entry : Var.MetaDataArray)
			{
				if (Entry.DataKey == FName(TEXT("tooltip"))) { Entry.DataValue = Tooltip; bSet = true; break; }
			}
			if (!bSet) Var.MetaDataArray.Add(FBPVariableMetaDataEntry(FName(TEXT("tooltip")), Tooltip));
		}
		break;
	}

	if (!bFound)
	{
		if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(BP))
		{
			if (WBP->WidgetTree)
			{
				TArray<UWidget*> AllWidgets;
				WBP->WidgetTree->GetAllWidgets(AllWidgets);
				for (UWidget* W : AllWidgets)
				{
					if (W && W->GetName().Equals(VarName, ESearchCase::IgnoreCase))
					{
						W->bIsVariable = true;
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
						UEditorAssetLibrary::SaveAsset(BpPath, false);
						TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject());
						R->SetBoolField(TEXT("success"), true);
						R->SetStringField(TEXT("blueprint_path"), BpPath);
						R->SetStringField(TEXT("variable_name"), VarName);
						R->SetStringField(TEXT("note"), TEXT("Widget component — bIsVariable already true (set automatically by add_widget). Other variable flags (InstanceEditable / ExposeOnSpawn / Replication / SaveGame) don't apply to widget components."));
						TSharedRef<TJsonWriter<>> WriterX = TJsonWriterFactory<>::Create(&OutJsonString);
						FJsonSerializer::Serialize(R.ToSharedRef(), WriterX);
						return;
					}
				}
			}
		}
		OutError = FString::Printf(TEXT("Variable '%s' not found on '%s'"), *VarName, *BpPath);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("variable_name"), VarName);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetFunctionReplication(const FString& BpPath, const FString& FunctionName,
	const FString& Replication, bool bReliable, bool bWithValidation,
	FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		for (UEdGraph* Graph : BP->EventGraphs)
		{
			if (Graph && Graph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
			{
				TargetGraph = Graph;
				break;
			}
		}
	}
	auto ApplyReplicationFlags = [&](uint32& Flags, FString& RepAppliedOut) -> bool
	{
		Flags &= ~(FUNC_Net | FUNC_NetMulticast | FUNC_NetServer | FUNC_NetClient | FUNC_NetReliable | FUNC_NetValidate);
		RepAppliedOut = TEXT("NotReplicated");
		if (Replication.Equals(TEXT("Server"), ESearchCase::IgnoreCase))
		{
			Flags |= FUNC_Net | FUNC_NetServer;
			RepAppliedOut = TEXT("Server");
		}
		else if (Replication.Equals(TEXT("Client"), ESearchCase::IgnoreCase))
		{
			Flags |= FUNC_Net | FUNC_NetClient;
			RepAppliedOut = TEXT("Client");
		}
		else if (Replication.Equals(TEXT("Multicast"), ESearchCase::IgnoreCase) ||
			Replication.Equals(TEXT("NetMulticast"), ESearchCase::IgnoreCase))
		{
			Flags |= FUNC_Net | FUNC_NetMulticast;
			RepAppliedOut = TEXT("Multicast");
		}
		if (bReliable && (Flags & FUNC_Net)) Flags |= FUNC_NetReliable;
		if (bWithValidation && (Flags & FUNC_NetServer)) Flags |= FUNC_NetValidate;
		return true;
	};

	FString RepApplied;

	if (TargetGraph)
	{
		UK2Node_FunctionEntry* EntryNode = nullptr;
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			EntryNode = Cast<UK2Node_FunctionEntry>(Node);
			if (EntryNode) break;
		}
		if (!EntryNode)
		{
			OutError = FString::Printf(TEXT("No FunctionEntry node found in graph '%s'"), *FunctionName);
			return;
		}
		FIntProperty* ExtraFlagsProp = FindFProperty<FIntProperty>(UK2Node_FunctionEntry::StaticClass(), TEXT("ExtraFlags"));
		if (!ExtraFlagsProp)
		{
			OutError = TEXT("Could not find ExtraFlags property on UK2Node_FunctionEntry via reflection");
			return;
		}
		uint32& Flags = reinterpret_cast<uint32&>(*ExtraFlagsProp->ContainerPtrToValuePtr<int32>(EntryNode));
		ApplyReplicationFlags(Flags, RepApplied);
	}
	else
	{
		UK2Node_CustomEvent* CustomEventNode = nullptr;
		auto SearchGraph = [&](UEdGraph* Graph)
		{
			if (!Graph) return;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node);
				if (CE && CE->CustomFunctionName.ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
				{
					CustomEventNode = CE;
					return;
				}
			}
		};
		for (UEdGraph* G : BP->FunctionGraphs)  { if (!CustomEventNode) SearchGraph(G); }
		for (UEdGraph* G : BP->UbergraphPages)   { if (!CustomEventNode) SearchGraph(G); }
		for (UEdGraph* G : BP->EventGraphs)      { if (!CustomEventNode) SearchGraph(G); }
		if (!CustomEventNode)
		{
			OutError = FString::Printf(
				TEXT("Custom event '%s' not found in Blueprint '%s'. "
				     "Create it via build_blueprint_graph before calling set_function_replication."),
				*FunctionName, *BpPath);
			return;
		}
		ApplyReplicationFlags(CustomEventNode->FunctionFlags, RepApplied);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"function\":\"%s\",\"replication\":\"%s\",\"reliable\":%s}"),
		*BpPath, *FunctionName, *RepApplied, bReliable ? TEXT("true") : TEXT("false"));
}

static FEdGraphPinType ParseDispatcherParamType(const FString& TypeStr)
{
	FEdGraphPinType PinType;
	if (ResolveVarType(TypeStr, PinType))
	{
		return PinType;
	}
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	PinType.PinCategory = K2Schema->PC_Object;
	return PinType;
}

void HandleAddEventDispatcher(const FString& BpPath, const FString& DispatcherName,
	const TArray<TSharedPtr<FJsonValue>>* Params, FString& OutJsonString, FString& OutError)
{

	if (DispatcherName.IsEmpty()) { OutError = TEXT("dispatcher_name is required"); return; }

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	for (const FBPVariableDescription& V : BP->NewVariables)
	{
		if (V.VarName == FName(*DispatcherName) && V.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Event dispatcher '%s' already exists\"}"), *DispatcherName);
			return;
		}
	}

	const FName DispFName(*DispatcherName);
	for (UEdGraph* ExistingGraph : BP->FunctionGraphs)
	{
		if (ExistingGraph && ExistingGraph->GetFName() == DispFName)
		{
			OutError = FString::Printf(
				TEXT("Cannot create dispatcher '%s' — a function graph with that name already exists. "
				     "Pick a different dispatcher name (e.g. 'On%sEvent'), OR if the existing function is "
				     "obsolete delete it first via delete_function. Proceeding would cause the UE compile "
				     "error: 'Graph named ''%s'' already exists'."),
				*DispatcherName, *DispatcherName, *DispatcherName);
			return;
		}
	}
	for (UEdGraph* UGraph : BP->UbergraphPages)
	{
		if (!UGraph) continue;
		for (UEdGraphNode* Node : UGraph->Nodes)
		{
			UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node);
			if (CE && CE->CustomFunctionName == DispFName)
			{
				OutError = FString::Printf(
					TEXT("Cannot create dispatcher '%s' — a CustomEvent named '%s' already exists in "
					     "EventGraph. CustomEvents and Dispatchers share the skeleton-function namespace, "
					     "so both claiming the same name triggers 'property %s already exists' at compile. "
					     "Either rename the dispatcher, OR delete the CustomEvent node via delete_nodes "
					     "first and re-add it later with a different name like '%s_Handler'."),
					*DispatcherName, *DispatcherName, *DispatcherName, *DispatcherName);
				return;
			}
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Add Event Dispatcher")));
	BP->Modify();

	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	if (!FBlueprintEditorUtils::AddMemberVariable(BP, FName(*DispatcherName), DelegateType))
	{
		OutError = FString::Printf(TEXT("Failed to add event dispatcher variable '%s'"), *DispatcherName);
		return;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraph* SigGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, FName(*DispatcherName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!SigGraph)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(BP, FName(*DispatcherName));
		OutError = TEXT("Failed to create delegate signature graph");
		return;
	}
	SigGraph->bEditable = false;
	K2Schema->CreateDefaultNodesForGraph(*SigGraph);
	K2Schema->CreateFunctionGraphTerminators(*SigGraph, (UClass*)nullptr);
	K2Schema->AddExtraFunctionFlags(SigGraph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
	K2Schema->MarkFunctionEntryAsEditable(SigGraph, true);
	BP->DelegateSignatureGraphs.Add(SigGraph);

	if (Params && Params->Num() > 0)
	{
		UK2Node_FunctionEntry* EntryNode = nullptr;
		for (UEdGraphNode* Node : SigGraph->Nodes)
		{
			EntryNode = Cast<UK2Node_FunctionEntry>(Node);
			if (EntryNode) break;
		}
		if (EntryNode)
		{
			for (const TSharedPtr<FJsonValue>& PV : *Params)
			{
				TSharedPtr<FJsonObject> PO = PV ? PV->AsObject() : nullptr;
				if (!PO.IsValid()) continue;
				FString PName, PType;
				PO->TryGetStringField(TEXT("name"), PName);
				PO->TryGetStringField(TEXT("type"), PType);
				if (PName.IsEmpty() || PType.IsEmpty()) continue;
				FEdGraphPinType PT = ParseDispatcherParamType(PType);
				EntryNode->CreateUserDefinedPin(FName(*PName), PT, EGPD_Output);
			}
			EntryNode->ReconstructNode();
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"dispatcher_name\":\"%s\",\"blueprint_path\":\"%s\",\"param_count\":%d}"),
		*DispatcherName, *BpPath, Params ? Params->Num() : 0);
}

void HandleDeleteEventDispatcher(const FString& BpPath, const FString& DispatcherName,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	bool bFound = false;
	for (const FBPVariableDescription& V : BP->NewVariables)
	{
		if (V.VarName == FName(*DispatcherName)) { bFound = true; break; }
	}
	if (!bFound) { OutError = FString::Printf(TEXT("Event dispatcher '%s' not found"), *DispatcherName); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Event Dispatcher")));
	BP->Modify();

	if (UEdGraph* SigGraph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(BP, FName(*DispatcherName)))
	{
		BP->DelegateSignatureGraphs.Remove(SigGraph);
		FBlueprintEditorUtils::RemoveGraph(BP, SigGraph, EGraphRemoveFlags::None);
	}

	FBlueprintEditorUtils::RemoveMemberVariable(BP, FName(*DispatcherName));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"message\":\"Deleted event dispatcher '%s'\"}"), *DispatcherName);
}

void HandleAddDispatcherParam(const FString& BpPath, const FString& DispatcherName,
	const FString& ParamName, const FString& ParamType, FString& OutJsonString, FString& OutError)
{

	if (ParamName.IsEmpty() || ParamType.IsEmpty()) { OutError = TEXT("param_name and param_type are required"); return; }

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UEdGraph* SigGraph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(BP, FName(*DispatcherName));
	if (!SigGraph) { OutError = FString::Printf(TEXT("Event dispatcher '%s' not found"), *DispatcherName); return; }

	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : SigGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}
	if (!EntryNode) { OutError = TEXT("Could not find signature entry node"); return; }

	for (const TSharedPtr<FUserPinInfo>& Pin : EntryNode->UserDefinedPins)
	{
		if (Pin.IsValid() && Pin->PinName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("Parameter '%s' already exists on dispatcher '%s'"), *ParamName, *DispatcherName);
			return;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Add Dispatcher Parameter")));
	BP->Modify();
	SigGraph->Modify();

	FEdGraphPinType PT = ParseDispatcherParamType(ParamType);
	EntryNode->CreateUserDefinedPin(FName(*ParamName), PT, EGPD_Output);
	EntryNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"dispatcher_name\":\"%s\",\"param_name\":\"%s\",\"param_type\":\"%s\"}"),
		*DispatcherName, *ParamName, *ParamType);
}

void HandleAddDispatcherParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("params"), ItemsArray))
	{
		FString OuterDispatcher;
		Args->TryGetStringField(TEXT("dispatcher_name"), OuterDispatcher);

		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			FString Dispatcher = BatchToolHelper::GetItemString(Item, TEXT("dispatcher_name"));
			if (Dispatcher.IsEmpty()) Dispatcher = OuterDispatcher;
			FString ParamName = BatchToolHelper::GetItemString(Item, TEXT("param_name"), TEXT("name"));
			FString ParamType = BatchToolHelper::GetItemString(Item, TEXT("param_type"), TEXT("type"));
			if (Dispatcher.IsEmpty() || ParamName.IsEmpty() || ParamType.IsEmpty())
			{
				Batch.AddFailure(i, TEXT("Missing dispatcher_name, param_name, or param_type"));
				continue;
			}

			FString ItemJson, ItemErr;
			HandleAddDispatcherParam(BpPath, Dispatcher, ParamName, ParamType, ItemJson, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("dispatcher_name"), Dispatcher);
				E->SetStringField(TEXT("param_name"), ParamName);
				Batch.AddSuccess(i, E);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString DispatcherName, ParamName, ParamType;
	Args->TryGetStringField(TEXT("dispatcher_name"), DispatcherName);
	if (!Args->TryGetStringField(TEXT("param_name"), ParamName))
		Args->TryGetStringField(TEXT("name"), ParamName);
	if (!Args->TryGetStringField(TEXT("param_type"), ParamType))
		Args->TryGetStringField(TEXT("type"), ParamType);
	HandleAddDispatcherParam(BpPath, DispatcherName, ParamName, ParamType, OutJsonString, OutError);
}

void HandleRemoveDispatcherParam(const FString& BpPath, const FString& DispatcherName,
	const FString& ParamName, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UEdGraph* SigGraph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(BP, FName(*DispatcherName));
	if (!SigGraph) { OutError = FString::Printf(TEXT("Event dispatcher '%s' not found"), *DispatcherName); return; }

	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : SigGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}
	if (!EntryNode) { OutError = TEXT("Could not find signature entry node"); return; }

	int32 PinIdx = INDEX_NONE;
	for (int32 i = 0; i < EntryNode->UserDefinedPins.Num(); ++i)
	{
		if (EntryNode->UserDefinedPins[i].IsValid() &&
			EntryNode->UserDefinedPins[i]->PinName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
		{
			PinIdx = i;
			break;
		}
	}
	if (PinIdx == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Parameter '%s' not found on dispatcher '%s'"), *ParamName, *DispatcherName);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Remove Dispatcher Parameter")));
	BP->Modify();
	SigGraph->Modify();

	EntryNode->RemoveUserDefinedPinByName(FName(*ParamName));
	EntryNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"message\":\"Removed parameter '%s' from dispatcher '%s'\"}"),
		*ParamName, *DispatcherName);
}

static UEdGraph* FindFunctionGraph(UBlueprint* BP, const FString& FunctionName)
{
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G && G->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
			return G;
	}
	return nullptr;
}

static UK2Node_FunctionEntry* FindEntryNode(UEdGraph* Graph)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* E = Cast<UK2Node_FunctionEntry>(Node))
			return E;
	}
	return nullptr;
}

static UK2Node_FunctionResult* FindResultNode(UEdGraph* Graph)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionResult* R = Cast<UK2Node_FunctionResult>(Node))
			return R;
	}
	return nullptr;
}

void HandleAddFunctionParam(const FString& BpPath, const FString& FunctionName, const FString& ParamName, const FString& ParamType, bool bIsOutput, FString& OutJsonString, FString& OutError)
{

	if (ParamName.IsEmpty()) { OutError = TEXT("Missing required parameter: param_name"); return; }
	if (ParamType.IsEmpty()) { OutError = TEXT("Missing required parameter: param_type"); return; }

	if (ParamType.Equals(TEXT("struct"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("param_type 'struct' is too vague — use the specific struct name instead (e.g. 'FHitResult', 'FVector', 'S_MyCustomStruct')");
		return;
	}

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath); return; }

	UEdGraph* Graph = FindFunctionGraph(BP, FunctionName);
	if (!Graph) { OutError = FString::Printf(TEXT("Function '%s' not found in Blueprint"), *FunctionName); return; }

	UK2Node_FunctionEntry* EntryNode = FindEntryNode(Graph);
	if (!EntryNode) { OutError = TEXT("Function entry node not found"); return; }

	FEdGraphPinType PinType;
	if (!ResolveVarType(ParamType, PinType))
	{
		OutError = FString::Printf(TEXT("Unknown parameter type: %s"), *ParamType);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Add Function Parameter")));
	BP->Modify(); Graph->Modify(); EntryNode->Modify();

	if (bIsOutput)
	{
		UK2Node_FunctionResult* ResultNode = FindResultNode(Graph);
		if (!ResultNode)
			ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
		if (!ResultNode) { OutError = TEXT("Failed to find or create function result node"); return; }
		ResultNode->Modify();
		ResultNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Input, true);
		ResultNode->ReconstructNode();
	}
	else
	{
		EntryNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output, true);
		EntryNode->ReconstructNode();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"function_name\":\"%s\",\"param_name\":\"%s\",\"param_type\":\"%s\",\"is_output\":%s,\"warning\":\"Existing call nodes for '%s' in other graphs have stale pins. You MUST remove_nodes and re-place them before connecting.\"}"),
		*FunctionName, *ParamName, *ParamType, bIsOutput ? TEXT("true") : TEXT("false"), *FunctionName);
}

void HandleAddFunctionParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath, FunctionName;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	Args->TryGetStringField(TEXT("function_name"), FunctionName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("params"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			FString ParamName = BatchToolHelper::GetItemString(Item, TEXT("param_name"), TEXT("name"));
			FString ParamType = BatchToolHelper::GetItemString(Item, TEXT("param_type"), TEXT("type"));
			bool bIsOutput = false;
			Item->TryGetBoolField(TEXT("is_output"), bIsOutput);

			FString ItemFN = BatchToolHelper::GetItemString(Item, TEXT("function_name"));
			if (ItemFN.IsEmpty()) ItemFN = FunctionName;

			FString ItemOut, ItemErr;
			HandleAddFunctionParam(BpPath, ItemFN, ParamName, ParamType, bIsOutput, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("param_name"), ParamName);
				E->SetStringField(TEXT("param_type"), ParamType);
				E->SetBoolField(TEXT("is_output"), bIsOutput);
				Batch.AddSuccess(i, E);
			}
			else
			{
				Batch.AddFailure(i, ItemErr);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString ParamName, ParamType;
	bool bIsOutput = false;
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	Args->TryGetStringField(TEXT("param_type"), ParamType);
	Args->TryGetBoolField(TEXT("is_output"), bIsOutput);
	HandleAddFunctionParam(BpPath, FunctionName, ParamName, ParamType, bIsOutput, OutJsonString, OutError);
}

void HandleRemoveFunctionParam(const FString& BpPath, const FString& FunctionName, const FString& ParamName, FString& OutJsonString, FString& OutError)
{

	if (ParamName.IsEmpty()) { OutError = TEXT("Missing required parameter: param_name"); return; }

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath); return; }

	UEdGraph* Graph = FindFunctionGraph(BP, FunctionName);
	if (!Graph) { OutError = FString::Printf(TEXT("Function '%s' not found in Blueprint"), *FunctionName); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Remove Function Parameter")));
	BP->Modify(); Graph->Modify();

	bool bRemoved = false;
	FName ParamFName(*ParamName);

	if (UK2Node_FunctionEntry* EntryNode = FindEntryNode(Graph))
	{
		for (const TSharedPtr<FUserPinInfo>& Pin : EntryNode->UserDefinedPins)
		{
			if (Pin.IsValid() && Pin->PinName == ParamFName)
			{
				EntryNode->Modify();
				EntryNode->RemoveUserDefinedPinByName(ParamFName);
				EntryNode->ReconstructNode();
				bRemoved = true;
				break;
			}
		}
	}
	if (!bRemoved)
	{
		if (UK2Node_FunctionResult* ResultNode = FindResultNode(Graph))
		{
			for (const TSharedPtr<FUserPinInfo>& Pin : ResultNode->UserDefinedPins)
			{
				if (Pin.IsValid() && Pin->PinName == ParamFName)
				{
					ResultNode->Modify();
					ResultNode->RemoveUserDefinedPinByName(ParamFName);
					ResultNode->ReconstructNode();
					bRemoved = true;
					break;
				}
			}
		}
	}

	if (!bRemoved)
	{
		OutError = FString::Printf(TEXT("Parameter '%s' not found on function '%s'"), *ParamName, *FunctionName);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"function_name\":\"%s\",\"param_name\":\"%s\",\"message\":\"Parameter removed\",\"warning\":\"Existing call nodes for '%s' in other graphs have stale pins. You MUST remove_nodes and re-place them before connecting.\"}"),
		*FunctionName, *ParamName, *FunctionName);
}

void HandleSetFunctionAccess(const FString& BpPath, const FString& FunctionName, const FString& Access, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath); return; }

	UEdGraph* Graph = FindFunctionGraph(BP, FunctionName);
	if (!Graph) { OutError = FString::Printf(TEXT("Function '%s' not found in Blueprint"), *FunctionName); return; }

	UK2Node_FunctionEntry* EntryNode = FindEntryNode(Graph);
	if (!EntryNode) { OutError = TEXT("Function entry node not found"); return; }

	FIntProperty* ExtraFlagsProp = FindFProperty<FIntProperty>(UK2Node_FunctionEntry::StaticClass(), TEXT("ExtraFlags"));
	if (!ExtraFlagsProp) { OutError = TEXT("Could not access ExtraFlags via reflection"); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Set Function Access")));
	BP->Modify(); EntryNode->Modify();

	uint32& Flags = reinterpret_cast<uint32&>(*ExtraFlagsProp->ContainerPtrToValuePtr<int32>(EntryNode));
	Flags &= ~(FUNC_Public | FUNC_Private | FUNC_Protected);
	if (Access.Equals(TEXT("private"), ESearchCase::IgnoreCase))
		Flags |= FUNC_Private;
	else if (Access.Equals(TEXT("protected"), ESearchCase::IgnoreCase))
		Flags |= FUNC_Protected;
	else
		Flags |= FUNC_Public;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"function_name\":\"%s\",\"access\":\"%s\"}"),
		*FunctionName, *Access.ToLower());
}

void HandleSetFunctionPure(const FString& BpPath, const FString& FunctionName, bool bPure, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath); return; }

	UEdGraph* Graph = FindFunctionGraph(BP, FunctionName);
	if (!Graph) { OutError = FString::Printf(TEXT("Function '%s' not found in Blueprint"), *FunctionName); return; }

	UK2Node_FunctionEntry* EntryNode = FindEntryNode(Graph);
	if (!EntryNode) { OutError = TEXT("Function entry node not found"); return; }

	FIntProperty* ExtraFlagsProp = FindFProperty<FIntProperty>(UK2Node_FunctionEntry::StaticClass(), TEXT("ExtraFlags"));
	if (!ExtraFlagsProp) { OutError = TEXT("Could not access ExtraFlags via reflection"); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Set Function Pure")));
	BP->Modify(); EntryNode->Modify();

	uint32& Flags = reinterpret_cast<uint32&>(*ExtraFlagsProp->ContainerPtrToValuePtr<int32>(EntryNode));
	if (bPure) Flags |= FUNC_BlueprintPure;
	else       Flags &= ~FUNC_BlueprintPure;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	int32 NodesReconstructed = 0;
	const FName FuncFName(*FunctionName);
	const auto ReconstructInGraph = [&NodesReconstructed, FuncFName](UEdGraph* G)
	{
		if (!G) return;
		TArray<UK2Node_CallFunction*> CallNodes;
		G->GetNodesOfClass<UK2Node_CallFunction>(CallNodes);
		for (UK2Node_CallFunction* CallNode : CallNodes)
		{
			if (CallNode && CallNode->FunctionReference.GetMemberName() == FuncFName)
			{
				CallNode->Modify();
				CallNode->ReconstructNode();
				++NodesReconstructed;
			}
		}
	};
	for (UEdGraph* G : BP->FunctionGraphs)  ReconstructInGraph(G);
	for (UEdGraph* G : BP->UbergraphPages)  ReconstructInGraph(G);
	for (UEdGraph* G : BP->MacroGraphs)     ReconstructInGraph(G);

	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipSave);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"function_name\":\"%s\",\"pure\":%s,\"call_sites_refreshed\":%d}"),
		*FunctionName, bPure ? TEXT("true") : TEXT("false"), NodesReconstructed);
}

void HandleListOverridableFunctions(const FString& BpPath, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath); return; }

	UClass* ParentClass = BP->ParentClass;
	if (!ParentClass) { OutError = TEXT("Blueprint has no parent class"); return; }

	TSet<FString> AlreadyOverridden;
	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G) AlreadyOverridden.Add(G->GetName().ToLower());
	}

	TArray<TSharedPtr<FJsonValue>> FunctionsArray;

	for (TFieldIterator<UFunction> It(ParentClass, EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		UFunction* Func = *It;
		if (!Func) continue;

		if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent)) continue;

		if (Func->HasAnyFunctionFlags(FUNC_Static)) continue;
		if (Func->HasAnyFunctionFlags(FUNC_Private)) continue;
		if (Func->HasAnyFunctionFlags(FUNC_Exec)) continue;

		FString FuncName = Func->GetName();
		if (AlreadyOverridden.Contains(FuncName.ToLower())) continue;

		FString FuncType = Func->HasAnyFunctionFlags(FUNC_BlueprintEvent) ? TEXT("event") : TEXT("function");

		TArray<TSharedPtr<FJsonValue>> Inputs, Outputs;
		for (TFieldIterator<FProperty> PropIt(Func); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			FProperty* Prop = *PropIt;
			bool bIsReturn = Prop->HasAnyPropertyFlags(CPF_ReturnParm);
			bool bIsOut    = Prop->HasAnyPropertyFlags(CPF_OutParm) && !bIsReturn;

			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), Prop->GetName());
			ParamObj->SetStringField(TEXT("type"), Prop->GetCPPType());

			if (bIsReturn || bIsOut)
				Outputs.Add(MakeShared<FJsonValueObject>(ParamObj));
			else
				Inputs.Add(MakeShared<FJsonValueObject>(ParamObj));
		}

		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), FuncName);
		FuncObj->SetStringField(TEXT("type"), FuncType);
		FuncObj->SetStringField(TEXT("declared_in"), ParentClass->GetName());
		FuncObj->SetArrayField(TEXT("inputs"), Inputs);
		FuncObj->SetArrayField(TEXT("outputs"), Outputs);

		FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("blueprint_path"), BpPath);
	Root->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	Root->SetArrayField(TEXT("functions"), FunctionsArray);
	Root->SetNumberField(TEXT("count"), FunctionsArray.Num());

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

void HandleReparentBlueprint(const FString& BpPath, const FString& NewParentClass, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath); return; }

	UClass* NewClass = FindFirstObject<UClass>(*NewParentClass, EFindFirstObjectOptions::NativeFirst);
	if (!NewClass)
	{
		NewClass = FindFirstObject<UClass>(*FString::Printf(TEXT("A%s"), *NewParentClass), EFindFirstObjectOptions::NativeFirst);
		if (!NewClass) NewClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *NewParentClass), EFindFirstObjectOptions::NativeFirst);
	}
	if (!NewClass)
	{
		FString Ref = NewParentClass;
		Ref.RemoveFromEnd(TEXT("_C"));
		UBlueprint* ParentBP = nullptr;
		if (Ref.Contains(TEXT("/")))
		{
			ParentBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Ref));
		}
		else
		{
			FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			FARFilter Filter;
			Filter.PackagePaths.Add(FName(TEXT("/Game")));
			Filter.bRecursivePaths = true;
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
			TArray<FAssetData> Hits;
			AR.Get().GetAssets(Filter, Hits);
			for (const FAssetData& AD : Hits)
				if (AD.AssetName.ToString().Equals(Ref, ESearchCase::IgnoreCase))
				{ ParentBP = Cast<UBlueprint>(AD.GetAsset()); break; }
		}
		if (ParentBP && ParentBP->GeneratedClass)
			NewClass = ParentBP->GeneratedClass;
	}
	if (!NewClass) { OutError = FString::Printf(TEXT("Class '%s' not found. Use an engine class name (e.g. 'Character', 'Pawn', 'Actor') OR a Blueprint asset path/name (e.g. '/Game/.../BP_Boss' or 'BP_Boss')."), *NewParentClass); return; }

	if (BP->GeneratedClass && (NewClass == BP->GeneratedClass || NewClass->IsChildOf(BP->GeneratedClass)))
	{
		OutError = FString::Printf(TEXT("Cannot reparent '%s' to '%s' — that class is this Blueprint itself or a descendant of it (would create a cycle)."), *BpPath, *NewClass->GetName());
		return;
	}

	FString OldParent = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None");

	const FScopedTransaction Transaction(FText::FromString(TEXT("Reparent Blueprint")));
	BP->Modify();
	BP->ParentClass = NewClass;
	FBlueprintEditorUtils::RefreshAllNodes(BP);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipSave);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"old_parent\":\"%s\",\"new_parent\":\"%s\"}"),
		*BpPath, *OldParent, *NewClass->GetName());
}

void HandleAddVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsSessionActive()) { OutError = TEXT("Session not ready — reload the editor and try again."); return; }
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::GraphAuthoring)) { OutError = TEXT("Graph authoring isn't enabled for this install's profile yet — reconnect and try again."); return; }
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
		if (!Args->TryGetStringField(TEXT("anim_blueprint_path"), BpPath) || BpPath.IsEmpty())
			Args->TryGetStringField(TEXT("anim_bp_path"), BpPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("variables"), ItemsArray))
	{
		if (BpPath.IsEmpty() && ItemsArray->Num() > 0)
		{
			TSharedPtr<FJsonObject> First = (*ItemsArray)[0]->AsObject();
			if (First.IsValid()) First->TryGetStringField(TEXT("blueprint_path"), BpPath);
		}
		if (BpPath.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path"); return; }
		HandleAddVariablesBulk(BpPath, *ItemsArray, OutJsonString, OutError);
		return;
	}

	if (BpPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	FString VarName, VarType, Default, Category;
	if (!Args->TryGetStringField(TEXT("variable_name"), VarName))
		if (!Args->TryGetStringField(TEXT("name"), VarName))
			Args->TryGetStringField(TEXT("var_name"), VarName);
	if (!Args->TryGetStringField(TEXT("variable_type"), VarType))
		if (!Args->TryGetStringField(TEXT("type"), VarType))
			Args->TryGetStringField(TEXT("var_type"), VarType);
	if (!Args->TryGetStringField(TEXT("default_value"), Default))
		Args->TryGetStringField(TEXT("default"), Default);
	Args->TryGetStringField(TEXT("category"), Category);

	if (VarType.Equals(TEXT("object"), ESearchCase::IgnoreCase))
	{
		FString SubType;
		if (!Args->TryGetStringField(TEXT("sub_type"), SubType))
			Args->TryGetStringField(TEXT("object_class"), SubType);
		if (!SubType.IsEmpty())
			VarType = TEXT("object:") + SubType;
	}
	bool bIsArray = false;
	Args->TryGetBoolField(TEXT("is_array"), bIsArray);
	if (bIsArray && !VarType.IsEmpty()
		&& !VarType.StartsWith(TEXT("TArray<"), ESearchCase::IgnoreCase)
		&& !VarType.StartsWith(TEXT("Array<"), ESearchCase::IgnoreCase))
	{
		VarType = TEXT("TArray<") + VarType + TEXT(">");
	}

	{
		FString MapKeyType, MapValueType;
		const bool bHasKey = Args->TryGetStringField(TEXT("map_key_type"), MapKeyType) && !MapKeyType.IsEmpty();
		const bool bHasVal = Args->TryGetStringField(TEXT("map_value_type"), MapValueType) && !MapValueType.IsEmpty();
		if (bHasKey && bHasVal)
		{
			VarType = FString::Printf(TEXT("TMap<%s,%s>"), *MapKeyType, *MapValueType);
		}
		else if (bHasKey != bHasVal)
		{
			OutError = TEXT("Map variable needs BOTH map_key_type and map_value_type. Pass both, or use type:'Map:KeyType,ValueType' / 'TMap<KeyType,ValueType>' directly.");
			return;
		}

		FString SetElementType;
		if (Args->TryGetStringField(TEXT("set_element_type"), SetElementType) && !SetElementType.IsEmpty())
		{
			VarType = FString::Printf(TEXT("TSet<%s>"), *SetElementType);
		}

		FString ArrayElementType;
		if (Args->TryGetStringField(TEXT("array_element_type"), ArrayElementType) && !ArrayElementType.IsEmpty())
		{
			VarType = FString::Printf(TEXT("TArray<%s>"), *ArrayElementType);
		}
	}

	HandleAddVariable(BpPath, VarName, VarType, Default, Category, OutError);
	if (OutError.IsEmpty())
	{
		const FString IE = BatchToolHelper::GetStringOrBool(Args, TEXT("instance_editable"));
		const FString EOS = BatchToolHelper::GetStringOrBool(Args, TEXT("expose_on_spawn"));
		const FString BRO = BatchToolHelper::GetStringOrBool(Args, TEXT("blueprint_read_only"));
		const FString SG = BatchToolHelper::GetStringOrBool(Args, TEXT("save_game"));
		const FString AD = BatchToolHelper::GetStringOrBool(Args, TEXT("advanced_display"));
		FString Rep; Args->TryGetStringField(TEXT("replication"), Rep);
		FString TT; Args->TryGetStringField(TEXT("tooltip"), TT);
		const bool bHasAnyFlag = !IE.IsEmpty() || !EOS.IsEmpty() || !BRO.IsEmpty()
			|| !SG.IsEmpty() || !AD.IsEmpty() || !Rep.IsEmpty() || !TT.IsEmpty();
		if (bHasAnyFlag)
		{
			FString FlagOut, FlagErr;
			HandleSetVariableFlags(BpPath, VarName, IE, EOS, TT, BRO, Rep, SG, AD, FlagOut, FlagErr);
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"variable_name\":\"%s\",\"variable_type\":\"%s\",\"blueprint_path\":\"%s\",\"flags_applied\":%s%s%s}"),
				*VarName, *VarType, *BpPath,
				FlagErr.IsEmpty() ? TEXT("true") : TEXT("false"),
				FlagErr.IsEmpty() ? TEXT("") : TEXT(",\"flags_error\":\""),
				FlagErr.IsEmpty() ? TEXT("") : *(FlagErr.Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"")));
			return;
		}

		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"variable_name\":\"%s\",\"variable_type\":\"%s\",\"blueprint_path\":\"%s\"}"),
			*VarName, *VarType, *BpPath);
	}
}

void HandleSetBlueprintVariableDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("defaults"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString VarName = BatchToolHelper::GetItemString(Item, TEXT("variable_name"), TEXT("name"));
			FString NewDefault = BatchToolHelper::GetItemString(Item, TEXT("default_value"), TEXT("value"));
			if (VarName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing variable_name")); continue; }
			FString ItemOut, ItemErr;
			HandleSetBlueprintVariableDefault(BpPath, VarName, NewDefault, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("variable_name"), VarName);
				Batch.AddSuccess(i, Extra);
			}
			else
				Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString VarName, NewDefault;
	if (!Args->TryGetStringField(TEXT("variable_name"), VarName))
		Args->TryGetStringField(TEXT("name"), VarName);
	if (!Args->TryGetStringField(TEXT("default_value"), NewDefault))
		Args->TryGetStringField(TEXT("value"), NewDefault);
	HandleSetBlueprintVariableDefault(BpPath, VarName, NewDefault, OutJsonString, OutError);
}

void HandleRenameVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("renames"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString OldName = BatchToolHelper::GetItemString(Item, TEXT("old_name"), TEXT("variable_name"));
			if (OldName.IsEmpty()) OldName = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString NewName = BatchToolHelper::GetItemString(Item, TEXT("new_name"));
			if (OldName.IsEmpty() || NewName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing old_name (or variable_name) / new_name")); continue; }
			FString ItemOut, ItemErr;
			HandleRenameVariable(BpPath, OldName, NewName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("old_name"), OldName);
				Extra->SetStringField(TEXT("new_name"), NewName);
				Batch.AddSuccess(i, Extra);
			}
			else
				Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString OldName, NewName;
	if (!Args->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		if (!Args->TryGetStringField(TEXT("variable_name"), OldName) || OldName.IsEmpty())
			Args->TryGetStringField(TEXT("name"), OldName);
	}
	Args->TryGetStringField(TEXT("new_name"), NewName);
	HandleRenameVariable(BpPath, OldName, NewName, OutJsonString, OutError);
}

void HandleSetVariableFlagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("flags"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString VarName = BatchToolHelper::GetItemString(Item, TEXT("variable_name"), TEXT("name"));
			if (VarName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing variable_name")); continue; }
			FString IE = BatchToolHelper::GetStringOrBool(Item, TEXT("instance_editable"));
			FString EOS = BatchToolHelper::GetStringOrBool(Item, TEXT("expose_on_spawn"));
			FString TT; Item->TryGetStringField(TEXT("tooltip"), TT);
			FString BRO = BatchToolHelper::GetStringOrBool(Item, TEXT("blueprint_read_only"));
			FString Rep; Item->TryGetStringField(TEXT("replication"), Rep);
			FString SG = BatchToolHelper::GetStringOrBool(Item, TEXT("save_game"));
			FString AD = BatchToolHelper::GetStringOrBool(Item, TEXT("advanced_display"));
			FString ItemOut, ItemErr;
			HandleSetVariableFlags(BpPath, VarName, IE, EOS, TT, BRO, Rep, SG, AD, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("variable_name"), VarName);
				Batch.AddSuccess(i, Extra);
			}
			else
				Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString VarName;
	if (!Args->TryGetStringField(TEXT("variable_name"), VarName))
		Args->TryGetStringField(TEXT("name"), VarName);
	FString IE = BatchToolHelper::GetStringOrBool(Args, TEXT("instance_editable"));
	FString EOS = BatchToolHelper::GetStringOrBool(Args, TEXT("expose_on_spawn"));
	FString TT; Args->TryGetStringField(TEXT("tooltip"), TT);
	FString BRO = BatchToolHelper::GetStringOrBool(Args, TEXT("blueprint_read_only"));
	FString Rep; Args->TryGetStringField(TEXT("replication"), Rep);
	FString SG = BatchToolHelper::GetStringOrBool(Args, TEXT("save_game"));
	FString AD = BatchToolHelper::GetStringOrBool(Args, TEXT("advanced_display"));
	HandleSetVariableFlags(BpPath, VarName, IE, EOS, TT, BRO, Rep, SG, AD, OutJsonString, OutError);
}

void HandleAddEventDispatcherFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("dispatchers"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString DispName = BatchToolHelper::GetItemString(Item, TEXT("dispatcher_name"), TEXT("name"));
			if (DispName.IsEmpty()) DispName = BatchToolHelper::GetItemString(Item, TEXT("event_name"));
			if (DispName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing dispatcher_name")); continue; }
			TArray<TSharedPtr<FJsonValue>> ItemParsedStorage;
			const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
			if (!Item->TryGetArrayField(TEXT("params"), Params) || !Params)
				if (!Item->TryGetArrayField(TEXT("parameters"), Params) || !Params)
					Item->TryGetArrayField(TEXT("inputs"), Params);
			if (!Params)
			{
				for (const TCHAR* Key : {TEXT("params"), TEXT("parameters"), TEXT("inputs")})
				{
					FString Str;
					if (!Item->TryGetStringField(Key, Str) || Str.IsEmpty()) continue;
					TSharedPtr<FJsonValue> ParsedVal;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Str);
					if (FJsonSerializer::Deserialize(Reader, ParsedVal) && ParsedVal.IsValid()
						&& ParsedVal->Type == EJson::Array)
					{
						ItemParsedStorage = ParsedVal->AsArray();
						Params = &ItemParsedStorage;
						break;
					}
				}
			}
			FString ItemOut, ItemErr;
			HandleAddEventDispatcher(BpPath, DispName, Params, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("dispatcher_name"), DispName);
				Batch.AddSuccess(i, Extra);
			}
			else
				Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString DispName;
	if (!Args->TryGetStringField(TEXT("dispatcher_name"), DispName))
		if (!Args->TryGetStringField(TEXT("name"), DispName))
			Args->TryGetStringField(TEXT("event_name"), DispName);
	TArray<TSharedPtr<FJsonValue>> ParsedParamsStorage;
	const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
	if (!Args->TryGetArrayField(TEXT("params"), Params) || !Params)
		if (!Args->TryGetArrayField(TEXT("parameters"), Params) || !Params)
			Args->TryGetArrayField(TEXT("inputs"), Params);
	if (!Params)
	{
		for (const TCHAR* Key : {TEXT("params"), TEXT("parameters"), TEXT("inputs")})
		{
			FString Str;
			if (!Args->TryGetStringField(Key, Str) || Str.IsEmpty()) continue;
			TSharedPtr<FJsonValue> ParsedVal;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Str);
			if (FJsonSerializer::Deserialize(Reader, ParsedVal) && ParsedVal.IsValid()
				&& ParsedVal->Type == EJson::Array)
			{
				ParsedParamsStorage = ParsedVal->AsArray();
				Params = &ParsedParamsStorage;
				break;
			}
		}
	}
	HandleAddEventDispatcher(BpPath, DispName, Params, OutJsonString, OutError);
}

static ELifetimeCondition ParseLifetimeCondition(const FString& CondName)
{
	if (CondName.Equals(TEXT("None"), ESearchCase::IgnoreCase))              return COND_None;
	if (CondName.Equals(TEXT("InitialOnly"), ESearchCase::IgnoreCase))       return COND_InitialOnly;
	if (CondName.Equals(TEXT("OwnerOnly"), ESearchCase::IgnoreCase))         return COND_OwnerOnly;
	if (CondName.Equals(TEXT("SkipOwner"), ESearchCase::IgnoreCase))         return COND_SkipOwner;
	if (CondName.Equals(TEXT("SimulatedOnly"), ESearchCase::IgnoreCase))     return COND_SimulatedOnly;
	if (CondName.Equals(TEXT("AutonomousOnly"), ESearchCase::IgnoreCase))    return COND_AutonomousOnly;
	if (CondName.Equals(TEXT("SimulatedOrPhysics"), ESearchCase::IgnoreCase))return COND_SimulatedOrPhysics;
	if (CondName.Equals(TEXT("InitialOrOwner"), ESearchCase::IgnoreCase))    return COND_InitialOrOwner;
	if (CondName.Equals(TEXT("Custom"), ESearchCase::IgnoreCase))            return COND_Custom;
	return COND_None;
}

void HandleSetVariableReplicationCondition(const FString& BpPath, const FString& VarName,
	const FString& Condition, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: '%s'"), *BpPath); return; }
	if (VarName.IsEmpty()) { OutError = TEXT("variable_name is required"); return; }

	for (FBPVariableDescription& Var : BP->NewVariables)
	{
		if (!Var.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase)) continue;

		if ((Var.PropertyFlags & CPF_Net) == 0)
		{
			Var.PropertyFlags |= CPF_Net;
			Var.PropertyFlags &= ~CPF_RepNotify;
			Var.RepNotifyFunc = NAME_None;
		}

		Var.ReplicationCondition = ParseLifetimeCondition(Condition);
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"variable\":\"%s\",\"replication_condition\":\"%s\"}"),
			*VarName, *Condition);
		return;
	}

	OutError = FString::Printf(TEXT("Variable '%s' not found in blueprint '%s'."), *VarName, *BpPath);
}

void HandleAddLocalVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, FN;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	if (!Args->TryGetStringField(TEXT("function_name"), FN))
		Args->TryGetStringField(TEXT("function_scope"), FN);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArr = nullptr;
	if ((Args->TryGetArrayField(TEXT("items"), ItemsArr) || Args->TryGetArrayField(TEXT("variables"), ItemsArr)) && ItemsArr)
	{
		int32 Added = 0, Failed = 0;
		FString Errs;
		for (const TSharedPtr<FJsonValue>& V : *ItemsArr)
		{
			TSharedPtr<FJsonObject> It = (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : nullptr;
			if (!It.IsValid()) { ++Failed; continue; }
			FString VN, VT, DV;
			if (!It->TryGetStringField(TEXT("var_name"), VN))
				if (!It->TryGetStringField(TEXT("variable_name"), VN))
					It->TryGetStringField(TEXT("name"), VN);
			if (!It->TryGetStringField(TEXT("var_type"), VT))
				if (!It->TryGetStringField(TEXT("variable_type"), VT))
					It->TryGetStringField(TEXT("type"), VT);
			It->TryGetStringField(TEXT("default_value"), DV);
			FString ItemOut, ItemErr;
			HandleAddLocalVariable(BP, FN, VN, VT, DV, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) ++Added; else { ++Failed; Errs += FString::Printf(TEXT("[%s] %s\n"), *VN, *ItemErr); }
		}
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), Failed == 0);
		R->SetNumberField(TEXT("added"), Added);
		R->SetNumberField(TEXT("failed"), Failed);
		if (!Errs.IsEmpty()) R->SetStringField(TEXT("errors"), Errs);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
		return;
	}

	FString VN, VT, DV;
	if (!Args->TryGetStringField(TEXT("var_name"), VN))
		if (!Args->TryGetStringField(TEXT("variable_name"), VN))
			Args->TryGetStringField(TEXT("name"), VN);
	if (!Args->TryGetStringField(TEXT("var_type"), VT))
		if (!Args->TryGetStringField(TEXT("variable_type"), VT))
			Args->TryGetStringField(TEXT("type"), VT);
	Args->TryGetStringField(TEXT("default_value"), DV);
	HandleAddLocalVariable(BP, FN, VN, VT, DV, OutJsonString, OutError);
}

void HandleSetVariableExposeOnSpawnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, VN, EOS;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	if (!Args->TryGetStringField(TEXT("variable_name"), VN))
		Args->TryGetStringField(TEXT("var_name"), VN);
	bool bExpose = true;
	if (!Args->TryGetBoolField(TEXT("expose_on_spawn"), bExpose))
	{
		if (Args->TryGetStringField(TEXT("expose_on_spawn"), EOS))
			bExpose = !EOS.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			       && !EOS.Equals(TEXT("0"), ESearchCase::IgnoreCase)
			       && !EOS.Equals(TEXT("no"), ESearchCase::IgnoreCase);
	}
	EOS = bExpose ? TEXT("true") : TEXT("false");
	HandleSetVariableFlags(BP, VN, TEXT(""), EOS, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), OutJsonString, OutError);
}

void HandleSetVariableReplicationConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, VN, Condition;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	if (!Args->TryGetStringField(TEXT("variable_name"), VN))
		Args->TryGetStringField(TEXT("var_name"), VN);
	if (!Args->TryGetStringField(TEXT("replication_condition"), Condition))
		Args->TryGetStringField(TEXT("condition"), Condition);
	HandleSetVariableReplicationCondition(BP, VN, Condition, OutJsonString, OutError);
}

void HandleCategorizeVariablesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	const TSharedPtr<FJsonObject>* CategoriesObj = nullptr;
	Args->TryGetObjectField(TEXT("categories"), CategoriesObj);
	HandleCategorizeVariables(BP, CategoriesObj, OutJsonString, OutError);
}

void HandleDeleteEventDispatcherFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, Name;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	if (!Args->TryGetStringField(TEXT("dispatcher_name"), Name))
		Args->TryGetStringField(TEXT("name"), Name);
	HandleDeleteEventDispatcher(BP, Name, OutJsonString, OutError);
}

void HandleRemoveDispatcherParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, Name, PN;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	if (!Args->TryGetStringField(TEXT("dispatcher_name"), Name))
		Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("param_name"), PN);
	HandleRemoveDispatcherParam(BP, Name, PN, OutJsonString, OutError);
}

void HandleSetFunctionReplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, FN, Replication;
	bool bReliable = false, bWithValidation = false;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("function_name"), FN);
	if (!Args->TryGetStringField(TEXT("replication"), Replication))
		Args->TryGetStringField(TEXT("replication_type"), Replication);
	Args->TryGetBoolField(TEXT("reliable"), bReliable);
	Args->TryGetBoolField(TEXT("with_validation"), bWithValidation);
	HandleSetFunctionReplication(BP, FN, Replication, bReliable, bWithValidation, OutJsonString, OutError);
}

void HandleRemoveFunctionParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, FN, PN;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("function_name"), FN);
	Args->TryGetStringField(TEXT("param_name"), PN);
	HandleRemoveFunctionParam(BP, FN, PN, OutJsonString, OutError);
}

void HandleSetFunctionAccessFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, FN, AC;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("function_name"), FN);
	Args->TryGetStringField(TEXT("access"), AC);
	HandleSetFunctionAccess(BP, FN, AC, OutJsonString, OutError);
}

void HandleSetFunctionPureFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("functions"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString FN = BatchToolHelper::GetItemString(Item, TEXT("function_name"), TEXT("name"));
			if (FN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing function_name")); continue; }
			bool bPure = false;
			Item->TryGetBoolField(TEXT("pure"), bPure);
			FString ItemOut, ItemErr;
			HandleSetFunctionPure(BP, FN, bPure, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("function_name"), FN); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString FN;
	bool bPure = false;
	Args->TryGetStringField(TEXT("function_name"), FN);
	Args->TryGetBoolField(TEXT("pure"), bPure);
	HandleSetFunctionPure(BP, FN, bPure, OutJsonString, OutError);
}

void HandleListOverridableFunctionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	HandleListOverridableFunctions(BP, OutJsonString, OutError);
}

void HandleReparentBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, PC;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("new_parent_class"), PC);
	HandleReparentBlueprint(BP, PC, OutJsonString, OutError);
}

namespace
{
	static FString JsonValueToMetaString(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return FString();
		switch (V->Type)
		{
			case EJson::String:  return V->AsString();
			case EJson::Number:
			{
				const double D = V->AsNumber();
				if (FMath::IsNearlyEqual(D, FMath::TruncToDouble(D)))
					return FString::Printf(TEXT("%lld"), (int64)D);
				return FString::SanitizeFloat(D);
			}
			case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
			default:             return FString();
		}
	}

	static FName NormaliseMetaKey(const FString& InKey)
	{
		static const TMap<FString, FString> KeyMap = {
			{ TEXT("ui_min"),                TEXT("UIMin") },
			{ TEXT("ui_max"),                TEXT("UIMax") },
			{ TEXT("clamp_min"),             TEXT("ClampMin") },
			{ TEXT("clamp_max"),             TEXT("ClampMax") },
			{ TEXT("slider_exponent"),       TEXT("SliderExponent") },
			{ TEXT("multiline"),             TEXT("MultiLine") },
			{ TEXT("display_name"),          TEXT("DisplayName") },
			{ TEXT("edit_condition"),        TEXT("EditCondition") },
			{ TEXT("edit_condition_hides"),  TEXT("EditConditionHides") },
			{ TEXT("category"),              TEXT("Category") },
			{ TEXT("bind_widget"),           TEXT("BindWidget") },
			{ TEXT("bind_widget_optional"),  TEXT("BindWidgetOptional") },
			{ TEXT("tooltip"),               TEXT("tooltip") },
			{ TEXT("advanced_display"),      TEXT("AdvancedDisplay") },
			{ TEXT("expose_on_spawn"),       TEXT("ExposeOnSpawn") },
			{ TEXT("make_structure_default_value"), TEXT("MakeStructureDefaultValue") },
			{ TEXT("get_options"),           TEXT("GetOptions") },
		};
		const FString Lower = InKey.ToLower();
		if (const FString* Mapped = KeyMap.Find(Lower)) return FName(**Mapped);
		return FName(*InKey);
	}
}

void HandleSetVariableMetadataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BpPath, VarName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("variable_name"), VarName))
		Args->TryGetStringField(TEXT("var_name"), VarName);
	if (VarName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: variable_name"); return; }

	UBlueprint* BP = LoadBPFromPath(BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at: %s"), *BpPath); return; }

	bool bFound = false;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase)) { bFound = true; break; }
	}
	if (!bFound)
	{ OutError = FString::Printf(TEXT("Variable '%s' not found on '%s'"), *VarName, *BpPath); return; }

	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Entries;
	const TSharedPtr<FJsonObject>* MetaObj = nullptr;
	if (Args->TryGetObjectField(TEXT("metadata"), MetaObj) && MetaObj && MetaObj->IsValid())
	{
		for (const auto& Pair : (*MetaObj)->Values)
			Entries.Add(TPair<FString, TSharedPtr<FJsonValue>>(Pair.Key, Pair.Value));
	}
	static const TArray<FString> FlatKeys = {
		TEXT("ui_min"), TEXT("ui_max"), TEXT("clamp_min"), TEXT("clamp_max"), TEXT("slider_exponent"),
		TEXT("multiline"), TEXT("display_name"), TEXT("edit_condition"), TEXT("edit_condition_hides"),
		TEXT("category"), TEXT("bind_widget"), TEXT("bind_widget_optional"), TEXT("tooltip"),
		TEXT("advanced_display"), TEXT("expose_on_spawn"), TEXT("make_structure_default_value"), TEXT("get_options")
	};
	for (const FString& K : FlatKeys)
	{
		const TSharedPtr<FJsonValue> V = Args->TryGetField(K);
		if (V.IsValid()) Entries.Add(TPair<FString, TSharedPtr<FJsonValue>>(K, V));
	}

	if (Entries.Num() == 0)
	{
		OutError = TEXT("No metadata keys provided. Pass metadata={key:value,...} or flat shortcuts like ui_min/ui_max/clamp_min/category/multiline.");
		return;
	}

	const FName VarFName(*VarName);
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	TArray<TSharedPtr<FJsonValue>> RemovedJson;
	for (const auto& KV : Entries)
	{
		const FName MetaKey = NormaliseMetaKey(KV.Key);
		const TSharedPtr<FJsonValue>& V = KV.Value;
		const bool bRemove = !V.IsValid() || V->Type == EJson::Null
			|| (V->Type == EJson::String && V->AsString().IsEmpty());

		if (bRemove)
		{
			FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, VarFName, nullptr, MetaKey);
			RemovedJson.Add(MakeShared<FJsonValueString>(MetaKey.ToString()));
			continue;
		}

		FString Value = JsonValueToMetaString(V);
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarFName, nullptr, MetaKey, Value);
		TSharedPtr<FJsonObject> Pair = MakeShared<FJsonObject>();
		Pair->SetStringField(TEXT("key"),   MetaKey.ToString());
		Pair->SetStringField(TEXT("value"), Value);
		AppliedJson.Add(MakeShared<FJsonValueObject>(Pair));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("variable_name"), VarName);
	Res->SetArrayField(TEXT("applied"), AppliedJson);
	Res->SetArrayField(TEXT("removed"), RemovedJson);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

}
