// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/BlueprintGraphTools.h"
#include "Tools/BlueprintNodeIdentity.h"
#include "Tools/BpFuzzyResolver.h"
#include "Tools/BpHandleKnowledge.h"
#include "Tools/PinTypeResolver.h"
#include "Managers/SettingsManager.h"
#include "Utils/EditorRuntime.h"
#include "Managers/CapabilityProfile.h"
#include "Tools/BatchToolHelper.h"
#include "UECPCoreModule.h"
#include "Services/IUECPArchitectService.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_Base.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_Message.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Self.h"
#include "K2Node_MakeArray.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Select.h"
#include "K2Node_FormatText.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintVariableNodeSpawner.h"
#include "BlueprintEventNodeSpawner.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Tunnel.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EdGraphNode_Comment.h"
#include "GraphLayout/BpLayoutEngine.h"
#include "GraphLayout/BpLayoutConverter.h"
#include "GraphLayout/BpLayoutApplier.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_GetSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "Subsystems/Subsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_BaseMCDelegate.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_AssignDelegate.h"
#include "InputTriggers.h"
#include "InputAction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Perception/AIPerceptionTypes.h"
#include "Engine/HitResult.h"
#include "UIConfigManager.h"
#include "K2Node_Timeline.h"
#include "Engine/TimelineTemplate.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "K2Node_GetDataTableRow.h"
#include "K2Node_GetEnumeratorName.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchInteger.h"
#include "Kismet2/StructureEditorUtils.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/EngineTypes.h"

#define LOCTEXT_NAMESPACE "BlueprintGraphTools"

#if PLATFORM_WINDOWS
#include <excpt.h>
static bool TryDispatchAsObject(FJsonValue* V, TSharedPtr<FJsonObject>& OutObject)
{
	__try
	{
		OutObject = V->AsObject();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
#else
static bool TryDispatchAsObject(FJsonValue* V, TSharedPtr<FJsonObject>& OutObject)
{
	OutObject = V->AsObject();
	return true;
}
#endif

#if PLATFORM_WINDOWS
template <typename TNodeClass>
static TNodeClass* SEHGuardedNewK2Node(UEdGraph* Graph)
{
	__try
	{
		return NewObject<TNodeClass>(Graph);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}
#else
template <typename TNodeClass>
static TNodeClass* SEHGuardedNewK2Node(UEdGraph* Graph)
{
	return NewObject<TNodeClass>(Graph);
}
#endif

static TSharedPtr<FJsonObject> SafeAsObject(const TSharedPtr<FJsonValue>& V)
{
	const UPTRINT RefAddr = reinterpret_cast<UPTRINT>(&V);
	if (RefAddr == ~UPTRINT(0) || RefAddr < 0x10000 || (RefAddr & 0x7) != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SafeAsObject: rejecting dangling reference (&V=0x%llx). "
			     "Iterator over freed array — caller continues with nullptr."),
			static_cast<uint64>(RefAddr));
		return nullptr;
	}
	if (RefAddr >= UPTRINT(0x800000000000ULL))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SafeAsObject: rejecting non-user-space reference (&V=0x%llx)."),
			static_cast<uint64>(RefAddr));
		return nullptr;
	}

	if (!V.IsValid()) return nullptr;

	const UPTRINT Ptr = reinterpret_cast<UPTRINT>(V.Get());
	if (Ptr == ~UPTRINT(0) || Ptr < 0x10000 || (Ptr & 0x7) != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SafeAsObject: rejecting corrupt TSharedPtr (Object=0x%llx). "
			     "Upstream memory corruption — caller continues with nullptr."),
			static_cast<uint64>(Ptr));
		return nullptr;
	}

	const int32 TypeAsInt = static_cast<int32>(V->Type);
	if (TypeAsInt < static_cast<int32>(EJson::None) ||
	    TypeAsInt > static_cast<int32>(EJson::Object))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SafeAsObject: rejecting freed FJsonValue (Type=%d out of range). "
			     "Use-after-free upstream — caller continues with nullptr."),
			TypeAsInt);
		return nullptr;
	}
	if (V->Type != EJson::Object) return nullptr;

	TSharedPtr<FJsonObject> Out;
	if (!TryDispatchAsObject(V.Get(), Out))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SafeAsObject: AsObject() dispatch AV'd — freed FJsonValue with stray vtable. "
			     "Caller continues with nullptr."));
		return nullptr;
	}
	return Out;
}

static UBlueprint* LoadBlueprintFromPath(const FString& Path)
{
	if (Path.Equals(TEXT("@level_blueprint"), ESearchCase::IgnoreCase))
	{
		if (!GEditor) return nullptr;
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World || !World->PersistentLevel) return nullptr;
		UBlueprint* LevelBP = World->PersistentLevel->GetLevelScriptBlueprint(false);
		BlueprintNodeIdentity::MigrateLegacyIds(LevelBP);
		return LevelBP;
	}
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
	// Import any legacy "GEID:" comment ids into the metadata sidecar so every tool below
	// sees one consistent identity source.
	BlueprintNodeIdentity::MigrateLegacyIds(BP);
	return BP;
}

static FString GetOrphanCommentText()
{
	const FString Lang = FUIConfigManager::Get().GetLanguage();
	if (Lang == TEXT("de")) return TEXT("Alte Logik - Kann gel\u00f6scht werden");
	if (Lang == TEXT("es")) return TEXT("L\u00f3gica antigua - Se puede eliminar");
	if (Lang == TEXT("fr")) return TEXT("Ancienne logique - Supprimable");
	if (Lang == TEXT("pt")) return TEXT("L\u00f3gica antiga - Pode ser exclu\u00edda");
	if (Lang == TEXT("zh")) return TEXT("\u65e7\u903b\u8f91 - \u53ef\u4ee5\u5220\u9664");
	if (Lang == TEXT("ja")) return TEXT("\u53e4\u3044\u30ed\u30b8\u30c3\u30af - \u524a\u9664\u53ef\u80fd");
	if (Lang == TEXT("ko")) return TEXT("\uc774\uc804 \ub85c\uc9c1 - \uc0ad\uc81c \uac00\ub2a5");
	if (Lang == TEXT("ru")) return TEXT("\u0421\u0442\u0430\u0440\u0430\u044f \u043b\u043e\u0433\u0438\u043a\u0430 - \u041c\u043e\u0436\u043d\u043e \u0443\u0434\u0430\u043b\u0438\u0442\u044c");
	if (Lang == TEXT("tr")) return TEXT("Eski Mant\u0131k - Silinebilir");
	if (Lang == TEXT("ar")) return TEXT("\u0645\u0646\u0637\u0642 \u0642\u062f\u064a\u0645 - \u064a\u0645\u0643\u0646 \u062d\u0630\u0641\u0647");
	return TEXT("Old Logic - Safe to Delete");
}

static bool IsOrphanCommentBox(const UEdGraphNode_Comment* Comment)
{
	return Comment->FontSize == 18
		&& Comment->bCommentBubbleVisible
		&& Comment->bColorCommentBubble
		&& FMath::IsNearlyEqual(Comment->CommentColor.R, 1.0f, 0.1f)
		&& FMath::IsNearlyEqual(Comment->CommentColor.G, 0.55f, 0.1f)
		&& FMath::IsNearlyEqual(Comment->CommentColor.B, 0.0f, 0.1f);
}

namespace GraphEditHelpers
{
	static bool IsTransientBpClass(const UClass* C)
	{
		if (!C) return false;
		const FString N = C->GetName();
		return N.StartsWith(TEXT("SKEL_"), ESearchCase::CaseSensitive)
		    || N.StartsWith(TEXT("REINST_"), ESearchCase::CaseSensitive)
		    || N.StartsWith(TEXT("TRASHCLASS_"), ESearchCase::CaseSensitive);
	}

	static UClass* FindClassByName(const FString& ClassName)
	{
		TArray<FString> Names = {
			ClassName, ClassName + TEXT("_C"),
			TEXT("U") + ClassName, TEXT("A") + ClassName,
			TEXT("U") + ClassName + TEXT("_C"), TEXT("A") + ClassName + TEXT("_C"),
		};
		for (const FString& Name : Names)
		{
			UClass* Found = FindFirstObject<UClass>(*Name);
			if (Found && !IsTransientBpClass(Found)) return Found;
		}
		UClass* TransientFallback = nullptr;
		const FString ClassNameC = ClassName + TEXT("_C");
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString N = It->GetName();
			if (N == ClassName || N == ClassNameC)
			{
				if (!IsTransientBpClass(*It)) return *It;
				TransientFallback = *It;
			}
		}
		return TransientFallback;
	}

	static FString BuildHandle(UBlueprintNodeSpawner* Spawner)
	{
		if (!Spawner || !Spawner->NodeClass)
		{
			return FString();
		}

		if (const UBlueprintFunctionNodeSpawner* FuncSpawner = Cast<UBlueprintFunctionNodeSpawner>(Spawner))
		{
			if (const UFunction* Func = FuncSpawner->GetFunction())
			{
				FString ClassName = Func->GetOwnerClass() ? Func->GetOwnerClass()->GetName() : TEXT("Unknown");
				ClassName.RemoveFromStart(TEXT("SKEL_"),   ESearchCase::CaseSensitive);
				ClassName.RemoveFromStart(TEXT("REINST_"), ESearchCase::CaseSensitive);
				int32 LastUnderscore = INDEX_NONE;
				if (ClassName.FindLastChar(TEXT('_'), LastUnderscore) && LastUnderscore > 0)
				{
					const FString Tail = ClassName.Mid(LastUnderscore + 1);
					bool bAllDigits = !Tail.IsEmpty();
					for (TCHAR C : Tail) if (C < TEXT('0') || C > TEXT('9')) { bAllDigits = false; break; }
					if (bAllDigits) ClassName.LeftInline(LastUnderscore, EAllowShrinking::No);
				}
				return FString::Printf(TEXT("fn.%s.%s"), *ClassName, *Func->GetName());
			}
		}

		if (const UBlueprintEventNodeSpawner* EventSpawner = Cast<UBlueprintEventNodeSpawner>(Spawner))
		{
			if (const UFunction* EventFunc = EventSpawner->GetEventFunction())
			{
				return FString::Printf(TEXT("ev.%s"), *EventFunc->GetName());
			}
			if (EventSpawner->IsForCustomEvent())
			{
				return TEXT("ev.CustomEvent");
			}
		}

		if (const UBlueprintVariableNodeSpawner* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Spawner))
		{
			const FProperty* Prop = VarSpawner->GetVarProperty();
			FString VarName = Prop ? Prop->GetName() : TEXT("Unknown");
			bool bIsGetter = Spawner->NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass());
			return FString::Printf(TEXT("var.%s.%s"), bIsGetter ? TEXT("get") : TEXT("set"), *VarName);
		}

		if (Spawner->NodeClass->IsChildOf(UK2Node_MacroInstance::StaticClass()))
		{
			FString Title = Spawner->DefaultMenuSignature.MenuName.ToString();
			if (Title.IsEmpty())
			{
				Title = TEXT("Macro");
			}
			return FString::Printf(TEXT("mc.%s"), *Title);
		}

		FString Title = Spawner->DefaultMenuSignature.MenuName.ToString();
		if (Title.IsEmpty())
		{
			const FString ClassName = Spawner->NodeClass->GetName();
			if (ClassName.StartsWith(TEXT("K2Node_"))) return FString();
			Title = ClassName;
		}
		return FString::Printf(TEXT("k2.%s"), *Title);
	}

	static FString NormalizeForSearch(const FString& Input)
	{
		FString Result;
		Result.Reserve(Input.Len() + 8);
		TCHAR PrevInputCh = 0;
		for (TCHAR Ch : Input)
		{
			if (FChar::IsAlnum(Ch))
			{
				if (FChar::IsUpper(Ch) && PrevInputCh != 0 && FChar::IsLower(PrevInputCh))
				{
					Result.AppendChar(TEXT(' '));
				}
				Result.AppendChar(FChar::ToLower(Ch));
			}
			else if (Ch == TEXT('_') || Ch == TEXT(' '))
			{
				Result.AppendChar(TEXT(' '));
			}
			PrevInputCh = Ch;
		}
		return Result;
	}

	static float ScoreMatch(const FString& Query, const FString& NodeName)
	{
		FString QueryLower = Query.ToLower();
		FString NameLower = NodeName.ToLower();

		if (QueryLower == NameLower) return 1.0f;

		if (NameLower.Contains(QueryLower)) return 0.85f;

		int32 LastDotIdx = INDEX_NONE;
		NameLower.FindLastChar('.', LastDotIdx);
		if (LastDotIdx != INDEX_NONE)
		{
			FString FuncPart = NameLower.Mid(LastDotIdx + 1);
			if (FuncPart.Contains(QueryLower)) return 0.88f;
			FString QueryAsUnderscore = QueryLower.Replace(TEXT(" "), TEXT("_"));
			if (FuncPart.Contains(QueryAsUnderscore)) return 0.87f;
		}

		FString QueryNorm = NormalizeForSearch(Query);
		FString NameNorm = NormalizeForSearch(NodeName);

		if (NameNorm.Contains(QueryNorm)) return 0.82f;

		if (LastDotIdx != INDEX_NONE)
		{
			FString FuncPartNorm = NormalizeForSearch(NodeName.Mid(LastDotIdx + 1));
			if (FuncPartNorm.Contains(QueryNorm)) return 0.83f;
		}

		TArray<FString> QueryWords;
		QueryNorm.ParseIntoArray(QueryWords, TEXT(" "), true);
		if (QueryWords.Num() == 0) return 0.0f;

		TArray<FString> NameWords;
		NameNorm.ParseIntoArray(NameWords, TEXT(" "), true);

		if (QueryWords.Num() > 0 && NameWords.Num() > 0 && QueryWords[0] == NameWords[0])
		{
			int32 ExtraMatched = 0, ExtraTotal = 0;
			for (int32 i = 1; i < QueryWords.Num(); i++)
			{
				if (QueryWords[i].Len() < 2) continue;
				ExtraTotal++;
				for (const FString& NWord : NameWords)
				{
					if (NWord == QueryWords[i]) { ExtraMatched++; break; }
				}
			}
			if (ExtraTotal == 0 || ExtraMatched == ExtraTotal)
			{
				return 0.80f;
			}
		}

		int32 MatchedWords = 0;
		for (const FString& QWord : QueryWords)
		{
			if (QWord.Len() < 2) continue;
			for (const FString& NWord : NameWords)
			{
				const bool bMeaningfulShortPrefix =
					QWord.StartsWith(NWord) &&
					NWord.Len() >= 3 &&
					NWord.Len() * 2 >= QWord.Len();
				if (NWord == QWord || NWord.StartsWith(QWord) || NWord.Contains(QWord) || bMeaningfulShortPrefix)
				{
					MatchedWords++;
					break;
				}
			}
		}

		int32 EffectiveQueryWords = 0;
		for (const FString& QWord : QueryWords) { if (QWord.Len() >= 2) EffectiveQueryWords++; }
		if (EffectiveQueryWords == 0) return 0.0f;

		float WordScore = (float)MatchedWords / (float)EffectiveQueryWords;
		if (WordScore >= 1.0f) return 0.75f;
		if (WordScore > 0.0f) return WordScore * 0.5f;

		return 0.0f;
	}

	static TArray<TSharedPtr<FJsonValue>> GetPinInfoArray(UEdGraphNode* Node)
	{
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		if (!Node) return PinsArray;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->bOrphanedPin)
			{
				continue;
			}

			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));

			FString TypeStr = Pin->PinType.PinCategory.ToString();
			if (Pin->PinType.PinSubCategoryObject.IsValid())
			{
				TypeStr += TEXT(":") + Pin->PinType.PinSubCategoryObject->GetName();
			}
			PinObj->SetStringField(TEXT("type"), TypeStr);
			if (Pin->PinType.ContainerType == EPinContainerType::Array)
				PinObj->SetBoolField(TEXT("is_array"), true);

			if (!Pin->DefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
			}

			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		return PinsArray;
	}

	static TArray<TSharedPtr<FJsonValue>> GetPinInfoArrayWithConnections(UEdGraphNode* Node, bool bIncludeUnconnectedPins)
	{
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		if (!Node) return PinsArray;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;

			bool bIsExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
			bool bHasConnection = Pin->LinkedTo.Num() > 0;
			bool bHasDefault = !Pin->DefaultValue.IsEmpty();

			if (!bIncludeUnconnectedPins && !bHasConnection && !bHasDefault && !bIsExec)
				continue;

			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));

			FString TypeStr = Pin->PinType.PinCategory.ToString();
			if (Pin->PinType.PinSubCategoryObject.IsValid())
				TypeStr += TEXT(":") + Pin->PinType.PinSubCategoryObject->GetName();
			PinObj->SetStringField(TEXT("type"), TypeStr);
			if (Pin->PinType.ContainerType == EPinContainerType::Array)
				PinObj->SetBoolField(TEXT("is_array"), true);

			if (bHasDefault)
				PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);

			if (bHasConnection)
			{
				TArray<TSharedPtr<FJsonValue>> ConnArray;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					UEdGraphNode* ConnNode = LinkedPin->GetOwningNodeUnchecked();
					if (!ConnNode || !IsValid(ConnNode)) continue;

					TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
					ConnObj->SetStringField(TEXT("node_id"), BlueprintNodeIdentity::GetNodeIdOrGuid(nullptr, ConnNode));
					ConnObj->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());
					ConnArray.Add(MakeShared<FJsonValueObject>(ConnObj));
				}
				PinObj->SetArrayField(TEXT("connected_to"), ConnArray);
			}

			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		return PinsArray;
	}

	static FString NormalizePinName(const FString& Name)
	{
		FString R;
		R.Reserve(Name.Len());
		for (TCHAR C : Name)
		{
			if (C != ' ' && C != '_') R.AppendChar(FChar::ToLower(C));
		}
		return R;
	}

	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
	{
		if (!Node) return nullptr;

		auto IsPinConnectable = [](UEdGraphPin* Pin) -> bool
		{
			return Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin;
		};

		if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			if (const UFunction* Func = CallNode->GetTargetFunction())
			{
				if (const UClass* OwnerClass = Func->GetOwnerClass())
				{
					const FString NodeHandle = FString::Printf(TEXT("fn.%s.%s"), *OwnerClass->GetName(), *Func->GetName());
					const FString Canonical = BpHandleKnowledge::LookupCanonicalPinName(NodeHandle, PinName);
					if (!Canonical.IsEmpty() && !Canonical.Equals(PinName, ESearchCase::IgnoreCase))
					{
						if (UEdGraphPin* Resolved = FindPinByName(Node, Canonical, Direction))
						{
							return Resolved;
						}
					}
				}
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
				Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
				Pin->PinFriendlyName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}

		if (PinName.Equals(TEXT("target"), ESearchCase::IgnoreCase))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bNotConnectable && !Pin->bOrphanedPin && Pin->Direction == Direction
				 && Pin->PinName == UEdGraphSchema_K2::PSC_Self)
				{
					return Pin;
				}
			}
		}

		if (PinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase) ||
			PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
			PinName.Equals(TEXT("in"), ESearchCase::IgnoreCase))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
					Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					return Pin;
				}
			}
		}

		if (PinName.Equals(TEXT("ret"), ESearchCase::IgnoreCase))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
					Pin->PinName.ToString().Equals(TEXT("ReturnValue"), ESearchCase::IgnoreCase))
					return Pin;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
					Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					return Pin;
			}
		}

		if (Direction == EGPD_Output && Cast<UK2Node_VariableSet>(Node) &&
			!PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase) &&
			!PinName.Equals(TEXT("out"), ESearchCase::IgnoreCase) &&
			!PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bNotConnectable && !Pin->bOrphanedPin && Pin->Direction == EGPD_Output &&
					Pin->PinName.ToString().Equals(TEXT("Output_Get"), ESearchCase::IgnoreCase))
				{
					return Pin;
				}
			}
		}

		if (Direction == EGPD_Output && Cast<UK2Node_Self>(Node))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsPinConnectable(Pin) && Pin->Direction == EGPD_Output &&
					Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					return Pin;
			}
		}

		if (PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase) ||
			PinName.Equals(TEXT("out"), ESearchCase::IgnoreCase))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
					Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					return Pin;
				}
			}
		}

		{
			static const TArray<FString> TypePrefixes = {
				TEXT("Int"), TEXT("Integer"), TEXT("Float"), TEXT("Double"),
				TEXT("Bool"), TEXT("Boolean"),
				TEXT("Vector"), TEXT("Vector2D"), TEXT("Vector4"),
				TEXT("Rotator"), TEXT("Transform"), TEXT("String"),
				TEXT("Name"), TEXT("Text"), TEXT("Byte"),
			};
			auto TryStripped = [&](const FString& Candidate) -> UEdGraphPin*
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
						Pin->PinName.ToString().Equals(Candidate, ESearchCase::IgnoreCase))
					{
						return Pin;
					}
				}
				return nullptr;
			};
			for (const FString& Prefix : TypePrefixes)
			{
				if (PinName.Len() > Prefix.Len() &&
					PinName.StartsWith(Prefix, ESearchCase::IgnoreCase))
				{
					if (UEdGraphPin* Hit = TryStripped(PinName.RightChop(Prefix.Len()))) return Hit;
				}
				const FString PrefixWithSpace = Prefix + TEXT(" ");
				if (PinName.StartsWith(PrefixWithSpace, ESearchCase::IgnoreCase) &&
					PinName.Len() > PrefixWithSpace.Len())
				{
					if (UEdGraphPin* Hit = TryStripped(PinName.RightChop(PrefixWithSpace.Len()).TrimStartAndEnd())) return Hit;
				}
			}
		}

		FString NormQuery = NormalizePinName(PinName);
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinConnectable(Pin) && Pin->Direction == Direction)
			{
				if (NormalizePinName(Pin->PinName.ToString()) == NormQuery ||
					NormalizePinName(Pin->PinFriendlyName.ToString()) == NormQuery)
				{
					return Pin;
				}
			}
		}

		if (NormQuery.Len() >= 4)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsPinConnectable(Pin) && Pin->Direction == Direction)
				{
					FString NormPin = NormalizePinName(Pin->PinName.ToString());
					if (NormPin.Len() >= 4 && (NormPin.Contains(NormQuery) || NormQuery.Contains(NormPin)))
					{
						return Pin;
					}
				}
			}
		}

		if (PinName.IsNumeric())
		{
			FString OptionName = FString::Printf(TEXT("Option %s"), *PinName);
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
					Pin->PinName.ToString().Equals(OptionName, ESearchCase::IgnoreCase))
				{
					return Pin;
				}
			}
		}

		{
			auto StripGuidSuffix = [](const FString& Name) -> FString
			{
				for (int32 i = Name.Len() - 1; i >= 2; --i)
				{
					if (Name[i] == '_')
					{
						int32 PrevUnderscore = Name.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromEnd, i - 1);
						if (PrevUnderscore != INDEX_NONE && PrevUnderscore < i - 1)
						{
							FString DigitPart = Name.Mid(PrevUnderscore + 1, i - PrevUnderscore - 1);
							if (DigitPart.IsNumeric() && PrevUnderscore > 0)
							{
								return Name.Left(PrevUnderscore);
							}
						}
					}
				}
				return Name;
			};

			FString QueryBase = StripGuidSuffix(PinName);
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!IsPinConnectable(Pin) || Pin->Direction != Direction) continue;
				FString PinBase = StripGuidSuffix(Pin->PinName.ToString());
				if (PinBase.Equals(QueryBase, ESearchCase::IgnoreCase) && !PinBase.IsEmpty())
				{
					return Pin;
				}
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bNotConnectable && !Pin->bOrphanedPin && Pin->Direction == Direction &&
				Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}

		{
			const float PinMinConfidence = FMath::Max(0.85f, BpFuzzyResolver::GetMinConfidence());
			UEdGraphPin* BestPin = nullptr;
			float BestScore = 0.0f;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!IsPinConnectable(Pin) || Pin->Direction != Direction) continue;
				const float Score = BpFuzzyResolver::ScoreMatch(PinName, Pin->PinName.ToString());
				if (Score > BestScore)
				{
					BestScore = Score;
					BestPin = Pin;
				}
			}
			if (BestPin && BestScore >= PinMinConfidence) return BestPin;
		}

		return nullptr;
	}

	static UEdGraphPin* FindPinByTypeHint(UEdGraphNode* Node, EEdGraphPinDirection Direction,
	                                      const FEdGraphPinType& HintType, UEdGraphPin* HintPin)
	{
		if (!Node || !HintPin) return nullptr;
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (!Schema) return nullptr;

		auto IsPinConnectable = [](UEdGraphPin* P) -> bool
		{
			return P && !P->bHidden && !P->bNotConnectable && !P->bOrphanedPin;
		};

		UEdGraphPin* FoundPin = nullptr;
		int32 CompatibleCount = 0;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsPinConnectable(Pin) || Pin->Direction != Direction) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
			    HintType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
			if (Direction == EGPD_Input && Pin->LinkedTo.Num() > 0) continue;

			UEdGraphPin* A = (Direction == EGPD_Output) ? Pin : HintPin;
			UEdGraphPin* B = (Direction == EGPD_Output) ? HintPin : Pin;
			const FPinConnectionResponse Response = Schema->CanCreateConnection(A, B);
			if (Response.Response == CONNECT_RESPONSE_MAKE ||
			    Response.Response == CONNECT_RESPONSE_MAKE_WITH_PROMOTION ||
			    Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE)
			{
				FoundPin = Pin;
				CompatibleCount++;
				if (CompatibleCount > 1) return nullptr;
			}
		}
		return CompatibleCount == 1 ? FoundPin : nullptr;
	}

	static FString GetAvailablePinNames(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		TArray<FString> Names;
		if (!Node) return TEXT("(none)");
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bOrphanedPin && Pin->Direction == Direction)
			{
				FString Name = Pin->PinName.ToString();
				if (Pin->bHidden) Name += TEXT("(hidden)");
				Names.Add(Name);
			}
		}
		return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : TEXT("(none)");
	}

	static UEdGraphNode* FindNodeInGraph(UEdGraph* Graph, const FString& NodeId)
	{
		if (!Graph) return nullptr;

		if (NodeId.Equals(TEXT("entry"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("function_entry"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("entryNode"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("functionEntry"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("eventEntry"), ESearchCase::IgnoreCase))
		{
			TArray<UK2Node_FunctionEntry*> EntryNodes;
			Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
			if (EntryNodes.Num() > 0) return EntryNodes[0];
			TArray<UK2Node_Tunnel*> Tunnels;
			Graph->GetNodesOfClass<UK2Node_Tunnel>(Tunnels);
			for (UK2Node_Tunnel* T : Tunnels)
				if (T && T->bCanHaveOutputs && !T->bCanHaveInputs) return T;
		}
		if (NodeId.Equals(TEXT("return"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("function_result"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("returnNode"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("functionResult"), ESearchCase::IgnoreCase)
		    || NodeId.Equals(TEXT("result"), ESearchCase::IgnoreCase))
		{
			TArray<UK2Node_FunctionResult*> ResultNodes;
			Graph->GetNodesOfClass<UK2Node_FunctionResult>(ResultNodes);
			if (ResultNodes.Num() > 0) return ResultNodes[0];
			TArray<UK2Node_Tunnel*> Tunnels;
			Graph->GetNodesOfClass<UK2Node_Tunnel>(Tunnels);
			for (UK2Node_Tunnel* T : Tunnels)
				if (T && T->bCanHaveInputs && !T->bCanHaveOutputs) return T;
		}

		if (UEdGraphNode* ByLogicalId = BlueprintNodeIdentity::FindNodeInGraphByLogicalId(Graph, NodeId, /*bCaseSensitive*/ true))
		{
			return ByLogicalId;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString() == NodeId)
			{
				return Node;
			}
		}
		{
			// Any FGuid text form (hyphenated, braced, ...) is accepted as well.
			FGuid ParsedGuid;
			if (FGuid::Parse(NodeId, ParsedGuid) && ParsedGuid.IsValid())
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node && Node->NodeGuid == ParsedGuid) return Node;
				}
			}
		}

		if (UEdGraphNode* ByLogicalIdCI = BlueprintNodeIdentity::FindNodeInGraphByLogicalId(Graph, NodeId, /*bCaseSensitive*/ false))
		{
			return ByLogicalIdCI;
		}

		{
			TArray<UEdGraphNode*> TitleMatches;
			for (UEdGraphNode* CandNode : Graph->Nodes)
			{
				if (!CandNode) continue;
				FString CandTitle = CandNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
				if (CandTitle.Equals(NodeId, ESearchCase::IgnoreCase)) { TitleMatches.Add(CandNode); continue; }
				FString CandEditTitle = CandNode->GetNodeTitle(ENodeTitleType::EditableTitle).ToString();
				if (!CandEditTitle.IsEmpty() && CandEditTitle.Equals(NodeId, ESearchCase::IgnoreCase)) TitleMatches.Add(CandNode);
			}
			if (TitleMatches.Num() == 1) return TitleMatches[0];
			if (TitleMatches.Num() > 1)
			{
				for (UEdGraphNode* N : TitleMatches)
					if (BlueprintNodeIdentity::HasLogicalId(nullptr, N)) return N;
				return TitleMatches[0];
			}
		}

		if (NodeId.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase))
		{
			FString EventName = NodeId.Mid(3);
			static const TMap<FString, FString> EventAliases = {
				{ TEXT("eventtick"),      TEXT("ReceiveTick") },
				{ TEXT("tick"),           TEXT("ReceiveTick") },
				{ TEXT("eventbeginplay"), TEXT("ReceiveBeginPlay") },
				{ TEXT("beginplay"),      TEXT("ReceiveBeginPlay") },
				{ TEXT("eventendplay"),   TEXT("ReceiveEndPlay") },
				{ TEXT("endplay"),        TEXT("ReceiveEndPlay") },
				{ TEXT("eventconstruct"), TEXT("Construct") },
				{ TEXT("event construct"), TEXT("Construct") },
				{ TEXT("event_construct"), TEXT("Construct") },
				{ TEXT("eventondestruct"), TEXT("Destruct") },
				{ TEXT("event ondestruct"), TEXT("Destruct") },
				{ TEXT("event destruct"), TEXT("Destruct") },
				{ TEXT("ondestruct"),     TEXT("Destruct") },
				{ TEXT("eventondrawpaint"), TEXT("OnPaint") },
				{ TEXT("event onpaint"),  TEXT("OnPaint") },
				{ TEXT("draw"),           TEXT("OnPaint") },
				{ TEXT("paint"),          TEXT("OnPaint") },
				{ TEXT("eventactorbeginoverlap"), TEXT("ReceiveActorBeginOverlap") },
				{ TEXT("actorbeginoverlap"),     TEXT("ReceiveActorBeginOverlap") },
				{ TEXT("eventactorendoverlap"),  TEXT("ReceiveActorEndOverlap") },
				{ TEXT("actorendoverlap"),       TEXT("ReceiveActorEndOverlap") },
				{ TEXT("eventhit"),       TEXT("ReceiveHit") },
				{ TEXT("hit"),            TEXT("ReceiveHit") },
				{ TEXT("eventanydamage"), TEXT("ReceiveAnyDamage") },
				{ TEXT("anydamage"),      TEXT("ReceiveAnyDamage") },
			};
			if (const FString* Resolved = EventAliases.Find(EventName.ToLower()))
				EventName = *Resolved;
			for (UEdGraphNode* CandNode : Graph->Nodes)
			{
				if (!CandNode) continue;
				if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(CandNode))
				{
					FString MemberName = EventNode->EventReference.GetMemberName().ToString();
					if (MemberName.Equals(EventName, ESearchCase::IgnoreCase))
						return EventNode;
				}
				if (UK2Node_CustomEvent* CustomEvt = Cast<UK2Node_CustomEvent>(CandNode))
				{
					FString CustName = CustomEvt->CustomFunctionName.ToString();
					if (CustName.Equals(EventName, ESearchCase::IgnoreCase))
						return CustomEvt;
				}
			}
		}

		if (NodeId.StartsWith(TEXT("k2.Timeline."), ESearchCase::IgnoreCase))
		{
			FString TLName = NodeId.Mid(12);
			for (UEdGraphNode* CandNode : Graph->Nodes)
			{
				if (UK2Node_Timeline* TL = Cast<UK2Node_Timeline>(CandNode))
				{
					if (TL->TimelineName.ToString().Equals(TLName, ESearchCase::IgnoreCase))
						return TL;
				}
			}
		}

		if (NodeId.Equals(TEXT("k2.Self"), ESearchCase::IgnoreCase) ||
		    NodeId.Equals(TEXT("k2.Self.self"), ESearchCase::IgnoreCase))
		{
			for (UEdGraphNode* CandNode : Graph->Nodes)
			{
				if (Cast<UK2Node_Self>(CandNode))
					return CandNode;
			}
		}

		const FString IdLower = NodeId.ToLower();
		const bool bLooksLikeHandle =
			IdLower.StartsWith(TEXT("fn.")) || IdLower.StartsWith(TEXT("var.")) ||
			IdLower.StartsWith(TEXT("ev.")) || IdLower.StartsWith(TEXT("k2."))   ||
			IdLower.StartsWith(TEXT("ia.")) || IdLower.StartsWith(TEXT("prop."));
		if (!bLooksLikeHandle && NodeId.Len() >= 3)
		{
			TArray<FString> Candidates;
			TMap<FString, UEdGraphNode*> CandidateMap;
			for (UEdGraphNode* CandNode : Graph->Nodes)
			{
				if (!CandNode) continue;
				const FString GeidId = BlueprintNodeIdentity::GetLogicalId(nullptr, CandNode);
				if (!GeidId.IsEmpty())
				{
					Candidates.Add(GeidId);
					CandidateMap.Add(GeidId, CandNode);
				}
			}
			if (Candidates.Num() > 0)
			{
				const TArray<BpFuzzyResolver::FResolvedCandidate> Top =
					BpFuzzyResolver::RankCandidates(NodeId, Candidates, 3);
				if (Top.Num() > 0 && Top[0].Confidence >= 0.85f)
				{
					if (Top.Num() < 2 || (Top[0].Confidence - Top[1].Confidence) > 0.05f)
					{
						if (UEdGraphNode** Found = CandidateMap.Find(Top[0].CandidateKey))
							return *Found;
					}
				}
			}
		}

		return nullptr;
	}

	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint) return nullptr;

		if (GraphName.IsEmpty() ||
			GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("BeginPlay"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("ReceiveBeginPlay"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("Tick"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("ReceiveTick"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("Event BeginPlay"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("Event Tick"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("ActorBeginPlay"), ESearchCase::IgnoreCase) ||
			GraphName.Equals(TEXT("ActorTick"), ESearchCase::IgnoreCase))
		{
			if (Blueprint->UbergraphPages.Num() > 0)
			{
				return Blueprint->UbergraphPages[0];
			}
		}

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				return Graph;
			}
		}

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			TArray<UK2Node_FunctionEntry*> EntryNodes;
			Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
			for (UK2Node_FunctionEntry* Entry : EntryNodes)
			{
				if (!Entry) continue;
				FName RefName = Entry->FunctionReference.GetMemberName();
				if (!RefName.IsNone() && RefName.ToString().Equals(GraphName, ESearchCase::IgnoreCase))
				{
					return Graph;
				}
				if (!Entry->CustomGeneratedFunctionName.IsNone() &&
					Entry->CustomGeneratedFunctionName.ToString().Equals(GraphName, ESearchCase::IgnoreCase))
				{
					return Graph;
				}
			}
		}

		for (UEdGraph* Graph : Blueprint->EventGraphs)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
				return Graph;
		}

		for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
		{
			for (UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				if (!Graph) continue;
				if (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
					return Graph;
				TArray<UK2Node_FunctionEntry*> EntryNodes;
				Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
				for (UK2Node_FunctionEntry* Entry : EntryNodes)
				{
					if (!Entry) continue;
					FName RefName = Entry->FunctionReference.GetMemberName();
					if (!RefName.IsNone() && RefName.ToString().Equals(GraphName, ESearchCase::IgnoreCase))
						return Graph;
				}
			}
		}

		return nullptr;
	}

	static FEdGraphPinType StringToPinType(const FString& TypeStr)
	{
		FEdGraphPinType PinType;
		if (!UECPPinTypes::ResolvePinTypeFromString(TypeStr, PinType))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		}
		return PinType;
	}

	static FString JsonToString(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Output;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Output;
	}

	static FString CamelCaseToSpaced(const FString& Input)
	{
		FString Result;
		for (int32 i = 0; i < Input.Len(); i++)
		{
			if (i > 0 && FChar::IsUpper(Input[i]) && FChar::IsLower(Input[i - 1]))
				Result.AppendChar(' ');
			Result.AppendChar(Input[i]);
		}
		return Result;
	}

	static void BuildSpawnerCache(UBlueprint* Blueprint, TMap<FString, UBlueprintNodeSpawner*>& OutCache)
	{
		if (!Blueprint) return;
		FBlueprintActionDatabase& ActionDB = FBlueprintActionDatabase::Get();
		ActionDB.RefreshAssetActions(Blueprint);
		const FBlueprintActionDatabase::FActionRegistry& AllActions = ActionDB.GetAllActions();
		for (const auto& ActionPair : AllActions)
		{
			for (UBlueprintNodeSpawner* Spawner : ActionPair.Value)
			{
				if (!Spawner || !IsValid(Spawner) || !Spawner->NodeClass) continue;
				FString Handle = BuildHandle(Spawner);
				if (!Handle.IsEmpty())
				{
					FString Key = Handle.ToLower();
					if (!OutCache.Contains(Key))
					{
						OutCache.Add(Key, Spawner);
					}
					int32 PipeIdx = INDEX_NONE;
					if (Key.FindLastChar(TEXT('|'), PipeIdx))
					{
						int32 DotIdx = INDEX_NONE;
						if (Key.FindChar(TEXT('.'), DotIdx) && DotIdx < PipeIdx)
						{
							FString LeafKey = Key.Left(DotIdx + 1) + Key.Mid(PipeIdx + 1);
							if (!OutCache.Contains(LeafKey))
							{
								OutCache.Add(LeafKey, Spawner);
							}
						}
					}
				}
			}
		}
	}

	static UBlueprintNodeSpawner* FindSpawnerInCache(const TMap<FString, UBlueprintNodeSpawner*>& Cache, const FString& Handle)
	{
		UBlueprintNodeSpawner* const* Found = Cache.Find(Handle.ToLower());
		return Found ? *Found : nullptr;
	}

	static UBlueprintNodeSpawner* FindSpawnerByHandle(UBlueprint* Blueprint, const FString& Handle)
	{
		if (!Blueprint || Handle.IsEmpty())
		{
			return nullptr;
		}

		FBlueprintActionDatabase& ActionDB = FBlueprintActionDatabase::Get();
		ActionDB.RefreshAssetActions(Blueprint);
		const FBlueprintActionDatabase::FActionRegistry& AllActions = ActionDB.GetAllActions();

		int32 TotalSpawners = 0;
		int32 EventSpawners = 0;
		UBlueprintNodeSpawner* FoundSpawner = nullptr;

		for (const auto& ActionPair : AllActions)
		{
			for (UBlueprintNodeSpawner* Spawner : ActionPair.Value)
			{
				if (!Spawner || !IsValid(Spawner) || !Spawner->NodeClass) continue;
				TotalSpawners++;

				if (Cast<UBlueprintEventNodeSpawner>(Spawner)) EventSpawners++;

				FString SpawnerHandle = BuildHandle(Spawner);
				if (SpawnerHandle.Equals(Handle, ESearchCase::IgnoreCase))
				{
					FoundSpawner = Spawner;
					break;
				}
			}
			if (FoundSpawner) break;
		}

		if (!FoundSpawner)
		{

			if (Handle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase))
			{
				for (const auto& ActionPair : AllActions)
				{
					for (UBlueprintNodeSpawner* Spawner : ActionPair.Value)
					{
						if (!Spawner || !IsValid(Spawner) || !Spawner->NodeClass) continue;
						if (Spawner->NodeClass->IsChildOf(UK2Node_CustomEvent::StaticClass()))
						{
							return Spawner;
						}
					}
				}
			}
		}

		return FoundSpawner;
	}

	static UEdGraphNode* CreateNodeFromHandle(
		UBlueprint* Blueprint, UEdGraph* Graph,
		const FString& HandleIn, int32 PosX, int32 PosY,
		const FString& CustomName,
		const TArray<TSharedPtr<FJsonValue>>* Inputs,
		FString& OutError,
		const TMap<FString, UBlueprintNodeSpawner*>* SpawnerCache = nullptr,
		UClass* SelfContextClass = nullptr,
		TArray<FString>* OutWarnings = nullptr)
	{
		if (!Blueprint || !Graph || HandleIn.IsEmpty())
		{
			OutError = TEXT("Invalid Blueprint, Graph, or empty handle");
			return nullptr;
		}

		UEdGraphNode* NewNode = nullptr;

		FString Handle = HandleIn;
		{
			static const TMap<FString, FString> HandleRedirects = {
				{TEXT("fn.Character.GetCharacterMovement"), TEXT("var.get.CharacterMovement")},
				{TEXT("fn.Pawn.GetCharacterMovement"),      TEXT("var.get.CharacterMovement")},
				{TEXT("k2.BreakHitResult"),                  TEXT("fn.GameplayStatics.BreakHitResult")},
				{TEXT("k2.Break Hit Result"),                 TEXT("fn.GameplayStatics.BreakHitResult")},
				{TEXT("k2.Break HitResult"),                  TEXT("fn.GameplayStatics.BreakHitResult")},
				{TEXT("k2.break hit result"),                 TEXT("fn.GameplayStatics.BreakHitResult")},
				{TEXT("fn.KismetSystemLibrary.BreakHitResult"), TEXT("fn.GameplayStatics.BreakHitResult")},
				{TEXT("fn.Actor.GetActorLocation"),          TEXT("fn.Actor.K2_GetActorLocation")},
				{TEXT("fn.Actor.SetActorLocation"),          TEXT("fn.Actor.K2_SetActorLocation")},
				{TEXT("fn.Actor.GetActorRotation"),          TEXT("fn.Actor.K2_GetActorRotation")},
				{TEXT("fn.Actor.SetActorRotation"),          TEXT("fn.Actor.K2_SetActorRotation")},
				{TEXT("fn.Actor.DestroyActor"),              TEXT("fn.Actor.K2_DestroyActor")},
				{TEXT("fn.Actor.SetActorTransform"),         TEXT("fn.Actor.K2_SetActorTransform")},
				{TEXT("fn.Actor.GetActorScale3D"),           TEXT("fn.Actor.K2_GetActorScale3D")},
				{TEXT("fn.Actor.SetActorScale3D"),           TEXT("fn.Actor.K2_SetActorScale3D")},
				{TEXT("fn.Actor.AddActorWorldOffset"),       TEXT("fn.Actor.K2_AddActorWorldOffset")},
				{TEXT("fn.Actor.AddActorWorldRotation"),     TEXT("fn.Actor.K2_AddActorWorldRotation")},
				{TEXT("fn.Actor.SetActorRelativeLocation"),  TEXT("fn.Actor.K2_SetActorRelativeLocation")},
				{TEXT("fn.Actor.SetActorRelativeRotation"),  TEXT("fn.Actor.K2_SetActorRelativeRotation")},
				{TEXT("fn.KismetMathLibrary.MakeLiteralFloat"),    TEXT("fn.KismetSystemLibrary.MakeLiteralDouble")},
				{TEXT("fn.KismetMathLibrary.MakeLiteralDouble"),   TEXT("fn.KismetSystemLibrary.MakeLiteralDouble")},
				{TEXT("fn.KismetSystemLibrary.MakeLiteralFloat"),  TEXT("fn.KismetSystemLibrary.MakeLiteralDouble")},
				{TEXT("fn.Actor.TeleportTo"),                TEXT("fn.Actor.K2_TeleportTo")},
				{TEXT("fn.SceneComponent.SetWorldLocation"), TEXT("fn.SceneComponent.K2_SetWorldLocation")},
				{TEXT("fn.SceneComponent.SetWorldRotation"), TEXT("fn.SceneComponent.K2_SetWorldRotation")},
				{TEXT("fn.SceneComponent.GetComponentLocation"), TEXT("fn.SceneComponent.K2_GetComponentLocation")},
				{TEXT("fn.SceneComponent.GetComponentRotation"), TEXT("fn.SceneComponent.K2_GetComponentRotation")},
				{TEXT("fn.TextRenderComponent.SetText"),     TEXT("fn.TextRenderComponent.K2_SetText")},
				{TEXT("fn.Controller.GetPawn"),                  TEXT("fn.Controller.K2_GetPawn")},
				{TEXT("fn.AIController.GetPawn"),                TEXT("fn.Controller.K2_GetPawn")},
				{TEXT("fn.Controller.GetControlledPawn"),        TEXT("fn.Controller.K2_GetPawn")},
				{TEXT("fn.Actor.GetClass"),                      TEXT("fn.GameplayStatics.GetObjectClass")},
				{TEXT("fn.KismetMathLibrary.MakeLiteralBool"),   TEXT("fn.KismetSystemLibrary.MakeLiteralBool")},
				{TEXT("fn.KismetMathLibrary.MakeLiteralInt"),    TEXT("fn.KismetSystemLibrary.MakeLiteralInt")},
				{TEXT("fn.KismetMathLibrary.MakeLiteralString"), TEXT("fn.KismetSystemLibrary.MakeLiteralString")},
				{TEXT("fn.KismetMathLibrary.MakeLiteralName"),   TEXT("fn.KismetSystemLibrary.MakeLiteralName")},
				{TEXT("ev.BeginPlay"),              TEXT("ev.ReceiveBeginPlay")},
				{TEXT("ev.OnBeginPlay"),             TEXT("ev.ReceiveBeginPlay")},
				{TEXT("ev.Tick"),                   TEXT("ev.ReceiveTick")},
				{TEXT("ev.OnTick"),                 TEXT("ev.ReceiveTick")},
				{TEXT("ev.ActorBeginOverlap"),       TEXT("ev.ReceiveActorBeginOverlap")},
				{TEXT("ev.OnActorBeginOverlap"),     TEXT("ev.ReceiveActorBeginOverlap")},
				{TEXT("ev.ActorEndOverlap"),         TEXT("ev.ReceiveActorEndOverlap")},
				{TEXT("ev.OnActorEndOverlap"),       TEXT("ev.ReceiveActorEndOverlap")},
				{TEXT("ev.ActorHit"),                TEXT("ev.ReceiveHit")},
				{TEXT("ev.OnActorHit"),              TEXT("ev.ReceiveHit")},
				{TEXT("ev.AnyDamage"),               TEXT("ev.ReceiveAnyDamage")},
				{TEXT("ev.TakeAnyDamage"),           TEXT("ev.ReceiveAnyDamage")},
				{TEXT("ev.OnTakeAnyDamage"),         TEXT("ev.ReceiveAnyDamage")},
				{TEXT("ev.Destroyed"),               TEXT("ev.ReceiveDestroyed")},
				{TEXT("ev.OnDestroyed"),             TEXT("ev.ReceiveDestroyed")},
				{TEXT("ev.OnConstruct"),             TEXT("ev.Construct")},
				{TEXT("ev.OnInit"),                  TEXT("ev.Construct")},
				{TEXT("ev.OnInitialized"),           TEXT("ev.Construct")},
				{TEXT("ev.UserWidget.Construct"),    TEXT("ev.Construct")},
				{TEXT("ev.UserWidget.OnConstruct"),  TEXT("ev.Construct")},
				{TEXT("ev.UserWidget.OnInitialized"),TEXT("ev.Construct")},
				{TEXT("ev.UserWidget.PreConstruct"), TEXT("ev.PreConstruct")},
				{TEXT("ev.Widget.Construct"),        TEXT("ev.Construct")},
				{TEXT("ev.Widget.PreConstruct"),     TEXT("ev.PreConstruct")},
				{TEXT("ev.UserWidget.Tick"),         TEXT("ev.Tick")},
				{TEXT("ev.Widget.Tick"),             TEXT("ev.Tick")},
				{TEXT("ev.ReceiveTickAI"),           TEXT("ev.ReceiveTick")},
				{TEXT("ev.TickAI"),                  TEXT("ev.ReceiveTick")},
				{TEXT("ev.OnTickAI"),                TEXT("ev.ReceiveTick")},
				{TEXT("ev.ReceiveExecuteAI"),        TEXT("ev.ReceiveEnterState")},
				{TEXT("ev.ExecuteAI"),               TEXT("ev.ReceiveEnterState")},
				{TEXT("ev.ReceiveAbortAI"),          TEXT("ev.ReceiveExitState")},
				{TEXT("ev.AbortAI"),                 TEXT("ev.ReceiveExitState")},
				{TEXT("ev.EnterState"),              TEXT("ev.ReceiveEnterState")},
				{TEXT("ev.OnEnterState"),            TEXT("ev.ReceiveEnterState")},
				{TEXT("ev.ExitState"),               TEXT("ev.ReceiveExitState")},
				{TEXT("ev.OnExitState"),             TEXT("ev.ReceiveExitState")},
				{TEXT("ev.StateCompleted"),          TEXT("ev.ReceiveStateCompleted")},
				{TEXT("ev.OnStateCompleted"),        TEXT("ev.ReceiveStateCompleted")},
				{TEXT("ev.TreeStart"),               TEXT("ev.ReceiveTreeStart")},
				{TEXT("ev.OnTreeStart"),             TEXT("ev.ReceiveTreeStart")},
				{TEXT("ev.TreeStop"),                TEXT("ev.ReceiveTreeStop")},
				{TEXT("ev.OnTreeStop"),              TEXT("ev.ReceiveTreeStop")},
				{TEXT("fn.KismetMathLibrary.Conv_IntToFloat"),    TEXT("fn.KismetMathLibrary.Conv_IntToDouble")},
				{TEXT("fn.KismetMathLibrary.Conv_FloatToInt"),    TEXT("fn.KismetMathLibrary.FTrunc")},
				{TEXT("fn.KismetMathLibrary.FTruncToInt"),        TEXT("fn.KismetMathLibrary.FTrunc")},
				{TEXT("fn.KismetMathLibrary.FFloor"),             TEXT("fn.KismetMathLibrary.FTrunc")},
				{TEXT("fn.KismetMathLibrary.Normalize"),          TEXT("fn.KismetMathLibrary.Normal")},
				{TEXT("k2.Array Length"),    TEXT("fn.KismetArrayLibrary.Array_Length")},
				{TEXT("k2.Array Add"),       TEXT("fn.KismetArrayLibrary.Array_Add")},
				{TEXT("k2.Array Remove"),    TEXT("fn.KismetArrayLibrary.Array_Remove")},
				{TEXT("k2.Array RemoveItem"),TEXT("fn.KismetArrayLibrary.Array_RemoveItem")},
				{TEXT("k2.Array Contains"),  TEXT("fn.KismetArrayLibrary.Array_Contains")},
				{TEXT("k2.Array Find"),      TEXT("fn.KismetArrayLibrary.Array_Find")},
				{TEXT("k2.Array Set"),       TEXT("fn.KismetArrayLibrary.Array_Set")},
				{TEXT("k2.Array Clear"),     TEXT("fn.KismetArrayLibrary.Array_Clear")},
				{TEXT("k2.Array Shuffle"),   TEXT("fn.KismetArrayLibrary.Array_Shuffle")},
				{TEXT("k2.Array Reverse"),   TEXT("fn.KismetArrayLibrary.Array_Reverse")},
				{TEXT("k2.Array LastIndex"), TEXT("fn.KismetArrayLibrary.Array_LastIndex")},
				{TEXT("k2.Array Get"),       TEXT("fn.KismetArrayLibrary.Array_Get")},
				{TEXT("fn.Self"),            TEXT("k2.Self")},
			};
			if (const FString* Redirect = HandleRedirects.Find(Handle))
				Handle = *Redirect;

			if (Handle.StartsWith(TEXT("fn.SKEL_"), ESearchCase::IgnoreCase))
				Handle = TEXT("fn.") + Handle.RightChop(8);

			if (Handle.Equals(TEXT("k2.Break"), ESearchCase::IgnoreCase) ||
				Handle.Equals(TEXT("k2.Make"),  ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(TEXT("Handle '%s' is missing the struct name. Use 'k2.Break <StructName>' / 'k2.Make <StructName>' (e.g. 'k2.Break HitResult', 'k2.Make Vector', 'k2.Break S_MyStruct'). Discover available structs via discover_nodes."), *Handle);
				return nullptr;
			}

			{
				static const TSet<FString> Placeholders = {
					TEXT("struct"), TEXT("mystruct"), TEXT("yourstruct"),
					TEXT("structname"), TEXT("type"), TEXT("typename"),
					TEXT("s_struct"), TEXT("s_mystruct"), TEXT("f_struct")
				};
				FString MaybeName;
				if (Handle.StartsWith(TEXT("k2.Break "), ESearchCase::IgnoreCase))
					MaybeName = Handle.RightChop(9).TrimStartAndEnd();
				else if (Handle.StartsWith(TEXT("k2.Make "), ESearchCase::IgnoreCase))
					MaybeName = Handle.RightChop(8).TrimStartAndEnd();
				if (!MaybeName.IsEmpty() && Placeholders.Contains(MaybeName.ToLower()))
				{
					OutError = FString::Printf(
						TEXT("Handle '%s' uses '%s' as a placeholder for the actual struct name. "
						     "Replace it with your real struct's asset name (e.g. 'k2.Break S_InventorySlot', "
						     "'k2.Make S_PlayerState'). For native struct types: 'k2.Break HitResult', 'k2.Make Vector'. "
						     "List available user-defined structs via find_assets(asset_type='UserDefinedStruct')."),
						*Handle, *MaybeName);
					return nullptr;
				}
			}
		}

		if (Handle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase))
		{
			auto EnsureCustomEventPins = [&](UK2Node_CustomEvent* EventNode) -> bool
			{
				if (!EventNode || !Inputs || Inputs->Num() == 0) return false;
				bool bChanged = false;
				for (const auto& InputVal : *Inputs)
				{
					TSharedPtr<FJsonObject> InputObj = SafeAsObject(InputVal);
					if (!InputObj.IsValid()) continue;
					FString PinName, PinTypeStr;
					InputObj->TryGetStringField(TEXT("name"), PinName);
					InputObj->TryGetStringField(TEXT("type"), PinTypeStr);
					if (PinName.IsEmpty() || PinTypeStr.IsEmpty()) continue;

					bool bAlreadyPresent = false;
					for (const TSharedPtr<FUserPinInfo>& UP : EventNode->UserDefinedPins)
					{
						if (UP.IsValid() && UP->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
						{
							bAlreadyPresent = true;
							break;
						}
					}
					if (bAlreadyPresent) continue;

					FEdGraphPinType PinType = StringToPinType(PinTypeStr);
					EventNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output);
					bChanged = true;
				}
				return bChanged;
			};

			if (!CustomName.IsEmpty())
			{
				UK2Node_CustomEvent* SuffixedFallback = nullptr;
				const FString SuffixPrefix = CustomName + TEXT("_");
				for (UEdGraphNode* ExistingNode : Graph->Nodes)
				{
					UK2Node_CustomEvent* Existing = Cast<UK2Node_CustomEvent>(ExistingNode);
					if (!Existing) continue;
					const FString ExistingName = Existing->CustomFunctionName.ToString();
					if (ExistingName.Equals(CustomName, ESearchCase::IgnoreCase))
					{
						if (EnsureCustomEventPins(Existing))
							Existing->ReconstructNode();
						return Existing;
					}
					if (!SuffixedFallback && ExistingName.StartsWith(SuffixPrefix, ESearchCase::IgnoreCase))
					{
						const FString Tail = ExistingName.RightChop(SuffixPrefix.Len());
						bool bAllDigits = !Tail.IsEmpty();
						for (TCHAR Ch : Tail) { if (!FChar::IsDigit(Ch)) { bAllDigits = false; break; } }
						if (bAllDigits) SuffixedFallback = Existing;
					}
				}
				if (SuffixedFallback)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[build_graph] Reusing suffixed CustomEvent '%s' for requested name '%s' (orphan from a prior build)."),
						*SuffixedFallback->CustomFunctionName.ToString(), *CustomName);
					if (EnsureCustomEventPins(SuffixedFallback))
						SuffixedFallback->ReconstructNode();
					return SuffixedFallback;
				}
			}

			UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
			EventNode->bIsEditable = true;
			EventNode->NodePosX = PosX;
			EventNode->NodePosY = PosY;
			EventNode->SetFlags(RF_Transactional);
			if (!CustomName.IsEmpty())
			{
				EventNode->CustomFunctionName = FName(*CustomName);
			}
			Graph->AddNode(EventNode, false, false);
			EventNode->AllocateDefaultPins();

			if (EnsureCustomEventPins(EventNode))
			{
				EventNode->ReconstructNode();
			}

			if (!CustomName.IsEmpty())
			{
				uint32 RepFlags = 0;
				if (CustomName.StartsWith(TEXT("Server_"), ESearchCase::IgnoreCase))
				{
					RepFlags = FUNC_Net | FUNC_NetServer | FUNC_NetReliable;
				}
				else if (CustomName.StartsWith(TEXT("Multicast_"), ESearchCase::IgnoreCase))
				{
					RepFlags = FUNC_Net | FUNC_NetMulticast;
				}
				else if (CustomName.StartsWith(TEXT("Client_"), ESearchCase::IgnoreCase))
				{
					RepFlags = FUNC_Net | FUNC_NetClient | FUNC_NetReliable;
				}
				if (RepFlags != 0)
				{
					EventNode->FunctionFlags |= RepFlags;
				}
			}

			NewNode = EventNode;
		}
		else if (Handle.StartsWith(TEXT("ev.Dispatcher."), ESearchCase::IgnoreCase) ||
				 Handle.StartsWith(TEXT("ev.DispatcherBind."), ESearchCase::IgnoreCase) ||
				 Handle.StartsWith(TEXT("ev.DispatcherUnbind."), ESearchCase::IgnoreCase) ||
				 Handle.StartsWith(TEXT("ev.DispatcherClear."), ESearchCase::IgnoreCase) ||
				 Handle.StartsWith(TEXT("ev.DispatcherAssign."), ESearchCase::IgnoreCase))
		{
			FString DispatcherName;
			UK2Node_BaseMCDelegate* DelegateNode = nullptr;

			if (Handle.StartsWith(TEXT("ev.DispatcherBind."), ESearchCase::IgnoreCase))
			{
				DispatcherName = Handle.RightChop(18);
				DelegateNode = NewObject<UK2Node_AddDelegate>(Graph);
			}
			else if (Handle.StartsWith(TEXT("ev.DispatcherUnbind."), ESearchCase::IgnoreCase))
			{
				DispatcherName = Handle.RightChop(20);
				DelegateNode = NewObject<UK2Node_RemoveDelegate>(Graph);
			}
			else if (Handle.StartsWith(TEXT("ev.DispatcherClear."), ESearchCase::IgnoreCase))
			{
				DispatcherName = Handle.RightChop(19);
				DelegateNode = NewObject<UK2Node_ClearDelegate>(Graph);
			}
			else if (Handle.StartsWith(TEXT("ev.DispatcherAssign."), ESearchCase::IgnoreCase))
			{
				DispatcherName = Handle.RightChop(20);
				DelegateNode = NewObject<UK2Node_AssignDelegate>(Graph);
			}
			else
			{
				DispatcherName = Handle.RightChop(14);
				DelegateNode = NewObject<UK2Node_CallDelegate>(Graph);
			}

			if (DispatcherName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Missing dispatcher name in handle: '%s'"), *Handle);
				return nullptr;
			}

			int32 ClassDotPos = INDEX_NONE;
			if (DispatcherName.FindChar(TEXT('.'), ClassDotPos))
			{
				const FString ExtClassName = DispatcherName.Left(ClassDotPos);
				const FString ExtDelegateName = DispatcherName.Mid(ClassDotPos + 1);
				UClass* ExtClass = FindClassByName(ExtClassName);
				if (!ExtClass)
				{
					OutError = FString::Printf(
						TEXT("External delegate class '%s' not found for handle '%s'. Use the C++ parent class name (e.g. 'AnimInstance', not 'ABP_Manny')."),
						*ExtClassName, *Handle);
					return nullptr;
				}
				FMulticastDelegateProperty* ExtDelegateProp = FindFProperty<FMulticastDelegateProperty>(ExtClass, *ExtDelegateName);
				if (!ExtDelegateProp)
				{
					OutError = FString::Printf(
						TEXT("Delegate '%s' not found on class '%s'. Make sure the property is a BlueprintAssignable multicast delegate."),
						*ExtDelegateName, *ExtClassName);
					return nullptr;
				}
				DelegateNode->SetFromProperty(ExtDelegateProp, false, ExtClass);
			}
			else
			{
				UBlueprint* BlueprintForNode = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
				if (BlueprintForNode && BlueprintForNode->SkeletonGeneratedClass)
				{
					FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
						BlueprintForNode->SkeletonGeneratedClass, *DispatcherName);
					if (DelegateProp)
					{
						DelegateNode->SetFromProperty(DelegateProp, true, BlueprintForNode->SkeletonGeneratedClass);
					}
					else
					{
						DelegateNode->DelegateReference.SetDirect(
							FName(*DispatcherName), FGuid(), BlueprintForNode->SkeletonGeneratedClass, true);
					}
				}
				else
				{
					DelegateNode->DelegateReference.SetDirect(FName(*DispatcherName), FGuid(), nullptr, true);
				}
			}

			DelegateNode->NodePosX = PosX;
			DelegateNode->NodePosY = PosY;
			DelegateNode->SetFlags(RF_Transactional);
			Graph->AddNode(DelegateNode, false, false);
			DelegateNode->AllocateDefaultPins();

			DelegateNode->PostPlacedNewNode();

			NewNode = DelegateNode;
		}
		else if (Handle.StartsWith(TEXT("ev.WidgetClick."), ESearchCase::IgnoreCase))
		{
			FString WidgetVarName = Handle.RightChop(15);

			for (UEdGraphNode* ExistingNode : Graph->Nodes)
			{
				UK2Node_ComponentBoundEvent* Existing = Cast<UK2Node_ComponentBoundEvent>(ExistingNode);
				if (Existing &&
					Existing->ComponentPropertyName.ToString().Equals(WidgetVarName, ESearchCase::IgnoreCase) &&
					Existing->DelegatePropertyName == FName(TEXT("OnClicked")))
				{
					return Existing;
				}
			}

			UClass* ButtonClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.Button"));
			if (!ButtonClass)
			{
				OutError = TEXT("Could not find UButton class — ensure UMG module is loaded");
				return nullptr;
			}

			UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(Graph);
			EventNode->ComponentPropertyName = FName(*WidgetVarName);
			EventNode->DelegatePropertyName   = FName(TEXT("OnClicked"));
			EventNode->DelegateOwnerClass     = ButtonClass;
			if (FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(ButtonClass, TEXT("OnClicked")))
			{
				if (UFunction* SigFunc = DelegateProp->SignatureFunction)
				{
					EventNode->EventReference.SetFromField<UFunction>(SigFunc,  false);
				}
			}
			EventNode->CustomFunctionName = FName(*FString::Printf(TEXT("BndEvt__%s_%s_K2Node_ComponentBoundEvent_OnClicked"),
				*Blueprint->GetName(), *WidgetVarName));
			EventNode->NodePosX = PosX;
			EventNode->NodePosY = PosY;
			EventNode->SetFlags(RF_Transactional);
			Graph->AddNode(EventNode, false, false);
			EventNode->AllocateDefaultPins();
			NewNode = EventNode;
		}
		else if (Cast<UWidgetBlueprint>(Blueprint) != nullptr
			&& Handle.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase)
			&& Handle.RightChop(3).Contains(TEXT("."))
			&& [&Handle]() -> bool {
				const FString AfterEv = Handle.RightChop(3);
				const FString Lower = AfterEv.ToLower();
				return !Lower.StartsWith(TEXT("dispatcher"))
				    && !Lower.StartsWith(TEXT("widgetclick"))
				    && !Lower.StartsWith(TEXT("widgetevent"));
			}())
		{
			UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint);
			FString AfterEv = Handle.RightChop(3);
			bool bExplicit = AfterEv.StartsWith(TEXT("WidgetEvent."), ESearchCase::IgnoreCase);
			if (bExplicit) AfterEv = AfterEv.RightChop(12);
			int32 DotPos = INDEX_NONE;
			AfterEv.FindChar(TEXT('.'), DotPos);
			if (DotPos > 0)
			{
				const FString SegA = AfterEv.Left(DotPos);
				const FString SegB = AfterEv.Mid(DotPos + 1);

				TArray<UWidget*> AllW;
				if (WBP->WidgetTree) WBP->WidgetTree->GetAllWidgets(AllW);

				UWidget* MatchA = nullptr;
				UWidget* MatchB = nullptr;
				for (UWidget* W : AllW)
				{
					if (!W) continue;
					if (!MatchA && W->GetName().Equals(SegA, ESearchCase::IgnoreCase)) MatchA = W;
					if (!MatchB && W->GetName().Equals(SegB, ESearchCase::IgnoreCase)) MatchB = W;
				}

				FString WidgetVarName;
				FString DelegateName;
				UWidget* TargetWidget = nullptr;
				if (bExplicit && MatchA) { WidgetVarName = SegA; DelegateName = SegB; TargetWidget = MatchA; }
				else if (MatchA)         { WidgetVarName = SegA; DelegateName = SegB; TargetWidget = MatchA; }
				else if (MatchB)         { WidgetVarName = SegB; DelegateName = SegA; TargetWidget = MatchB; }

				if (TargetWidget)
				{
					TargetWidget->bIsVariable = true;

					UClass* OwnerClass = TargetWidget->GetClass();
					FMulticastDelegateProperty* Matched = nullptr;
					TArray<FString> AvailableDelegates;
					for (TFieldIterator<FMulticastDelegateProperty> It(OwnerClass); It; ++It)
					{
						AvailableDelegates.Add(It->GetName());
						if (!Matched && It->GetName().Equals(DelegateName, ESearchCase::IgnoreCase))
						{
							Matched = *It;
							DelegateName = It->GetName();
						}
					}
					if (!Matched)
					{
						OutError = FString::Printf(
							TEXT("Delegate '%s' not found on widget '%s' (class %s). Available delegates: [%s]."),
							*DelegateName, *WidgetVarName, *OwnerClass->GetName(), *FString::Join(AvailableDelegates, TEXT(", ")));
						return nullptr;
					}

					for (UEdGraphNode* ExistingNode : Graph->Nodes)
					{
						UK2Node_ComponentBoundEvent* Existing = Cast<UK2Node_ComponentBoundEvent>(ExistingNode);
						if (Existing
							&& Existing->ComponentPropertyName.ToString().Equals(WidgetVarName, ESearchCase::IgnoreCase)
							&& Existing->DelegatePropertyName == FName(*DelegateName))
						{
							return Existing;
						}
					}

					UK2Node_ComponentBoundEvent* EvtNode = NewObject<UK2Node_ComponentBoundEvent>(Graph);
					EvtNode->ComponentPropertyName = FName(*WidgetVarName);
					EvtNode->DelegatePropertyName  = FName(*DelegateName);
					EvtNode->DelegateOwnerClass    = OwnerClass;
					if (UFunction* SigFunc = Matched->SignatureFunction)
					{
						EvtNode->EventReference.SetFromField<UFunction>(SigFunc,  false);
					}
					EvtNode->CustomFunctionName = FName(*FString::Printf(TEXT("BndEvt__%s_%s_K2Node_ComponentBoundEvent_%s"),
						*Blueprint->GetName(), *WidgetVarName, *DelegateName));
					EvtNode->NodePosX = PosX;
					EvtNode->NodePosY = PosY;
					EvtNode->SetFlags(RF_Transactional);
					Graph->AddNode(EvtNode, false, false);
					EvtNode->AllocateDefaultPins();
					NewNode = EvtNode;
				}
			}
			if (!NewNode)
			{
				FString EventName = AfterEv;
				UEdGraphNode* FirstNamedMatch = nullptr;
				for (UEdGraphNode* ExistingNode : Graph->Nodes)
				{
					UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(ExistingNode);
					if (ExistingEvent && ExistingEvent->EventReference.GetMemberName().ToString().Equals(EventName, ESearchCase::IgnoreCase))
						return ExistingEvent;
				}
				OutError = FString::Printf(TEXT("Widget event handle '%s' could not be resolved — no widget component named '%s' or '%s' in the tree."),
					*Handle, DotPos > 0 ? *AfterEv.Left(DotPos) : TEXT(""), DotPos > 0 ? *AfterEv.Mid(DotPos + 1) : TEXT(""));
				return nullptr;
			}
		}
		else if (Blueprint
			&& Cast<UWidgetBlueprint>(Blueprint) == nullptr
			&& Handle.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase)
			&& Handle.RightChop(3).Contains(TEXT("."))
			&& [&Handle]() -> bool {
				const FString AfterEv = Handle.RightChop(3);
				const FString Lower = AfterEv.ToLower();
				return !Lower.StartsWith(TEXT("dispatcher"))
					&& !Lower.StartsWith(TEXT("widgetclick"))
					&& !Lower.StartsWith(TEXT("widgetevent"))
					&& !Lower.StartsWith(TEXT("custom"));
			}())
		{
			const FString AfterEv = Handle.RightChop(3);
			int32 DotPos = INDEX_NONE;
			AfterEv.FindChar(TEXT('.'), DotPos);
			if (DotPos > 0)
			{
				const FString CompName = AfterEv.Left(DotPos);
				const FString DispName = AfterEv.Mid(DotPos + 1);

				UClass* OwnerClass = nullptr;
				if (Blueprint->SkeletonGeneratedClass)
				{
					if (FObjectProperty* ObjProp = FindFProperty<FObjectProperty>(
						Blueprint->SkeletonGeneratedClass, *CompName))
					{
						OwnerClass = ObjProp->PropertyClass;
					}
				}
				if (!OwnerClass)
				{
					OutError = FString::Printf(
						TEXT("Component '%s' not found on this Blueprint — cannot bind 'ev.%s.%s'. "
						     "Add the component first via blueprint(action='add_component', component_name='%s', ...)."),
						*CompName, *CompName, *DispName, *CompName);
					return nullptr;
				}

				FMulticastDelegateProperty* Matched = nullptr;
				FString CanonName = DispName;
				TArray<FString> AvailableDelegates;
				for (TFieldIterator<FMulticastDelegateProperty> It(OwnerClass); It; ++It)
				{
					AvailableDelegates.Add(It->GetName());
					if (!Matched && It->GetName().Equals(DispName, ESearchCase::IgnoreCase))
					{
						Matched = *It;
						CanonName = It->GetName();
					}
				}
				if (!Matched)
				{
					OutError = FString::Printf(
						TEXT("Dispatcher '%s' not found on component '%s' (class %s). Available: [%s]. "
						     "Add via blueprint(action='add_event_dispatcher', blueprint_path='<%s path>', dispatcher_name='%s', params=[...]) "
						     "then compile that BP before binding."),
						*DispName, *CompName, *OwnerClass->GetName(),
						*FString::Join(AvailableDelegates, TEXT(", ")),
						*OwnerClass->GetName(), *DispName);
					return nullptr;
				}

				for (UEdGraphNode* ExistingNode : Graph->Nodes)
				{
					UK2Node_ComponentBoundEvent* Existing = Cast<UK2Node_ComponentBoundEvent>(ExistingNode);
					if (Existing
						&& Existing->ComponentPropertyName.ToString().Equals(CompName, ESearchCase::IgnoreCase)
						&& Existing->DelegatePropertyName == FName(*CanonName))
					{
						return Existing;
					}
				}

				UK2Node_ComponentBoundEvent* EvtNode = NewObject<UK2Node_ComponentBoundEvent>(Graph);
				EvtNode->ComponentPropertyName = FName(*CompName);
				EvtNode->DelegatePropertyName  = FName(*CanonName);
				EvtNode->DelegateOwnerClass    = OwnerClass;
				if (UFunction* SigFunc = Matched->SignatureFunction)
				{
					EvtNode->EventReference.SetFromField<UFunction>(SigFunc,  false);
				}
				EvtNode->CustomFunctionName = FName(*FString::Printf(TEXT("BndEvt__%s_%s_K2Node_ComponentBoundEvent_%s"),
					*Blueprint->GetName(), *CompName, *CanonName));
				EvtNode->NodePosX = PosX;
				EvtNode->NodePosY = PosY;
				EvtNode->SetFlags(RF_Transactional);
				Graph->AddNode(EvtNode, false, false);
				EvtNode->AllocateDefaultPins();
				NewNode = EvtNode;
			}
		}
		else if (Handle.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase))
		{
			FString EventName = Handle.RightChop(3);

			static const TMap<FString, FString> PlaceEventAliases = {
				{ TEXT("eventtick"),      TEXT("ReceiveTick") },
				{ TEXT("tick"),           TEXT("ReceiveTick") },
				{ TEXT("eventbeginplay"), TEXT("ReceiveBeginPlay") },
				{ TEXT("beginplay"),      TEXT("ReceiveBeginPlay") },
				{ TEXT("eventendplay"),   TEXT("ReceiveEndPlay") },
				{ TEXT("endplay"),        TEXT("ReceiveEndPlay") },
				{ TEXT("eventconstruct"), TEXT("Construct") },
				{ TEXT("event construct"), TEXT("Construct") },
				{ TEXT("event_construct"), TEXT("Construct") },
				{ TEXT("eventondestruct"), TEXT("Destruct") },
				{ TEXT("event ondestruct"), TEXT("Destruct") },
				{ TEXT("event destruct"), TEXT("Destruct") },
				{ TEXT("ondestruct"),     TEXT("Destruct") },
				{ TEXT("destruct"),       TEXT("Destruct") },
				{ TEXT("construct"),      TEXT("Construct") },
				{ TEXT("eventondrawpaint"), TEXT("OnPaint") },
				{ TEXT("event onpaint"),  TEXT("OnPaint") },
				{ TEXT("draw"),           TEXT("OnPaint") },
				{ TEXT("paint"),          TEXT("OnPaint") },
				{ TEXT("eventactorbeginoverlap"), TEXT("ReceiveActorBeginOverlap") },
				{ TEXT("actorbeginoverlap"),     TEXT("ReceiveActorBeginOverlap") },
				{ TEXT("eventactorendoverlap"),  TEXT("ReceiveActorEndOverlap") },
				{ TEXT("actorendoverlap"),       TEXT("ReceiveActorEndOverlap") },
				{ TEXT("eventhit"),       TEXT("ReceiveHit") },
				{ TEXT("hit"),            TEXT("ReceiveHit") },
				{ TEXT("eventanydamage"), TEXT("ReceiveAnyDamage") },
				{ TEXT("anydamage"),      TEXT("ReceiveAnyDamage") },
			};
			if (const FString* Resolved = PlaceEventAliases.Find(EventName.ToLower()))
				EventName = *Resolved;

			UEdGraphNode* FirstNamedMatch = nullptr;
			for (UEdGraphNode* ExistingNode : Graph->Nodes)
			{
				UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(ExistingNode);
				if (ExistingEvent && ExistingEvent->EventReference.GetMemberName().ToString().Equals(EventName, ESearchCase::IgnoreCase))
				{
					ExistingEvent->ReconstructNode();
					return ExistingEvent;
				}
				UK2Node_CustomEvent* CustomEvt = Cast<UK2Node_CustomEvent>(ExistingNode);
				if (CustomEvt)
				{
					const FString ExistingCustomName = CustomEvt->CustomFunctionName.ToString();
					if (ExistingCustomName.Equals(EventName, ESearchCase::IgnoreCase))
					{
						return CustomEvt;
					}
					if (!FirstNamedMatch && ExistingCustomName.StartsWith(EventName + TEXT("_"), ESearchCase::IgnoreCase))
					{
						FirstNamedMatch = CustomEvt;
					}
				}
			}
			if (FirstNamedMatch)
			{
				UE_LOG(LogTemp, Warning, TEXT("[build_graph] Reusing disambiguated event '%s' found in graph (previous build left it behind). Handle: %s"),
					*Cast<UK2Node_CustomEvent>(FirstNamedMatch)->CustomFunctionName.ToString(), *Handle);
				return FirstNamedMatch;
			}

		}
		else if (Handle.StartsWith(TEXT("ia."), ESearchCase::IgnoreCase))
		{
			FString ActionRef = Handle.RightChop(3);

			for (UEdGraphNode* ExistingNode : Graph->Nodes)
			{
				UK2Node_EnhancedInputAction* Existing = Cast<UK2Node_EnhancedInputAction>(ExistingNode);
				if (Existing && Existing->InputAction)
				{
					FString ExistingName = Existing->InputAction->GetName();
					if (ExistingName.Equals(ActionRef, ESearchCase::IgnoreCase) ||
						ExistingName.Equals(FPaths::GetBaseFilename(ActionRef), ESearchCase::IgnoreCase))
					{
						return Existing;
					}
				}
			}

			UInputAction* InputAction = nullptr;
			if (ActionRef.StartsWith(TEXT("/Game/")) || ActionRef.StartsWith(TEXT("/Engine/")))
			{
				InputAction = Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ActionRef));
			}
			if (!InputAction)
			{
				const FString WantName = FPaths::GetBaseFilename(ActionRef);
				FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				TArray<FAssetData> Assets;
				ARM.Get().GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), Assets);

				TArray<FAssetData> Matches;
				for (const FAssetData& AD : Assets)
				{
					const FString AName = AD.AssetName.ToString();
					if (AName.Equals(ActionRef, ESearchCase::IgnoreCase) || AName.Equals(WantName, ESearchCase::IgnoreCase))
					{
						Matches.Add(AD);
					}
				}

				if (Matches.Num() == 1)
				{
					InputAction = Cast<UInputAction>(Matches[0].GetAsset());
				}
				else if (Matches.Num() > 1)
				{
					const FString BpPkg = Blueprint->GetOutermost()->GetName();
					auto SharedDepth = [](const FString& A, const FString& B) -> int32
					{
						TArray<FString> SA, SB;
						A.ParseIntoArray(SA, TEXT("/"));
						B.ParseIntoArray(SB, TEXT("/"));
						const int32 N = FMath::Min(SA.Num(), SB.Num());
						int32 D = 0;
						for (int32 i = 0; i < N; ++i) { if (SA[i].Equals(SB[i], ESearchCase::IgnoreCase)) ++D; else break; }
						return D;
					};
					const FAssetData* Best = nullptr;
					int32 BestDepth = -1;
					FDateTime BestTime = FDateTime::MinValue();
					for (const FAssetData& AD : Matches)
					{
						const FString Pkg = AD.PackageName.ToString();
						const int32 Depth = SharedDepth(Pkg, BpPkg);
						const FString Filename = FPackageName::LongPackageNameToFilename(Pkg, FPackageName::GetAssetPackageExtension());
						const FDateTime TS = IFileManager::Get().GetTimeStamp(*Filename);
						if (Depth > BestDepth || (Depth == BestDepth && TS > BestTime))
						{
							Best = &AD;
							BestDepth = Depth;
							BestTime = TS;
						}
					}
					if (Best)
					{
						InputAction = Cast<UInputAction>(Best->GetAsset());
					}

					if (InputAction && OutWarnings)
					{
						FString CandList;
						for (const FAssetData& AD : Matches)
						{
							if (!CandList.IsEmpty()) CandList += TEXT(", ");
							CandList += AD.PackageName.ToString();
						}
						OutWarnings->Add(FString::Printf(
							TEXT("ia.%s matched %d InputActions (%s) — bound '%s'. Multiple assets share this name; ")
							TEXT("the binding was chosen by folder/recency and may be wrong. Pass the full path ")
							TEXT("(ia.%s) to bind a specific asset."),
							*WantName, Matches.Num(), *CandList, *InputAction->GetPathName(), *InputAction->GetPathName()));
					}
				}
			}

			if (InputAction)
			{
				UK2Node_EnhancedInputAction* IANode = NewObject<UK2Node_EnhancedInputAction>(Graph);
				IANode->InputAction = InputAction;
				IANode->NodePosX = PosX;
				IANode->NodePosY = PosY;
				IANode->SetFlags(RF_Transactional);
				Graph->AddNode(IANode, false, false);
				IANode->AllocateDefaultPins();
				NewNode = IANode;
			}
			else
			{
				OutError = FString::Printf(TEXT("InputAction asset '%s' not found. Ensure it exists in the Content Browser."), *ActionRef);
			}
		}
		else if (Handle.StartsWith(TEXT("k2.Debug Key "), ESearchCase::IgnoreCase))
		{
			const FString ExpectedTitle = TEXT("Debug Key ") + Handle.RightChop(13).TrimStartAndEnd();
			for (UEdGraphNode* ExistingNode : Graph->Nodes)
			{
				if (!ExistingNode) continue;
				const FString ExistingTitle = ExistingNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
				if (ExistingTitle.Equals(ExpectedTitle, ESearchCase::IgnoreCase))
				{
					NewNode = ExistingNode;
					break;
				}
			}
		}
		else if (Handle.StartsWith(TEXT("msg."), ESearchCase::IgnoreCase))
		{
			const FString ClassDotFunc = Handle.RightChop(4);
			int32 DotIdx;
			if (!ClassDotFunc.FindChar(TEXT('.'), DotIdx))
			{
				OutError = FString::Printf(TEXT("Bad msg. handle '%s' — expected msg.<InterfaceName>.<FuncName>."), *Handle);
			}
			else
			{
				FString IfaceName = ClassDotFunc.Left(DotIdx);
				const FString FuncName = ClassDotFunc.Mid(DotIdx + 1);

				UClass* IfaceClass = nullptr;
				const TArray<FString> Search = { IfaceName, IfaceName + TEXT("_C"), TEXT("U") + IfaceName, TEXT("I") + IfaceName };
				for (const FString& Try : Search)
				{
					UClass* C = FindFirstObject<UClass>(*Try);
					if (C && IsTransientBpClass(C))
					{
						for (TObjectIterator<UClass> It; It; ++It)
							if (It->GetName() == Try && !IsTransientBpClass(*It)) { C = *It; break; }
					}
					if (C) { IfaceClass = C; break; }
				}

				if (!IfaceClass)
				{
					OutError = FString::Printf(TEXT("Interface '%s' not found. For a Blueprint Interface asset use just the asset name (e.g. msg.BPI_Interactable.Interact)."), *IfaceName);
				}
				else if (!IfaceClass->HasAnyClassFlags(CLASS_Interface))
				{
					OutError = FString::Printf(TEXT("'%s' resolved but is not a Blueprint Interface (CLASS_Interface flag absent). Use fn.%s.%s for normal class methods."),
						*IfaceName, *IfaceName, *FuncName);
				}
				else
				{
					UFunction* Function = IfaceClass->FindFunctionByName(FName(*FuncName));
					if (!Function)
					{
						if (UBlueprint* OwnerBP = UBlueprint::GetBlueprintFromClass(IfaceClass))
							if (OwnerBP->SkeletonGeneratedClass)
								Function = OwnerBP->SkeletonGeneratedClass->FindFunctionByName(FName(*FuncName));
					}
					if (!Function)
					{
						OutError = FString::Printf(TEXT("Interface '%s' has no function '%s'. Compile the interface first if you just added it."),
							*IfaceName, *FuncName);
					}
					else
					{
						UK2Node_Message* MsgNode = NewObject<UK2Node_Message>(Graph);
						MsgNode->FunctionReference.SetExternalMember(Function->GetFName(), IfaceClass);
						MsgNode->NodePosX = PosX;
						MsgNode->NodePosY = PosY;
						MsgNode->SetFlags(RF_Transactional);
						Graph->AddNode(MsgNode, false, false);
						MsgNode->AllocateDefaultPins();
						NewNode = MsgNode;
					}
				}
			}
		}
		else if (Handle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase))
		{
			FString ClassDotFunc = Handle.RightChop(3);
			FString ClassName, FuncName;
			bool bIsSelfCall = false;

			if (ClassDotFunc.Split(TEXT("."), &ClassName, &FuncName))
			{
				if (ClassName.Equals(TEXT("Self"), ESearchCase::IgnoreCase))
				{
					bIsSelfCall = true;
				}
				else
				{
					static const TMap<FString, FString> ClassRedirects = {
						{TEXT("KismetMathLibrary.Conv_DoubleToString"),  TEXT("KismetStringLibrary")},
						{TEXT("KismetMathLibrary.Conv_FloatToString"),   TEXT("KismetStringLibrary")},
						{TEXT("KismetMathLibrary.Conv_IntToString"),     TEXT("KismetStringLibrary")},
						{TEXT("KismetMathLibrary.Conv_BoolToString"),    TEXT("KismetStringLibrary")},
						{TEXT("KismetMathLibrary.Conv_NameToString"),    TEXT("KismetStringLibrary")},
						{TEXT("KismetMathLibrary.BuildString_Double"),   TEXT("KismetStringLibrary")},
						{TEXT("KismetMathLibrary.BuildString_Int"),      TEXT("KismetStringLibrary")},
						{TEXT("KismetMathLibrary.Concat_StrStr"),        TEXT("KismetStringLibrary")},
					};
					if (const FString* RedirClass = ClassRedirects.Find(ClassName + TEXT(".") + FuncName))
						ClassName = *RedirClass;

					if (ClassName.Equals(TEXT("KismetMathLibrary"), ESearchCase::IgnoreCase) && FuncName.EndsWith(TEXT("_FloatFloat")))
					{
						FuncName = FuncName.LeftChop(11) + TEXT("_DoubleDouble");
					}

					static const TMap<FString, FString> FnAliases = {
						{TEXT("KismetStringLibrary.Append"),            TEXT("Concat_StrStr")},
						{TEXT("KismetStringLibrary.Concat"),            TEXT("Concat_StrStr")},
						{TEXT("KismetStringLibrary.Add"),               TEXT("Concat_StrStr")},
						{TEXT("KismetStringLibrary.Conv_FloatToString"),TEXT("Conv_DoubleToString")},
						{TEXT("KismetMathLibrary.Subtract"),            TEXT("Subtract_DoubleDouble")},
						{TEXT("KismetMathLibrary.Add"),                 TEXT("Add_DoubleDouble")},
						{TEXT("KismetMathLibrary.Multiply"),            TEXT("Multiply_DoubleDouble")},
						{TEXT("KismetMathLibrary.Divide"),              TEXT("Divide_DoubleDouble")},
					};
					if (const FString* Alias = FnAliases.Find(ClassName + TEXT(".") + FuncName))
						FuncName = *Alias;
				}
			}
			else
			{
				FuncName = ClassDotFunc;
				bIsSelfCall = true;
			}

			if (bIsSelfCall && Blueprint)
			{
				FName FuncFName(*FuncName);
				bool bFoundFunc = false;
				for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
				{
					if (FuncGraph && FuncGraph->GetFName() == FuncFName)
					{
						bFoundFunc = true;
						break;
					}
				}
				UClass* BPClass = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass : Blueprint->GeneratedClass;
				if (!bFoundFunc && BPClass)
				{
					UFunction* Func = BPClass->FindFunctionByName(FuncFName);
					if (Func) bFoundFunc = true;
				}

				if (bFoundFunc)
				{
					UK2Node_CallFunction* FuncNode = NewObject<UK2Node_CallFunction>(Graph);
					FuncNode->FunctionReference.SetSelfMember(FuncFName);
					FuncNode->NodePosX = PosX;
					FuncNode->NodePosY = PosY;
					FuncNode->SetFlags(RF_Transactional);
					Graph->AddNode(FuncNode, false, false);
					FuncNode->AllocateDefaultPins();
					NewNode = FuncNode;
				}
				else
				{
					int32 DotIdx = INDEX_NONE;
					if (FuncName.FindChar(TEXT('.'), DotIdx))
					{
						const FString MaybeComponent = FuncName.Left(DotIdx);
						const FString MaybeFunction  = FuncName.RightChop(DotIdx + 1);

						bool bIsKnownComponent = false;
						FString ComponentClassName;
						if (Blueprint->SimpleConstructionScript)
						{
							for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
							{
								if (Node && Node->GetVariableName().ToString().Equals(MaybeComponent, ESearchCase::IgnoreCase))
								{
									bIsKnownComponent = true;
									if (Node->ComponentTemplate && Node->ComponentTemplate->GetClass())
									{
										ComponentClassName = Node->ComponentTemplate->GetClass()->GetName();
										ComponentClassName.RemoveFromEnd(TEXT("_C"));
									}
									break;
								}
							}
						}

						if (bIsKnownComponent)
						{
							OutError = FString::Printf(
								TEXT("'fn.Self.%s.%s' isn't valid — UE doesn't chain Self.<Component>.<Func>. ")
								TEXT("Cross-component call pattern: place a 'var.get.%s' node, then 'fn.%s.%s', and wire ")
								TEXT("var.get.%s.%s → call.self. Exec stays on the caller's chain (the var.get is pure)."),
								*MaybeComponent, *MaybeFunction,
								*MaybeComponent,
								ComponentClassName.IsEmpty() ? *MaybeComponent : *ComponentClassName, *MaybeFunction,
								*MaybeComponent, *MaybeComponent);
							return nullptr;
						}
					}

					OutError = FString::Printf(TEXT("Self-function '%s' not found on this blueprint. Available functions: "), *FuncName);
					for (UEdGraph* FG : Blueprint->FunctionGraphs)
					{
						if (FG) OutError += FG->GetName() + TEXT(", ");
					}
					OutError += TEXT("Compile the blueprint first if you just added the function.");
				}
			}
			else if (!bIsSelfCall && !FuncName.IsEmpty())
			{
				UFunction* Function = nullptr;
				UClass* OwnerClass = nullptr;
				if (ClassName.StartsWith(TEXT("SKEL_")))
					ClassName.RightChopInline(5);
				else if (ClassName.StartsWith(TEXT("REINST_")))
					ClassName.RightChopInline(7);

				TArray<FString> SearchNames = { ClassName, ClassName + TEXT("_C"), TEXT("U") + ClassName, TEXT("A") + ClassName };

				static const TMap<FString, const TCHAR*> NativeClassPaths = {
					{TEXT("Actor"),                      TEXT("/Script/Engine.Actor")},
					{TEXT("Pawn"),                       TEXT("/Script/Engine.Pawn")},
					{TEXT("Character"),                  TEXT("/Script/Engine.Character")},
					{TEXT("Controller"),                 TEXT("/Script/Engine.Controller")},
					{TEXT("AIController"),               TEXT("/Script/AIModule.AIController")},
					{TEXT("PlayerController"),           TEXT("/Script/Engine.PlayerController")},
					{TEXT("GameMode"),                   TEXT("/Script/Engine.GameMode")},
					{TEXT("GameModeBase"),               TEXT("/Script/Engine.GameModeBase")},
					{TEXT("GameInstance"),               TEXT("/Script/Engine.GameInstance")},
					{TEXT("GameState"),                  TEXT("/Script/Engine.GameState")},
					{TEXT("GameStateBase"),              TEXT("/Script/Engine.GameStateBase")},
					{TEXT("PlayerState"),                TEXT("/Script/Engine.PlayerState")},
					{TEXT("ActorComponent"),             TEXT("/Script/Engine.ActorComponent")},
					{TEXT("SceneComponent"),             TEXT("/Script/Engine.SceneComponent")},
					{TEXT("PrimitiveComponent"),         TEXT("/Script/Engine.PrimitiveComponent")},
					{TEXT("StaticMeshComponent"),        TEXT("/Script/Engine.StaticMeshComponent")},
					{TEXT("SkeletalMeshComponent"),      TEXT("/Script/Engine.SkeletalMeshComponent")},
					{TEXT("CharacterMovementComponent"), TEXT("/Script/Engine.CharacterMovementComponent")},
					{TEXT("CapsuleComponent"),           TEXT("/Script/Engine.CapsuleComponent")},
					{TEXT("BoxComponent"),               TEXT("/Script/Engine.BoxComponent")},
					{TEXT("SphereComponent"),            TEXT("/Script/Engine.SphereComponent")},
					{TEXT("CameraComponent"),            TEXT("/Script/Engine.CameraComponent")},
					{TEXT("SpringArmComponent"),         TEXT("/Script/Engine.SpringArmComponent")},
					{TEXT("UserWidget"),                 TEXT("/Script/UMG.UserWidget")},
					{TEXT("Widget"),                     TEXT("/Script/UMG.Widget")},
					{TEXT("PlayerCameraManager"),        TEXT("/Script/Engine.PlayerCameraManager")},
				};

				UClass* LastClassSeen = nullptr;
				for (const FString& Name : SearchNames)
				{
					UClass* FoundClass = nullptr;

					if (const TCHAR* const* NativePath = NativeClassPaths.Find(Name))
					{
						FoundClass = FindObject<UClass>(nullptr, *NativePath);
						if (!FoundClass)
						{
							FoundClass = Cast<UClass>(StaticFindObject(UClass::StaticClass(), nullptr, *NativePath));
						}
					}

					if (!FoundClass)
					{
						FoundClass = FindFirstObject<UClass>(*Name);
					}

					if (FoundClass && IsTransientBpClass(FoundClass))
					{
						UClass* Canonical = nullptr;
						for (TObjectIterator<UClass> It; It; ++It)
						{
							if (It->GetName().Equals(Name, ESearchCase::IgnoreCase) && !IsTransientBpClass(*It))
							{ Canonical = *It; break; }
						}
						if (Canonical) FoundClass = Canonical;
					}

					if (!FoundClass)
					{
						for (TObjectIterator<UClass> It; It; ++It)
						{
							if (It->GetName().Equals(Name, ESearchCase::IgnoreCase) && !IsTransientBpClass(*It))
							{ FoundClass = *It; break; }
						}
					}

					if (FoundClass)
					{
						LastClassSeen = FoundClass;
						Function = FoundClass->FindFunctionByName(FName(*FuncName));
						if (Function) { OwnerClass = FoundClass; break; }

						if (UBlueprint* OwnerBP = UBlueprint::GetBlueprintFromClass(FoundClass))
						{
							if (OwnerBP->SkeletonGeneratedClass)
							{
								if (UFunction* SkelFunc = OwnerBP->SkeletonGeneratedClass->FindFunctionByName(FName(*FuncName)))
								{
									Function = SkelFunc;
									OwnerClass = FoundClass;
									break;
								}
							}
						}
					}
				}

				if (!Function)
				{
					FString CanonName = ClassName;
					CanonName.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);
					if (FAssetRegistryModule* ARM = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
					{
						IAssetRegistry& AR = ARM->Get();
						TArray<FAssetData> Assets;
						AR.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets, true);
						AR.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, true);
						for (const FAssetData& AD : Assets)
						{
							if (AD.AssetName.ToString() != CanonName) continue;
							UBlueprint* OtherBP = Cast<UBlueprint>(AD.GetAsset());
							if (!OtherBP) continue;
							UClass* GenCls = OtherBP->GeneratedClass;
							UClass* SkelCls = OtherBP->SkeletonGeneratedClass;
							const FName FN(*FuncName);
							if (GenCls)  { if (UFunction* F = GenCls ->FindFunctionByName(FN)) { Function = F; OwnerClass = GenCls;  break; } }
							if (SkelCls) { if (UFunction* F = SkelCls->FindFunctionByName(FN)) { Function = F; OwnerClass = GenCls ? GenCls : SkelCls; break; } }
						}
					}
				}

				if (!Function)
				{
					if (LastClassSeen)
					{
						OutError = FString::Printf(
							TEXT("Could not resolve fn.%s.%s — class '%s' was found, but no UFUNCTION named '%s' exists on it (or any base class). ")
							TEXT("Possible cause: the function name changed in this engine version, the UFUNCTION metadata was removed, or you meant a UPROPERTY accessor (var.get.X) instead. ")
							TEXT("Use discover_nodes(query='%s') to find the correct handle."),
							*ClassName, *FuncName, *ClassName, *FuncName, *FuncName);
					}
					else
					{
						OutError = FString::Printf(
							TEXT("Could not resolve fn.%s.%s — no class named '%s' (or '%s_C') found in memory or AssetRegistry. ")
							TEXT("If the function lives on a DIFFERENT Blueprint, make sure that BP exists at the expected path. ")
							TEXT("If the function is on THIS Blueprint, use fn.Self.%s instead. ")
							TEXT("If the OTHER BP just had this function added, compile it first."),
							*ClassName, *FuncName, *ClassName, *ClassName, *FuncName);
					}
				}

				if (Function && OwnerClass)
				{
					const bool bIsInterfaceCall = OwnerClass->HasAnyClassFlags(CLASS_Interface);
					bool bIsArrayLibFunc = !bIsInterfaceCall
						&& (OwnerClass->GetName() == TEXT("KismetArrayLibrary")
							|| OwnerClass->GetName() == TEXT("UKismetArrayLibrary"));
					UK2Node_CallFunction* FuncNode = bIsInterfaceCall
						? NewObject<UK2Node_Message>(Graph)
						: (bIsArrayLibFunc
							? NewObject<UK2Node_CallArrayFunction>(Graph)
							: NewObject<UK2Node_CallFunction>(Graph));
					FuncNode->FunctionReference.SetExternalMember(Function->GetFName(), OwnerClass);
					FuncNode->NodePosX = PosX;
					FuncNode->NodePosY = PosY;
					FuncNode->SetFlags(RF_Transactional);
					Graph->AddNode(FuncNode, false, false);
					FuncNode->AllocateDefaultPins();
					NewNode = FuncNode;
				}
			}
		}
		else if (Handle.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase))
		{
			FString VarName = Handle.RightChop(8);
			FName VarFName(*VarName);
			bool bVarExists = false;
			bool bIsLocalVar = false;
			bool bIsCrossClassVar = false;
			FGuid LocalVarGuid;
			FString LocalVarScopeName;
			FBPVariableDescription LocalVarDesc;
			FProperty* ClassPropertyForFallback = nullptr;
			if (SelfContextClass)
			{
				FProperty* Prop = FindFProperty<FProperty>(SelfContextClass, VarFName);
				if (Prop) { bVarExists = true; bIsCrossClassVar = true; ClassPropertyForFallback = Prop; }
			}
			if (!bVarExists)
			{
				for (const FBPVariableDescription& Var : Blueprint->NewVariables)
				{
					if (Var.VarName == VarFName) { bVarExists = true; break; }
				}
			}
			if (!bVarExists && Blueprint->GeneratedClass)
			{
				FProperty* Prop = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarFName);
				if (Prop) { bVarExists = true; ClassPropertyForFallback = Prop; }
			}
			if (!bVarExists && Blueprint->SkeletonGeneratedClass)
			{
				FProperty* Prop = FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, VarFName);
				if (Prop) { bVarExists = true; ClassPropertyForFallback = Prop; }
			}
			if (!bVarExists)
			{
				TArray<UEdGraph*> LocalScanGraphs;
				if (Graph) LocalScanGraphs.Add(Graph);
				Blueprint->GetAllGraphs(LocalScanGraphs);
				for (UEdGraph* G : LocalScanGraphs)
				{
					if (!G) continue;
					TArray<UK2Node_FunctionEntry*> EntryNodes;
					G->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
					for (UK2Node_FunctionEntry* Entry : EntryNodes)
					{
						for (const FBPVariableDescription& LV : Entry->LocalVariables)
						{
							if (LV.VarName == VarFName)
							{
								bVarExists = true; bIsLocalVar = true;
								LocalVarGuid = LV.VarGuid;
								LocalVarScopeName = G->GetName();
								LocalVarDesc = LV;
								break;
							}
						}
						if (bVarExists) break;
					}
					if (bVarExists) break;
				}
			}
			if (!bVarExists)
			{
				UTimelineTemplate* TLTemplate = Blueprint->FindTimelineTemplateByVariableName(VarFName);
				if (TLTemplate)
				{
					bVarExists = true;
				}
			}
			if (!bVarExists)
			{
				if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint))
				{
					if (WBP->WidgetTree)
					{
						TArray<UWidget*> AllWidgets;
						WBP->WidgetTree->GetAllWidgets(AllWidgets);
						for (UWidget* W : AllWidgets)
						{
							if (W && W->bIsVariable && W->GetFName() == VarFName)
							{
								bVarExists = true;
								break;
							}
						}
					}
				}
			}
			if (!bVarExists)
			{
				OutError = FString::Printf(TEXT("Variable '%s' not found. Use add_variable first."), *VarName);
				return nullptr;
			}

			UK2Node_VariableGet* GetNode = SEHGuardedNewK2Node<UK2Node_VariableGet>(Graph);
			if (!GetNode)
			{
				OutError = FString::Printf(
					TEXT("Variable '%s' node creation failed at NewObject (engine name-pool race). "
					     "Retry the build_blueprint_graph / place_node call; if it recurs, restart the editor "
					     "to reset the FName pool."), *VarName);
				return nullptr;
			}
			if (bIsCrossClassVar)
				GetNode->VariableReference.SetExternalMember(VarFName, SelfContextClass);
			else if (bIsLocalVar)
				GetNode->VariableReference.SetLocalMember(VarFName, LocalVarScopeName, LocalVarGuid);
			else
				GetNode->VariableReference.SetSelfMember(VarFName);
			GetNode->NodePosX = PosX;
			GetNode->NodePosY = PosY;
			GetNode->SetFlags(RF_Transactional);
			Graph->AddNode(GetNode, false, false);
			GetNode->AllocateDefaultPins();

			auto HasValuePin = [&]() {
				for (UEdGraphPin* P : GetNode->Pins)
					if (P && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) return true;
				return false;
			};
			if (bIsLocalVar && !HasValuePin())
			{
				UEdGraphPin* ValPin = GetNode->CreatePin(EGPD_Output,
					LocalVarDesc.VarType.PinCategory,
					LocalVarDesc.VarType.PinSubCategory,
					LocalVarDesc.VarType.PinSubCategoryObject.Get(),
					VarFName);
				if (ValPin) ValPin->PinType = LocalVarDesc.VarType;
			}
			else if (!HasValuePin() && ClassPropertyForFallback)
			{
				FEdGraphPinType PinType;
				const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
				if (K2Schema && K2Schema->ConvertPropertyToPinType(ClassPropertyForFallback, PinType))
				{
					UEdGraphPin* ValPin = GetNode->CreatePin(EGPD_Output, PinType, VarFName);
					if (ValPin) ValPin->PinType = PinType;
				}
			}

			if (GetNode->Pins.Num() == 0)
			{
				Graph->RemoveNode(GetNode);
				OutError = FString::Printf(TEXT("Variable '%s' pin creation failed."), *VarName);
				return nullptr;
			}
			NewNode = GetNode;
		}
		else if (Handle.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase))
		{
			FString VarName = Handle.RightChop(8);
			FName VarFName(*VarName);
			bool bVarExists = false;
			bool bIsLocalVar = false;
			bool bIsCrossClassVar = false;
			FGuid LocalVarGuid;
			FString LocalVarScopeName;
			FBPVariableDescription LocalVarDesc;
			FProperty* ClassPropertyForFallback = nullptr;
			if (SelfContextClass)
			{
				FProperty* Prop = FindFProperty<FProperty>(SelfContextClass, VarFName);
				if (Prop) { bVarExists = true; bIsCrossClassVar = true; ClassPropertyForFallback = Prop; }
			}
			if (!bVarExists)
			{
				for (const FBPVariableDescription& Var : Blueprint->NewVariables)
				{
					if (Var.VarName == VarFName) { bVarExists = true; break; }
				}
			}
			if (!bVarExists && Blueprint->GeneratedClass)
			{
				FProperty* Prop = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarFName);
				if (Prop) { bVarExists = true; ClassPropertyForFallback = Prop; }
			}
			if (!bVarExists && Blueprint->SkeletonGeneratedClass)
			{
				FProperty* Prop = FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, VarFName);
				if (Prop) { bVarExists = true; ClassPropertyForFallback = Prop; }
			}
			if (!bVarExists)
			{
				TArray<UEdGraph*> LocalScanGraphs;
				if (Graph) LocalScanGraphs.Add(Graph);
				Blueprint->GetAllGraphs(LocalScanGraphs);
				for (UEdGraph* G : LocalScanGraphs)
				{
					if (!G) continue;
					TArray<UK2Node_FunctionEntry*> EntryNodes;
					G->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
					for (UK2Node_FunctionEntry* Entry : EntryNodes)
					{
						for (const FBPVariableDescription& LV : Entry->LocalVariables)
						{
							if (LV.VarName == VarFName)
							{
								bVarExists = true; bIsLocalVar = true;
								LocalVarGuid = LV.VarGuid;
								LocalVarScopeName = G->GetName();
								LocalVarDesc = LV;
								break;
							}
						}
						if (bVarExists) break;
					}
					if (bVarExists) break;
				}
			}
			if (!bVarExists)
			{
				UTimelineTemplate* TLTemplate = Blueprint->FindTimelineTemplateByVariableName(VarFName);
				if (TLTemplate) bVarExists = true;
			}
			if (!bVarExists)
			{
				if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint))
				{
					if (WBP->WidgetTree)
					{
						TArray<UWidget*> AllWidgets;
						WBP->WidgetTree->GetAllWidgets(AllWidgets);
						for (UWidget* W : AllWidgets)
						{
							if (W && W->bIsVariable && W->GetFName() == VarFName)
							{
								bVarExists = true;
								break;
							}
						}
					}
				}
			}
			if (!bVarExists)
			{
				OutError = FString::Printf(TEXT("Variable '%s' not found. Use add_variable first."), *VarName);
				return nullptr;
			}

			UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);
			if (bIsCrossClassVar)
				SetNode->VariableReference.SetExternalMember(VarFName, SelfContextClass);
			else if (bIsLocalVar)
				SetNode->VariableReference.SetLocalMember(VarFName, LocalVarScopeName, LocalVarGuid);
			else
				SetNode->VariableReference.SetSelfMember(VarFName);
			SetNode->NodePosX = PosX;
			SetNode->NodePosY = PosY;
			SetNode->SetFlags(RF_Transactional);
			Graph->AddNode(SetNode, false, false);
			SetNode->AllocateDefaultPins();

			auto SetHasValuePin = [&]() {
				for (UEdGraphPin* P : SetNode->Pins)
					if (P && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) return true;
				return false;
			};
			if (bIsLocalVar && !SetHasValuePin())
			{
				UEdGraphPin* ValPin = SetNode->CreatePin(EGPD_Input,
					LocalVarDesc.VarType.PinCategory,
					LocalVarDesc.VarType.PinSubCategory,
					LocalVarDesc.VarType.PinSubCategoryObject.Get(),
					VarFName);
				if (ValPin) ValPin->PinType = LocalVarDesc.VarType;
			}
			else if (!SetHasValuePin() && ClassPropertyForFallback)
			{
				FEdGraphPinType PinType;
				const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
				if (K2Schema && K2Schema->ConvertPropertyToPinType(ClassPropertyForFallback, PinType))
				{
					UEdGraphPin* ValPin = SetNode->CreatePin(EGPD_Input, PinType, VarFName);
					if (ValPin) ValPin->PinType = PinType;
				}
			}

			if (SetNode->Pins.Num() == 0)
			{
				Graph->RemoveNode(SetNode);
				OutError = FString::Printf(TEXT("Variable '%s' pin creation failed."), *VarName);
				return nullptr;
			}
			NewNode = SetNode;
		}
		else if (Handle.StartsWith(TEXT("prop.set."), ESearchCase::IgnoreCase))
		{
			FString ClassDotProp = Handle.RightChop(9);
			FString ClassName, PropName;
			if (ClassDotProp.Split(TEXT("."), &ClassName, &PropName))
			{
				UClass* OwnerClass = FindClassByName(ClassName);
				if (OwnerClass)
				{
					FProperty* Prop = FindFProperty<FProperty>(OwnerClass, FName(*PropName));
					if (!Prop)
					{
						for (TFieldIterator<FProperty> It(OwnerClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
						{
							if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase)) { Prop = *It; break; }
						}
					}
					if (Prop)
					{
						UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);
						SetNode->VariableReference.SetExternalMember(Prop->GetFName(), OwnerClass);
						SetNode->NodePosX = PosX;
						SetNode->NodePosY = PosY;
						SetNode->SetFlags(RF_Transactional);
						Graph->AddNode(SetNode, false, false);
						SetNode->AllocateDefaultPins();
						NewNode = SetNode;
					}
					else
					{
						FString Available;
						int32 Count = 0;
						for (TFieldIterator<FProperty> It(OwnerClass, EFieldIteratorFlags::IncludeSuper); It && Count < 20; ++It, ++Count)
						{
							if (!Available.IsEmpty()) Available += TEXT(", ");
							Available += It->GetName();
						}
						OutError = FString::Printf(TEXT("Property '%s' not found on class '%s'. Available: %s"),
							*PropName, *ClassName, *Available);
					}
				}
				else { OutError = FString::Printf(TEXT("Class '%s' not found for prop.set handle"), *ClassName); }
			}
		}
		else if (Handle.StartsWith(TEXT("prop.get."), ESearchCase::IgnoreCase))
		{
			FString ClassDotProp = Handle.RightChop(9);
			FString ClassName, PropName;
			if (ClassDotProp.Split(TEXT("."), &ClassName, &PropName))
			{
				UClass* OwnerClass = FindClassByName(ClassName);
				if (OwnerClass)
				{
					FProperty* Prop = FindFProperty<FProperty>(OwnerClass, FName(*PropName));
					if (!Prop)
					{
						for (TFieldIterator<FProperty> It(OwnerClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
						{
							if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase)) { Prop = *It; break; }
						}
					}
					if (Prop)
					{
						UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
						GetNode->VariableReference.SetExternalMember(Prop->GetFName(), OwnerClass);
						GetNode->NodePosX = PosX;
						GetNode->NodePosY = PosY;
						GetNode->SetFlags(RF_Transactional);
						Graph->AddNode(GetNode, false, false);
						GetNode->AllocateDefaultPins();
						NewNode = GetNode;
					}
					else
					{
						FString Available;
						int32 Count = 0;
						for (TFieldIterator<FProperty> It(OwnerClass, EFieldIteratorFlags::IncludeSuper); It && Count < 20; ++It, ++Count)
						{
							if (!Available.IsEmpty()) Available += TEXT(", ");
							Available += It->GetName();
						}
						OutError = FString::Printf(TEXT("Property '%s' not found on class '%s'. Available: %s"),
							*PropName, *ClassName, *Available);
					}
				}
				else { OutError = FString::Printf(TEXT("Class '%s' not found for prop.get handle"), *ClassName); }
			}
		}
		else if (Handle.StartsWith(TEXT("k2.Timeline."), ESearchCase::IgnoreCase))
		{
			FString TLName = Handle.RightChop(12);
			for (UEdGraphNode* ExNode : Graph->Nodes)
			{
				if (UK2Node_Timeline* TL = Cast<UK2Node_Timeline>(ExNode))
				{
					if (TL->TimelineName.ToString().Equals(TLName, ESearchCase::IgnoreCase))
					{
						TL->NodePosX = PosX;
						TL->NodePosY = PosY;
						TL->ReconstructNode();
						return TL;
					}
				}
			}
			UK2Node_Timeline* TLNode = NewObject<UK2Node_Timeline>(Graph);
			TLNode->TimelineName = FName(*TLName);
			TLNode->NodePosX = PosX;
			TLNode->NodePosY = PosY;
			TLNode->SetFlags(RF_Transactional);
			Graph->AddNode(TLNode, false, false);
			TLNode->CreateNewGuid();
			TLNode->PostPlacedNewNode();
			TLNode->AllocateDefaultPins();
			NewNode = TLNode;
		}
		else if (Handle.Equals(TEXT("k2.FunctionEntry"), ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.CallFunction"), ESearchCase::IgnoreCase))
		{
			OutError = TEXT("build_blueprint_graph cannot create function graphs. Use ev.CustomEvent instead of separate functions.");
			return nullptr;
		}
		else if (Handle.Equals(TEXT("k2.Branch"), ESearchCase::IgnoreCase))
		{
			UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
			BranchNode->NodePosX = PosX;
			BranchNode->NodePosY = PosY;
			BranchNode->SetFlags(RF_Transactional);
			Graph->AddNode(BranchNode, false, false);
			BranchNode->AllocateDefaultPins();
			NewNode = BranchNode;
		}
		else if (Handle.Equals(TEXT("k2.Sequence"), ESearchCase::IgnoreCase))
		{
			UK2Node_ExecutionSequence* SeqNode = NewObject<UK2Node_ExecutionSequence>(Graph);
			SeqNode->NodePosX = PosX;
			SeqNode->NodePosY = PosY;
			SeqNode->SetFlags(RF_Transactional);
			Graph->AddNode(SeqNode, false, false);
			SeqNode->AllocateDefaultPins();
			NewNode = SeqNode;
		}
		else if (Handle.Equals(TEXT("k2.Self"), ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.self"), ESearchCase::IgnoreCase))
		{
			UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
			SelfNode->NodePosX = PosX;
			SelfNode->NodePosY = PosY;
			SelfNode->SetFlags(RF_Transactional);
			Graph->AddNode(SelfNode, false, false);
			SelfNode->AllocateDefaultPins();
			NewNode = SelfNode;
		}
		else if (Handle.Equals(TEXT("k2.MakeArray"), ESearchCase::IgnoreCase))
		{
			UK2Node_MakeArray* ArrNode = NewObject<UK2Node_MakeArray>(Graph);
			ArrNode->NodePosX = PosX;
			ArrNode->NodePosY = PosY;
			ArrNode->SetFlags(RF_Transactional);
			Graph->AddNode(ArrNode, false, false);
			ArrNode->AllocateDefaultPins();
			NewNode = ArrNode;
		}
		else if (Handle.Equals(TEXT("k2.Select"),       ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.SelectNode"),   ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.Select Node"),  ESearchCase::IgnoreCase))
		{
			UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(Graph);
			SelectNode->NodePosX = PosX;
			SelectNode->NodePosY = PosY;
			SelectNode->SetFlags(RF_Transactional);
			Graph->AddNode(SelectNode, false, false);
			SelectNode->AllocateDefaultPins();
			NewNode = SelectNode;
		}
		else if (Handle.Equals(TEXT("k2.SpawnActorFromClass"),    ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.Spawn Actor from Class"), ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.SpawnActor"),             ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.Spawn Actor"),            ESearchCase::IgnoreCase))
		{
			UK2Node_SpawnActorFromClass* SpawnNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
			SpawnNode->NodePosX = PosX;
			SpawnNode->NodePosY = PosY;
			SpawnNode->SetFlags(RF_Transactional);
			Graph->AddNode(SpawnNode, false, false);
			SpawnNode->AllocateDefaultPins();
			NewNode = SpawnNode;
		}
		else if (Handle.Equals(TEXT("k2.FormatText"), ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.Format Text"), ESearchCase::IgnoreCase) ||
				 Handle.Equals(TEXT("k2.K2Node_FormatText"), ESearchCase::IgnoreCase))
		{
			UK2Node_FormatText* FmtNode = NewObject<UK2Node_FormatText>(Graph);
			FmtNode->NodePosX = PosX;
			FmtNode->NodePosY = PosY;
			FmtNode->SetFlags(RF_Transactional);
			Graph->AddNode(FmtNode, false, false);
			FmtNode->AllocateDefaultPins();
			NewNode = FmtNode;
		}
		else if (Handle.StartsWith(TEXT("k2.Break "), ESearchCase::IgnoreCase))
		{
			FString StructName = Handle.RightChop(9).TrimStartAndEnd();
			if (StructName.Contains(TEXT("(")) && StructName.EndsWith(TEXT(")")))
			{
				const int32 OpenParen = StructName.Find(TEXT("("));
				FString Inner = StructName.Mid(OpenParen + 1, StructName.Len() - OpenParen - 2).TrimStartAndEnd();
				if (!Inner.IsEmpty()) StructName = Inner;
			}
			UScriptStruct* FoundStruct = nullptr;

			{
				IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				TArray<FAssetData> StructAssets;
				AR.GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), StructAssets, true);
				for (const FAssetData& Asset : StructAssets)
				{
					if (Asset.AssetName.ToString().Equals(StructName, ESearchCase::IgnoreCase))
					{
						FoundStruct = Cast<UScriptStruct>(Asset.GetAsset());
						break;
					}
				}
			}
			if (!FoundStruct)
			{
				for (TObjectIterator<UScriptStruct> It; It; ++It)
				{
					if (It->GetName().Equals(StructName, ESearchCase::IgnoreCase) ||
						It->GetName().Equals(TEXT("F") + StructName, ESearchCase::IgnoreCase))
					{
						FoundStruct = *It;
						break;
					}
				}
			}

			if (FoundStruct)
			{
				UK2Node_BreakStruct* BreakNode = NewObject<UK2Node_BreakStruct>(Graph);
				BreakNode->StructType = FoundStruct;
				BreakNode->CreateNewGuid();
				BreakNode->NodePosX = PosX;
				BreakNode->NodePosY = PosY;
				BreakNode->SetFlags(RF_Transactional);
				Graph->AddNode(BreakNode, false, false);
				BreakNode->AllocateDefaultPins();
				NewNode = BreakNode;
			}
			else
			{
				TArray<FString> Available;
				IAssetRegistry& AR2 = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				TArray<FAssetData> UDSAssets;
				AR2.GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), UDSAssets, true);
				for (const FAssetData& A : UDSAssets) { Available.AddUnique(A.AssetName.ToString()); if (Available.Num() >= 12) break; }
				OutError = FString::Printf(
					TEXT("Struct '%s' not found. Check the name matches a User Defined Struct asset OR a native struct (e.g. 'HitResult', 'Vector', 'Rotator'). Available UDS: %s."),
					*StructName,
					Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("(none in project)"));
			}
		}
		else if (Handle.StartsWith(TEXT("k2.Make "), ESearchCase::IgnoreCase))
		{
			FString StructName = Handle.RightChop(8).TrimStartAndEnd();
			if (StructName.Contains(TEXT("(")) && StructName.EndsWith(TEXT(")")))
			{
				const int32 OpenParen = StructName.Find(TEXT("("));
				FString Inner = StructName.Mid(OpenParen + 1, StructName.Len() - OpenParen - 2).TrimStartAndEnd();
				if (!Inner.IsEmpty()) StructName = Inner;
			}
			UScriptStruct* FoundStruct = nullptr;

			{
				IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				TArray<FAssetData> StructAssets;
				AR.GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), StructAssets, true);
				for (const FAssetData& Asset : StructAssets)
				{
					if (Asset.AssetName.ToString().Equals(StructName, ESearchCase::IgnoreCase))
					{
						FoundStruct = Cast<UScriptStruct>(Asset.GetAsset());
						break;
					}
				}
			}
			if (!FoundStruct)
			{
				for (TObjectIterator<UScriptStruct> It; It; ++It)
				{
					if (It->GetName().Equals(StructName, ESearchCase::IgnoreCase) ||
						It->GetName().Equals(TEXT("F") + StructName, ESearchCase::IgnoreCase))
					{
						FoundStruct = *It;
						break;
					}
				}
			}

			if (FoundStruct)
			{
				UK2Node_MakeStruct* MakeNode = NewObject<UK2Node_MakeStruct>(Graph);
				MakeNode->StructType = FoundStruct;
				MakeNode->CreateNewGuid();
				MakeNode->NodePosX = PosX;
				MakeNode->NodePosY = PosY;
				MakeNode->SetFlags(RF_Transactional);
				Graph->AddNode(MakeNode, false, false);
				MakeNode->AllocateDefaultPins();
				NewNode = MakeNode;
			}
			else
			{
				TArray<FString> Available;
				IAssetRegistry& AR2 = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				TArray<FAssetData> UDSAssets;
				AR2.GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), UDSAssets, true);
				for (const FAssetData& A : UDSAssets) { Available.AddUnique(A.AssetName.ToString()); if (Available.Num() >= 12) break; }
				OutError = FString::Printf(
					TEXT("Struct '%s' not found. Check the name matches a User Defined Struct asset OR a native struct (e.g. 'HitResult', 'Vector', 'Rotator'). Available UDS: %s."),
					*StructName,
					Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("(none in project)"));
			}
		}
		else if (Handle.StartsWith(TEXT("k2.Cast To "), ESearchCase::IgnoreCase))
		{
			FString ClassName = Handle.RightChop(11).TrimStartAndEnd();
			UClass* TargetClass = nullptr;
			for (const FString& Prefix : {FString(TEXT("")), FString(TEXT("U")), FString(TEXT("A"))})
			{
				TargetClass = FindFirstObject<UClass>(*(Prefix + ClassName), EFindFirstObjectOptions::NativeFirst);
				if (TargetClass) break;
			}
			if (!TargetClass)
			{
				FString ClassNameWithC = ClassName.EndsWith(TEXT("_C")) ? ClassName : ClassName + TEXT("_C");
				TargetClass = FindFirstObject<UClass>(*ClassNameWithC);
			}
			if (!TargetClass)
			{
				FString AssetPath = ClassName.StartsWith(TEXT("/")) ? ClassName : TEXT("/Game/") + ClassName;
				UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
				if (UBlueprint* LoadedBP = Cast<UBlueprint>(Loaded))
					TargetClass = LoadedBP->GeneratedClass;
			}
			if (!TargetClass)
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					FString Name = It->GetName();
					if (Name.Equals(ClassName, ESearchCase::IgnoreCase) ||
						Name.Equals(ClassName + TEXT("_C"), ESearchCase::IgnoreCase))
					{ TargetClass = *It; break; }
				}
			}
			if (TargetClass)
			{
				const bool bIsSnapshot = TargetClass->HasAnyFlags(RF_Transient)
					|| TargetClass->GetOutermost()->GetName().StartsWith(TEXT("/Temp/DiffSnap"), ESearchCase::IgnoreCase);
				if (bIsSnapshot)
				{
					const FString RealName = TargetClass->GetName();
					UClass* Real = nullptr;
					for (TObjectIterator<UClass> It; It; ++It)
					{
						if (It->HasAnyFlags(RF_Transient)) continue;
						if (It->GetName().Equals(RealName, ESearchCase::CaseSensitive)
							&& !It->GetOutermost()->GetName().StartsWith(TEXT("/Temp/"), ESearchCase::IgnoreCase))
						{ Real = *It; break; }
					}
					if (Real) TargetClass = Real;
				}
			}
			if (TargetClass && IsTransientBpClass(TargetClass))
			{
				const FString CanonName = TargetClass->GetName();
				FString StripFrom = CanonName;
				StripFrom.RemoveFromStart(TEXT("SKEL_"),   ESearchCase::CaseSensitive);
				StripFrom.RemoveFromStart(TEXT("REINST_"), ESearchCase::CaseSensitive);
				int32 LastUnderscore = INDEX_NONE;
				if (StripFrom.FindLastChar(TEXT('_'), LastUnderscore) && LastUnderscore > 0)
				{
					const FString Tail = StripFrom.Mid(LastUnderscore + 1);
					bool bAllDigits = !Tail.IsEmpty();
					for (TCHAR C : Tail) if (C < TEXT('0') || C > TEXT('9')) { bAllDigits = false; break; }
					if (bAllDigits) StripFrom.LeftInline(LastUnderscore, EAllowShrinking::No);
				}
				if (StripFrom != CanonName)
				{
					for (TObjectIterator<UClass> It; It; ++It)
					{
						if (It->GetName() == StripFrom && !IsTransientBpClass(*It))
						{ TargetClass = *It; break; }
					}
				}
			}

			if (TargetClass)
			{
				UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(Graph);
				CastNode->TargetType = TargetClass;
				CastNode->CreateNewGuid();
				CastNode->NodePosX = PosX;
				CastNode->NodePosY = PosY;
				CastNode->SetFlags(RF_Transactional);
				Graph->AddNode(CastNode, false, false);
				CastNode->PostPlacedNewNode();
				CastNode->AllocateDefaultPins();
				NewNode = CastNode;
			}
			else
			{
				OutError = FString::Printf(TEXT("Cast target class '%s' not found"), *ClassName);
				return nullptr;
			}
		}

		else if (Handle.StartsWith(TEXT("k2.Get "), ESearchCase::IgnoreCase) && !NewNode)
		{
			FString SubsystemName = Handle.RightChop(7).TrimStartAndEnd();
			FString NormalizedName = SubsystemName.Replace(TEXT(" "), TEXT(""));
			UClass* SubsystemClass = nullptr;
			for (const FString& Prefix : {FString(TEXT("")), FString(TEXT("U"))})
			{
				SubsystemClass = FindFirstObject<UClass>(*(Prefix + NormalizedName), EFindFirstObjectOptions::NativeFirst);
				if (SubsystemClass) break;
			}
			if (!SubsystemClass)
			{
				static const TArray<TPair<FString, FString>> KnownSubsystemPaths = {
					{ TEXT("EnhancedInputLocalPlayerSubsystem"), TEXT("/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem") },
					{ TEXT("EnhancedInputWorldSubsystem"),       TEXT("/Script/EnhancedInput.EnhancedInputWorldSubsystem") },
					{ TEXT("AssetManager"),                      TEXT("/Script/Engine.AssetManager") },
					{ TEXT("EngineSubsystem"),                   TEXT("/Script/Engine.EngineSubsystem") },
					{ TEXT("GameInstanceSubsystem"),             TEXT("/Script/Engine.GameInstanceSubsystem") },
					{ TEXT("LocalPlayerSubsystem"),              TEXT("/Script/Engine.LocalPlayerSubsystem") },
					{ TEXT("WorldSubsystem"),                    TEXT("/Script/Engine.WorldSubsystem") },
				};
				for (const TPair<FString, FString>& Entry : KnownSubsystemPaths)
				{
					if (Entry.Key.Equals(NormalizedName, ESearchCase::IgnoreCase))
					{
						SubsystemClass = FindObject<UClass>(nullptr, *Entry.Value);
						if (!SubsystemClass) SubsystemClass = LoadObject<UClass>(nullptr, *Entry.Value);
						if (SubsystemClass) break;
					}
				}
			}
			if (!SubsystemClass)
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					const FString CName = It->GetName();
					if ((CName == NormalizedName || CName == FString(TEXT("U")) + NormalizedName)
						&& It->IsChildOf(USubsystem::StaticClass()))
					{
						SubsystemClass = *It;
						break;
					}
				}
			}
			if (SubsystemClass)
			{
				UK2Node_GetSubsystem* SubNode = nullptr;
				if (SubsystemClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()))
				{
						UClass* FromPCNodeClass = FindObject<UClass>(nullptr, TEXT("/Script/BlueprintGraph.K2Node_GetSubsystemFromPC"));
						if (!FromPCNodeClass)
						{
								FromPCNodeClass = LoadObject<UClass>(nullptr, TEXT("/Script/BlueprintGraph.K2Node_GetSubsystemFromPC"));
						}
						if (FromPCNodeClass)
						{
								SubNode = Cast<UK2Node_GetSubsystem>(NewObject<UObject>(Graph, FromPCNodeClass));
						}
						else
						{
								SubNode = NewObject<UK2Node_GetSubsystem>(Graph);
						}
				}
				else
				{
						SubNode = NewObject<UK2Node_GetSubsystem>(Graph);
				}
				SubNode->CreateNewGuid();
				SubNode->NodePosX = PosX;
				SubNode->NodePosY = PosY;
				SubNode->SetFlags(RF_Transactional);
				Graph->AddNode(SubNode, false, false);
				SubNode->Initialize(SubsystemClass);
				SubNode->PostPlacedNewNode();
				SubNode->AllocateDefaultPins();
				NewNode = SubNode;
			}
		}

		if (!NewNode)
		{
			FString MacroBaseName;
			if (Handle.StartsWith(TEXT("k2."), ESearchCase::IgnoreCase))
				MacroBaseName = Handle.RightChop(3);
			else if (Handle.StartsWith(TEXT("mc."), ESearchCase::IgnoreCase))
				MacroBaseName = Handle.RightChop(3);

			if (!MacroBaseName.IsEmpty())
			{
				FString NormName = MacroBaseName.Replace(TEXT(" "), TEXT("")).ToLower();

				static TMap<FString, FString> MacroMap;
				if (MacroMap.Num() == 0)
				{
					MacroMap.Add(TEXT("foreachloop"), TEXT("ForEachLoop"));
					MacroMap.Add(TEXT("foreachloopwithbreak"), TEXT("ForEachLoopWithBreak"));
					MacroMap.Add(TEXT("forloop"), TEXT("ForLoop"));
					MacroMap.Add(TEXT("forloopwithbreak"), TEXT("ForLoopWithBreak"));
					MacroMap.Add(TEXT("whileloop"), TEXT("WhileLoop"));
					MacroMap.Add(TEXT("flipflop"), TEXT("FlipFlop"));
					MacroMap.Add(TEXT("doonce"), TEXT("DoOnce"));
					MacroMap.Add(TEXT("don"), TEXT("DoN"));
					MacroMap.Add(TEXT("gate"), TEXT("Gate"));
					MacroMap.Add(TEXT("isvalid"), TEXT("IsValid"));
				}

				FString* MacroGraphName = MacroMap.Find(NormName);
				if (MacroGraphName)
				{
					UBlueprint* MacroBP = LoadObject<UBlueprint>(nullptr,
						TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
					if (MacroBP)
					{
						UEdGraph* MacroGraph = nullptr;
						for (UEdGraph* MG : MacroBP->MacroGraphs)
						{
							if (MG && MG->GetName() == *MacroGraphName)
							{
								MacroGraph = MG;
								break;
							}
						}
						if (MacroGraph)
						{
							UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(Graph);
							MacroNode->SetMacroGraph(MacroGraph);
							MacroNode->NodePosX = PosX;
							MacroNode->NodePosY = PosY;
							MacroNode->SetFlags(RF_Transactional);
							Graph->AddNode(MacroNode, false, false);
							MacroNode->AllocateDefaultPins();
							NewNode = MacroNode;
						}
					}
				}

				if (!NewNode && Handle.StartsWith(TEXT("mc."), ESearchCase::IgnoreCase))
				{
					FString QualLib;
					FString MacroName = MacroBaseName;
					int32 DotIdx;
					if (MacroBaseName.FindChar(TEXT('.'), DotIdx))
					{
						QualLib = MacroBaseName.Left(DotIdx);
						MacroName = MacroBaseName.Mid(DotIdx + 1);
					}

					FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					TArray<FAssetData> BpAssets;
					ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")), BpAssets);

					UEdGraph* FoundMacroGraph = nullptr;
					for (const FAssetData& AD : BpAssets)
					{
						if (!QualLib.IsEmpty() && !AD.AssetName.ToString().Equals(QualLib, ESearchCase::IgnoreCase))
							continue;
						FString BpTypeStr;
						if (AD.GetTagValue(FBlueprintTags::BlueprintType, BpTypeStr)
							&& BpTypeStr != TEXT("BPTYPE_MacroLibrary"))
						{
							continue;
						}
						UBlueprint* CandidateBP = Cast<UBlueprint>(AD.GetAsset());
						if (!CandidateBP || CandidateBP->BlueprintType != BPTYPE_MacroLibrary) continue;
						for (UEdGraph* MG : CandidateBP->MacroGraphs)
						{
							if (MG && MG->GetName().Equals(MacroName, ESearchCase::IgnoreCase))
							{
								FoundMacroGraph = MG;
								break;
							}
						}
						if (FoundMacroGraph) break;
					}
					if (FoundMacroGraph)
					{
						UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(Graph);
						MacroNode->SetMacroGraph(FoundMacroGraph);
						MacroNode->NodePosX = PosX;
						MacroNode->NodePosY = PosY;
						MacroNode->SetFlags(RF_Transactional);
						Graph->AddNode(MacroNode, false, false);
						MacroNode->AllocateDefaultPins();
						NewNode = MacroNode;
					}
				}
			}
		}

		if (!NewNode && Handle.StartsWith(TEXT("k2.Break "), ESearchCase::IgnoreCase))
		{
			FString StructName = Handle.Mid(9).TrimStartAndEnd();
			if (!StructName.IsEmpty())
			{
				FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				TArray<FAssetData> StructAssets;
				ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedStruct")), StructAssets);
				UScriptStruct* FoundStruct = nullptr;
				for (const FAssetData& AD : StructAssets)
				{
					if (AD.AssetName.ToString().Equals(StructName, ESearchCase::IgnoreCase))
					{
						FoundStruct = Cast<UScriptStruct>(AD.GetAsset());
						break;
					}
				}
				if (FoundStruct)
				{
					if (UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(FoundStruct))
						FStructureEditorUtils::CompileStructure(UDS);
					UK2Node_BreakStruct* BreakNode = NewObject<UK2Node_BreakStruct>(Graph);
					BreakNode->StructType = FoundStruct;
					BreakNode->NodePosX = PosX;
					BreakNode->NodePosY = PosY;
					BreakNode->SetFlags(RF_Transactional);
					Graph->AddNode(BreakNode, false, false);
					BreakNode->AllocateDefaultPins();
					NewNode = BreakNode;
				}
			}
		}

		if (!NewNode && Handle.StartsWith(TEXT("k2.Make "), ESearchCase::IgnoreCase))
		{
			FString StructName = Handle.Mid(8).TrimStartAndEnd();
			static const TArray<FString> ExcludedMakeNames = {
				TEXT("Array"), TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"),
				TEXT("Quat"), TEXT("Color"), TEXT("Box"), TEXT("Box2D"),
				TEXT("Vector4"), TEXT("Vector2D"), TEXT("Timespan"), TEXT("DateTime")
			};
			bool bExcluded = false;
			for (const FString& Exc : ExcludedMakeNames)
			{
				if (StructName.Equals(Exc, ESearchCase::IgnoreCase)) { bExcluded = true; break; }
			}
			if (!StructName.IsEmpty() && !bExcluded)
			{
				FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				TArray<FAssetData> StructAssets;
				ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedStruct")), StructAssets);
				UScriptStruct* FoundStruct = nullptr;
				for (const FAssetData& AD : StructAssets)
				{
					if (AD.AssetName.ToString().Equals(StructName, ESearchCase::IgnoreCase))
					{
						FoundStruct = Cast<UScriptStruct>(AD.GetAsset());
						break;
					}
				}
				if (FoundStruct)
				{
					if (UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(FoundStruct))
						FStructureEditorUtils::CompileStructure(UDS);
					UK2Node_MakeStruct* MakeNode = NewObject<UK2Node_MakeStruct>(Graph);
					MakeNode->StructType = FoundStruct;
					MakeNode->NodePosX = PosX;
					MakeNode->NodePosY = PosY;
					MakeNode->SetFlags(RF_Transactional);
					Graph->AddNode(MakeNode, false, false);
					MakeNode->AllocateDefaultPins();
					NewNode = MakeNode;
				}
			}
		}

		if (!NewNode && (Handle.Equals(TEXT("k2.Get Data Table Row"), ESearchCase::IgnoreCase) ||
						 Handle.Equals(TEXT("k2.GetDataTableRow"), ESearchCase::IgnoreCase)))
		{
			UK2Node_GetDataTableRow* DTRowNode = NewObject<UK2Node_GetDataTableRow>(Graph);
			DTRowNode->CreateNewGuid();
			DTRowNode->NodePosX = PosX;
			DTRowNode->NodePosY = PosY;
			DTRowNode->SetFlags(RF_Transactional);
			Graph->AddNode(DTRowNode, false, false);
			DTRowNode->PostPlacedNewNode();
			DTRowNode->AllocateDefaultPins();
			NewNode = DTRowNode;
		}

		if (!NewNode && (Handle.Equals(TEXT("k2.Enum to Name"), ESearchCase::IgnoreCase) ||
						 Handle.Equals(TEXT("k2.EnumToName"), ESearchCase::IgnoreCase) ||
						 Handle.Equals(TEXT("k2.Enum to String"), ESearchCase::IgnoreCase) ||
						 Handle.Equals(TEXT("k2.EnumToString"), ESearchCase::IgnoreCase) ||
						 Handle.StartsWith(TEXT("k2.Enum to "), ESearchCase::IgnoreCase) ||
						 Handle.Equals(TEXT("k2.K2Node_GetEnumeratorName"), ESearchCase::IgnoreCase)))
		{
			UK2Node_GetEnumeratorName* EnumNameNode = NewObject<UK2Node_GetEnumeratorName>(Graph);
			EnumNameNode->CreateNewGuid();
			EnumNameNode->NodePosX = PosX;
			EnumNameNode->NodePosY = PosY;
			EnumNameNode->SetFlags(RF_Transactional);
			Graph->AddNode(EnumNameNode, false, false);
			EnumNameNode->AllocateDefaultPins();
			NewNode = EnumNameNode;
		}

		if (!NewNode && Handle.StartsWith(TEXT("k2.Switch on "), ESearchCase::IgnoreCase))
		{
			FString EnumClassName = Handle.Mid(13).TrimStartAndEnd();
			if (!EnumClassName.IsEmpty())
			{
				UEnum* FoundEnum = FindFirstObject<UEnum>(*EnumClassName, EFindFirstObjectOptions::None);
				if (!FoundEnum)
				{
					if (EnumClassName.StartsWith(TEXT("E_")))
						FoundEnum = FindFirstObject<UEnum>(*EnumClassName.Mid(2), EFindFirstObjectOptions::None);
					if (!FoundEnum && !EnumClassName.StartsWith(TEXT("E")))
						FoundEnum = FindFirstObject<UEnum>(*(TEXT("E_") + EnumClassName), EFindFirstObjectOptions::None);
				}
				if (!FoundEnum)
				{
					FAssetRegistryModule& ARM2 = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					TArray<FAssetData> EnumAssets;
					ARM2.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedEnum")), EnumAssets);
					for (const FAssetData& AD : EnumAssets)
					{
						if (AD.AssetName.ToString().Equals(EnumClassName, ESearchCase::IgnoreCase))
						{
							FoundEnum = Cast<UEnum>(AD.GetAsset());
							break;
						}
					}
				}
				if (FoundEnum)
				{
					UK2Node_SwitchEnum* SwitchNode = NewObject<UK2Node_SwitchEnum>(Graph);
					SwitchNode->Enum = FoundEnum;
					SwitchNode->NodePosX = PosX;
					SwitchNode->NodePosY = PosY;
					SwitchNode->SetFlags(RF_Transactional);
					Graph->AddNode(SwitchNode, false, false);
					SwitchNode->AllocateDefaultPins();
					NewNode = SwitchNode;
				}
			}
		}

		if (!NewNode)
		{
			auto LookupSpawner = [&](const FString& LookupHandle) -> UBlueprintNodeSpawner*
			{
				if (SpawnerCache)
					return FindSpawnerInCache(*SpawnerCache, LookupHandle);
				return FindSpawnerByHandle(Blueprint, LookupHandle);
			};

			UBlueprintNodeSpawner* Spawner = LookupSpawner(Handle);

			if (!Spawner && Handle.StartsWith(TEXT("k2."), ESearchCase::IgnoreCase))
				Spawner = LookupSpawner(TEXT("mc.") + Handle.RightChop(3));
			if (!Spawner && Handle.StartsWith(TEXT("mc."), ESearchCase::IgnoreCase))
				Spawner = LookupSpawner(TEXT("k2.") + Handle.RightChop(3));

			if (!Spawner)
			{
				FString Prefix, BaseName;
				if (Handle.Split(TEXT("."), &Prefix, &BaseName, ESearchCase::IgnoreCase, ESearchDir::FromStart))
				{
					FString SpacedName = CamelCaseToSpaced(BaseName);
					if (SpacedName != BaseName)
					{
						Spawner = LookupSpawner(Prefix + TEXT(".") + SpacedName);
						if (!Spawner) Spawner = LookupSpawner(TEXT("k2.") + SpacedName);
						if (!Spawner) Spawner = LookupSpawner(TEXT("mc.") + SpacedName);
					}
				}
			}

			if (Spawner)
			{
				if (Spawner->NodeClass && Spawner->NodeClass->IsChildOf(UAnimGraphNode_Base::StaticClass())
					&& (!Blueprint || !Blueprint->IsA(UAnimBlueprint::StaticClass())))
				{
					OutError = FString::Printf(
						TEXT("Handle '%s' resolves to an AnimGraph node (%s) which can only be placed on an AnimBlueprint. "
						     "This blueprint is '%s'. Use a non-AnimGraph handle, or skip this node entirely."),
						*Handle,
						*Spawner->NodeClass->GetName(),
						Blueprint ? *Blueprint->GetClass()->GetName() : TEXT("<null>"));
					return nullptr;
				}

				IBlueprintNodeBinder::FBindingSet Bindings;
				NewNode = Spawner->Invoke(Graph, Bindings, FVector2D(PosX, PosY));
				if (NewNode) NewNode->SetFlags(RF_Transactional);
			}
		}

		if (!NewNode)
		{
			if (OutError.IsEmpty())
			{
				const FString HandleLower = Handle.ToLower();
				FString SpecificHint;
				if (HandleLower.StartsWith(TEXT("fn.timelinecomponent.add"))
				 && HandleLower.Contains(TEXT("track")))
				{
					SpecificHint = TEXT("Timeline tracks are NOT graph nodes — they are set on the Timeline template. "
					                    "Use blueprint(action='add_timeline_track', blueprint_path=..., timeline_name=..., "
					                    "track_type='float'|'vector'|'color'|'event', track_name=...) BEFORE referencing the "
					                    "track output pin in build_blueprint_graph.");
				}
				else if (HandleLower.StartsWith(TEXT("fn.timelinecomponent."))
				      && !HandleLower.EndsWith(TEXT(".play")) && !HandleLower.EndsWith(TEXT(".stop"))
				      && !HandleLower.EndsWith(TEXT(".reverse")) && !HandleLower.EndsWith(TEXT(".playfromstart"))
				      && !HandleLower.EndsWith(TEXT(".reversefromend")) && !HandleLower.EndsWith(TEXT(".setplayrate"))
				      && !HandleLower.EndsWith(TEXT(".setnewtime")) && !HandleLower.EndsWith(TEXT(".settimelinelength"))
				      && !HandleLower.EndsWith(TEXT(".isplaying")) && !HandleLower.EndsWith(TEXT(".isreversing"))
				      && !HandleLower.EndsWith(TEXT(".getplayrate")) && !HandleLower.EndsWith(TEXT(".getplaybackposition"))
				      && !HandleLower.EndsWith(TEXT(".gettimelinelength")))
				{
					SpecificHint = TEXT("Most Timeline operations are handled by the Timeline NODE itself (k2.Timeline.<Name>) "
					                    "— its outputs are the track pins. Valid fn.TimelineComponent.* calls: Play, PlayFromStart, "
					                    "Reverse, ReverseFromEnd, Stop, SetPlayRate, SetNewTime, SetTimelineLength, IsPlaying, "
					                    "IsReversing, GetPlayRate, GetPlaybackPosition, GetTimelineLength.");
				}
				else if (HandleLower.Contains(TEXT("bindevent")) ||
				         HandleLower.Contains(TEXT("bindaction")) ||
				         HandleLower.Contains(TEXT(".bindtocomponent")) ||
				         HandleLower.Contains(TEXT(".bindtobegin")) ||
				         HandleLower.Contains(TEXT(".bindtoend")) ||
				         HandleLower.Contains(TEXT(".addbinding")))
				{
					SpecificHint = TEXT("Delegate binding is NOT a function call in Blueprint. For component overlap events "
					                    "(TriggerBox, etc.) use a ComponentBoundEvent: handle 'ev.<ComponentName>.OnComponentBeginOverlap' "
					                    "(or OnComponentEndOverlap / OnClicked / etc.) — this creates a red event node bound to the "
					                    "specific component's delegate. For ACTOR-level overlap (fires for ANY collider on the actor) "
					                    "use 'ev.ReceiveActorBeginOverlap'. The component must have 'Is Variable' enabled (set via "
					                    "blueprint action='set_component_variable' or happens automatically for named components).");
				}
				else if (HandleLower.EndsWith(TEXT(".getrelativerotation")) ||
				         HandleLower.EndsWith(TEXT(".getrelativelocation")) ||
				         HandleLower.EndsWith(TEXT(".getrelativescale3d")) ||
				         HandleLower.EndsWith(TEXT(".k2_getrelativerotation")) ||
				         HandleLower.EndsWith(TEXT(".k2_getrelativelocation")))
				{
					SpecificHint = TEXT("GetRelative{Rotation|Location|Scale3D} is NOT exposed to Blueprint. "
					                    "Only fn.SceneComponent.GetRelativeTransform is callable. "
					                    "Pattern: var.get.<Component> → fn.SceneComponent.GetRelativeTransform.Target → "
					                    "fn.KismetMathLibrary.BreakTransform → .Rotation/.Location/.Scale pin. "
					                    "Or for current world rotation: fn.SceneComponent.K2_GetComponentToWorld → BreakTransform.");
				}
				else if (HandleLower.StartsWith(TEXT("fn.scenecomponent.")) ||
				         HandleLower.StartsWith(TEXT("fn.primitivecomponent.")) ||
				         HandleLower.StartsWith(TEXT("fn.staticmeshcomponent.")) ||
				         HandleLower.StartsWith(TEXT("fn.skeletalmeshcomponent.")))
				{
					SpecificHint = TEXT("If this call fails with 'blueprint (self) is not a SceneComponent, Target must have a connection', "
					                    "wire a var.get.<ComponentName> to the Target pin — the Actor BP itself is not a component. "
					                    "Example: var.get.DoorMesh.DoorMesh → setRelRot.Target.");
				}

				else if (HandleLower == TEXT("k2.enhancedinputaction") ||
				         HandleLower.StartsWith(TEXT("k2.inputaction"), ESearchCase::IgnoreCase) ||
				         HandleLower == TEXT("k2.ia") ||
				         HandleLower == TEXT("ia") ||
				         HandleLower == TEXT("ev.inputaction") ||
				         HandleLower == TEXT("ev.enhancedinputaction") ||
				         HandleLower.StartsWith(TEXT("ev.ia"), ESearchCase::IgnoreCase) ||
				         HandleLower.StartsWith(TEXT("ev.inputaction."), ESearchCase::IgnoreCase))
				{
					SpecificHint = TEXT("Enhanced Input event handle is 'ia.<ActionAssetName>' — pass the InputAction "
					                    "asset name (or full path). Examples: 'ia.IA_Jump', 'ia.IA_Move', "
					                    "'ia./Game/Input/IA_Interact'. The Triggered/Started/Completed exec output pins "
					                    "appear on the node automatically. NOT `ev.*` or `k2.*` — those prefixes don't "
					                    "resolve Enhanced Input.");
				}
				else if (HandleLower.StartsWith(TEXT("k2.enum "), ESearchCase::IgnoreCase) ||
				         HandleLower.StartsWith(TEXT("k2.enum."), ESearchCase::IgnoreCase) ||
				         HandleLower == TEXT("k2.enum"))
				{
					FString EnumName = Handle.Mid(8).TrimStartAndEnd();
					if (EnumName.IsEmpty()) EnumName = TEXT("E_YourEnum");
					SpecificHint = FString::Printf(
						TEXT("There is no 'k2.Enum <X>' literal node. If you want to BRANCH on an enum value, use "
						     "`k2.Switch on %s` (one exec output per enum entry, wire each to its case body). "
						     "If you want to PASS an enum value as data, set it via defaults[{node_id, pin_name, "
						     "value:'%s::SomeValue'}] on the consuming node, or wire `var.get.<EnumVarName>`."),
						*EnumName, *EnumName);
				}
				else if (HandleLower.StartsWith(TEXT("fn.")) && [&]() {
					int32 DotCount = 0;
					for (TCHAR C : HandleLower) if (C == TEXT('.')) ++DotCount;
					return DotCount >= 3;
				}())
				{
					SpecificHint = TEXT("fn.<Class>.<Method> handles take EXACTLY one class and one method segment. "
					                    "You can't chain property access like fn.A.B.C — that's two separate nodes. "
					                    "Example: instead of 'fn.PlayerController.PlayerCameraManager.GetCameraLocation', use TWO nodes — "
					                    "(1) 'var.get.PlayerCameraManager' (or fn.PlayerController.K2_GetPlayerCameraManager), then "
					                    "(2) 'fn.PlayerCameraManager.GetCameraLocation' with the first node's output wired to its Target pin. "
					                    "Or use 'fn.PlayerController.GetPlayerViewPoint' which returns Location+Rotation in one call.");
				}

				if (SpecificHint.IsEmpty())
				{
					if (HandleLower == TEXT("var.get") || HandleLower == TEXT("var.set") ||
					    HandleLower == TEXT("get_variable") || HandleLower == TEXT("set_variable"))
						SpecificHint = TEXT("Missing variable name — format is 'var.get.VarName' / 'var.set.VarName' "
						                    "(e.g. 'var.get.CurrentHealth'). Names are case-sensitive. "
						                    "Use get_blueprint_skeleton(blueprint_path) to list variable names.");
					else if (HandleLower == TEXT("comment") || HandleLower == TEXT("k2.comment") ||
					         HandleLower == TEXT("k2.Comment"))
						SpecificHint = TEXT("Comments are NOT placed via nodes[] — they're a separate top-level "
						                    "parameter. Use comments=[{text:'My note', node_ids:['n1','n2'], "
						                    "color:{r:0.2,g:0.4,b:0.2,a:0.5}}] inside build_blueprint_graph, or "
						                    "blueprint(action='add_blueprint_comment', text='...', x=..., y=...) "
						                    "for a standalone comment.");
					else if (HandleLower == TEXT("entry") || HandleLower == TEXT("return"))
						SpecificHint = TEXT("'entry' and 'return' are IMPLICIT in function graphs — never declare them "
						                    "in nodes[]. Reference them directly in connections[] using the literal id "
						                    "'entry' / 'return' (e.g. {from:'entry',from_pin:'then',to:'n1',to_pin:'execute'} "
						                    "or {from:'lastNode',from_pin:'then',to:'return',to_pin:'execute'}). "
						                    "In an EventGraph there is no 'entry' / 'return' — wire from your event node directly.");
				}
				OutError = SpecificHint.IsEmpty()
					? FString::Printf(TEXT("Failed to create node for handle '%s'. Use discover_nodes to find the correct handle."), *Handle)
					: FString::Printf(TEXT("Failed to create node for handle '%s'. %s"), *Handle, *SpecificHint);
			}
		}

		if (NewNode && !NewNode->NodeGuid.IsValid())
		{
			NewNode->NodeGuid = FGuid::NewGuid();
		}

		return NewNode;
	}
}

struct FSpawnerSnapshotEntry
{
	FString Handle;
	FString Name;
	FString NameLower;
	FString Category;
	bool    bIsPure = false;
};

namespace
{
	struct FSpawnerSnapshotCache
	{
		TArray<FSpawnerSnapshotEntry> Entries;
		bool bValid = false;
		bool bDelegatesRegistered = false;
		FDelegateHandle EntryUpdatedHandle;
		FDelegateHandle EntryRemovedHandle;
	};

	static FSpawnerSnapshotCache GSpawnerSnapshot;

	static void InvalidateSpawnerSnapshot(UObject* )
	{
		GSpawnerSnapshot.bValid = false;
		GSpawnerSnapshot.Entries.Reset();
	}

	static void BuildSpawnerSnapshot()
	{
		check(IsInGameThread());

		GSpawnerSnapshot.Entries.Reset();

		FBlueprintActionDatabase& ActionDB = FBlueprintActionDatabase::Get();
		const FBlueprintActionDatabase::FActionRegistry& AllActions = ActionDB.GetAllActions();

		GSpawnerSnapshot.Entries.Reserve(8192);

		for (const auto& ActionPair : AllActions)
		{
			for (UBlueprintNodeSpawner* Spawner : ActionPair.Value)
			{
				if (!Spawner || !IsValid(Spawner) || !Spawner->NodeClass) continue;

				FString NodeName = Spawner->DefaultMenuSignature.MenuName.ToString();
				if (NodeName.IsEmpty())
				{
					if (const UBlueprintFunctionNodeSpawner* FuncSpawner = Cast<UBlueprintFunctionNodeSpawner>(Spawner))
					{
						if (const UFunction* Func = FuncSpawner->GetFunction())
						{
							NodeName = Func->GetDisplayNameText().ToString();
							if (NodeName.IsEmpty()) NodeName = Func->GetName();
						}
					}
					else if (const UBlueprintEventNodeSpawner* EventSpawner = Cast<UBlueprintEventNodeSpawner>(Spawner))
					{
						if (const UFunction* EventFunc = EventSpawner->GetEventFunction())
						{
							NodeName = EventFunc->GetDisplayNameText().ToString();
							if (NodeName.IsEmpty()) NodeName = EventFunc->GetName();
						}
					}
				}
				if (NodeName.IsEmpty()) NodeName = Spawner->NodeClass->GetName();

				FString Handle = GraphEditHelpers::BuildHandle(Spawner);
				if (Handle.IsEmpty()) continue;

				bool bIsPure = false;
				if (Spawner->NodeClass->IsChildOf(UK2Node::StaticClass()))
				{
					if (const UK2Node* CDO = Spawner->NodeClass->GetDefaultObject<UK2Node>())
						bIsPure = CDO->IsNodePure();
				}
				if (const UBlueprintFunctionNodeSpawner* FuncSpawner = Cast<UBlueprintFunctionNodeSpawner>(Spawner))
				{
					if (const UFunction* Func = FuncSpawner->GetFunction())
						bIsPure = Func->HasAnyFunctionFlags(FUNC_BlueprintPure);
				}

				FSpawnerSnapshotEntry E;
				E.Handle    = MoveTemp(Handle);
				E.NameLower = NodeName.ToLower();
				E.Name      = MoveTemp(NodeName);
				E.Category  = Spawner->DefaultMenuSignature.Category.ToString();
				E.bIsPure   = bIsPure;
				GSpawnerSnapshot.Entries.Add(MoveTemp(E));
			}
		}

		if (!GSpawnerSnapshot.bDelegatesRegistered)
		{
			GSpawnerSnapshot.EntryUpdatedHandle =
				ActionDB.OnEntryUpdated().AddStatic(&InvalidateSpawnerSnapshot);
			GSpawnerSnapshot.EntryRemovedHandle =
				ActionDB.OnEntryRemoved().AddStatic(&InvalidateSpawnerSnapshot);
			GSpawnerSnapshot.bDelegatesRegistered = true;
		}

		GSpawnerSnapshot.bValid = true;
	}

	static const TArray<FSpawnerSnapshotEntry>& GetSpawnerSnapshot()
	{
		check(IsInGameThread());
		if (!GSpawnerSnapshot.bValid)
		{
			BuildSpawnerSnapshot();
		}
		return GSpawnerSnapshot.Entries;
	}
}

namespace BlueprintGraphTools
{

static bool IsClearProtectedNode(UEdGraphNode* Node)
{
	if (!IsValid(Node)) return false;
	if (Node->IsA<UK2Node_FunctionEntry>())  return true;
	if (Node->IsA<UK2Node_FunctionResult>()) return true;
	if (Node->IsA<UEdGraphNode_Comment>())   return true;
	if (Node->IsA<UK2Node_Timeline>())       return true;
	bool bHasExecIn = false, bHasExecOut = false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
		if (Pin->Direction == EGPD_Input)  bHasExecIn  = true;
		else                               bHasExecOut = true;
	}
	if (!bHasExecIn && bHasExecOut) return true;
	return false;
}

static FString StripBlueprintClassDecorations(FString Name)
{
	Name.RemoveFromStart(TEXT("SKEL_"),   ESearchCase::CaseSensitive);
	Name.RemoveFromStart(TEXT("REINST_"), ESearchCase::CaseSensitive);
	Name.RemoveFromEnd  (TEXT("_C"),      ESearchCase::CaseSensitive);
	int32 Underscore = INDEX_NONE;
	if (Name.FindLastChar(TEXT('_'), Underscore) && Underscore > 0)
	{
		const FString Tail = Name.Mid(Underscore + 1);
		bool bAllDigits = !Tail.IsEmpty();
		for (TCHAR C : Tail) if (C < TEXT('0') || C > TEXT('9')) { bAllDigits = false; break; }
		if (bAllDigits) Name.LeftInline(Underscore, EAllowShrinking::No);
	}
	return Name;
}

static bool TryConnectWithSkelBypass(UEdGraphPin* FromPin, UEdGraphPin* ToPin,
	const UEdGraphSchema* Schema, const FPinConnectionResponse& Response,
	bool* OutBypassUsed = nullptr)
{
	if (Response.Response != CONNECT_RESPONSE_DISALLOW) return false;
	if (!FromPin || !ToPin) return false;
	if (FromPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object) return false;
	if (ToPin  ->PinType.PinCategory != UEdGraphSchema_K2::PC_Object) return false;
	if (!FromPin->PinType.PinSubCategoryObject.IsValid()) return false;
	if (!ToPin  ->PinType.PinSubCategoryObject.IsValid()) return false;

	const FString FromBase = StripBlueprintClassDecorations(FromPin->PinType.PinSubCategoryObject->GetName());
	const FString ToBase   = StripBlueprintClassDecorations(ToPin  ->PinType.PinSubCategoryObject->GetName());
	if (!FromBase.Equals(ToBase, ESearchCase::IgnoreCase)) return false;

	auto IsTransient = [](const UClass* C) -> bool
	{
		if (!C) return false;
		const FString N = C->GetName();
		return N.StartsWith(TEXT("SKEL_"), ESearchCase::CaseSensitive)
			|| N.StartsWith(TEXT("REINST_"), ESearchCase::CaseSensitive)
			|| N.StartsWith(TEXT("TRASHCLASS_"), ESearchCase::CaseSensitive);
	};
	UClass* FromCls = Cast<UClass>(FromPin->PinType.PinSubCategoryObject.Get());
	UClass* ToCls   = Cast<UClass>(ToPin  ->PinType.PinSubCategoryObject.Get());
	UClass* Canon = nullptr;
	if (FromCls && !IsTransient(FromCls)) Canon = FromCls;
	else if (ToCls && !IsTransient(ToCls)) Canon = ToCls;
	else
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString N = It->GetName();
			if (StripBlueprintClassDecorations(N).Equals(FromBase, ESearchCase::IgnoreCase)
				&& !IsTransient(*It))
			{
				Canon = *It;
				break;
			}
		}
		if (!Canon) Canon = FromCls ? FromCls : ToCls;
	}
	if (Canon)
	{
		FromPin->PinType.PinSubCategoryObject = Canon;
		ToPin  ->PinType.PinSubCategoryObject = Canon;
	}

	FromPin->MakeLinkTo(ToPin);
	const bool bConnected = FromPin->LinkedTo.Contains(ToPin);
	if (bConnected && OutBypassUsed) *OutBypassUsed = true;
	return bConnected;
}

void HandleDiscoverNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);

	TArray<FString> Queries;
	const TArray<TSharedPtr<FJsonValue>>* QueriesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("queries"), QueriesArray) && QueriesArray && QueriesArray->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& QV : *QueriesArray)
		{
			FString QStr = QV->AsString();
			if (!QStr.IsEmpty()) Queries.Add(QStr);
		}
	}
	if (Queries.IsEmpty())
	{
		FString SingleQuery;
		if (!Args->TryGetStringField(TEXT("query"), SingleQuery) || SingleQuery.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: query (or queries array)");
			return;
		}
		Queries.Add(SingleQuery);
	}

	auto CamelSplit = [](const FString& In) -> FString
	{
		if (In.Contains(TEXT(" "))) return In;
		FString Out;
		Out.Reserve(In.Len() + 8);
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (i > 0 && FChar::IsUpper(C) && (FChar::IsLower(In[i - 1]) || FChar::IsDigit(In[i - 1])))
				Out.AppendChar(TEXT(' '));
			Out.AppendChar(C);
		}
		return Out;
	};
	for (FString& Q : Queries) Q = CamelSplit(Q);

	bool bIncludePins = false;
	Args->TryGetBoolField(TEXT("include_pins"), bIncludePins);

	int32 ConfiguredLimit = 5;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("DiscoverNodesLimit"), ConfiguredLimit, FSettingsManager::GetGlobalConfigPath());
	if (ConfiguredLimit < 1) ConfiguredLimit = 5;
	int32 MaxResults = bIncludePins ? 3 : ConfiguredLimit;
	if (Args->HasField(TEXT("max_results")))
	{
		MaxResults = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_results")), 1, 50);
	}

	UBlueprint* Blueprint = nullptr;
	if (!BlueprintPath.IsEmpty())
	{
		Blueprint = LoadBlueprintFromPath(BlueprintPath);
		if (!Blueprint)
		{
			if (UObject* Asset = UEditorAssetLibrary::LoadAsset(BlueprintPath))
			{
				OutError = FString::Printf(
					TEXT("Asset at '%s' is a %s, not a Blueprint. discover_nodes scope=BP only works on UBlueprint. Drop blueprint_path for a global catalogue search."),
					*BlueprintPath, *Asset->GetClass()->GetName());
			}
			else
			{
				OutError = FString::Printf(TEXT("Blueprint not found: %s. Drop blueprint_path for global catalogue search."), *BlueprintPath);
			}
			return;
		}
	}

	bool bBatchMode = Queries.Num() > 1;
	FString SingleQuery = Queries[0];

	struct FNodeMatch
	{
		float Score;
		FString Handle;
		FString Name;
		FString Category;
		bool bIsPure;
		UBlueprintNodeSpawner* Spawner;
	};

	if (bBatchMode)
	{
		struct FQueryState
		{
			FString Q;
			FString QLower;
			TArray<FString> WordsLower;
			TArray<FNodeMatch> Matches;
			TSet<FString>     SeenHandles;
		};
		TArray<FQueryState> States;
		States.Reserve(Queries.Num());
		for (const FString& Q : Queries)
		{
			FQueryState S; S.Q = Q; S.QLower = Q.ToLower();
			FString QNorm = S.QLower;
			QNorm.ReplaceInline(TEXT("."), TEXT(" "));
			QNorm.ReplaceInline(TEXT("_"), TEXT(" "));
			QNorm.ParseIntoArray(S.WordsLower, TEXT(" "), true);
			S.WordsLower.RemoveAll([](const FString& W) { return W.Len() < 3; });
			States.Add(MoveTemp(S));
		}

		auto FilterFor = [](const FQueryState& S, const FString& NameLower) -> bool
		{
			if (NameLower.IsEmpty()) return true;
			if (NameLower.Contains(S.QLower)) return true;
			if (S.QLower.Contains(NameLower) && NameLower.Len() >= 3) return true;
			for (const FString& W : S.WordsLower)
			{
				if (NameLower.Contains(W)) return true;
			}
			return false;
		};

		if (Blueprint)
		{
			FBlueprintActionDatabase::Get().RefreshAssetActions(Blueprint);
		}

		const TArray<FSpawnerSnapshotEntry>& Snapshot = GetSpawnerSnapshot();

		for (const FSpawnerSnapshotEntry& Entry : Snapshot)
		{
			bool bAnyPasses = false;
			for (const FQueryState& S : States)
			{
				if (FilterFor(S, Entry.NameLower)) { bAnyPasses = true; break; }
			}
			if (!bAnyPasses) continue;

			if (Blueprint && (Entry.Handle.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase)
				|| Entry.Handle.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase)))
			{
				const FString VarName = Entry.Handle.RightChop(8);
				const FName VarFName(*VarName);
				bool bOnSelf = false;
				for (const FBPVariableDescription& Var : Blueprint->NewVariables)
				{
					if (Var.VarName == VarFName) { bOnSelf = true; break; }
				}
				if (!bOnSelf && Blueprint->GeneratedClass
					&& FindFProperty<FProperty>(Blueprint->GeneratedClass, VarFName))
				{
					bOnSelf = true;
				}
				if (!bOnSelf && Blueprint->SkeletonGeneratedClass
					&& FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, VarFName))
				{
					bOnSelf = true;
				}
				if (!bOnSelf) continue;
			}

			for (FQueryState& S : States)
			{
				if (S.SeenHandles.Contains(Entry.Handle)) continue;
				if (!FilterFor(S, Entry.NameLower)) continue;

				float Score = GraphEditHelpers::ScoreMatch(S.Q, Entry.Name);
				float HandleScore = GraphEditHelpers::ScoreMatch(S.Q, Entry.Handle);
				Score = FMath::Max(Score, HandleScore);
				if (Score < 0.2f) continue;

				S.SeenHandles.Add(Entry.Handle);
				FNodeMatch M;
				M.Score    = Score;
				M.Handle   = Entry.Handle;
				M.Name     = Entry.Name;
				M.Category = Entry.Category;
				M.bIsPure  = Entry.bIsPure;
				M.Spawner  = nullptr;
				S.Matches.Add(M);
			}
		}

		TArray<TSharedPtr<FJsonValue>> BatchResultsArr;
		for (FQueryState& S : States)
		{
			TSharedPtr<FJsonObject> SubArgs = MakeShared<FJsonObject>();
			SubArgs->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			SubArgs->SetStringField(TEXT("query"), S.Q);
			SubArgs->SetNumberField(TEXT("max_results"), FMath::Max(MaxResults * 2, 20));
			SubArgs->SetBoolField(TEXT("_uecp_skip_main_pass"), true);
			if (bIncludePins) SubArgs->SetBoolField(TEXT("include_pins"), true);
			FString SubOut, SubErr;
			HandleDiscoverNodes(SubArgs, SubOut, SubErr);

			TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
			EntryObj->SetStringField(TEXT("query"), S.Q);
			if (!SubErr.IsEmpty())
			{
				EntryObj->SetStringField(TEXT("error"), SubErr);
				BatchResultsArr.Add(MakeShared<FJsonValueObject>(EntryObj));
				continue;
			}

			TSharedPtr<FJsonObject> ParsedSub;
			TSharedRef<TJsonReader<>> SubReader = TJsonReaderFactory<>::Create(SubOut);
			if (FJsonSerializer::Deserialize(SubReader, ParsedSub) && ParsedSub.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* SubNodes = nullptr;
				if (ParsedSub->TryGetArrayField(TEXT("nodes"), SubNodes) && SubNodes)
				{
					for (const TSharedPtr<FJsonValue>& Val : *SubNodes)
					{
						TSharedPtr<FJsonObject> Row = SafeAsObject(Val);
						if (!Row.IsValid()) continue;
						FString H, N, Cat;
						bool bP = false;
						Row->TryGetStringField(TEXT("handle"), H);
						Row->TryGetStringField(TEXT("name"), N);
						Row->TryGetStringField(TEXT("category"), Cat);
						Row->TryGetBoolField(TEXT("is_pure"), bP);
						if (H.IsEmpty() || S.SeenHandles.Contains(H)) continue;
						S.SeenHandles.Add(H);
						FNodeMatch M;
						M.Score    = 0.8f;
						M.Handle   = H;
						M.Name     = N;
						M.Category = Cat;
						M.bIsPure  = bP;
						M.Spawner  = nullptr;
						S.Matches.Add(M);
					}
				}
			}

			if (Blueprint && Blueprint->GeneratedClass)
			{
				const FString SelfClassPrefix = FString::Printf(TEXT("fn.%s."), *Blueprint->GeneratedClass->GetName());
				for (FNodeMatch& M : S.Matches)
				{
					const bool bIsBPScoped =
						M.Handle.StartsWith(TEXT("var.get."))      ||
						M.Handle.StartsWith(TEXT("var.set."))      ||
						M.Handle.StartsWith(TEXT("prop.get."))     ||
						M.Handle.StartsWith(TEXT("prop.set."))     ||
						M.Handle.StartsWith(TEXT("ev.Dispatcher")) ||
						M.Handle.StartsWith(SelfClassPrefix);
					if (bIsBPScoped) M.Score += 0.15f;
				}
			}

			S.Matches.Sort([](const FNodeMatch& A, const FNodeMatch& B)
			{
				if (FMath::Abs(A.Score - B.Score) > 0.001f) return A.Score > B.Score;
				auto IsStdlib = [](const FString& H)
				{
					return H.Contains(TEXT("KismetMathLibrary")) || H.Contains(TEXT("KismetSystemLibrary"))
						|| H.Contains(TEXT("KismetStringLibrary")) || H.Contains(TEXT("KismetArrayLibrary"));
				};
				bool bAStd = IsStdlib(A.Handle), bBStd = IsStdlib(B.Handle);
				if (bAStd != bBStd) return bAStd;
				return A.Name.Len() < B.Name.Len();
			});
			if (S.Matches.Num() > MaxResults) S.Matches.SetNum(MaxResults);

			TArray<TSharedPtr<FJsonValue>> NodesArr;
			for (const FNodeMatch& M : S.Matches)
			{
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("handle"), M.Handle);
				Row->SetStringField(TEXT("name"), M.Name);
				if (!M.Category.IsEmpty()) Row->SetStringField(TEXT("category"), M.Category);
				Row->SetBoolField(TEXT("is_pure"), M.bIsPure);
				NodesArr.Add(MakeShared<FJsonValueObject>(Row));
			}
			EntryObj->SetArrayField(TEXT("nodes"), NodesArr);
			EntryObj->SetNumberField(TEXT("count"), NodesArr.Num());
			BatchResultsArr.Add(MakeShared<FJsonValueObject>(EntryObj));
		}
		TSharedPtr<FJsonObject> BatchResult = MakeShared<FJsonObject>();
		BatchResult->SetStringField(TEXT("blueprint"), BlueprintPath);
		BatchResult->SetNumberField(TEXT("query_count"), Queries.Num());
		BatchResult->SetArrayField(TEXT("results"), BatchResultsArr);
		OutJsonString = GraphEditHelpers::JsonToString(BatchResult);
		return;
	}

	FString Query = SingleQuery;

	const FString QueryLower = Query.ToLower();
	TArray<FString> QueryWordsLower;
	{
		FString QNorm = QueryLower;
		QNorm.ReplaceInline(TEXT("."), TEXT(" "));
		QNorm.ReplaceInline(TEXT("_"), TEXT(" "));
		QNorm.ParseIntoArray(QueryWordsLower, TEXT(" "), true);
		QueryWordsLower.RemoveAll([](const FString& W) { return W.Len() < 3; });
	}
	auto CheapNameFilter = [&](const FString& NameLower) -> bool
	{
		if (NameLower.IsEmpty()) return true;
		if (NameLower.Contains(QueryLower)) return true;
		if (QueryLower.Contains(NameLower) && NameLower.Len() >= 3) return true;
		for (const FString& QW : QueryWordsLower)
		{
			if (NameLower.Contains(QW)) return true;
		}
		return false;
	};

	TArray<FNodeMatch> Matches;
	TSet<FString> SeenHandles;

	bool bSkipMainPass = false;
	Args->TryGetBoolField(TEXT("_uecp_skip_main_pass"), bSkipMainPass);

	if (Blueprint)
	{
		FBlueprintActionDatabase& ActionDB = FBlueprintActionDatabase::Get();
		if (IsInGameThread())
		{
			ActionDB.RefreshAssetActions(Blueprint);
		}
		else
		{
			UBlueprint* BlueprintCopy = Blueprint;
			FEvent* Done = FPlatformProcess::GetSynchEventFromPool();
			AsyncTask(ENamedThreads::GameThread, [&ActionDB, BlueprintCopy, Done]()
			{
				ActionDB.RefreshAssetActions(BlueprintCopy);
				Done->Trigger();
			});
			Done->Wait();
			FPlatformProcess::ReturnSynchEventToPool(Done);
		}
	}

	if (!bSkipMainPass)
	{
		const TArray<FSpawnerSnapshotEntry>& Snapshot = GetSpawnerSnapshot();
		for (const FSpawnerSnapshotEntry& Entry : Snapshot)
		{
			if (!CheapNameFilter(Entry.NameLower))
			{
				continue;
			}

			if (Blueprint && (Entry.Handle.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase)
				|| Entry.Handle.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase)))
			{
				const FString VarName = Entry.Handle.RightChop(8);
				const FName VarFName(*VarName);
				bool bOnSelf = false;
				for (const FBPVariableDescription& Var : Blueprint->NewVariables)
				{
					if (Var.VarName == VarFName) { bOnSelf = true; break; }
				}
				if (!bOnSelf && Blueprint->GeneratedClass
					&& FindFProperty<FProperty>(Blueprint->GeneratedClass, VarFName))
				{
					bOnSelf = true;
				}
				if (!bOnSelf && Blueprint->SkeletonGeneratedClass
					&& FindFProperty<FProperty>(Blueprint->SkeletonGeneratedClass, VarFName))
				{
					bOnSelf = true;
				}
				if (!bOnSelf) continue;
			}

			if (SeenHandles.Contains(Entry.Handle)) continue;

			float Score = GraphEditHelpers::ScoreMatch(Query, Entry.Name);
			float HandleScore = GraphEditHelpers::ScoreMatch(Query, Entry.Handle);
			Score = FMath::Max(Score, HandleScore);
			if (Score < 0.2f) continue;

			SeenHandles.Add(Entry.Handle);

			FNodeMatch Match;
			Match.Score    = Score;
			Match.Handle   = Entry.Handle;
			Match.Name     = Entry.Name;
			Match.Category = Entry.Category;
			Match.bIsPure  = Entry.bIsPure;
			Match.Spawner  = nullptr;
			Matches.Add(Match);
		}
	}

	auto SearchClassProperties = [&](UClass* CompClass)
	{
		if (!CompClass) return;
		FString CompClassName = CompClass->GetName();
		for (TFieldIterator<FProperty> PropIt(CompClass); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop->HasAnyPropertyFlags(CPF_BlueprintVisible)) continue;
			FString PropName = Prop->GetName();
			float Score = GraphEditHelpers::ScoreMatch(Query, PropName);
			if (Score < 0.2f) continue;

			bool bReadOnly = !Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly) || Prop->HasAnyPropertyFlags(CPF_Edit);
			FString GetHandle = FString::Printf(TEXT("prop.get.%s.%s"), *CompClassName, *PropName);
			if (!SeenHandles.Contains(GetHandle))
			{
				SeenHandles.Add(GetHandle);
				FNodeMatch M; M.Score = Score * 0.9f; M.Handle = GetHandle;
				M.Name = FString::Printf(TEXT("Get %s (%s)"), *PropName, *CompClassName);
				M.Category = TEXT("Component Properties"); M.bIsPure = true; M.Spawner = nullptr;
				Matches.Add(M);
			}
			if (Prop->HasAllPropertyFlags(CPF_BlueprintVisible) && !Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
			{
				FString SetHandle = FString::Printf(TEXT("prop.set.%s.%s"), *CompClassName, *PropName);
				if (!SeenHandles.Contains(SetHandle))
				{
					SeenHandles.Add(SetHandle);
					FNodeMatch M; M.Score = Score * 0.95f; M.Handle = SetHandle;
					M.Name = FString::Printf(TEXT("Set %s (%s)"), *PropName, *CompClassName);
					M.Category = TEXT("Component Properties"); M.bIsPure = false; M.Spawner = nullptr;
					Matches.Add(M);
				}
			}
		}
	};

	if (Blueprint)
	{
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* SCSNode : SCS->GetAllNodes())
		{
			if (SCSNode && SCSNode->ComponentClass)
				SearchClassProperties(SCSNode->ComponentClass);
		}
	}
	if (Blueprint->ParentClass)
	{
		TArray<UObject*> DefaultSubobjects;
		Blueprint->ParentClass->GetDefaultObject()->GetDefaultSubobjects(DefaultSubobjects);
		for (UObject* Sub : DefaultSubobjects)
		{
			if (UActorComponent* Comp = Cast<UActorComponent>(Sub))
				SearchClassProperties(Comp->GetClass());
		}
	}

	if (Blueprint->SkeletonGeneratedClass)
	{
		for (TFieldIterator<FMulticastDelegateProperty> PropIt(Blueprint->SkeletonGeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
		{
			FMulticastDelegateProperty* Prop = *PropIt;
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintAssignable)) continue;
			FString DispName = Prop->GetName();

			float Score = GraphEditHelpers::ScoreMatch(Query, DispName);
			if (Score < 0.2f) continue;

			static const TTuple<const TCHAR*, const TCHAR*> DispHandles[] = {
				{ TEXT("ev.Dispatcher."),    TEXT("Call/broadcast — fires dispatcher to all bound delegates") },
				{ TEXT("ev.DispatcherBind."),    TEXT("Bind — register a delegate to this dispatcher") },
				{ TEXT("ev.DispatcherUnbind."),  TEXT("Unbind — unregister a delegate") },
				{ TEXT("ev.DispatcherAssign."),  TEXT("Assign — create inline event bound to this dispatcher") },
			};
			float Priority = Score;
			for (const auto& HT : DispHandles)
			{
				FString Handle = FString(HT.Get<0>()) + DispName;
				if (!SeenHandles.Contains(Handle))
				{
					SeenHandles.Add(Handle);
					FNodeMatch M;
					M.Score = Priority;
					M.Handle = Handle;
					M.Name = FString::Printf(TEXT("%s%s (%s)"), HT.Get<0>(), *DispName, HT.Get<1>());
					M.Category = TEXT("Event Dispatchers");
					M.bIsPure = false;
					M.Spawner = nullptr;
					Matches.Add(M);
				}
				Priority -= 0.01f;
			}
		}
	}

	{
		auto AddVarHandles = [&](const FString& VarName, float BaseScore)
		{
			FString GetHandle = FString::Printf(TEXT("var.get.%s"), *VarName);
			if (!SeenHandles.Contains(GetHandle))
			{
				SeenHandles.Add(GetHandle);
				FNodeMatch M; M.Score = BaseScore + 0.05f; M.Handle = GetHandle;
				M.Name = FString::Printf(TEXT("Get %s (variable)"), *VarName);
				M.Category = TEXT("Variables"); M.bIsPure = true; M.Spawner = nullptr;
				Matches.Add(M);
			}
			FString SetHandle = FString::Printf(TEXT("var.set.%s"), *VarName);
			if (!SeenHandles.Contains(SetHandle))
			{
				SeenHandles.Add(SetHandle);
				FNodeMatch M; M.Score = BaseScore; M.Handle = SetHandle;
				M.Name = FString::Printf(TEXT("Set %s (variable)"), *VarName);
				M.Category = TEXT("Variables"); M.bIsPure = false; M.Spawner = nullptr;
				Matches.Add(M);
			}
		};

		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			const FString VarName = Var.VarName.ToString();
			float Score = GraphEditHelpers::ScoreMatch(Query, VarName);
			if (Score >= 0.2f) AddVarHandles(VarName, Score);
		}
		if (Blueprint->SkeletonGeneratedClass)
		{
			for (TFieldIterator<FProperty> PropIt(Blueprint->SkeletonGeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop->HasAnyPropertyFlags(CPF_BlueprintVisible)) continue;
				if (CastField<FMulticastDelegateProperty>(Prop)) continue;
				const FString VarName = Prop->GetName();
				float Score = GraphEditHelpers::ScoreMatch(Query, VarName);
				if (Score >= 0.2f) AddVarHandles(VarName, Score * 0.9f);
			}
		}
	}
	}

	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> BpAssets;
		ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")), BpAssets);
		for (const FAssetData& AD : BpAssets)
		{
			FString BpTypeStr;
			if (AD.GetTagValue(FBlueprintTags::BlueprintType, BpTypeStr)
				&& BpTypeStr != TEXT("BPTYPE_MacroLibrary"))
			{
				continue;
			}
			UBlueprint* CandidateBP = Cast<UBlueprint>(AD.GetAsset());
			if (!CandidateBP || CandidateBP->BlueprintType != BPTYPE_MacroLibrary) continue;
			const FString LibName = CandidateBP->GetName();
			for (UEdGraph* MG : CandidateBP->MacroGraphs)
			{
				if (!MG) continue;
				const FString MacroName = MG->GetName();
				const float NameScore = GraphEditHelpers::ScoreMatch(Query, MacroName);
				const float QualScore = GraphEditHelpers::ScoreMatch(Query, FString::Printf(TEXT("%s.%s"), *LibName, *MacroName));
				const float Score = FMath::Max(NameScore, QualScore);
				if (Score < 0.2f) continue;
				const FString BareHandle = FString::Printf(TEXT("mc.%s"), *MacroName);
				const FString QualHandle = FString::Printf(TEXT("mc.%s.%s"), *LibName, *MacroName);
				if (!SeenHandles.Contains(BareHandle))
				{
					SeenHandles.Add(BareHandle);
					FNodeMatch M; M.Score = Score; M.Handle = BareHandle;
					M.Name = FString::Printf(TEXT("%s (macro from %s)"), *MacroName, *LibName);
					M.Category = FString::Printf(TEXT("Macro|%s"), *LibName);
					M.bIsPure = false; M.Spawner = nullptr;
					Matches.Add(M);
				}
				if (!SeenHandles.Contains(QualHandle))
				{
					SeenHandles.Add(QualHandle);
					FNodeMatch M; M.Score = Score - 0.05f; M.Handle = QualHandle;
					M.Name = FString::Printf(TEXT("%s (qualified)"), *MacroName);
					M.Category = FString::Printf(TEXT("Macro|%s"), *LibName);
					M.bIsPure = false; M.Spawner = nullptr;
					Matches.Add(M);
				}
			}
		}
	}

	{
		const FString QLower = Query.ToLower();
		const bool bWantsDebugKey = QLower.Contains(TEXT("debug key")) || QLower.Contains(TEXT("debugkey"));
		const bool bWantsInputKey = !bWantsDebugKey && (
			QLower == TEXT("input key") || QLower == TEXT("inputkey") ||
			QLower == TEXT("keyboard") || QLower == TEXT("keyboard event") ||
			QLower.StartsWith(TEXT("key ")) || QLower.EndsWith(TEXT(" key")) || QLower == TEXT("key"));
		if (bWantsDebugKey || bWantsInputKey)
		{
			static const TArray<FString> CommonKeys = {
				TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F"), TEXT("G"),
				TEXT("H"), TEXT("I"), TEXT("J"), TEXT("K"), TEXT("L"), TEXT("M"), TEXT("N"),
				TEXT("O"), TEXT("P"), TEXT("Q"), TEXT("R"), TEXT("S"), TEXT("T"), TEXT("U"),
				TEXT("V"), TEXT("W"), TEXT("X"), TEXT("Y"), TEXT("Z"),
				TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"),
				TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"),
				TEXT("SpaceBar"), TEXT("Tab"), TEXT("Enter"), TEXT("Escape"),
				TEXT("LeftShift"), TEXT("LeftControl"), TEXT("LeftAlt"),
				TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("MiddleMouseButton")
			};
			const FString Prefix = bWantsDebugKey ? TEXT("k2.Debug Key ") : TEXT("k2.");
			const FString Category = bWantsDebugKey ? TEXT("Input|Debug Events|Keyboard Events") : TEXT("Input|Keyboard Events");
			for (const FString& K : CommonKeys)
			{
				const FString Handle = Prefix + K;
				if (SeenHandles.Contains(Handle)) continue;
				SeenHandles.Add(Handle);
				FNodeMatch M;
				M.Score = 0.85f;
				M.Handle = Handle;
				M.Name = bWantsDebugKey ? FString::Printf(TEXT("Debug Key %s"), *K) : K;
				M.Category = Category;
				M.bIsPure = false;
				M.Spawner = nullptr;
				Matches.Add(M);
			}
		}
	}

	{
		const bool bLooksLikeExactFuncName =
			!Query.IsEmpty() && !Query.Contains(TEXT(" ")) && Query.Contains(TEXT("_"));
		if (bLooksLikeExactFuncName)
		{
			static const TArray<const TCHAR*> WellKnownLibPaths = {
				TEXT("/Script/Engine.KismetMathLibrary"),
				TEXT("/Script/Engine.KismetSystemLibrary"),
				TEXT("/Script/Engine.KismetStringLibrary"),
				TEXT("/Script/Engine.KismetArrayLibrary"),
				TEXT("/Script/Engine.GameplayStatics"),
				TEXT("/Script/Engine.BlueprintMapLibrary"),
				TEXT("/Script/Engine.BlueprintSetLibrary"),
			};
			const FName QueryFName(*Query);
			for (const TCHAR* LibPath : WellKnownLibPaths)
			{
				UClass* LibClass = LoadObject<UClass>(nullptr, LibPath);
				if (!LibClass) continue;
				UFunction* Found = LibClass->FindFunctionByName(QueryFName);
				if (!Found) continue;

				const FString Handle = FString::Printf(TEXT("fn.%s.%s"),
					*LibClass->GetName(), *Found->GetName());
				if (SeenHandles.Contains(Handle)) break;
				SeenHandles.Add(Handle);

				FNodeMatch M;
				M.Score = 0.95f;
				M.Handle = Handle;
				M.Name = Found->GetDisplayNameText().ToString();
				if (M.Name.IsEmpty()) M.Name = Found->GetName();
				M.Category = LibClass->GetName().Replace(TEXT("Kismet"), TEXT("")).Replace(TEXT("Library"), TEXT(""));
				M.bIsPure = Found->HasAnyFunctionFlags(FUNC_BlueprintPure);
				M.Spawner = nullptr;
				Matches.Add(M);
				break;
			}
		}
	}

	if (Blueprint)
	{
		const FString SelfClassPrefix = FString::Printf(TEXT("fn.%s."), *Blueprint->GeneratedClass->GetName());
		for (FNodeMatch& M : Matches)
		{
			const bool bIsBPScoped =
				M.Handle.StartsWith(TEXT("var.get."))      ||
				M.Handle.StartsWith(TEXT("var.set."))      ||
				M.Handle.StartsWith(TEXT("prop.get."))     ||
				M.Handle.StartsWith(TEXT("prop.set."))     ||
				M.Handle.StartsWith(TEXT("ev.Dispatcher")) ||
				M.Handle.StartsWith(SelfClassPrefix);
			if (bIsBPScoped) M.Score += 0.15f;
		}
	}

	Matches.Sort([](const FNodeMatch& A, const FNodeMatch& B)
	{
		if (FMath::Abs(A.Score - B.Score) > 0.001f) return A.Score > B.Score;
		auto IsStdlib = [](const FString& H)
		{
			return H.Contains(TEXT("KismetMathLibrary")) || H.Contains(TEXT("KismetSystemLibrary"))
				|| H.Contains(TEXT("KismetStringLibrary")) || H.Contains(TEXT("KismetArrayLibrary"));
		};
		bool bAStd = IsStdlib(A.Handle), bBStd = IsStdlib(B.Handle);
		if (bAStd != bBStd) return bAStd;
		return A.Name.Len() < B.Name.Len();
	});

	if (Matches.Num() > MaxResults)
	{
		Matches.SetNum(MaxResults);
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> NodesArray;

	UEdGraph* TempPinGraph = nullptr;
	bool bPinsSkippedOffThread = false;
	if (bIncludePins && Blueprint)
	{
		if (IsInGameThread())
		{
			TempPinGraph = NewObject<UEdGraph>(Blueprint, UEdGraph::StaticClass(), NAME_None, RF_Transient);
			TempPinGraph->Schema = UEdGraphSchema_K2::StaticClass();
		}
		else
		{
			bPinsSkippedOffThread = true;
		}
	}

	for (const FNodeMatch& Match : Matches)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("handle"), Match.Handle);
		NodeObj->SetStringField(TEXT("name"), Match.Name);
		if (!Match.Category.IsEmpty()) NodeObj->SetStringField(TEXT("category"), Match.Category);
		NodeObj->SetBoolField(TEXT("is_pure"), Match.bIsPure);

		bool bSkipPinInspection = false;
		if (Match.Spawner && Match.Spawner->NodeClass &&
			Match.Spawner->NodeClass->IsChildOf(UAnimGraphNode_Base::StaticClass()) &&
			(!Blueprint || !Blueprint->IsA(UAnimBlueprint::StaticClass())))
		{
			bSkipPinInspection = true;
		}
		if (bIncludePins && TempPinGraph && Blueprint && Match.Spawner && !Match.Handle.StartsWith(TEXT("prop.")) && !bSkipPinInspection)
		{
			FString PinSpawnError;
			UEdGraphNode* TempNode = GraphEditHelpers::CreateNodeFromHandle(
				Blueprint, TempPinGraph, Match.Handle, 0, 0, TEXT(""), nullptr, PinSpawnError);
			if (TempNode && PinSpawnError.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> PinsArray;
				for (UEdGraphPin* Pin : TempNode->Pins)
				{
					if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
					TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
					PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
					PinObj->SetStringField(TEXT("dir"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
					PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
					if (!Pin->PinType.PinSubCategory.IsNone())
						PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategory.ToString());
					if (Pin->PinType.ContainerType == EPinContainerType::Array) PinObj->SetBoolField(TEXT("is_array"), true);
					if (!Pin->DefaultValue.IsEmpty()) PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
					PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
				}
				NodeObj->SetArrayField(TEXT("pins"), PinsArray);
				TempNode->DestroyNode();
			}
		}

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	ResultObj->SetStringField(TEXT("blueprint"), BlueprintPath);
	ResultObj->SetStringField(TEXT("query"), Query);
	ResultObj->SetNumberField(TEXT("count"), NodesArray.Num());
	ResultObj->SetArrayField(TEXT("nodes"), NodesArray);
	if (bPinsSkippedOffThread)
	{
		ResultObj->SetStringField(TEXT("include_pins_note"),
			TEXT("include_pins=true was requested but discover_nodes ran off the GameThread (where pin inspection requires UObject construction). The handles + names came back without pin metadata. If you need pin details, call get_handle_reference(handle) for the specific handle."));
	}

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

void HandlePlaceNode(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsGraphContextReady()) { OutError = TEXT("Blueprint editor context unavailable — ensure the editor is fully loaded and try again."); return; }

	FString Handle;
	Args->TryGetStringField(TEXT("handle"), Handle);
	if (Handle.IsEmpty()) Args->TryGetStringField(TEXT("type"), Handle);
	if (Handle.IsEmpty()) Args->TryGetStringField(TEXT("node_type"), Handle);
	if (Handle.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: handle");
		return;
	}

	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	double X = 0, Y = 0;
	if (Args->HasField(TEXT("x"))) X = Args->GetNumberField(TEXT("x"));
	if (Args->HasField(TEXT("y"))) Y = Args->GetNumberField(TEXT("y"));

	FString CustomName;
	Args->TryGetStringField(TEXT("custom_name"), CustomName);

	if (Handle.Equals(TEXT("k2.InputKey"), ESearchCase::IgnoreCase)
	 || Handle.Equals(TEXT("k2.Input Key"), ESearchCase::IgnoreCase)
	 || Handle.Equals(TEXT("k2.Key"), ESearchCase::IgnoreCase))
	{
		FString KeyName;
		if (Args->TryGetStringField(TEXT("key"), KeyName) && !KeyName.IsEmpty())
		{
			Handle = TEXT("k2.") + KeyName;
		}
	}
	else if (Handle.Equals(TEXT("k2.InputDebugKey"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.K2Node_InputDebugKey"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.Debug Key"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.DebugKey"), ESearchCase::IgnoreCase))
	{
		FString KeyName;
		if (Args->TryGetStringField(TEXT("key"), KeyName) && !KeyName.IsEmpty())
		{
			Handle = TEXT("k2.Debug Key ") + KeyName.ToUpper();
		}
	}

	FString UserId;
	Args->TryGetStringField(TEXT("id"), UserId);
	if (UserId.IsEmpty()) Args->TryGetStringField(TEXT("node_id"), UserId);
	if (UserId.IsEmpty()) Args->TryGetStringField(TEXT("node_id_tag"), UserId);
	if (UserId.IsEmpty()) Args->TryGetStringField(TEXT("tag"), UserId);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
	Args->TryGetArrayField(TEXT("inputs"), InputsArray);

	FString CreateError;
	UEdGraphNode* NewNode = GraphEditHelpers::CreateNodeFromHandle(
		Blueprint, Graph, Handle, (int32)X, (int32)Y, CustomName, InputsArray, CreateError);

	if (!NewNode)
	{
		OutError = CreateError;
		return;
	}

	if (NewNode->Pins.Num() == 0 && Handle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase))
	{
		if (UK2Node_CallFunction* FnNode = Cast<UK2Node_CallFunction>(NewNode))
		{
			const FName MemberName = FnNode->FunctionReference.GetMemberName();
			UClass* MemberParent = FnNode->FunctionReference.GetMemberParentClass();
			const bool bUnresolved = MemberName.IsNone()
				|| !MemberParent
				|| !MemberParent->FindFunctionByName(MemberName);
			if (bUnresolved)
			{
				Graph->RemoveNode(NewNode);
				OutError = FString::Printf(
					TEXT("Function reference for handle '%s' did not resolve — placed node had zero pins. Qualify the handle: `fn.Self.<Func>` for a function on THIS Blueprint, `fn.<Class>_C.<Func>` for a cross-Blueprint call (e.g. fn.StatusEffectComponent_C.ApplyStatusEffect), or `fn.<Library>.<Func>` for a library function (e.g. fn.KismetMathLibrary.Add_FloatFloat). Letting the unresolved node compile produces cascading 'In use pin no longer exists' errors."),
					*Handle);
				return;
			}
		}
	}

	static constexpr int32 MinGapX = 250;
	static constexpr int32 SameRowThreshold = 80;
	for (int32 Pass = 0; Pass < 10; Pass++)
	{
		bool bCollision = false;
		for (UEdGraphNode* Other : Graph->Nodes)
		{
			if (!Other || Other == NewNode) continue;
			int32 DX = FMath::Abs(NewNode->NodePosX - Other->NodePosX);
			int32 DY = FMath::Abs(NewNode->NodePosY - Other->NodePosY);
			if (DX < MinGapX && DY < SameRowThreshold)
			{
				NewNode->NodePosX = Other->NodePosX + MinGapX;
				bCollision = true;
				break;
			}
		}
		if (!bCollision) break;
	}

	Graph->NotifyGraphChanged();

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	FString UniqueId;
	if (!UserId.IsEmpty())
	{
		UniqueId = UserId;
	}
	else
	{
		FString BaseTitle = NewNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		UniqueId = BaseTitle;
		int32 DupCount = 0;
		for (UEdGraphNode* Existing : Graph->Nodes)
		{
			if (Existing && Existing != NewNode &&
				Existing->GetNodeTitle(ENodeTitleType::ListView).ToString().Equals(BaseTitle, ESearchCase::IgnoreCase))
			{
				DupCount++;
			}
		}
		if (DupCount > 0)
		{
			UniqueId = FString::Printf(TEXT("%s_%d"), *BaseTitle, DupCount);
		}
	}

	BlueprintNodeIdentity::SetLogicalId(Blueprint, NewNode, UniqueId);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("node_id"), UniqueId);
	ResultObj->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString());
	ResultObj->SetStringField(TEXT("node_class"), NewNode->GetClass()->GetName());
	ResultObj->SetArrayField(TEXT("pins"), GraphEditHelpers::GetPinInfoArray(NewNode));

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

static void ForceResolveWildcardPins(UEdGraphNode* Node)
{
	if (!Node) return;
	bool bChanged = false;
	FEdGraphPinType ResolvedScalarType;
	bool bHasResolvedType = false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard) continue;
		if (Pin->LinkedTo.Num() == 0) continue;
		UEdGraphPin* SourcePin = Pin->LinkedTo[0];
		if (!SourcePin || SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard) continue;
		EPinContainerType OrigContainer = Pin->PinType.ContainerType;
		Pin->PinType = SourcePin->PinType;
		if (OrigContainer == EPinContainerType::Array && SourcePin->PinType.ContainerType == EPinContainerType::None)
			Pin->PinType.ContainerType = EPinContainerType::Array;
		bChanged = true;
		if (!bHasResolvedType)
		{
			ResolvedScalarType = SourcePin->PinType;
			if (ResolvedScalarType.ContainerType == EPinContainerType::Array)
				ResolvedScalarType.ContainerType = EPinContainerType::None;
			bHasResolvedType = true;
		}
	}
	if (bHasResolvedType)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard) continue;
			if (Pin->LinkedTo.Num() > 0) continue;
			if (Pin->PinType.ContainerType == EPinContainerType::Array)
			{
				Pin->PinType = ResolvedScalarType;
				Pin->PinType.ContainerType = EPinContainerType::Array;
			}
			else
			{
				Pin->PinType = ResolvedScalarType;
			}
			bChanged = true;
		}
	}
	if (bChanged)
		Node->NodeConnectionListChanged();
}

void HandleConnectPins(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsGraphContextReady()) { OutError = TEXT("Blueprint editor context unavailable — ensure the editor is fully loaded and try again."); return; }

	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	for (const TCHAR* Field : { TEXT("connections"), TEXT("items") })
	{
		FString StrVal;
		const TArray<TSharedPtr<FJsonValue>>* CheckArr = nullptr;
		if (!Args->TryGetArrayField(Field, CheckArr) &&
			Args->TryGetStringField(Field, StrVal) && !StrVal.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Parsed;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(StrVal);
			if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.Num() > 0)
				Args->SetArrayField(Field, Parsed);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	if ((Args->TryGetArrayField(TEXT("connections"), ConnectionsArray) && ConnectionsArray && ConnectionsArray->Num() > 0) ||
		(Args->TryGetArrayField(TEXT("items"), ConnectionsArray) && ConnectionsArray && ConnectionsArray->Num() > 0))
	{
		HandleConnectPinsBulk(Args, OutJsonString, OutError);
		return;
	}

	FString FromNodeId, FromPinName, ToNodeId, ToPinName;
	if (!Args->TryGetStringField(TEXT("from_node"), FromNodeId) && !Args->TryGetStringField(TEXT("from_node_id"), FromNodeId)) FromNodeId = TEXT("");
	if (!Args->TryGetStringField(TEXT("to_node"), ToNodeId) && !Args->TryGetStringField(TEXT("to_node_id"), ToNodeId)) ToNodeId = TEXT("");
	if (!Args->TryGetStringField(TEXT("from_pin"), FromPinName)) FromPinName = TEXT("");
	if (!Args->TryGetStringField(TEXT("to_pin"), ToPinName)) ToPinName = TEXT("");
	if (FromNodeId.Equals(TEXT("k2.Self.self"), ESearchCase::IgnoreCase))
	{
		FromNodeId = TEXT("k2.Self");
		if (FromPinName.IsEmpty()) FromPinName = TEXT("self");
	}
	if (ToNodeId.Equals(TEXT("k2.Self.self"), ESearchCase::IgnoreCase))
	{
		ToNodeId = TEXT("k2.Self");
		if (ToPinName.IsEmpty()) ToPinName = TEXT("self");
	}
	if (FromNodeId.IsEmpty() || FromPinName.IsEmpty() || ToNodeId.IsEmpty() || ToPinName.IsEmpty())
	{
		OutError = TEXT("Missing required parameters: from_node, from_pin, to_node, to_pin (or use connect_pins_bulk with a 'connections' array for multiple connections)");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	UEdGraphNode* FromNode = GraphEditHelpers::FindNodeInGraph(Graph, FromNodeId);
	UEdGraphNode* ToNode = GraphEditHelpers::FindNodeInGraph(Graph, ToNodeId);

	if (!FromNode)
	{
		OutError = FString::Printf(TEXT("Source node not found: '%s' in graph '%s' (did you forget graph_name?). Available nodes: "), *FromNodeId, *GraphName);
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (!N) continue;
			FString Id = BlueprintNodeIdentity::GetLogicalId(Blueprint, N);
			if (Id.IsEmpty()) Id = N->GetNodeTitle(ENodeTitleType::ListView).ToString();
			OutError += Id + TEXT(", ");
		}
		return;
	}
	if (!ToNode)
	{
		OutError = FString::Printf(TEXT("Target node not found: '%s' in graph '%s' (did you forget graph_name?). Available nodes: "), *ToNodeId, *GraphName);
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (!N) continue;
			FString Id = BlueprintNodeIdentity::GetLogicalId(Blueprint, N);
			if (Id.IsEmpty()) Id = N->GetNodeTitle(ENodeTitleType::ListView).ToString();
			OutError += Id + TEXT(", ");
		}
		return;
	}

	UEdGraphPin* FromPin = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Output);
	UEdGraphPin* ToPin = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Input);

	if (!FromPin || !ToPin)
	{
		UEdGraphPin* AltFrom = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Input);
		UEdGraphPin* AltTo = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Output);

		if (AltFrom && AltTo)
		{
			FromPin = AltTo;
			ToPin = AltFrom;
			UEdGraphNode* TempNode = FromNode;
			FromNode = ToNode;
			ToNode = TempNode;
		}
	}

	if (!FromPin && ToPin)
	{
		if (UEdGraphPin* Hinted = GraphEditHelpers::FindPinByTypeHint(FromNode, EGPD_Output, ToPin->PinType, ToPin))
		{
			FromPin = Hinted;
		}
	}
	if (FromPin && !ToPin)
	{
		if (UEdGraphPin* Hinted = GraphEditHelpers::FindPinByTypeHint(ToNode, EGPD_Input, FromPin->PinType, FromPin))
		{
			ToPin = Hinted;
		}
	}

	if (!FromPin)
	{
		UEdGraphPin* WrongDir = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Input);
		if (WrongDir)
		{
			OutError = FString::Printf(TEXT("Pin '%s' is an INPUT on node '%s', not an output. Available output pins: "), *FromPinName, *FromNodeId);
		}
		else
		{
			OutError = FString::Printf(TEXT("Pin '%s' not found on node '%s'. Available output pins: "), *FromPinName, *FromNodeId);
		}
		for (UEdGraphPin* P : FromNode->Pins)
		{
			if (P && P->Direction == EGPD_Output && !P->bHidden)
				OutError += FString::Printf(TEXT("'%s' (%s), "), *P->PinName.ToString(), *P->PinType.PinCategory.ToString());
		}
		return;
	}
	if (!ToPin)
	{
		UEdGraphPin* WrongDir = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Output);
		if (WrongDir)
		{
			OutError = FString::Printf(TEXT("Pin '%s' is an OUTPUT on node '%s', not an input. Available input pins: "), *ToPinName, *ToNodeId);
		}
		else
		{
			OutError = FString::Printf(TEXT("Pin '%s' not found on node '%s'. Available input pins: "), *ToPinName, *ToNodeId);
		}
		for (UEdGraphPin* P : ToNode->Pins)
		{
			if (P && P->Direction == EGPD_Input && !P->bHidden)
				OutError += FString::Printf(TEXT("'%s' (%s), "), *P->PinName.ToString(), *P->PinType.PinCategory.ToString());
		}
		return;
	}

	bool bForce = false;
	Args->TryGetBoolField(TEXT("force"), bForce);
	int32 ForceBrokenCount = 0;
	if (bForce && ToPin->LinkedTo.Num() > 0)
	{
		TArray<UEdGraphPin*> ExistingLinks = ToPin->LinkedTo;
		for (UEdGraphPin* OtherPin : ExistingLinks)
		{
			if (OtherPin && OtherPin != FromPin)
			{
				ToPin->BreakLinkTo(OtherPin);
				++ForceBrokenCount;
			}
		}
	}

	if (FromPin->LinkedTo.Contains(ToPin))
	{
		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetStringField(TEXT("status"), TEXT("already_connected"));
		if (ForceBrokenCount > 0)
		{
			ResultObj->SetNumberField(TEXT("force_broken_links"), ForceBrokenCount);
		}
		OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
		return;
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		OutError = TEXT("Graph has no schema");
		return;
	}

	FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);

	bool bConnected = false;
	FString ConnectionType = TEXT("direct");

	switch (Response.Response.GetValue())
	{
	case CONNECT_RESPONSE_MAKE:
	case CONNECT_RESPONSE_BREAK_OTHERS_A:
	case CONNECT_RESPONSE_BREAK_OTHERS_B:
	case CONNECT_RESPONSE_BREAK_OTHERS_AB:
		bConnected = Schema->TryCreateConnection(FromPin, ToPin);
		break;

	case CONNECT_RESPONSE_MAKE_WITH_PROMOTION:
		bConnected = Schema->TryCreateConnection(FromPin, ToPin);
		ConnectionType = TEXT("with_type_promotion");
		break;

	case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
		bConnected = Schema->TryCreateConnection(FromPin, ToPin);
		ConnectionType = TEXT("with_conversion_node");
		break;

	default:
		{
			bool bBypass = false;
			bConnected = TryConnectWithSkelBypass(FromPin, ToPin, Schema, Response, &bBypass);
			if (bBypass) ConnectionType = TEXT("direct_skel_bypass");
		}
		if (!bConnected)
		{
			OutError = FString::Printf(
				TEXT("Cannot connect '%s' (%s, %s) -> '%s' (%s, %s): %s"),
				*FromPin->PinName.ToString(), *FromPin->PinType.PinCategory.ToString(),
				FromPin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"),
				*ToPin->PinName.ToString(), *ToPin->PinType.PinCategory.ToString(),
				ToPin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"),
				*Response.Message.ToString()
			);
			return;
		}
		break;
	}

	if (!bConnected)
	{
		OutError = FString::Printf(TEXT("TryCreateConnection failed: %s"), *Response.Message.ToString());
		return;
	}

	ForceResolveWildcardPins(FromPin->GetOwningNodeUnchecked());
	ForceResolveWildcardPins(ToPin->GetOwningNodeUnchecked());

	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("status"), TEXT("connected"));
	ResultObj->SetStringField(TEXT("connection_type"), ConnectionType);
	ResultObj->SetStringField(TEXT("from"), FString::Printf(TEXT("%s.%s"), *FromNodeId, *FromPinName));
	ResultObj->SetStringField(TEXT("to"), FString::Printf(TEXT("%s.%s"), *ToNodeId, *ToPinName));
	if (FromPin && !FromPin->PinName.ToString().Equals(FromPinName, ESearchCase::CaseSensitive))
	{
		ResultObj->SetStringField(TEXT("resolved_from_pin"), FromPin->PinName.ToString());
	}
	if (ToPin && !ToPin->PinName.ToString().Equals(ToPinName, ESearchCase::CaseSensitive))
	{
		ResultObj->SetStringField(TEXT("resolved_to_pin"), ToPin->PinName.ToString());
	}
	if (ForceBrokenCount > 0)
		ResultObj->SetNumberField(TEXT("force_broken_links"), ForceBrokenCount);

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

void HandleSetPinDefault(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{

	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	FString NodeId, PinName, Value;
	Args->TryGetStringField(TEXT("node_id"), NodeId);
	if (NodeId.IsEmpty()) Args->TryGetStringField(TEXT("node_id_or_handle"), NodeId);
	if (NodeId.IsEmpty()) Args->TryGetStringField(TEXT("handle"), NodeId);
	bool bHasPin = Args->TryGetStringField(TEXT("pin_name"), PinName);
	const bool bHasValue = Args->TryGetStringField(TEXT("value"), Value);

	if (!bHasPin && !NodeId.IsEmpty())
	{
		const bool bLooksLikeHandle =
			NodeId.StartsWith(TEXT("var.")) || NodeId.StartsWith(TEXT("fn.")) ||
			NodeId.StartsWith(TEXT("k2.")) || NodeId.StartsWith(TEXT("ev.")) ||
			NodeId.StartsWith(TEXT("cast.")) || NodeId.StartsWith(TEXT("entry.")) ||
			NodeId.StartsWith(TEXT("return.")) || NodeId.StartsWith(TEXT("self."));
		int32 LastDot = INDEX_NONE;
		NodeId.FindLastChar(TEXT('.'), LastDot);
		if (!bLooksLikeHandle && LastDot != INDEX_NONE && LastDot > 0 && LastDot < NodeId.Len() - 1)
		{
			PinName = NodeId.Mid(LastDot + 1);
			NodeId = NodeId.Left(LastDot);
			bHasPin = true;
		}
	}

	if (NodeId.IsEmpty() || !bHasPin || !bHasValue)
	{
		OutError = TEXT("Missing required parameters: node_id (alias: node_id_or_handle), pin_name, value");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	UEdGraphNode* Node = GraphEditHelpers::FindNodeInGraph(Graph, NodeId);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Node not found: '%s'"), *NodeId);
		return;
	}

	UEdGraphPin* Pin = GraphEditHelpers::FindPinByName(Node, PinName, EGPD_Input);
	if (!Pin)
	{
		OutError = FString::Printf(TEXT("Input pin '%s' not found on node '%s'. Available input pins: "), *PinName, *NodeId);
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && P->Direction == EGPD_Input && !P->bHidden)
				OutError += FString::Printf(TEXT("'%s' (%s), "), *P->PinName.ToString(), *P->PinType.PinCategory.ToString());
		}
		return;
	}

	FString Warning;
	bool bByRefAutoLiteralPlaced = false;
	FString AutoLiteralHandle;
	if (Pin->PinType.bIsReference && !Value.IsEmpty())
	{
		const FName Cat = Pin->PinType.PinCategory;
		FString MakeLiteralFunc;
		if      (Cat == UEdGraphSchema_K2::PC_Boolean) MakeLiteralFunc = TEXT("MakeLiteralBool");
		else if (Cat == UEdGraphSchema_K2::PC_Int)     MakeLiteralFunc = TEXT("MakeLiteralInt");
		else if (Cat == UEdGraphSchema_K2::PC_Int64)   MakeLiteralFunc = TEXT("MakeLiteralInt64");
		else if (Cat == UEdGraphSchema_K2::PC_Real || Cat == UEdGraphSchema_K2::PC_Float ||
		         Cat == UEdGraphSchema_K2::PC_Double) MakeLiteralFunc = TEXT("MakeLiteralDouble");
		else if (Cat == UEdGraphSchema_K2::PC_Byte)    MakeLiteralFunc = TEXT("MakeLiteralByte");
		else if (Cat == UEdGraphSchema_K2::PC_Name)    MakeLiteralFunc = TEXT("MakeLiteralName");
		else if (Cat == UEdGraphSchema_K2::PC_String)  MakeLiteralFunc = TEXT("MakeLiteralString");
		else if (Cat == UEdGraphSchema_K2::PC_Text)    MakeLiteralFunc = TEXT("MakeLiteralText");

		if (!MakeLiteralFunc.IsEmpty())
		{
			TMap<FString, UBlueprintNodeSpawner*> InlineCache;
			GraphEditHelpers::BuildSpawnerCache(Blueprint, InlineCache);
			AutoLiteralHandle = FString::Printf(TEXT("fn.KismetSystemLibrary.%s"), *MakeLiteralFunc);
			FString CreateErr;
			UEdGraphNode* LiteralNode = GraphEditHelpers::CreateNodeFromHandle(
				Blueprint, Graph, AutoLiteralHandle,
				Node->NodePosX - 250, Node->NodePosY,
				FString(), nullptr, CreateErr, &InlineCache);
			if (LiteralNode)
			{
				LiteralNode->SetFlags(RF_Transactional);
				UEdGraphPin* LiteralValuePin = nullptr;
				UEdGraphPin* LiteralReturnPin = nullptr;
				for (UEdGraphPin* P : LiteralNode->Pins)
				{
					if (!P || P->bHidden) continue;
					if (P->Direction == EGPD_Input && P->PinName.ToString().Equals(TEXT("Value"), ESearchCase::IgnoreCase))
						LiteralValuePin = P;
					else if (P->Direction == EGPD_Output && P->PinName.ToString().Equals(TEXT("ReturnValue"), ESearchCase::IgnoreCase))
						LiteralReturnPin = P;
				}
				if (LiteralValuePin && LiteralReturnPin)
				{
					const UEdGraphSchema* Sch = Graph->GetSchema();
					if (Sch) Sch->TrySetDefaultValue(*LiteralValuePin, Value);
					else LiteralValuePin->DefaultValue = Value;
					LiteralReturnPin->MakeLinkTo(Pin);
					Graph->NotifyGraphChanged();
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					bByRefAutoLiteralPlaced = true;
				}
			}
		}
		if (!bByRefAutoLiteralPlaced)
		{
			Warning = FString::Printf(
				TEXT("Pin '%s' is passed by reference — default values cannot be stored on by-ref pins. "
				     "Auto-create-literal failed (no MakeLiteral exists for type '%s'). "
				     "Use a local variable or Set node to supply the value before calling this node."),
				*PinName, *Cat.ToString());
		}
	}

	if (bByRefAutoLiteralPlaced)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetStringField(TEXT("status"), TEXT("set_via_auto_literal"));
		ResultObj->SetStringField(TEXT("node"), NodeId);
		ResultObj->SetStringField(TEXT("pin"), PinName);
		ResultObj->SetStringField(TEXT("value"), Value);
		ResultObj->SetStringField(TEXT("auto_created_literal_handle"), AutoLiteralHandle);
		ResultObj->SetStringField(TEXT("note"), TEXT("By-reference pin can't store defaults; auto-placed a MakeLiteral node and wired it."));
		OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
		return;
	}

	FString ResolvedValue = Value;
	const UEdGraphSchema_K2* K2SchemaSPD = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
	const bool bIsClassOrObjectPin = K2SchemaSPD &&
	    (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
	     Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
	     Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
	     Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass);

	if (bIsClassOrObjectPin && !Value.IsEmpty() && Value.StartsWith(TEXT("/")) && !Value.Contains(TEXT(".")))
	{
		const FString ShortName = FPackageName::GetShortName(Value);
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			ResolvedValue = FString::Printf(TEXT("%s.%s_C"), *Value, *ShortName);
		}
		else
		{
			ResolvedValue = FString::Printf(TEXT("%s.%s"), *Value, *ShortName);
		}
	}

	if (bIsClassOrObjectPin && !Value.IsEmpty() && !Value.StartsWith(TEXT("/")))
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
		{
			TArray<FAssetData> BpAssets;
			AR.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BpAssets, true);
			for (const FAssetData& AD : BpAssets)
			{
				if (AD.AssetName.ToString().Equals(Value, ESearchCase::IgnoreCase))
				{
					ResolvedValue = FString::Printf(TEXT("%s.%s_C"), *AD.PackageName.ToString(), *AD.AssetName.ToString());
					break;
				}
			}
		}
		else
		{
			TArray<FAssetData> AllAssets;
			AR.GetAssetsByPackageName(FName(*FString::Printf(TEXT("/Game/%s"), *Value)), AllAssets);

			if (AllAssets.Num() == 0)
			{
				FARFilter Filter;
				Filter.bRecursivePaths = true;
				Filter.PackagePaths.Add(FName(TEXT("/Game")));
				TArray<FAssetData> GameAssets;
				AR.GetAssets(Filter, GameAssets);
				for (const FAssetData& AD : GameAssets)
				{
					if (AD.AssetName.ToString().Equals(Value, ESearchCase::IgnoreCase))
					{
						ResolvedValue = FString::Printf(TEXT("%s.%s"), *AD.PackageName.ToString(), *AD.AssetName.ToString());
						break;
					}
				}
			}
		}
	}

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && Pin->PinType.PinSubCategoryObject.IsValid())
	{
		if (UEnum* PinEnum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			for (int32 Ei = 0; Ei < PinEnum->NumEnums() - 1; ++Ei)
			{
				FString FullName = PinEnum->GetNameStringByIndex(Ei);
				FString DisplayName = PinEnum->GetDisplayNameTextByIndex(Ei).ToString();
				FString ShortName = FullName.Contains(TEXT("::")) ? FullName.RightChop(FullName.Find(TEXT("::")) + 2) : FullName;
				if (ResolvedValue.Equals(ShortName, ESearchCase::IgnoreCase) ||
					ResolvedValue.Equals(DisplayName, ESearchCase::IgnoreCase) ||
					ResolvedValue.Equals(FullName, ESearchCase::IgnoreCase))
				{
					ResolvedValue = Cast<UUserDefinedEnum>(PinEnum) ? DisplayName : FullName;
					break;
				}
			}
		}
	}

	bool bSetViaObject = false;
	if (ResolvedValue.StartsWith(TEXT("/")) &&
		(Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface))
	{
		UObject* LoadedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *ResolvedValue);
		if (LoadedObj)
		{
			Pin->DefaultObject = LoadedObj;
			Pin->DefaultValue = TEXT("");
			bSetViaObject = true;
		}
	}

	if (!bSetViaObject)
	{
		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema)
		{
			Schema->TrySetDefaultValue(*Pin, ResolvedValue);
		}
		else
		{
			Pin->DefaultValue = ResolvedValue;
		}
	}
	Node->PinDefaultValueChanged(Pin);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("status"), Warning.IsEmpty() ? TEXT("set") : TEXT("set_with_warning"));
	ResultObj->SetStringField(TEXT("node"), NodeId);
	ResultObj->SetStringField(TEXT("pin"), PinName);
	ResultObj->SetStringField(TEXT("value"), Value);
	if (!Warning.IsEmpty())
	{
		ResultObj->SetStringField(TEXT("warning"), Warning);
	}

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

void HandleRemoveNode(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{

	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	FString NodeId;
	if (!Args->TryGetStringField(TEXT("node_id"), NodeId) || NodeId.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: node_id");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	UEdGraphNode* Node = GraphEditHelpers::FindNodeInGraph(Graph, NodeId);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Node not found: '%s'"), *NodeId);
		return;
	}

	FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();

	Node->Modify();
	Node->BreakAllNodeLinks();

	BlueprintNodeIdentity::ClearLogicalId(Blueprint, Node);
	Graph->RemoveNode(Node);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("status"), TEXT("removed"));
	ResultObj->SetStringField(TEXT("node"), NodeTitle);

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

void HandleRemoveNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("node_ids"), NodeIdsArray) || !NodeIdsArray || NodeIdsArray->Num() == 0)
	{
		OutError = TEXT("Missing required parameter: node_ids (array of node IDs)");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	int32 Removed = 0, NotFound = 0;
	TArray<FString> NotFoundIds;
	for (const TSharedPtr<FJsonValue>& IdVal : *NodeIdsArray)
	{
		FString NodeId = IdVal->AsString();
		if (NodeId.IsEmpty()) continue;

		UEdGraphNode* Node = GraphEditHelpers::FindNodeInGraph(Graph, NodeId);
		if (!Node)
		{
			NotFound++;
			NotFoundIds.Add(NodeId);
			continue;
		}
		Node->Modify();
		Node->BreakAllNodeLinks();
		BlueprintNodeIdentity::ClearLogicalId(Blueprint, Node);
		Graph->RemoveNode(Node);
		Removed++;
	}

	if (Removed > 0)
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("status"), TEXT("removed"));
	ResultObj->SetNumberField(TEXT("removed"), Removed);
	ResultObj->SetNumberField(TEXT("not_found"), NotFound);
	if (NotFoundIds.Num() > 0)
		ResultObj->SetStringField(TEXT("not_found_ids"), FString::Join(NotFoundIds, TEXT(", ")));

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);

	if (Removed == 0 && NotFound > 0)
	{
		OutError = FString::Printf(
			TEXT("delete_nodes removed 0 of %d requested node(s) — none of the IDs were found in graph '%s'. "
			     "Missing IDs: %s. Common causes: (1) node IDs from the JSON spec only persist within a single build_blueprint_graph call — use get_blueprint_graph(graph_name) to see actual node GUIDs, "
			     "(2) the nodes were already cleaned up by a prior pre-flight pass, "
			     "(3) wrong graph_name."),
			NodeIdsArray->Num(), *GraphName, *FString::Join(NotFoundIds, TEXT(", ")));
	}
	return;
}

void HandleConnectPinsBulk(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{

	FString BlueprintPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	if ((!Args->TryGetArrayField(TEXT("connections"), ConnectionsArray) || !ConnectionsArray || ConnectionsArray->Num() == 0) &&
		(!Args->TryGetArrayField(TEXT("items"), ConnectionsArray) || !ConnectionsArray || ConnectionsArray->Num() == 0))
	{
		OutError = TEXT("Missing or empty required parameter: connections (array of {from_node, from_pin, to_node, to_pin})");
		return;
	}

	if (BlueprintPath.IsEmpty())
	{
		for (const TSharedPtr<FJsonValue>& ItemVal : *ConnectionsArray)
		{
			TSharedPtr<FJsonObject> Item = SafeAsObject(ItemVal);
			if (Item.IsValid() && Item->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) && !BlueprintPath.IsEmpty())
				break;
		}
	}
	if (BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path (must be at top level of connect_pins call, OR inside each item — accepted both ways)");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	const bool bGraphNameExplicit = Args->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	if (!bGraphNameExplicit && ConnectionsArray && ConnectionsArray->Num() > 0)
	{
		TSharedPtr<FJsonObject> FirstItem = SafeAsObject((*ConnectionsArray)[0]);
		if (FirstItem.IsValid())
		{
			FString ProbeId;
			FirstItem->TryGetStringField(TEXT("from"), ProbeId);
			if (ProbeId.IsEmpty()) FirstItem->TryGetStringField(TEXT("from_node"), ProbeId);
			if (ProbeId.IsEmpty()) FirstItem->TryGetStringField(TEXT("from_node_id"), ProbeId);

			if (ProbeId.Equals(TEXT("entry"), ESearchCase::IgnoreCase) ||
			    ProbeId.Equals(TEXT("return"), ESearchCase::IgnoreCase))
			{
				ProbeId.Empty();
				FirstItem->TryGetStringField(TEXT("to"), ProbeId);
				if (ProbeId.IsEmpty()) FirstItem->TryGetStringField(TEXT("to_node"), ProbeId);
				if (ProbeId.IsEmpty()) FirstItem->TryGetStringField(TEXT("to_node_id"), ProbeId);
				if (ProbeId.Equals(TEXT("entry"), ESearchCase::IgnoreCase) ||
				    ProbeId.Equals(TEXT("return"), ESearchCase::IgnoreCase))
				{
					ProbeId.Empty();
				}
			}

			if (!ProbeId.IsEmpty() && !GraphEditHelpers::FindNodeInGraph(Graph, ProbeId))
			{
				auto TryGraphList = [&](const TArray<UEdGraph*>& Graphs) -> bool {
					for (UEdGraph* G : Graphs)
					{
						if (G && GraphEditHelpers::FindNodeInGraph(G, ProbeId))
						{
							Graph = G;
							GraphName = G->GetName();
							return true;
						}
					}
					return false;
				};
				if (!TryGraphList(Blueprint->FunctionGraphs))
					if (!TryGraphList(Blueprint->MacroGraphs))
						TryGraphList(Blueprint->UbergraphPages);
			}
		}
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		OutError = TEXT("Graph has no schema");
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (int32 i = 0; i < ConnectionsArray->Num(); i++)
	{
		TSharedPtr<FJsonObject> Conn = SafeAsObject((*ConnectionsArray)[i]);
		TSharedPtr<FJsonObject> EntryResult = MakeShared<FJsonObject>();
		EntryResult->SetNumberField(TEXT("index"), i);

		if (!Conn.IsValid())
		{
			EntryResult->SetBoolField(TEXT("success"), false);
			EntryResult->SetStringField(TEXT("error"), TEXT("Invalid connection object"));
			FailCount++;
			ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
			continue;
		}

		FString FromNodeId, FromPinName, ToNodeId, ToPinName;
		Conn->TryGetStringField(TEXT("from_node"), FromNodeId);
		if (FromNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("from_node_id"), FromNodeId);
		if (FromNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("from"), FromNodeId);
		Conn->TryGetStringField(TEXT("from_pin"), FromPinName);
		Conn->TryGetStringField(TEXT("to_node"), ToNodeId);
		if (ToNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("to_node_id"), ToNodeId);
		if (ToNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("to"), ToNodeId);
		Conn->TryGetStringField(TEXT("to_pin"), ToPinName);

		auto IsHandlePrefixBulk = [](const FString& Id) -> bool {
			return Id.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase)
				|| Id.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase)
				|| Id.StartsWith(TEXT("fn."),    ESearchCase::IgnoreCase)
				|| Id.StartsWith(TEXT("k2."),    ESearchCase::IgnoreCase)
				|| Id.StartsWith(TEXT("ev."),    ESearchCase::IgnoreCase)
				|| Id.StartsWith(TEXT("prop."),  ESearchCase::IgnoreCase);
		};
		if (FromPinName.IsEmpty() && FromNodeId.Contains(TEXT(".")) && !IsHandlePrefixBulk(FromNodeId))
		{
			int32 DotIdx;
			if (FromNodeId.FindChar(TEXT('.'), DotIdx))
			{ FromPinName = FromNodeId.Mid(DotIdx + 1); FromNodeId = FromNodeId.Left(DotIdx); }
		}
		if (ToPinName.IsEmpty() && ToNodeId.Contains(TEXT(".")) && !IsHandlePrefixBulk(ToNodeId))
		{
			int32 DotIdx;
			if (ToNodeId.FindChar(TEXT('.'), DotIdx))
			{ ToPinName = ToNodeId.Mid(DotIdx + 1); ToNodeId = ToNodeId.Left(DotIdx); }
		}

		if (FromNodeId.Equals(TEXT("k2.Self.self"), ESearchCase::IgnoreCase) && FromPinName.IsEmpty())
		{ FromNodeId = TEXT("k2.Self"); FromPinName = TEXT("self"); }
		if (ToNodeId.Equals(TEXT("k2.Self.self"), ESearchCase::IgnoreCase) && ToPinName.IsEmpty())
		{ ToNodeId = TEXT("k2.Self"); ToPinName = TEXT("self"); }

		if (FromNodeId.IsEmpty() || FromPinName.IsEmpty() || ToNodeId.IsEmpty() || ToPinName.IsEmpty())
		{
			EntryResult->SetBoolField(TEXT("success"), false);
			EntryResult->SetStringField(TEXT("error"), TEXT("Missing from_node, from_pin, to_node, or to_pin"));
			FailCount++;
			ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
			continue;
		}

		UEdGraphNode* FromNode = GraphEditHelpers::FindNodeInGraph(Graph, FromNodeId);
		UEdGraphNode* ToNode = GraphEditHelpers::FindNodeInGraph(Graph, ToNodeId);

		auto ResolveHandleToExistingOrPlace = [&](const FString& HandleId) -> UEdGraphNode*
		{
			if (HandleId.IsEmpty()) return nullptr;
			const FString HL = HandleId.ToLower();
			const bool bIsHandle = HL.StartsWith(TEXT("var.get.")) || HL.StartsWith(TEXT("var.set."))
				|| HL.StartsWith(TEXT("fn.")) || HL.StartsWith(TEXT("k2."))
				|| HL.StartsWith(TEXT("ev.")) || HL.StartsWith(TEXT("prop."));
			if (!bIsHandle) return nullptr;

			if (HL.StartsWith(TEXT("var.get.")) || HL.StartsWith(TEXT("var.set.")))
			{
				const FName VarName(*HandleId.RightChop(8));
				const bool bWantGetter = HL.StartsWith(TEXT("var.get."));
				for (UEdGraphNode* N : Graph->Nodes)
				{
					if (!IsValid(N)) continue;
					if (bWantGetter)
					{
						UK2Node_VariableGet* G = Cast<UK2Node_VariableGet>(N);
						if (G && G->VariableReference.GetMemberName() == VarName) return G;
					}
					else
					{
						UK2Node_VariableSet* S = Cast<UK2Node_VariableSet>(N);
						if (S && S->VariableReference.GetMemberName() == VarName) return S;
					}
				}
			}
			else if (HL == TEXT("k2.self"))
			{
				for (UEdGraphNode* N : Graph->Nodes)
				{
					if (Cast<UK2Node_Self>(N)) return N;
				}
			}

			TMap<FString, UBlueprintNodeSpawner*> InlineCache;
			GraphEditHelpers::BuildSpawnerCache(Blueprint, InlineCache);
			FString CreateErr;
			UEdGraphNode* Created = GraphEditHelpers::CreateNodeFromHandle(
				Blueprint, Graph, HandleId, 0, 0, FString(), nullptr, CreateErr, &InlineCache);
			if (Created)
			{
				Created->SetFlags(RF_Transactional);
				Graph->NotifyGraphChanged();
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
			return Created;
		};

		if (!FromNode) FromNode = ResolveHandleToExistingOrPlace(FromNodeId);
		if (!ToNode)   ToNode   = ResolveHandleToExistingOrPlace(ToNodeId);

		if (!FromNode)
		{
			EntryResult->SetBoolField(TEXT("success"), false);
			const bool bLooksLikeFriendlyId = !FromNodeId.Contains(TEXT("-")) && !FromNodeId.IsNumeric();
			const FString FriendlyIdHint = bLooksLikeFriendlyId
				? TEXT(" Friendly IDs from build_blueprint_graph are call-scoped and cannot be used here. Call get_blueprint_graph(graph_name) to retrieve hex GUIDs, then retry connect_pins with those GUIDs.")
				: TEXT("");
			EntryResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Source node not found: '%s'.%s"), *FromNodeId, *FriendlyIdHint));
			FailCount++;
			ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
			continue;
		}
		if (!ToNode)
		{
			EntryResult->SetBoolField(TEXT("success"), false);
			const bool bLooksLikeFriendlyId = !ToNodeId.Contains(TEXT("-")) && !ToNodeId.IsNumeric();
			const FString FriendlyIdHint = bLooksLikeFriendlyId
				? TEXT(" Friendly IDs from build_blueprint_graph are call-scoped and cannot be used here. Call get_blueprint_graph(graph_name) to retrieve hex GUIDs, then retry connect_pins with those GUIDs.")
				: TEXT("");
			EntryResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Target node not found: '%s'.%s"), *ToNodeId, *FriendlyIdHint));
			FailCount++;
			ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
			continue;
		}

		UEdGraphPin* FromPin = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Output);
		UEdGraphPin* ToPin = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Input);

		auto TryTier3SinglePinRecovery = [&]()
		{
			if (!FromPin && ToPin)
			{
				if (UEdGraphPin* Hinted = GraphEditHelpers::FindPinByTypeHint(FromNode, EGPD_Output, ToPin->PinType, ToPin))
					FromPin = Hinted;
			}
			if (FromPin && !ToPin)
			{
				if (UEdGraphPin* Hinted = GraphEditHelpers::FindPinByTypeHint(ToNode, EGPD_Input, FromPin->PinType, FromPin))
					ToPin = Hinted;
			}
		};

		if (!FromPin || !ToPin)
		{
			UEdGraphPin* AltFrom = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Input);
			UEdGraphPin* AltTo = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Output);
			if (AltFrom && AltTo)
			{
				FromPin = AltTo;
				ToPin = AltFrom;
			}
		}

		TryTier3SinglePinRecovery();

		if (!FromPin)
		{
			EntryResult->SetBoolField(TEXT("success"), false);
			UEdGraphPin* WrongDir = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Input);
			FString AvailPins;
			for (UEdGraphPin* P : FromNode->Pins)
			{
				if (P && P->Direction == EGPD_Output && !P->bHidden)
					AvailPins += FString::Printf(TEXT("'%s', "), *P->PinName.ToString());
			}
			FString Hint = WrongDir ? TEXT(" (it IS an INPUT — wrong direction)") : TEXT("");
			EntryResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Pin '%s' not found as output on '%s'%s. Available outputs: %s"), *FromPinName, *FromNodeId, *Hint, *AvailPins));
			FailCount++;
			ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
			continue;
		}
		if (!ToPin)
		{
			EntryResult->SetBoolField(TEXT("success"), false);
			UEdGraphPin* WrongDir = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Output);
			FString AvailPins;
			for (UEdGraphPin* P : ToNode->Pins)
			{
				if (P && P->Direction == EGPD_Input && !P->bHidden)
					AvailPins += FString::Printf(TEXT("'%s', "), *P->PinName.ToString());
			}
			FString Hint = WrongDir ? TEXT(" (it IS an OUTPUT — wrong direction)") : TEXT("");
			EntryResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Pin '%s' not found as input on '%s'%s. Available inputs: %s"), *ToPinName, *ToNodeId, *Hint, *AvailPins));
			FailCount++;
			ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
			continue;
		}

		bool bForce = false;
		Args->TryGetBoolField(TEXT("force"), bForce);
		Conn->TryGetBoolField(TEXT("force"), bForce);
		int32 ForceBrokenCount = 0;
		if (bForce && ToPin->LinkedTo.Num() > 0)
		{
			TArray<UEdGraphPin*> ExistingLinks = ToPin->LinkedTo;
			for (UEdGraphPin* OtherPin : ExistingLinks)
			{
				if (OtherPin && OtherPin != FromPin)
				{
					ToPin->BreakLinkTo(OtherPin);
					++ForceBrokenCount;
				}
			}
		}

		if (FromPin->LinkedTo.Contains(ToPin))
		{
			EntryResult->SetBoolField(TEXT("success"), true);
			EntryResult->SetStringField(TEXT("status"), TEXT("already_connected"));
			if (ForceBrokenCount > 0)
				EntryResult->SetNumberField(TEXT("force_broken_links"), ForceBrokenCount);
			SuccessCount++;
			ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
			continue;
		}

		FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
		bool bConnected = false;

		switch (Response.Response.GetValue())
		{
		case CONNECT_RESPONSE_MAKE:
		case CONNECT_RESPONSE_BREAK_OTHERS_A:
		case CONNECT_RESPONSE_BREAK_OTHERS_B:
		case CONNECT_RESPONSE_BREAK_OTHERS_AB:
		case CONNECT_RESPONSE_MAKE_WITH_PROMOTION:
		case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
			bConnected = Schema->TryCreateConnection(FromPin, ToPin);
			break;
		default:
			bConnected = TryConnectWithSkelBypass(FromPin, ToPin, Schema, Response);
			break;
		}

		if (bConnected)
		{
			EntryResult->SetBoolField(TEXT("success"), true);
			EntryResult->SetStringField(TEXT("status"), TEXT("connected"));
			if (ForceBrokenCount > 0)
				EntryResult->SetNumberField(TEXT("force_broken_links"), ForceBrokenCount);
			SuccessCount++;
			ForceResolveWildcardPins(FromPin->GetOwningNodeUnchecked());
			ForceResolveWildcardPins(ToPin->GetOwningNodeUnchecked());
		}
		else
		{
			FString ConnErr = FString::Printf(TEXT("Cannot connect: %s"), *Response.Message.ToString());

			if (FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
			 && ToPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
			{
				UClass* FromClass = Cast<UClass>(FromPin->PinType.PinSubCategoryObject.Get());
				UClass* ToClass   = Cast<UClass>(ToPin->PinType.PinSubCategoryObject.Get());
				if (FromClass && ToClass && FromClass != ToClass)
				{
					const bool bUpcast   = FromClass->IsChildOf(ToClass);
					const bool bDowncast = ToClass->IsChildOf(FromClass);
					if (bUpcast || bDowncast)
					{
						UClass* TargetClass = bDowncast ? ToClass : FromClass;
						const FString TargetName = TargetClass->GetName();
						const bool bUserBP = TargetClass->ClassGeneratedBy != nullptr;
						if (bUserBP)
						{
							ConnErr += FString::Printf(
								TEXT(" Type mismatch: source is %s, target wants %s (a USER Blueprint). "
								     "Avoid k2.Cast To %s (hard-loads the target into every consumer). Prefer: "
								     "BlueprintInterface call (fn.<BPI>.<Method>, gate with DoesImplementInterface), "
								     "GetComponentByClass for component access, or an event dispatcher for signalling. "
								     "Cast is fine for engine base types only."),
								*FromClass->GetName(), *TargetName, *TargetName);
						}
						else
						{
							ConnErr += FString::Printf(
								TEXT(" Insert k2.Cast To %s between '%s' and '%s' — Object pins on cross-class calls "
								     "need exact type matching. Wire %s.%s -> cast.Object, then cast.As %s -> %s.%s."),
								*TargetName, *FromNodeId, *ToNodeId,
								*FromNodeId, *FromPinName, *TargetName, *ToNodeId, *ToPinName);
						}
					}
				}
			}

			EntryResult->SetBoolField(TEXT("success"), false);
			EntryResult->SetStringField(TEXT("error"), ConnErr);
			FailCount++;
		}
		ResultsArray.Add(MakeShared<FJsonValueObject>(EntryResult));
	}

	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetNumberField(TEXT("total"), ConnectionsArray->Num());
	ResultObj->SetNumberField(TEXT("connected"), SuccessCount);
	ResultObj->SetNumberField(TEXT("failed"), FailCount);
	ResultObj->SetArrayField(TEXT("results"), ResultsArray);

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

static void ExpandCompoundHandles(TSharedPtr<FJsonObject> Args)
{
	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr || NodesArr->IsEmpty()) return;

	TArray<TSharedPtr<FJsonValue>> NewNodes;
	TArray<TSharedPtr<FJsonValue>> InternalConns;
	bool bExpandedAny = false;

	for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArr)
	{
		TSharedPtr<FJsonObject> NodeObj = SafeAsObject(NodeVal);
		if (!NodeObj.IsValid()) { NewNodes.Add(NodeVal); continue; }

		FString Handle;
		NodeObj->TryGetStringField(TEXT("handle"), Handle);
		if (!Handle.StartsWith(TEXT("compound."), ESearchCase::CaseSensitive)) { NewNodes.Add(NodeVal); continue; }

		FString NodeId;
		NodeObj->TryGetStringField(TEXT("id"), NodeId);

		FString AfterPrefix = Handle.Mid(9);
		FString Type, Param;
		if (!AfterPrefix.Split(TEXT("."), &Type, &Param)) { NewNodes.Add(NodeVal); continue; }

		double PosX = 0.0, PosY = 0.0;
		NodeObj->TryGetNumberField(TEXT("x"), PosX);
		NodeObj->TryGetNumberField(TEXT("y"), PosY);

		auto MakeNode = [&](const FString& Id, const FString& NewHandle, double X, double Y) -> TSharedPtr<FJsonValue>
		{
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("id"),     Id);
			N->SetStringField(TEXT("handle"), NewHandle);
			N->SetNumberField(TEXT("x"),      X);
			N->SetNumberField(TEXT("y"),      Y);
			return MakeShared<FJsonValueObject>(N);
		};

		auto MakeConn = [&](const FString& From, const FString& FromPin, const FString& To, const FString& ToPin) -> TSharedPtr<FJsonValue>
		{
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("from"),      From);
			C->SetStringField(TEXT("from_pin"),  FromPin);
			C->SetStringField(TEXT("to"),        To);
			C->SetStringField(TEXT("to_pin"),    ToPin);
			return MakeShared<FJsonValueObject>(C);
		};

		if (Type.Equals(TEXT("ForEachArray"), ESearchCase::IgnoreCase))
		{
			FString ArrId = NodeId + TEXT("__arr");
			NewNodes.Add(MakeNode(ArrId, TEXT("var.get.") + Param, PosX - 250.0, PosY));
			NewNodes.Add(MakeNode(NodeId, TEXT("k2.ForEachLoop"), PosX, PosY));
			InternalConns.Add(MakeConn(ArrId, Param, NodeId, TEXT("Array")));
			bExpandedAny = true;
		}
		else if (Type.Equals(TEXT("BranchOnVar"), ESearchCase::IgnoreCase))
		{
			FString VarId = NodeId + TEXT("__var");
			NewNodes.Add(MakeNode(VarId, TEXT("var.get.") + Param, PosX - 200.0, PosY));
			NewNodes.Add(MakeNode(NodeId, TEXT("k2.Branch"), PosX, PosY));
			InternalConns.Add(MakeConn(VarId, Param, NodeId, TEXT("Condition")));
			bExpandedAny = true;
		}
		else
		{
			NewNodes.Add(NodeVal);
		}
	}

	if (!bExpandedAny) return;

	Args->SetArrayField(TEXT("nodes"), NewNodes);

	if (!InternalConns.IsEmpty())
	{
		const TArray<TSharedPtr<FJsonValue>>* ExistingConns = nullptr;
		TArray<TSharedPtr<FJsonValue>> AllConns = InternalConns;
		if (Args->TryGetArrayField(TEXT("connections"), ExistingConns) && ExistingConns)
			AllConns.Append(*ExistingConns);
		Args->SetArrayField(TEXT("connections"), AllConns);
	}
}

static UClass* ResolveUFunctionParamClass(UFunction* Func, const FString& PinName, bool bWantOutputs)
{
	if (!Func) return nullptr;
	const FName Target = PinName.IsEmpty() ? FName(TEXT("ReturnValue")) : FName(*PinName);
	for (TFieldIterator<FProperty> It(Func); It; ++It)
	{
		FProperty* P = *It;
		if (!P) continue;
		if (!P->GetFName().IsEqual(Target, ENameCase::IgnoreCase)) continue;

		const bool bIsReturn = P->HasAnyPropertyFlags(CPF_ReturnParm);
		const bool bIsOut    = P->HasAnyPropertyFlags(CPF_OutParm) && !P->HasAnyPropertyFlags(CPF_ConstParm);
		const bool bIsParamOutput = bIsReturn || bIsOut;

		if (bWantOutputs)
		{
			if (!bIsParamOutput) continue;
		}
		else
		{
			if (bIsParamOutput) continue;
		}

		if (FObjectProperty* OP = CastField<FObjectProperty>(P)) return OP->PropertyClass;
		if (FClassProperty*  CP = CastField<FClassProperty>(P))  return CP->MetaClass;
		return nullptr;
	}
	return nullptr;
}

static void SplitEndpoint(const FString& Endpoint, FString& OutNode, FString& OutPin)
{
	OutNode = Endpoint;
	OutPin  = FString();
	if (Endpoint.IsEmpty()) return;
	if (Endpoint.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase)
	 || Endpoint.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase))
	{
		OutNode = Endpoint;
		return;
	}
	int32 Dot = INDEX_NONE;
	if (Endpoint.FindChar(TEXT('.'), Dot) && Dot > 0)
	{
		OutNode = Endpoint.Left(Dot);
		OutPin  = Endpoint.Mid(Dot + 1);
	}
}

static FString FindNodeHandleInArray(const TArray<TSharedPtr<FJsonValue>>* NodesArray, const FString& NodeId)
{
	if (!NodesArray) return FString();
	for (const TSharedPtr<FJsonValue>& V : *NodesArray)
	{
		TSharedPtr<FJsonObject> N = SafeAsObject(V);
		if (!N.IsValid()) continue;
		FString Id, Handle;
		N->TryGetStringField(TEXT("id"), Id);
		if (!Id.Equals(NodeId, ESearchCase::IgnoreCase)) continue;
		N->TryGetStringField(TEXT("handle"), Handle);
		if (Handle.IsEmpty()) N->TryGetStringField(TEXT("type"), Handle);
		return Handle;
	}
	return FString();
}

static UFunction* FindEventUFunction(UBlueprint* Blueprint, const FName EventName)
{
	if (!Blueprint) return nullptr;
	for (const FBPInterfaceDescription& IFace : Blueprint->ImplementedInterfaces)
	{
		if (!IFace.Interface) continue;
		if (UFunction* F = IFace.Interface->FindFunctionByName(EventName)) return F;
	}
	if (Blueprint->ParentClass)
	{
		if (UFunction* F = Blueprint->ParentClass->FindFunctionByName(EventName)) return F;
	}
	if (Blueprint->SkeletonGeneratedClass)
	{
		if (UFunction* F = Blueprint->SkeletonGeneratedClass->FindFunctionByName(EventName)) return F;
	}
	return nullptr;
}

static void BuildSelfWireContextMap(
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray,
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	TMap<FString, UClass*>& OutMap,
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr)
{
	if (!ConnectionsArray || !Blueprint) return;

	for (const TSharedPtr<FJsonValue>& V : *ConnectionsArray)
	{
		TSharedPtr<FJsonObject> C = SafeAsObject(V);
		if (!C.IsValid()) continue;
		FString From, To, FromPin, ToPin;
		C->TryGetStringField(TEXT("from"),     From);
		C->TryGetStringField(TEXT("to"),       To);
		C->TryGetStringField(TEXT("from_pin"), FromPin);
		C->TryGetStringField(TEXT("to_pin"),   ToPin);

		FString FromNode = From, ToNode = To;
		if (FromPin.IsEmpty()) SplitEndpoint(From, FromNode, FromPin);
		if (ToPin.IsEmpty())   SplitEndpoint(To,   ToNode,   ToPin);

		if (!ToPin.Equals(TEXT("self"), ESearchCase::IgnoreCase)) continue;
		if (ToNode.IsEmpty()) continue;

		UClass* SourceClass = nullptr;

		if (FromNode.Equals(TEXT("entry"), ESearchCase::IgnoreCase) && !FromPin.IsEmpty() && Graph)
		{
			TArray<UK2Node_FunctionEntry*> EntryNodes;
			Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
			for (UK2Node_FunctionEntry* EN : EntryNodes)
			{
				if (!EN) continue;
				for (const TSharedPtr<FUserPinInfo>& UPI : EN->UserDefinedPins)
				{
					if (!UPI.IsValid()) continue;
					if (UPI->PinName.ToString().Equals(FromPin, ESearchCase::IgnoreCase))
					{
						SourceClass = Cast<UClass>(UPI->PinType.PinSubCategoryObject.Get());
						break;
					}
				}
				if (SourceClass) break;
			}
		}
		else if (FromNode.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase))
		{
			const FString VarName = FromNode.RightChop(8);
			const FName VarFName(*VarName);
			for (const FBPVariableDescription& Var : Blueprint->NewVariables)
			{
				if (Var.VarName == VarFName)
				{
					SourceClass = Cast<UClass>(Var.VarType.PinSubCategoryObject.Get());
					break;
				}
			}
			if (!SourceClass && Blueprint->SkeletonGeneratedClass)
			{
				if (FObjectProperty* OProp = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, VarFName))
					SourceClass = OProp->PropertyClass;
			}
		}
		else if (!FromPin.IsEmpty())
		{
			if (Graph && SourceClass == nullptr)
			{
				if (UEdGraphNode* SrcNode = GraphEditHelpers::FindNodeInGraph(Graph, FromNode))
				{
					for (UEdGraphPin* Pin : SrcNode->Pins)
					{
						if (!Pin) continue;
						if (Pin->Direction != EGPD_Output) continue;
						if (!Pin->PinName.ToString().Equals(FromPin, ESearchCase::IgnoreCase)) continue;
						SourceClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
						break;
					}
				}
			}

			if (SourceClass == nullptr && NodesArray)
			{
				const FString SrcHandle = FindNodeHandleInArray(NodesArray, FromNode);
				if (SrcHandle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase))
				{
					const FString AfterFn = SrcHandle.RightChop(3);
					int32 LastDot = INDEX_NONE;
					if (AfterFn.FindLastChar(TEXT('.'), LastDot) && LastDot > 0)
					{
						const FString ClassName = AfterFn.Left(LastDot);
						const FString FuncName  = AfterFn.Mid(LastDot + 1);
						if (UClass* OwnerCls = GraphEditHelpers::FindClassByName(ClassName))
						{
							if (UFunction* Fn = OwnerCls->FindFunctionByName(FName(*FuncName)))
								SourceClass = ResolveUFunctionParamClass(Fn, FromPin,  true);
						}
					}
				}
				else if (SrcHandle.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase))
				{
					const FString EventName = SrcHandle.RightChop(3);
					if (UFunction* Fn = FindEventUFunction(Blueprint, FName(*EventName)))
						SourceClass = ResolveUFunctionParamClass(Fn, FromPin,  false);
				}

				FString TraceInputPin;
				if (SrcHandle.StartsWith(TEXT("fn.KismetArrayLibrary."), ESearchCase::IgnoreCase)
				    && FromPin.Equals(TEXT("Item"), ESearchCase::IgnoreCase))
				{ TraceInputPin = TEXT("TargetArray"); }
				else if ((SrcHandle.Equals(TEXT("k2.ForEachLoop"), ESearchCase::IgnoreCase)
				       || SrcHandle.Equals(TEXT("k2.ForEachLoopWithBreak"), ESearchCase::IgnoreCase)
				       || SrcHandle.Equals(TEXT("macro.ForEachLoop"), ESearchCase::IgnoreCase)
				       || SrcHandle.Equals(TEXT("macro.ForEachLoopWithBreak"), ESearchCase::IgnoreCase))
				    && (FromPin.Equals(TEXT("ArrayElement"), ESearchCase::IgnoreCase)
				     || FromPin.Equals(TEXT("Array Element"), ESearchCase::IgnoreCase)))
				{ TraceInputPin = TEXT("Array"); }

				if (SourceClass == nullptr && !TraceInputPin.IsEmpty())
				{
					for (const TSharedPtr<FJsonValue>& V2 : *ConnectionsArray)
					{
						TSharedPtr<FJsonObject> C2 = SafeAsObject(V2);
						if (!C2.IsValid()) continue;
						FString From2, To2, FromPin2, ToPin2;
						C2->TryGetStringField(TEXT("from"),     From2);
						C2->TryGetStringField(TEXT("to"),       To2);
						C2->TryGetStringField(TEXT("from_pin"), FromPin2);
						C2->TryGetStringField(TEXT("to_pin"),   ToPin2);

						FString FromNode2 = From2, ToNode2 = To2;
						if (FromPin2.IsEmpty()) SplitEndpoint(From2, FromNode2, FromPin2);
						if (ToPin2.IsEmpty())   SplitEndpoint(To2,   ToNode2,   ToPin2);
						if (!ToNode2.Equals(FromNode, ESearchCase::IgnoreCase)) continue;
						if (!ToPin2.Equals(TraceInputPin, ESearchCase::IgnoreCase)) continue;

						if (FromNode2.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase))
						{
							const FString VarName = FromNode2.RightChop(8);
							const FName VarFName(*VarName);
							for (const FBPVariableDescription& Var : Blueprint->NewVariables)
							{
								if (Var.VarName == VarFName)
								{ SourceClass = Cast<UClass>(Var.VarType.PinSubCategoryObject.Get()); break; }
							}
						}
						else if (FromNode2.Equals(TEXT("entry"), ESearchCase::IgnoreCase) && !FromPin2.IsEmpty() && Graph)
						{
							TArray<UK2Node_FunctionEntry*> EntryNodes;
							Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
							for (UK2Node_FunctionEntry* EN : EntryNodes)
							{
								if (!EN) continue;
								for (const TSharedPtr<FUserPinInfo>& UPI : EN->UserDefinedPins)
								{
									if (UPI.IsValid() && UPI->PinName.ToString().Equals(FromPin2, ESearchCase::IgnoreCase))
									{ SourceClass = Cast<UClass>(UPI->PinType.PinSubCategoryObject.Get()); break; }
								}
								if (SourceClass) break;
							}
						}
						else if (Graph)
						{
							if (UEdGraphNode* SrcNode2 = GraphEditHelpers::FindNodeInGraph(Graph, FromNode2))
							{
								for (UEdGraphPin* Pin2 : SrcNode2->Pins)
								{
									if (!Pin2 || Pin2->Direction != EGPD_Output) continue;
									if (!Pin2->PinName.ToString().Equals(FromPin2, ESearchCase::IgnoreCase)) continue;
									SourceClass = Cast<UClass>(Pin2->PinType.PinSubCategoryObject.Get());
									break;
								}
							}
						}
						if (SourceClass) break;
					}
				}
			}
		}

		if (SourceClass)
		{
			OutMap.Add(ToNode, SourceClass);
		}
	}
}

static void PreFlightVariableHandles(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FString& GraphName,
	TSharedPtr<FJsonObject> Args,
	TArray<TSharedPtr<FJsonValue>>& PreFlightIssues,
	TArray<TSharedPtr<FJsonValue>>& RepairsMade)
{
	if (!Args.IsValid() || !Blueprint) return;
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("nodes"), NodesArray) || !NodesArray) return;

	TSet<FString> BlueprintVarNames;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		BlueprintVarNames.Add(Var.VarName.ToString().ToLower());
	if (Blueprint->SkeletonGeneratedClass)
		for (TFieldIterator<FProperty> It(Blueprint->SkeletonGeneratedClass); It; ++It)
			BlueprintVarNames.Add(It->GetName().ToLower());
	{
		TArray<UEdGraph*> PFLocalGraphs;
		Blueprint->GetAllGraphs(PFLocalGraphs);
		for (UEdGraph* G : PFLocalGraphs)
		{
			if (!G) continue;
			TArray<UK2Node_FunctionEntry*> ENs;
			G->GetNodesOfClass<UK2Node_FunctionEntry>(ENs);
			for (UK2Node_FunctionEntry* EN : ENs)
				for (const FBPVariableDescription& LV : EN->LocalVariables)
					BlueprintVarNames.Add(LV.VarName.ToString().ToLower());
		}
	}

	TSet<FString> OutputParamNames;
	TSet<FString> InputParamNames;
	if (Graph)
	{
		TArray<UK2Node_FunctionResult*> ResultNodes;
		Graph->GetNodesOfClass<UK2Node_FunctionResult>(ResultNodes);
		for (UK2Node_FunctionResult* RN : ResultNodes)
		{
			if (!RN) continue;
			for (const TSharedPtr<FUserPinInfo>& UPI : RN->UserDefinedPins)
				if (UPI.IsValid()) OutputParamNames.Add(UPI->PinName.ToString().ToLower());
		}
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
		for (UK2Node_FunctionEntry* EN : EntryNodes)
		{
			if (!EN) continue;
			for (const TSharedPtr<FUserPinInfo>& UPI : EN->UserDefinedPins)
				if (UPI.IsValid()) InputParamNames.Add(UPI->PinName.ToString().ToLower());
		}
	}

	TMap<FString, FString> VarSetOutputNodeIds;

	TMap<FString, UClass*> SelfWireCtx;
	{
		const TArray<TSharedPtr<FJsonValue>>* ConnArr = nullptr;
		if (Args->TryGetArrayField(TEXT("connections"), ConnArr))
			BuildSelfWireContextMap(ConnArr, Blueprint, Graph, SelfWireCtx, NodesArray);
	}

	for (int32 PFvi = 0; PFvi < NodesArray->Num(); PFvi++)
	{
		TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[PFvi]);
		if (!NodeObj.IsValid()) continue;
		FString NId, NHandle;
		NodeObj->TryGetStringField(TEXT("id"), NId);
		NodeObj->TryGetStringField(TEXT("handle"), NHandle);
		if (NHandle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), NHandle);
		if (NHandle.IsEmpty()) continue;

		const bool bIsSet = NHandle.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase);
		const bool bIsGet = NHandle.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase);
		if (!bIsGet && !bIsSet) continue;

		const FString VarName = NHandle.Mid(8);
		if (VarName.IsEmpty()) continue;
		const FString VarLow = VarName.ToLower();
		if (BlueprintVarNames.Contains(VarLow)) continue;

		if (UClass* const* CtxPtr = SelfWireCtx.Find(NId))
		{
			if (FindFProperty<FProperty>(*CtxPtr, FName(*VarName)))
				continue;
		}

		if (bIsSet && OutputParamNames.Contains(VarLow))
		{
			VarSetOutputNodeIds.Add(NId, VarName);
			TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
			Repair->SetStringField(TEXT("repair"), TEXT("var_set_output_param_rewritten"));
			Repair->SetStringField(TEXT("node_id"), NId);
			Repair->SetStringField(TEXT("variable_name"), VarName);
			Repair->SetStringField(TEXT("reason"), FString::Printf(
				TEXT("'%s' is the OUTPUT PARAMETER of function '%s' — there is no var.set for it. "
				     "Removed the var.set node and rewrote its connections onto the return node "
				     "(value-pin → return.%s, execute → return.execute). For early-return with different "
				     "values in future builds, add an extra return node via {\"handle\":\"return.new\"} and "
				     "wire to <id>.execute + <id>.%s."),
				*VarName, *GraphName, *VarName, *VarName));
			RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
			continue;
		}

		if (bIsSet && InputParamNames.Contains(VarLow))
		{
			TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("type"), TEXT("var_set_on_input_param"));
			Issue->SetStringField(TEXT("node_id"), NId);
			Issue->SetStringField(TEXT("variable_name"), VarName);
			Issue->SetStringField(TEXT("hint"), FString::Printf(
				TEXT("'%s' is an INPUT PARAMETER of function '%s' — input params are read-only. "
				     "Read its value via entry.%s, transform it, then wire the result wherever you need it "
				     "(another node's data input, a class variable via var.set, or a return-node output)."),
				*VarName, *GraphName, *VarName));
			PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
			continue;
		}

		if (bIsGet && InputParamNames.Contains(VarLow))
		{
			TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("type"), TEXT("var_get_on_input_param"));
			Issue->SetStringField(TEXT("node_id"), NId);
			Issue->SetStringField(TEXT("variable_name"), VarName);
			Issue->SetStringField(TEXT("hint"), FString::Printf(
				TEXT("'%s' is an INPUT PARAMETER of function '%s', not a class variable — drop the var.get node and "
				     "wire 'entry.%s' directly to the consumer pin (entry.%s is a value pin on the function-entry node)."),
				*VarName, *GraphName, *VarName, *VarName));
			PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
			continue;
		}

		TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("type"), TEXT("variable_not_found"));
		Issue->SetStringField(TEXT("node_id"), NId);
		Issue->SetStringField(TEXT("variable_name"), VarName);
		Issue->SetStringField(TEXT("hint"), FString::Printf(
			TEXT("Node '%s' uses variable '%s' but that variable does not exist on this blueprint. "
			     "Call blueprint(action='add_variable', variable_name='%s', variable_type='...') "
			     "BEFORE this build_blueprint_graph call, then retry. "
			     "The build will fail with 'Variable not found' for every node referencing '%s'."),
			*NId, *VarName, *VarName, *VarName));
		PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
	}

	if (VarSetOutputNodeIds.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> NewNodes;
		NewNodes.Reserve(NodesArray->Num());
		for (const TSharedPtr<FJsonValue>& V : *NodesArray)
		{
			TSharedPtr<FJsonObject> N = SafeAsObject(V);
			FString NIdCheck;
			if (N.IsValid()) N->TryGetStringField(TEXT("id"), NIdCheck);
			if (!NIdCheck.IsEmpty() && VarSetOutputNodeIds.Contains(NIdCheck)) continue;
			NewNodes.Add(V);
		}
		Args->SetArrayField(TEXT("nodes"), NewNodes);

		const TArray<TSharedPtr<FJsonValue>>* ConnArray = nullptr;
		if (Args->TryGetArrayField(TEXT("connections"), ConnArray) && ConnArray)
		{
			TArray<TSharedPtr<FJsonValue>> NewConns;
			NewConns.Reserve(ConnArray->Num());
			for (const TSharedPtr<FJsonValue>& V : *ConnArray)
			{
				TSharedPtr<FJsonObject> C = SafeAsObject(V);
				if (!C.IsValid()) { NewConns.Add(V); continue; }
				FString From, To;
				C->TryGetStringField(TEXT("from"), From);
				C->TryGetStringField(TEXT("to"), To);

				auto SplitPin = [](const FString& Endpoint, FString& OutNode, FString& OutPin) {
					int32 DotIdx = INDEX_NONE;
					if (Endpoint.FindChar(TEXT('.'), DotIdx) && DotIdx > 0)
					{
						OutNode = Endpoint.Left(DotIdx);
						OutPin  = Endpoint.Mid(DotIdx + 1);
					}
				};

				FString FromNode, FromPin, ToNode, ToPin;
				SplitPin(From, FromNode, FromPin);
				SplitPin(To,   ToNode,   ToPin);

				const FString* OutParamFrom = FromNode.IsEmpty() ? nullptr : VarSetOutputNodeIds.Find(FromNode);
				const FString* OutParamTo   = ToNode.IsEmpty()   ? nullptr : VarSetOutputNodeIds.Find(ToNode);

				if (OutParamTo)
				{
					if (ToPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
						C->SetStringField(TEXT("to"), TEXT("return.execute"));
					else
						C->SetStringField(TEXT("to"), FString::Printf(TEXT("return.%s"), **OutParamTo));
					NewConns.Add(MakeShared<FJsonValueObject>(C));
					continue;
				}
				if (OutParamFrom)
					continue;

				NewConns.Add(V);
			}
			Args->SetArrayField(TEXT("connections"), NewConns);
		}
	}
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4883)
#endif
void HandleBuildGraph(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsGraphContextReady()) { OutError = TEXT("Blueprint editor context unavailable — ensure the editor is fully loaded and try again."); return; }
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::GraphAuthoring)) { OutError = TEXT("Graph authoring isn't enabled for this install's profile yet — reconnect and try again."); return; }

	if (IUECPCoreModule::IsAvailable() &&
		IUECPCoreModule::Get().GetArchitectService().MaybeApplyCachedTemplate(Args, OutJsonString, OutError))
	{
		return;
	}

	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	for (const TCHAR* Field : { TEXT("nodes"), TEXT("connections"), TEXT("defaults"), TEXT("comments") })
	{
		FString StrVal;
		const TArray<TSharedPtr<FJsonValue>>* CheckArr = nullptr;
		if (!Args->TryGetArrayField(Field, CheckArr) &&
			Args->TryGetStringField(Field, StrVal) && !StrVal.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Parsed;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(StrVal);
			if (FJsonSerializer::Deserialize(R, Parsed))
				Args->SetArrayField(Field, Parsed);
		}
	}

	for (const TCHAR* Field : { TEXT("nodes"), TEXT("connections"), TEXT("defaults"), TEXT("comments") })
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() == 0) continue;
		bool bAnyString = false;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
			if (V.IsValid() && V->Type == EJson::String) { bAnyString = true; break; }
		if (!bAnyString) continue;

		TArray<TSharedPtr<FJsonValue>> Fixed;
		Fixed.Reserve(Arr->Num());
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				const FString Str = V->AsString();
				TSharedPtr<FJsonObject> ParsedObj;
				TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Str);
				if (FJsonSerializer::Deserialize(R, ParsedObj) && ParsedObj.IsValid())
					Fixed.Add(MakeShared<FJsonValueObject>(ParsedObj));
				else
					Fixed.Add(V);
			}
			else
			{
				Fixed.Add(V);
			}
		}
		Args->SetArrayField(Field, Fixed);
	}
	{
		const TArray<TSharedPtr<FJsonValue>>* NodesCheck = nullptr;
		FString ItemsStr, NodesStr;
		const bool bNodesAbsent = !Args->TryGetArrayField(TEXT("nodes"), NodesCheck)
		                       && !Args->TryGetStringField(TEXT("nodes"), NodesStr);
		if (bNodesAbsent && Args->TryGetStringField(TEXT("items"), ItemsStr) && !ItemsStr.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Parsed;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(ItemsStr);
			if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.Num() > 0)
				Args->SetArrayField(TEXT("nodes"), Parsed);
		}
	}

	ExpandCompoundHandles(Args);

	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	bool bHasNodes = Args->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray && NodesArray->Num() > 0;
	if (!bHasNodes)
	{
		FString CodeParam;
		const bool bPassedCode = Args->TryGetStringField(TEXT("code"), CodeParam) && !CodeParam.IsEmpty();
		if (bPassedCode)
		{
			OutError = TEXT("build_blueprint_graph does not accept a 'code' parameter. Use nodes=[{\"id\":\"n1\",\"handle\":\"ev.ReceiveBeginPlay\",\"x\":0,\"y\":0},...] instead. See KEY HANDLES in system prompt.");
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* CheckNodes = nullptr;
		if (!Args->TryGetArrayField(TEXT("nodes"), CheckNodes))
		{
			FString NodeStr;
			if (Args->TryGetStringField(TEXT("nodes"), NodeStr) && !NodeStr.IsEmpty())
			{
				OutError = TEXT("'nodes' was passed as a string, not an array. Pass nodes as a JSON array: nodes=[{\"id\":\"n1\",\"handle\":\"ev.ReceiveBeginPlay\",...},...]. Do not stringify or quote the array value.");
				return;
			}
			OutError = TEXT("Missing required parameter: nodes. Pass nodes as a JSON array (not a string, not C++ code). Use nodes:[] for connections-only graphs.");
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
		const bool bHasConns = Args->TryGetArrayField(TEXT("connections"), ConnsArr) && ConnsArr && ConnsArr->Num() > 0;
		const TArray<TSharedPtr<FJsonValue>>* DefaultsArr = nullptr;
		const bool bHasDefaults = Args->TryGetArrayField(TEXT("defaults"), DefaultsArr) && DefaultsArr && DefaultsArr->Num() > 0;
		if (!bHasConns && !bHasDefaults)
		{
			FString GraphNameForHint = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphNameForHint);
			OutError = FString::Printf(
				TEXT("EMPTY BUILD REJECTED: build_blueprint_graph with nodes:[], connections:[], defaults:[] is a NO-OP — it does NOT clear the graph. To clear AND rebuild in one call: add clear_before_build=true to your build_blueprint_graph with real nodes/connections. To clear only: blueprint(action='clear_blueprint_graph', blueprint_path='%s', graph_name='%s')."),
				*BlueprintPath, *GraphNameForHint);
			return;
		}
	}

	FString GraphName;
	if (!Args->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
		if (!Args->TryGetStringField(TEXT("function_name"), GraphName) || GraphName.IsEmpty())
			Args->TryGetStringField(TEXT("graph"), GraphName);
	if (GraphName.IsEmpty()) GraphName = TEXT("EventGraph");

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	Args->TryGetArrayField(TEXT("connections"), ConnectionsArray);

	const TArray<TSharedPtr<FJsonValue>>* DefaultsArray = nullptr;
	Args->TryGetArrayField(TEXT("defaults"), DefaultsArray);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		TArray<FString> Names;
		Names.Reserve(AllGraphs.Num());
		for (UEdGraph* G : AllGraphs) { if (G) Names.Add(G->GetName()); }
		const FString Available = Names.Num() > 0
			? FString::Join(Names, TEXT(", "))
			: FString(TEXT("(none)"));
		OutError = FString::Printf(
			TEXT("Graph not found: '%s'. Available graphs: %s. Pass graph_name= the function/event-graph name (function_name and graph also accepted as aliases)."),
			*GraphName, *Available);
		return;
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		OutError = TEXT("Graph has no schema");
		return;
	}

	bool bClearFirst = false;
	{
		if (!Args->TryGetBoolField(TEXT("clear_before_build"), bClearFirst))
		{
			FString ClearStr;
			if (Args->TryGetStringField(TEXT("clear_before_build"), ClearStr))
				bClearFirst = ClearStr.Equals(TEXT("true"), ESearchCase::IgnoreCase);
		}
		if (!bClearFirst)
		{
			if (!Args->TryGetBoolField(TEXT("clear_graph"), bClearFirst))
			{
				FString ClearStr;
				if (Args->TryGetStringField(TEXT("clear_graph"), ClearStr))
					bClearFirst = ClearStr.Equals(TEXT("true"), ESearchCase::IgnoreCase);
			}
		}

		if (bClearFirst)
		{
			bool bClearForce = false;
			Args->TryGetBoolField(TEXT("clear_force"), bClearForce);
			int32 UserNodeCount = 0;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node)) continue;
				if (IsClearProtectedNode(Node)) continue;
				++UserNodeCount;
			}
			const int32 Threshold = 5;
			if (!bClearForce && UserNodeCount > Threshold)
			{
				TArray<FString> SampleTitles;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!IsValid(Node) || IsClearProtectedNode(Node)) continue;
					SampleTitles.Add(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					if (SampleTitles.Num() >= 6) break;
				}
				const FString Sample = SampleTitles.Num() > 0 ? FString::Join(SampleTitles, TEXT(", ")) : FString();
				OutError = FString::Printf(
					TEXT("REFUSED: clear_before_build=true would wipe %d user nodes from graph '%s' — that's almost always a mistake. ")
					TEXT("Rebuild without clear_before_build (additive build over existing nodes), OR call clear_blueprint_graph(anchor_id=...) first ")
					TEXT("to surgically remove just the failing event chain, OR retry with `clear_force=true` if you intend a full wipe. ")
					TEXT("Sample of nodes that would be deleted: [%s]."),
					UserNodeCount, *GraphName, *Sample);
				return;
			}

			const FScopedTransaction ClearTx(FText::FromString(FString::Printf(TEXT("Clear Graph Before Build: %s"), *GraphName)));
			Blueprint->Modify();
			Graph->Modify();

			TArray<UEdGraphNode*> ToRemove;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node)) continue;
				if (IsClearProtectedNode(Node)) continue;
				ToRemove.Add(Node);
			}
			for (UEdGraphNode* Node : ToRemove)
			{
				Node->Modify();
				Node->DestroyNode();
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	bool bEnablePreFlight = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("EnablePreFlight"), bEnablePreFlight, FSettingsManager::GetGlobalConfigPath());

	TArray<TSharedPtr<FJsonValue>> PreFlightIssues;
	TSet<int32> ConnectionsWithPreFlightIssue;
	TMap<FString, FString> NodeIdFuzzyRewrites;
	TArray<TSharedPtr<FJsonValue>> RepairsMade;
	TSet<FString> PreFlightSkipNodeIds;
	TSet<FString> PreFlightDropNodeIds;
	TMap<FString, FString> PreFlightIdAliases;
	TSet<int32>   PreFlightDropConnectionIndices;

	if (bEnablePreFlight && NodesArray)
	{
		TSet<FString> DeclaredNodeIds;
		DeclaredNodeIds.Add(TEXT("entry"));
		DeclaredNodeIds.Add(TEXT("return"));
		TMap<FString, int32> IdCounts;

		for (int32 PFi = 0; PFi < NodesArray->Num(); PFi++)
		{
			TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[PFi]);
			if (!NodeObj.IsValid()) continue;

			FString NodeId, Handle;
			NodeObj->TryGetStringField(TEXT("id"), NodeId);
			NodeObj->TryGetStringField(TEXT("handle"), Handle);
			if (Handle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), Handle);
			if (Handle.IsEmpty()) NodeObj->TryGetStringField(TEXT("node_type"), Handle);

			if (!NodeId.IsEmpty())
			{
				DeclaredNodeIds.Add(NodeId);
				IdCounts.FindOrAdd(NodeId)++;
			}

			{
				bool bIsPlaceholder = false;
				FString PlaceholderHint;
				const FString HandleLower = Handle.ToLower();
				if (HandleLower == TEXT("k2.make") || HandleLower == TEXT("k2.make ") ||
					HandleLower == TEXT("k2.break") || HandleLower == TEXT("k2.break "))
				{
					bIsPlaceholder = true;
					PlaceholderHint = FString::Printf(
						TEXT("'%s' alone is the doc placeholder, not a valid handle — it needs the struct/class name. "
						     "Use `k2.Make <StructName>` / `k2.Break <StructName>` (e.g. `k2.Make S_InventorySlot`, `k2.Break FVector`). "
						     "For common math types prefer `fn.KismetMathLibrary.MakeVector` / `BreakVector` / `MakeRotator` / `BreakRotator`."),
						*Handle);
				}
				else if (HandleLower.StartsWith(TEXT("var.get.")) || HandleLower.StartsWith(TEXT("var.set.")))
				{
					const FString VarName = Handle.Mid(8);
					if (VarName.Len() == 1 && VarName[0] >= TEXT('A') && VarName[0] <= TEXT('Z') &&
						(VarName == TEXT("X") || VarName == TEXT("Y") || VarName == TEXT("Z") ||
						 VarName == TEXT("N") || VarName == TEXT("K") || VarName == TEXT("T")))
					{
						bIsPlaceholder = true;
						PlaceholderHint = FString::Printf(
							TEXT("'%s' looks like the doc placeholder — `X` / `Y` / `Z` / `N` / `K` / `T` in the docs stand in for "
							     "the actual variable name. Use the real variable's name, e.g. `var.get.CurrentHealth`. "
							     "If you genuinely have a variable named '%s', rename it to something descriptive; "
							     "single-letter variables collide with the doc syntax."),
							*Handle, *VarName);
					}
				}

				if (bIsPlaceholder)
				{
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("literal_doc_placeholder_rejected"));
					Repair->SetStringField(TEXT("node_id"), NodeId);
					Repair->SetStringField(TEXT("bad_handle"), Handle);
					Repair->SetStringField(TEXT("hint"), PlaceholderHint);
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					if (!NodeId.IsEmpty()) PreFlightDropNodeIds.Add(NodeId);
					continue;
				}
			}

			if (NodeId == TEXT("return") || NodeId == TEXT("entry")
				|| Handle.Equals(TEXT("return"), ESearchCase::IgnoreCase)
				|| Handle.Equals(TEXT("entry"), ESearchCase::IgnoreCase))
			{
				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("removed_reserved_node"));
				Repair->SetStringField(TEXT("node_id"), NodeId);
				Repair->SetStringField(TEXT("bad_handle"), Handle);
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("'%s' is a reserved node that is auto-created when the function is added. "
					     "Do NOT include it in nodes[] — this creates a duplicate that causes 'failed:1' "
					     "and leaves orphaned nodes. Remove from nodes[] and only reference it in "
					     "connections[] (e.g. {to_node:'return', to_pin:'execute'} or {from_node:'entry', from_pin:'ParamName'})."),
					*NodeId));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				if (!NodeId.IsEmpty()) PreFlightSkipNodeIds.Add(NodeId);
				continue;
			}

			if (!Handle.IsEmpty()
				&& Handle.StartsWith(TEXT("entry."), ESearchCase::IgnoreCase)
				&& !Handle.Equals(TEXT("entry"), ESearchCase::IgnoreCase))
			{
				const FString PinName = Handle.RightChop(6);
				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("removed_invalid_node"));
				Repair->SetStringField(TEXT("node_id"), NodeId);
				Repair->SetStringField(TEXT("bad_handle"), Handle);
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("'%s' is not a node handle — entry.X is a PIN on the FunctionEntry node, not a node "
					     "you place. Remove this from nodes[] and reference it directly in connections as "
					     "{from_node:'entry', from_pin:'%s'}. Node skipped."),
					*Handle, *PinName));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				if (!NodeId.IsEmpty()) PreFlightSkipNodeIds.Add(NodeId);
			}

			if (!Handle.IsEmpty()
				&& !GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase)
				&& !GraphName.IsEmpty())
			{
				static const TArray<FString> LatentHandles = {
					TEXT("fn.KismetSystemLibrary.Delay"),
					TEXT("fn.KismetSystemLibrary.RetriggerableDelay"),
					TEXT("fn.KismetSystemLibrary.MoveComponentTo"),
					TEXT("k2.Delay"),
					TEXT("k2.RetriggerableDelay"),
					TEXT("k2.MoveComponentTo"),
				};
				for (const FString& LH : LatentHandles)
				{
					if (Handle.Equals(LH, ESearchCase::IgnoreCase))
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("type"), TEXT("latent_node_in_function_graph"));
						Issue->SetStringField(TEXT("node_id"), NodeId);
						Issue->SetStringField(TEXT("handle"), Handle);
						Issue->SetStringField(TEXT("graph_name"), GraphName);
						Issue->SetStringField(TEXT("hint"), FString::Printf(
							TEXT("Latent node '%s' is only valid in EventGraph. In function graph '%s' it "
							     "compiles but triggers a compile error 'contains a latent call'. "
							     "Use fn.KismetSystemLibrary.K2_SetTimer (with a callback function) "
							     "for timed logic inside function graphs, or move the delay logic to EventGraph."),
							*Handle, *GraphName));
						PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						if (!NodeId.IsEmpty()) PreFlightDropNodeIds.Add(NodeId);
						break;
					}
				}
			}

			if (!Handle.IsEmpty()
				&& Handle.StartsWith(TEXT("k2.Cast"), ESearchCase::IgnoreCase)
				&& !Handle.StartsWith(TEXT("k2.Cast To "), ESearchCase::IgnoreCase))
			{
				FString GuessedClass = Handle.RightChop(7);
				if (GuessedClass.StartsWith(TEXT("To"), ESearchCase::IgnoreCase))
					GuessedClass = GuessedClass.RightChop(2);
				GuessedClass = GuessedClass.TrimStart().Replace(TEXT("_"), TEXT(""));

				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("wrong_cast_handle_format"));
				Repair->SetStringField(TEXT("node_id"), NodeId);
				Repair->SetStringField(TEXT("bad_handle"), Handle);
				Repair->SetStringField(TEXT("correct_handle"),
					FString::Printf(TEXT("k2.Cast To %s"), *GuessedClass));
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("Cast handle format requires SPACES: 'k2.Cast To ClassName'. "
					     "Got '%s' which is missing the spaces. "
					     "Correct handle: 'k2.Cast To %s'. Node skipped — fix the handle and re-submit."),
					*Handle, *GuessedClass));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				if (!NodeId.IsEmpty()) PreFlightSkipNodeIds.Add(NodeId);
			}

			if (!Handle.IsEmpty())
			{
				static const TMap<FString, FString> HandleCorrections = {
					{ TEXT("k2.MakeLiteralBool"),                          TEXT("fn.KismetSystemLibrary.MakeLiteralBool") },
					{ TEXT("k2.MakeLiteralInt"),                           TEXT("fn.KismetSystemLibrary.MakeLiteralInt") },
					{ TEXT("k2.MakeLiteralFloat"),                         TEXT("fn.KismetSystemLibrary.MakeLiteralFloat") },
					{ TEXT("k2.MakeLiteralString"),                        TEXT("fn.KismetSystemLibrary.MakeLiteralString") },
					{ TEXT("k2.MakeLiteralName"),                          TEXT("fn.KismetSystemLibrary.MakeLiteralName") },
					{ TEXT("fn.KismetMathLibrary.MakeLiteralBool"),        TEXT("fn.KismetSystemLibrary.MakeLiteralBool") },
					{ TEXT("fn.KismetMathLibrary.MakeLiteralInt"),         TEXT("fn.KismetSystemLibrary.MakeLiteralInt") },
					{ TEXT("fn.KismetMathLibrary.MakeLiteralFloat"),       TEXT("fn.KismetSystemLibrary.MakeLiteralFloat") },
					{ TEXT("fn.KismetMathLibrary.MakeLiteralString"),      TEXT("fn.KismetSystemLibrary.MakeLiteralString") },

					{ TEXT("k2.PrintString"),                              TEXT("fn.KismetSystemLibrary.PrintString") },
					{ TEXT("k2.PrintText"),                                TEXT("fn.KismetSystemLibrary.PrintText") },

					{ TEXT("k2.SetTimer"),                                 TEXT("fn.KismetSystemLibrary.K2_SetTimer") },
					{ TEXT("k2.K2_SetTimer"),                              TEXT("fn.KismetSystemLibrary.K2_SetTimer") },
					{ TEXT("k2.SetTimerByEvent"),                          TEXT("fn.KismetSystemLibrary.K2_SetTimerDelegate") },
					{ TEXT("fn.KismetSystemLibrary.SetTimer"),             TEXT("fn.KismetSystemLibrary.K2_SetTimer") },
					{ TEXT("fn.KismetSystemLibrary.SetTimerByFunctionName"), TEXT("fn.KismetSystemLibrary.K2_SetTimer") },
					{ TEXT("fn.GameplayStatics.SetTimerByEvent"),          TEXT("fn.KismetSystemLibrary.K2_SetTimer") },
					{ TEXT("fn.KismetSystemLibrary.SetTimerByEvent"),      TEXT("fn.KismetSystemLibrary.K2_SetTimer") },
					{ TEXT("fn.KismetSystemLibrary.ClearTimer"),           TEXT("fn.KismetSystemLibrary.K2_ClearTimer") },
					{ TEXT("k2.ClearTimer"),                               TEXT("fn.KismetSystemLibrary.K2_ClearTimer") },
					{ TEXT("k2.K2_ClearTimer"),                            TEXT("fn.KismetSystemLibrary.K2_ClearTimer") },

					{ TEXT("fn.KismetSystemLibrary.Conv_IntToString"),     TEXT("fn.KismetStringLibrary.Conv_IntToString") },
					{ TEXT("fn.KismetSystemLibrary.Conv_FloatToString"),   TEXT("fn.KismetStringLibrary.Conv_DoubleToString") },
					{ TEXT("fn.KismetSystemLibrary.Conv_DoubleToString"),  TEXT("fn.KismetStringLibrary.Conv_DoubleToString") },
					{ TEXT("fn.KismetSystemLibrary.Conv_BoolToString"),    TEXT("fn.KismetStringLibrary.Conv_BoolToString") },
					{ TEXT("fn.KismetSystemLibrary.Conv_VectorToString"),  TEXT("fn.KismetStringLibrary.Conv_VectorToString") },
					{ TEXT("fn.KismetMathLibrary.Conv_IntToString"),       TEXT("fn.KismetStringLibrary.Conv_IntToString") },
					{ TEXT("k2.Conv_IntToString"),                         TEXT("fn.KismetStringLibrary.Conv_IntToString") },
					{ TEXT("fn.KismetStringLibrary.Conv_FloatToString"),   TEXT("fn.KismetStringLibrary.Conv_DoubleToString") },

					{ TEXT("fn.KismetTextLibrary.AsText_String"),          TEXT("fn.KismetTextLibrary.Conv_StringToText") },
					{ TEXT("fn.KismetTextLibrary.StringToText"),           TEXT("fn.KismetTextLibrary.Conv_StringToText") },
					{ TEXT("fn.KismetStringLibrary.Conv_StringToText"),    TEXT("fn.KismetTextLibrary.Conv_StringToText") },
					{ TEXT("fn.KismetTextLibrary.TextFromString"),         TEXT("fn.KismetTextLibrary.Conv_StringToText") },
					{ TEXT("fn.KismetTextLibrary.TextToString"),           TEXT("fn.KismetTextLibrary.Conv_TextToString") },
					{ TEXT("fn.KismetTextLibrary.AsString_Text"),          TEXT("fn.KismetTextLibrary.Conv_TextToString") },

					{ TEXT("fn.KismetSystemLibrary.EqualEqual_ObjectObject"), TEXT("fn.KismetMathLibrary.EqualEqual_ObjectObject") },
					{ TEXT("fn.KismetSystemLibrary.NotEqual_ObjectObject"),   TEXT("fn.KismetMathLibrary.NotEqual_ObjectObject") },

					{ TEXT("fn.KismetMathLibrary.BooleanNOT"),             TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("fn.KismetMathLibrary.Not_Bool"),               TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("fn.KismetMathLibrary.Not"),                    TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("k2.Not_PreBool"),                              TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("k2.Toggle Bool"),                              TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("k2.BooleanNOT"),                               TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("k2.BooleanNot"),                               TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("k2.NOT"),                                      TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("k2.Bool"),                                     TEXT("fn.KismetMathLibrary.Not_PreBool") },

					{ TEXT("k2.Self.self"),                                TEXT("k2.Self") },
					{ TEXT("k2.self.self"),                                TEXT("k2.Self") },

					{ TEXT("fn.Pawn.GetPlayerState"),                      TEXT("var.get.PlayerState") },
					{ TEXT("fn.Pawn.GetController"),                       TEXT("var.get.Controller") },
					{ TEXT("fn.Pawn.GetInstigatorController"),             TEXT("var.get.Controller") },

					{ TEXT("fn.KismetMathLibrary.Rotator_MakeRotator"),    TEXT("fn.KismetMathLibrary.MakeRotator") },
					{ TEXT("fn.KismetMathLibrary.Vector_MakeVector"),      TEXT("fn.KismetMathLibrary.MakeVector") },
					{ TEXT("fn.KismetMathLibrary.MakeRotFromXYZ"),         TEXT("fn.KismetMathLibrary.MakeRotator") },
					{ TEXT("fn.Actor.GetActorNameOrLabel"),                TEXT("fn.KismetSystemLibrary.GetDisplayName") },
					{ TEXT("fn.Actor.GetDisplayName"),                     TEXT("fn.KismetSystemLibrary.GetDisplayName") },
					{ TEXT("fn.PlayerController.SetInputMode_UIOnlyEx"),      TEXT("fn.WidgetBlueprintLibrary.SetInputMode_UIOnly") },
					{ TEXT("fn.PlayerController.SetInputMode_GameAndUIEx"),   TEXT("fn.WidgetBlueprintLibrary.SetInputMode_GameAndUI") },
					{ TEXT("fn.PlayerController.SetInputMode_GameOnlyEx"),    TEXT("fn.WidgetBlueprintLibrary.SetInputMode_GameOnly") },
					{ TEXT("fn.SceneComponent.GetRelativeRotation"),       TEXT("fn.SceneComponent.GetRelativeTransform") },
					{ TEXT("fn.SceneComponent.GetRelativeLocation"),       TEXT("fn.SceneComponent.GetRelativeTransform") },
					{ TEXT("fn.SceneComponent.GetRelativeScale3D"),        TEXT("fn.SceneComponent.GetRelativeTransform") },
					{ TEXT("fn.AIController.SetFocus"),                    TEXT("fn.AIController.K2_SetFocus") },
					{ TEXT("fn.AIController.ClearFocus"),                  TEXT("fn.AIController.K2_ClearFocus") },

					{ TEXT("branch"),                                      TEXT("k2.Branch") },
					{ TEXT("Branch"),                                      TEXT("k2.Branch") },
					{ TEXT("k2.IfThenElse"),                               TEXT("k2.Branch") },
					{ TEXT("k2.If"),                                       TEXT("k2.Branch") },

					{ TEXT("fn.KismetArrayLibrary.Array_Empty"),           TEXT("fn.KismetArrayLibrary.Array_Clear") },

					{ TEXT("ev.EventReceiveConstruct"),                    TEXT("ev.Construct") },
					{ TEXT("ev.ReceiveConstruct"),                         TEXT("ev.Construct") },
					{ TEXT("ev.OnConstruct"),                              TEXT("ev.Construct") },

					{ TEXT("Add_IntInt"),                                  TEXT("fn.KismetMathLibrary.Add_IntInt") },
					{ TEXT("Subtract_IntInt"),                             TEXT("fn.KismetMathLibrary.Subtract_IntInt") },
					{ TEXT("Multiply_IntInt"),                             TEXT("fn.KismetMathLibrary.Multiply_IntInt") },
					{ TEXT("Divide_IntInt"),                               TEXT("fn.KismetMathLibrary.Divide_IntInt") },
					{ TEXT("Add_DoubleDouble"),                            TEXT("fn.KismetMathLibrary.Add_DoubleDouble") },
					{ TEXT("Subtract_DoubleDouble"),                       TEXT("fn.KismetMathLibrary.Subtract_DoubleDouble") },
					{ TEXT("Multiply_DoubleDouble"),                       TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("Divide_DoubleDouble"),                         TEXT("fn.KismetMathLibrary.Divide_DoubleDouble") },
					{ TEXT("Greater_DoubleDouble"),                        TEXT("fn.KismetMathLibrary.Greater_DoubleDouble") },
					{ TEXT("GreaterEqual_DoubleDouble"),                   TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("Less_DoubleDouble"),                           TEXT("fn.KismetMathLibrary.Less_DoubleDouble") },
					{ TEXT("LessEqual_DoubleDouble"),                      TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("EqualEqual_DoubleDouble"),                     TEXT("fn.KismetMathLibrary.EqualEqual_DoubleDouble") },
					{ TEXT("NotEqual_DoubleDouble"),                       TEXT("fn.KismetMathLibrary.NotEqual_DoubleDouble") },

					{ TEXT("fn.KismetMathLibrary.Vector_Length"),          TEXT("fn.KismetMathLibrary.VSize") },
					{ TEXT("fn.KismetMathLibrary.VectorLength"),           TEXT("fn.KismetMathLibrary.VSize") },
					{ TEXT("fn.KismetMathLibrary.GetVectorLength"),        TEXT("fn.KismetMathLibrary.VSize") },

					{ TEXT("fn.KismetMathLibrary.Add_RotatorRotator"),        TEXT("fn.KismetMathLibrary.ComposeRotators") },
					{ TEXT("fn.KismetMathLibrary.Subtract_RotatorRotator"),   TEXT("fn.KismetMathLibrary.NormalizedDeltaRotator") },
					{ TEXT("fn.KismetMathLibrary.GetDegreesBetweenRotators"), TEXT("fn.KismetMathLibrary.NormalizedDeltaRotator") },

					{ TEXT("fn.KismetArrayLibrary.MakeArray"),             TEXT("k2.MakeArray") },
					{ TEXT("fn.KismetSystemLibrary.MakeArray"),            TEXT("k2.MakeArray") },

					{ TEXT("fn.KistemArrayLibrary.Array_Get"),             TEXT("fn.KismetArrayLibrary.Array_Get") },
					{ TEXT("fn.KistemArrayLibrary.Array_Set"),             TEXT("fn.KismetArrayLibrary.Array_Set") },
					{ TEXT("fn.KistemArrayLibrary.Array_Add"),             TEXT("fn.KismetArrayLibrary.Array_Add") },
					{ TEXT("fn.KistemArrayLibrary.Array_Length"),          TEXT("fn.KismetArrayLibrary.Array_Length") },
					{ TEXT("fn.KistemArrayLibrary.Array_Clear"),           TEXT("fn.KismetArrayLibrary.Array_Clear") },
					{ TEXT("fn.KistemArrayLibrary.Array_Contains"),        TEXT("fn.KismetArrayLibrary.Array_Contains") },
					{ TEXT("fn.KistemArrayLibrary.Array_Find"),            TEXT("fn.KismetArrayLibrary.Array_Find") },
					{ TEXT("fn.KistemArrayLibrary.Array_Remove"),          TEXT("fn.KismetArrayLibrary.Array_Remove") },
					{ TEXT("fn.KistemArrayLibrary.Array_RemoveItem"),      TEXT("fn.KismetArrayLibrary.Array_RemoveItem") },

					{ TEXT("fn.AIController.GetBlackboardComponent"),      TEXT("fn.AIBlueprintHelperLibrary.GetBlackboard") },
					{ TEXT("fn.AIController.GetBlackboard"),               TEXT("fn.AIBlueprintHelperLibrary.GetBlackboard") },

					{ TEXT("fn.Character.ApplyDamage"),                    TEXT("fn.GameplayStatics.ApplyDamage") },
					{ TEXT("fn.Actor.ApplyDamage"),                        TEXT("fn.GameplayStatics.ApplyDamage") },
					{ TEXT("fn.Pawn.ApplyDamage"),                         TEXT("fn.GameplayStatics.ApplyDamage") },

					{ TEXT("fn.Character.GetMesh"),                        TEXT("var.get.Mesh") },
					{ TEXT("fn.Pawn.GetMesh"),                             TEXT("var.get.Mesh") },
					{ TEXT("fn.Character.GetCapsuleComponent"),            TEXT("var.get.CapsuleComponent") },
					{ TEXT("fn.Pawn.GetCapsuleComponent"),                 TEXT("var.get.CapsuleComponent") },
					{ TEXT("fn.Character.GetCharacterMovement"),           TEXT("var.get.CharacterMovement") },
					{ TEXT("fn.Actor.GetRootComponent"),                   TEXT("var.get.RootComponent") },
					{ TEXT("fn.Actor.GetActorRootComponent"),              TEXT("var.get.RootComponent") },

					{ TEXT("fn.EnhancedInputLocalPlayerSubsystem.GetEnhancedInputLocalPlayerSubsystem"), TEXT("k2.Get EnhancedInputLocalPlayerSubsystem") },
					{ TEXT("fn.EnhancedInputSubsystem.Get"),               TEXT("k2.Get EnhancedInputLocalPlayerSubsystem") },

					{ TEXT("k2.Break Vector 2D"),                          TEXT("fn.KismetMathLibrary.BreakVector2D") },
					{ TEXT("k2.BreakVector2D"),                            TEXT("fn.KismetMathLibrary.BreakVector2D") },
					{ TEXT("k2.Break Vector2D"),                           TEXT("fn.KismetMathLibrary.BreakVector2D") },
					{ TEXT("k2.Make Vector 2D"),                           TEXT("fn.KismetMathLibrary.MakeVector2D") },
					{ TEXT("k2.MakeVector2D"),                             TEXT("fn.KismetMathLibrary.MakeVector2D") },
					{ TEXT("k2.Make Vector2D"),                            TEXT("fn.KismetMathLibrary.MakeVector2D") },

					{ TEXT("k2.Break Rotator"),                            TEXT("fn.KismetMathLibrary.BreakRotator") },
					{ TEXT("k2.BreakRotator"),                             TEXT("fn.KismetMathLibrary.BreakRotator") },
					{ TEXT("k2.Make Rotator"),                             TEXT("fn.KismetMathLibrary.MakeRotator") },
					{ TEXT("k2.MakeRotator"),                              TEXT("fn.KismetMathLibrary.MakeRotator") },

					{ TEXT("fn.KismetSystemLibrary.K2_GetDeltaSeconds"),   TEXT("fn.GameplayStatics.GetWorldDeltaSeconds") },
					{ TEXT("fn.KismetSystemLibrary.GetDeltaSeconds"),      TEXT("fn.GameplayStatics.GetWorldDeltaSeconds") },
					{ TEXT("fn.KismetMathLibrary.GetDeltaSeconds"),        TEXT("fn.GameplayStatics.GetWorldDeltaSeconds") },
					{ TEXT("fn.GameplayStatics.K2_GetDeltaSeconds"),       TEXT("fn.GameplayStatics.GetWorldDeltaSeconds") },
					{ TEXT("fn.GameplayStatics.GetDeltaSeconds"),          TEXT("fn.GameplayStatics.GetWorldDeltaSeconds") },
					{ TEXT("k2.GetDeltaSeconds"),                          TEXT("fn.GameplayStatics.GetWorldDeltaSeconds") },
					{ TEXT("k2.Get Delta Seconds"),                        TEXT("fn.GameplayStatics.GetWorldDeltaSeconds") },

					{ TEXT("fn.Actor.GetActorLocation"),                   TEXT("fn.Actor.K2_GetActorLocation") },
					{ TEXT("fn.Actor.GetActorRotation"),                   TEXT("fn.Actor.K2_GetActorRotation") },
					{ TEXT("fn.Actor.GetActorTransform"),                  TEXT("fn.Actor.GetTransform") },
					{ TEXT("fn.Actor.SetActorLocation"),                   TEXT("fn.Actor.K2_SetActorLocation") },
					{ TEXT("fn.Actor.SetActorRotation"),                   TEXT("fn.Actor.K2_SetActorRotation") },
					{ TEXT("fn.Actor.SetActorLocationAndRotation"),        TEXT("fn.Actor.K2_SetActorLocationAndRotation") },
					{ TEXT("fn.Actor.SetActorTransform"),                  TEXT("fn.Actor.K2_SetActorTransform") },
					{ TEXT("fn.Actor.AttachToActor"),                      TEXT("fn.Actor.K2_AttachToActor") },
					{ TEXT("fn.Actor.AttachToComponent"),                  TEXT("fn.Actor.K2_AttachToComponent") },
					{ TEXT("fn.Actor.DetachFromActor"),                    TEXT("fn.Actor.K2_DetachFromActor") },
					{ TEXT("fn.Actor.TeleportTo"),                         TEXT("fn.Actor.K2_TeleportTo") },
					{ TEXT("fn.Actor.AddActorLocalOffset"),                TEXT("fn.Actor.K2_AddActorLocalOffset") },
					{ TEXT("fn.Actor.AddActorLocalRotation"),              TEXT("fn.Actor.K2_AddActorLocalRotation") },
					{ TEXT("fn.Actor.AddActorWorldOffset"),                TEXT("fn.Actor.K2_AddActorWorldOffset") },
					{ TEXT("fn.Actor.AddActorWorldRotation"),              TEXT("fn.Actor.K2_AddActorWorldRotation") },

					{ TEXT("fn.SceneComponent.SetWorldLocation"),          TEXT("fn.SceneComponent.K2_SetWorldLocation") },
					{ TEXT("fn.SceneComponent.SetWorldRotation"),          TEXT("fn.SceneComponent.K2_SetWorldRotation") },
					{ TEXT("fn.SceneComponent.SetWorldScale3D"),           TEXT("fn.SceneComponent.K2_SetWorldScale3D") },
					{ TEXT("fn.SceneComponent.SetRelativeLocation"),       TEXT("fn.SceneComponent.K2_SetRelativeLocation") },
					{ TEXT("fn.SceneComponent.SetRelativeRotation"),       TEXT("fn.SceneComponent.K2_SetRelativeRotation") },
					{ TEXT("fn.SceneComponent.SetRelativeScale3D"),        TEXT("fn.SceneComponent.K2_SetRelativeScale3D") },
					{ TEXT("fn.SceneComponent.AddWorldOffset"),            TEXT("fn.SceneComponent.K2_AddWorldOffset") },
					{ TEXT("fn.SceneComponent.AddWorldRotation"),          TEXT("fn.SceneComponent.K2_AddWorldRotation") },
					{ TEXT("fn.SceneComponent.AddLocalOffset"),            TEXT("fn.SceneComponent.K2_AddLocalOffset") },
					{ TEXT("fn.SceneComponent.AddLocalRotation"),          TEXT("fn.SceneComponent.K2_AddLocalRotation") },
					{ TEXT("fn.SceneComponent.AttachToComponent"),         TEXT("fn.SceneComponent.K2_AttachToComponent") },
					{ TEXT("fn.SceneComponent.DetachFromComponent"),       TEXT("fn.SceneComponent.K2_DetachFromComponent") },
					{ TEXT("fn.SceneComponent.GetWorldLocation"),          TEXT("fn.SceneComponent.K2_GetComponentLocation") },
					{ TEXT("fn.SceneComponent.GetWorldRotation"),          TEXT("fn.SceneComponent.K2_GetComponentRotation") },
					{ TEXT("fn.SceneComponent.GetWorldScale3D"),           TEXT("fn.SceneComponent.K2_GetComponentScale") },
					{ TEXT("fn.SceneComponent.GetWorldScale"),             TEXT("fn.SceneComponent.K2_GetComponentScale") },
					{ TEXT("fn.SceneComponent.GetWorldTransform"),         TEXT("fn.SceneComponent.K2_GetComponentToWorld") },
					{ TEXT("fn.SceneComponent.GetComponentLocation"),      TEXT("fn.SceneComponent.K2_GetComponentLocation") },
					{ TEXT("fn.SceneComponent.GetComponentRotation"),      TEXT("fn.SceneComponent.K2_GetComponentRotation") },
					{ TEXT("fn.SceneComponent.GetComponentScale"),         TEXT("fn.SceneComponent.K2_GetComponentScale") },
					{ TEXT("fn.SceneComponent.GetComponentTransform"),     TEXT("fn.SceneComponent.K2_GetComponentToWorld") },

					{ TEXT("fn.KismetMathLibrary.Boolean_AND"),            TEXT("fn.KismetMathLibrary.BooleanAND") },
					{ TEXT("fn.KismetMathLibrary.Boolean_OR"),             TEXT("fn.KismetMathLibrary.BooleanOR") },
					{ TEXT("fn.KismetMathLibrary.Boolean_NAND"),           TEXT("fn.KismetMathLibrary.BooleanNAND") },
					{ TEXT("fn.KismetMathLibrary.Boolean_NOR"),            TEXT("fn.KismetMathLibrary.BooleanNOR") },
					{ TEXT("fn.KismetMathLibrary.Boolean_XOR"),            TEXT("fn.KismetMathLibrary.BooleanXOR") },
					{ TEXT("fn.KismetMathLibrary.BooleanAnd"),             TEXT("fn.KismetMathLibrary.BooleanAND") },
					{ TEXT("fn.KismetMathLibrary.BooleanOr"),              TEXT("fn.KismetMathLibrary.BooleanOR") },

					{ TEXT("forEach"),                                     TEXT("k2.ForEachLoop") },
					{ TEXT("k2.forEach"),                                  TEXT("k2.ForEachLoop") },
					{ TEXT("k2.ForEach"),                                  TEXT("k2.ForEachLoop") },
					{ TEXT("k2.For Each Loop"),                            TEXT("k2.ForEachLoop") },
					{ TEXT("ForEachLoop"),                                 TEXT("k2.ForEachLoop") },
					{ TEXT("fn.KismetSystemLibrary.ForEachLoop"),          TEXT("k2.ForEachLoop") },
					{ TEXT("fn.KismetMathLibrary.ForEachLoop"),            TEXT("k2.ForEachLoop") },
					{ TEXT("fn.KismetSystemLibrary.ForLoop"),              TEXT("k2.ForLoop") },
					{ TEXT("fn.KismetMathLibrary.ForLoop"),                TEXT("k2.ForLoop") },
					{ TEXT("forLoop"),                                     TEXT("k2.ForLoop") },
					{ TEXT("k2.For Loop"),                                 TEXT("k2.ForLoop") },

					{ TEXT("event.BeginPlay"),                             TEXT("ev.ReceiveBeginPlay") },
					{ TEXT("event.Tick"),                                  TEXT("ev.ReceiveTick") },
					{ TEXT("event.EndPlay"),                               TEXT("ev.ReceiveEndPlay") },
					{ TEXT("event.ActorBeginOverlap"),                     TEXT("ev.ReceiveActorBeginOverlap") },
					{ TEXT("event.ActorEndOverlap"),                       TEXT("ev.ReceiveActorEndOverlap") },
					{ TEXT("event.AnyDamage"),                             TEXT("ev.ReceiveAnyDamage") },
					{ TEXT("event.Hit"),                                   TEXT("ev.ReceiveActorHit") },
					{ TEXT("event.ControllerChanged"),                     TEXT("ev.ReceiveControllerChanged") },

					{ TEXT("fn.KismetMathLibrary.Less_equal_DoubleDouble"),    TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.less_equal_DoubleDouble"),    TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Greater_equal_DoubleDouble"), TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.greater_equal_DoubleDouble"), TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Less_equal_FloatFloat"),      TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Greater_equal_FloatFloat"),   TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.LessEqual_FloatFloat"),       TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.GreaterEqual_FloatFloat"),    TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Less_FloatFloat"),            TEXT("fn.KismetMathLibrary.Less_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Greater_FloatFloat"),         TEXT("fn.KismetMathLibrary.Greater_DoubleDouble") },
					{ TEXT("fn.GameplayStatics.SpawnActor"),               TEXT("k2.Spawn Actor from Class") },
					{ TEXT("fn.GameplayStatics.SpawnActorFromClass"),      TEXT("k2.Spawn Actor from Class") },
					{ TEXT("fn.World.SpawnActor"),                         TEXT("k2.Spawn Actor from Class") },
					{ TEXT("fn.World.SpawnActorFromClass"),                TEXT("k2.Spawn Actor from Class") },
					{ TEXT("k2.SpawnActor"),                               TEXT("k2.Spawn Actor from Class") },
					{ TEXT("k2.SpawnActorFromClass"),                      TEXT("k2.Spawn Actor from Class") },
					{ TEXT("k2.Spawn Actor"),                              TEXT("k2.Spawn Actor from Class") },

					{ TEXT("fn.NavigationSystemV1.GetRandomReachablePointInRadius"),  TEXT("fn.NavigationSystemV1.K2_GetRandomReachablePointInRadius") },
					{ TEXT("fn.NavigationSystem.GetRandomReachablePointInRadius"),    TEXT("fn.NavigationSystemV1.K2_GetRandomReachablePointInRadius") },
					{ TEXT("fn.NavigationSystem.K2_GetRandomReachablePointInRadius"), TEXT("fn.NavigationSystemV1.K2_GetRandomReachablePointInRadius") },
					{ TEXT("fn.NavigationSystemV1.GetRandomPointInRadius"),           TEXT("fn.NavigationSystemV1.K2_GetRandomPointInRadius") },
					{ TEXT("fn.NavigationSystem.GetRandomPointInRadius"),             TEXT("fn.NavigationSystemV1.K2_GetRandomPointInRadius") },

					{ TEXT("k2.Less"),                                     TEXT("fn.KismetMathLibrary.Less_DoubleDouble") },
					{ TEXT("k2.LessEqual"),                                TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("k2.Greater"),                                  TEXT("fn.KismetMathLibrary.Greater_DoubleDouble") },
					{ TEXT("k2.Greater_DoubleDouble"),                     TEXT("fn.KismetMathLibrary.Greater_DoubleDouble") },
					{ TEXT("k2.GreaterEqual_DoubleDouble"),                TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("k2.Less_DoubleDouble"),                        TEXT("fn.KismetMathLibrary.Less_DoubleDouble") },
					{ TEXT("k2.LessEqual_DoubleDouble"),                   TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("k2.EqualEqual_DoubleDouble"),                  TEXT("fn.KismetMathLibrary.EqualEqual_DoubleDouble") },
					{ TEXT("k2.NotEqual_DoubleDouble"),                    TEXT("fn.KismetMathLibrary.NotEqual_DoubleDouble") },
					{ TEXT("k2.Add_DoubleDouble"),                         TEXT("fn.KismetMathLibrary.Add_DoubleDouble") },
					{ TEXT("k2.Subtract_DoubleDouble"),                    TEXT("fn.KismetMathLibrary.Subtract_DoubleDouble") },
					{ TEXT("k2.Multiply_DoubleDouble"),                    TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("k2.Divide_DoubleDouble"),                      TEXT("fn.KismetMathLibrary.Divide_DoubleDouble") },
					{ TEXT("k2.Greater_IntInt"),                           TEXT("fn.KismetMathLibrary.Greater_IntInt") },
					{ TEXT("k2.GreaterEqual_IntInt"),                      TEXT("fn.KismetMathLibrary.GreaterEqual_IntInt") },
					{ TEXT("k2.Less_IntInt"),                              TEXT("fn.KismetMathLibrary.Less_IntInt") },
					{ TEXT("k2.LessEqual_IntInt"),                         TEXT("fn.KismetMathLibrary.LessEqual_IntInt") },
					{ TEXT("k2.EqualEqual_IntInt"),                        TEXT("fn.KismetMathLibrary.EqualEqual_IntInt") },
					{ TEXT("k2.NotEqual_IntInt"),                          TEXT("fn.KismetMathLibrary.NotEqual_IntInt") },
					{ TEXT("k2.Add_IntInt"),                               TEXT("fn.KismetMathLibrary.Add_IntInt") },
					{ TEXT("k2.Subtract_IntInt"),                          TEXT("fn.KismetMathLibrary.Subtract_IntInt") },
					{ TEXT("k2.Multiply_IntInt"),                          TEXT("fn.KismetMathLibrary.Multiply_IntInt") },
					{ TEXT("k2.Divide_IntInt"),                            TEXT("fn.KismetMathLibrary.Divide_IntInt") },
					{ TEXT("k2.GreaterEqual"),                             TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },

					{ TEXT("fn.KismetMathLibrary.Float_AddFloat"),         TEXT("fn.KismetMathLibrary.Add_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_FloatAdd"),         TEXT("fn.KismetMathLibrary.Add_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.FloatPlusFloat"),         TEXT("fn.KismetMathLibrary.Add_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.FloatMinusFloat"),        TEXT("fn.KismetMathLibrary.Subtract_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.FloatTimesFloat"),        TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.FloatDivFloat"),          TEXT("fn.KismetMathLibrary.Divide_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_Float"),            TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Int_Int"),                TEXT("fn.KismetMathLibrary.Multiply_IntInt") },
					{ TEXT("fn.KismetMathLibrary.Double_Double"),          TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_SubtractFloat"),    TEXT("fn.KismetMathLibrary.Subtract_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_FloatSubtract"),    TEXT("fn.KismetMathLibrary.Subtract_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_MultiplyFloat"),    TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_FloatMultiply"),    TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_DivideFloat"),      TEXT("fn.KismetMathLibrary.Divide_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Float_FloatDivide"),      TEXT("fn.KismetMathLibrary.Divide_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Multiply_FloatFloat"),    TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Multiply_FloatInt"),      TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Multiply_IntFloat"),      TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Add_FloatFloat"),         TEXT("fn.KismetMathLibrary.Add_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Subtract_FloatFloat"),    TEXT("fn.KismetMathLibrary.Subtract_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Divide_FloatFloat"),      TEXT("fn.KismetMathLibrary.Divide_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Percent_FloatFloat"),     TEXT("fn.KismetMathLibrary.Percent_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.Abs_Float"),              TEXT("fn.KismetMathLibrary.Abs") },
					{ TEXT("fn.KismetMathLibrary.FloatEqual_FloatFloat"),  TEXT("fn.KismetMathLibrary.EqualEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.FloatNotEqual_FloatFloat"), TEXT("fn.KismetMathLibrary.NotEqual_DoubleDouble") },
					{ TEXT("k2.FloatGreaterEqual_FloatFloat"),             TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("k2.FloatLessEqual_FloatFloat"),                TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("k2.FloatGreater_FloatFloat"),                  TEXT("fn.KismetMathLibrary.Greater_DoubleDouble") },
					{ TEXT("k2.FloatLess_FloatFloat"),                     TEXT("fn.KismetMathLibrary.Less_DoubleDouble") },

					{ TEXT("fn.Actor.AttachToComponent"),                  TEXT("fn.Actor.K2_AttachRootComponentTo") },
					{ TEXT("fn.Actor.AttachActorToComponent"),             TEXT("fn.Actor.K2_AttachRootComponentTo") },
					{ TEXT("fn.KismetSystemLibrary.AttachActorToComponent"), TEXT("fn.Actor.K2_AttachRootComponentTo") },
					{ TEXT("fn.KismetSystemLibrary.AttachToComponent"),    TEXT("fn.Actor.K2_AttachRootComponentTo") },

					{ TEXT("fn.MaterialFunctionLibrary.CreateDynamicInstance"), TEXT("fn.PrimitiveComponent.CreateDynamicMaterialInstance") },
					{ TEXT("fn.Material.CreateDynamicInstance"),           TEXT("fn.PrimitiveComponent.CreateDynamicMaterialInstance") },
					{ TEXT("fn.KismetMaterialLibrary.CreateDynamicInstance"), TEXT("fn.PrimitiveComponent.CreateDynamicMaterialInstance") },

					{ TEXT("k2.Delay"),                                    TEXT("fn.KismetSystemLibrary.Delay") },
					{ TEXT("k2.RetriggerableDelay"),                       TEXT("fn.KismetSystemLibrary.RetriggerableDelay") },

					{ TEXT("k2.Break Vector"),                             TEXT("fn.KismetMathLibrary.BreakVector") },
					{ TEXT("k2.Break Rotator"),                            TEXT("fn.KismetMathLibrary.BreakRotator") },
					{ TEXT("k2.Make Vector"),                              TEXT("fn.KismetMathLibrary.MakeVector") },
					{ TEXT("k2.Make Rotator"),                             TEXT("fn.KismetMathLibrary.MakeRotator") },
					{ TEXT("k2.Make Transform"),                           TEXT("fn.KismetMathLibrary.MakeTransform") },
					{ TEXT("k2.Break Transform"),                          TEXT("fn.KismetMathLibrary.BreakTransform") },

					{ TEXT("fn.KismetMathLibrary.Conv_DoubleToString"),    TEXT("fn.KismetStringLibrary.Conv_DoubleToString") },
					{ TEXT("fn.KismetMathLibrary.Conv_FloatToString"),     TEXT("fn.KismetStringLibrary.Conv_DoubleToString") },
					{ TEXT("fn.KismetMathLibrary.Conv_IntToString"),       TEXT("fn.KismetStringLibrary.Conv_IntToString") },
					{ TEXT("fn.KismetMathLibrary.Conv_BoolToString"),      TEXT("fn.KismetStringLibrary.Conv_BoolToString") },
					{ TEXT("fn.KismetMathLibrary.Conv_NameToString"),      TEXT("fn.KismetStringLibrary.Conv_NameToString") },
					{ TEXT("fn.KismetSystemLibrary.Conv_NameToString"),    TEXT("fn.KismetStringLibrary.Conv_NameToString") },

					{ TEXT("fn.KismetMathLibrary.PrintString"),            TEXT("fn.KismetSystemLibrary.PrintString") },
					{ TEXT("fn.KismetMathLibrary.PrintText"),              TEXT("fn.KismetSystemLibrary.PrintText") },

					{ TEXT("fn.WidgetBlueprintLibrary.CreateWidget"),      TEXT("k2.Create Widget") },
					{ TEXT("fn.WidgetBlueprintLibrary.Create"),            TEXT("k2.Create Widget") },
					{ TEXT("fn.UUserWidget.CreateWidget"),                 TEXT("k2.Create Widget") },
					{ TEXT("fn.UserWidget.CreateWidget"),                  TEXT("k2.Create Widget") },
					{ TEXT("k2.CreateWidget"),                             TEXT("k2.Create Widget") },
					{ TEXT("k2.Create Widget Instance"),                   TEXT("k2.Create Widget") },

					{ TEXT("fn.UniformGridPanel.AddChild"),                TEXT("fn.UniformGridPanel.AddChildToUniformGrid") },
					{ TEXT("fn.GridPanel.AddChild"),                       TEXT("fn.GridPanel.AddChildToGrid") },
					{ TEXT("fn.VerticalBox.AddChild"),                     TEXT("fn.VerticalBox.AddChildToVerticalBox") },
					{ TEXT("fn.HorizontalBox.AddChild"),                   TEXT("fn.HorizontalBox.AddChildToHorizontalBox") },
					{ TEXT("fn.Overlay.AddChild"),                         TEXT("fn.Overlay.AddChildToOverlay") },
					{ TEXT("fn.WrapBox.AddChild"),                         TEXT("fn.WrapBox.AddChildWrapBox") },
					{ TEXT("fn.ScrollBox.AddChild"),                       TEXT("fn.ScrollBox.AddChild") },
					{ TEXT("fn.CanvasPanel.AddChild"),                     TEXT("fn.CanvasPanel.AddChildToCanvas") },

					{ TEXT("fn.UserWidget.RemoveFromViewport"),            TEXT("fn.Widget.RemoveFromParent") },
					{ TEXT("fn.UUserWidget.RemoveFromViewport"),           TEXT("fn.Widget.RemoveFromParent") },

					{ TEXT("fn.KismetMathLibrary.LessThan_IntInt"),        TEXT("fn.KismetMathLibrary.Less_IntInt") },
					{ TEXT("fn.KismetMathLibrary.LessThanOrEqual_IntInt"), TEXT("fn.KismetMathLibrary.LessEqual_IntInt") },
					{ TEXT("fn.KismetMathLibrary.GreaterThan_IntInt"),     TEXT("fn.KismetMathLibrary.Greater_IntInt") },
					{ TEXT("fn.KismetMathLibrary.GreaterThanOrEqual_IntInt"), TEXT("fn.KismetMathLibrary.GreaterEqual_IntInt") },
					{ TEXT("fn.KismetMathLibrary.GreaterEqual_Int64Int"),   TEXT("fn.KismetMathLibrary.GreaterEqual_IntInt") },
					{ TEXT("fn.KismetMathLibrary.GreaterEqual_Int64Int64"), TEXT("fn.KismetMathLibrary.GreaterEqual_IntInt") },
					{ TEXT("fn.KismetMathLibrary.Less_Int64Int64"),         TEXT("fn.KismetMathLibrary.Less_IntInt") },
					{ TEXT("fn.KismetMathLibrary.LessEqual_Int64Int64"),    TEXT("fn.KismetMathLibrary.LessEqual_IntInt") },
					{ TEXT("fn.KismetMathLibrary.Greater_Int64Int64"),      TEXT("fn.KismetMathLibrary.Greater_IntInt") },
					{ TEXT("fn.KismetMathLibrary.Conv_DoubleToInt"),        TEXT("fn.KismetMathLibrary.FTrunc") },
					{ TEXT("fn.KismetMathLibrary.Conv_FloatToInt"),         TEXT("fn.KismetMathLibrary.FTrunc") },

					{ TEXT("fn.KismetStringLibrary.GetLength"),             TEXT("fn.KismetStringLibrary.Len") },
					{ TEXT("fn.KismetStringLibrary.StringLength"),          TEXT("fn.KismetStringLibrary.Len") },
					{ TEXT("fn.KismetStringLibrary.Length"),                TEXT("fn.KismetStringLibrary.Len") },

					{ TEXT("fn.KismetArrayLibrary.Array_Sort"),             TEXT("fn.KismetArrayLibrary.Array_Sort") },
					{ TEXT("fn.KismetMathLibrary.VectorToInt"),             TEXT("fn.KismetMathLibrary.VectorToInt") },
					{ TEXT("fn.KismetMathLibrary.LessThan_DoubleDouble"),  TEXT("fn.KismetMathLibrary.Less_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.LessThanOrEqual_DoubleDouble"), TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.GreaterThan_DoubleDouble"), TEXT("fn.KismetMathLibrary.Greater_DoubleDouble") },
					{ TEXT("fn.KismetMathLibrary.GreaterThanOrEqual_DoubleDouble"), TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },

					{ TEXT("Branch"),                                      TEXT("k2.Branch") },
					{ TEXT("branch"),                                      TEXT("k2.Branch") },
					{ TEXT("Sequence"),                                    TEXT("k2.Sequence") },
					{ TEXT("DoOnce"),                                      TEXT("k2.Do Once") },
					{ TEXT("k2.DoOnce"),                                   TEXT("k2.Do Once") },
					{ TEXT("IsValid"),                                     TEXT("k2.IsValid") },
					{ TEXT("Select"),                                      TEXT("k2.Select") },
					{ TEXT("Gate"),                                        TEXT("k2.Gate") },
					{ TEXT("MultiGate"),                                   TEXT("k2.MultiGate") },
					{ TEXT("FlipFlop"),                                    TEXT("k2.FlipFlop") },
					{ TEXT("SwitchOnInt"),                                 TEXT("k2.SwitchOnInt") },
					{ TEXT("SwitchOnString"),                              TEXT("k2.SwitchOnString") },
					{ TEXT("SwitchOnEnum"),                                TEXT("k2.SwitchOnEnum") },

					{ TEXT("fn.Actor.GetAllActorsOfClass"),                TEXT("fn.GameplayStatics.GetAllActorsOfClass") },
					{ TEXT("fn.World.GetAllActorsOfClass"),                TEXT("fn.GameplayStatics.GetAllActorsOfClass") },
					{ TEXT("fn.KismetSystemLibrary.GetAllActorsOfClass"), TEXT("fn.GameplayStatics.GetAllActorsOfClass") },
					{ TEXT("fn.Actor.GetActorOfClass"),                    TEXT("fn.GameplayStatics.GetActorOfClass") },
					{ TEXT("fn.World.GetActorOfClass"),                    TEXT("fn.GameplayStatics.GetActorOfClass") },
					{ TEXT("fn.KismetSystemLibrary.GetActorOfClass"),      TEXT("fn.GameplayStatics.GetActorOfClass") },
					{ TEXT("fn.Actor.GetGameMode"),                        TEXT("fn.GameplayStatics.GetGameMode") },
					{ TEXT("fn.World.GetGameMode"),                        TEXT("fn.GameplayStatics.GetGameMode") },
					{ TEXT("fn.KismetSystemLibrary.GetGameMode"),          TEXT("fn.GameplayStatics.GetGameMode") },
					{ TEXT("fn.Actor.GetPlayerController"),                TEXT("fn.GameplayStatics.GetPlayerController") },
					{ TEXT("fn.World.GetPlayerController"),                TEXT("fn.GameplayStatics.GetPlayerController") },
					{ TEXT("fn.KismetSystemLibrary.GetPlayerController"),  TEXT("fn.GameplayStatics.GetPlayerController") },
					{ TEXT("fn.Actor.GetPlayerCharacter"),                 TEXT("fn.GameplayStatics.GetPlayerCharacter") },
					{ TEXT("fn.World.GetPlayerCharacter"),                 TEXT("fn.GameplayStatics.GetPlayerCharacter") },
					{ TEXT("fn.KismetSystemLibrary.GetPlayerCharacter"),   TEXT("fn.GameplayStatics.GetPlayerCharacter") },
					{ TEXT("fn.Actor.GetPlayerPawn"),                      TEXT("fn.GameplayStatics.GetPlayerPawn") },
					{ TEXT("fn.World.GetPlayerPawn"),                      TEXT("fn.GameplayStatics.GetPlayerPawn") },
					{ TEXT("fn.KismetSystemLibrary.GetPlayerPawn"),        TEXT("fn.GameplayStatics.GetPlayerPawn") },
					{ TEXT("fn.KismetSystemLibrary.OpenLevel"),            TEXT("fn.GameplayStatics.OpenLevel") },
					{ TEXT("fn.Actor.OpenLevel"),                          TEXT("fn.GameplayStatics.OpenLevel") },
					{ TEXT("fn.World.OpenLevel"),                          TEXT("fn.GameplayStatics.OpenLevel") },
					{ TEXT("fn.KismetSystemLibrary.GetAllActorsWithInterface"), TEXT("fn.GameplayStatics.GetAllActorsWithInterface") },
					{ TEXT("fn.Actor.GetAllActorsWithInterface"),          TEXT("fn.GameplayStatics.GetAllActorsWithInterface") },
					{ TEXT("fn.KismetSystemLibrary.SpawnSound2D"),         TEXT("fn.GameplayStatics.SpawnSound2D") },
					{ TEXT("fn.KismetSystemLibrary.SpawnSoundAtLocation"),  TEXT("fn.GameplayStatics.SpawnSoundAtLocation") },

					{ TEXT("fn.KismetMathLibrary.ClampFloat"),             TEXT("fn.KismetMathLibrary.FClamp") },
					{ TEXT("fn.KismetMathLibrary.ClampDouble"),            TEXT("fn.KismetMathLibrary.FClamp") },
					{ TEXT("fn.KismetMathLibrary.Clamp_Float"),            TEXT("fn.KismetMathLibrary.FClamp") },
					{ TEXT("fn.KismetMathLibrary.ClampF"),                 TEXT("fn.KismetMathLibrary.FClamp") },
					{ TEXT("fn.KismetMathLibrary.MapRange"),               TEXT("fn.KismetMathLibrary.MapRangeClamped") },
					{ TEXT("fn.KismetMathLibrary.Map_Range"),              TEXT("fn.KismetMathLibrary.MapRangeClamped") },
					{ TEXT("fn.KismetMathLibrary.VectorLength"),           TEXT("fn.KismetMathLibrary.VSize") },
					{ TEXT("fn.KismetMathLibrary.VectorSize"),             TEXT("fn.KismetMathLibrary.VSize") },
					{ TEXT("fn.KismetMathLibrary.Vector_Size"),            TEXT("fn.KismetMathLibrary.VSize") },
					{ TEXT("fn.KismetMathLibrary.GetLength"),              TEXT("fn.KismetMathLibrary.VSize") },
					{ TEXT("fn.KismetMathLibrary.Distance"),               TEXT("fn.KismetMathLibrary.Vector_Distance") },
					{ TEXT("fn.KismetMathLibrary.VectorDistance"),         TEXT("fn.KismetMathLibrary.Vector_Distance") },
					{ TEXT("fn.KismetMathLibrary.GetDistance"),            TEXT("fn.KismetMathLibrary.Vector_Distance") },
					{ TEXT("fn.KismetMathLibrary.DotProduct"),             TEXT("fn.KismetMathLibrary.Dot_VectorVector") },
					{ TEXT("fn.KismetMathLibrary.Dot"),                    TEXT("fn.KismetMathLibrary.Dot_VectorVector") },
					{ TEXT("fn.KismetMathLibrary.CrossProduct"),           TEXT("fn.KismetMathLibrary.Cross_VectorVector") },
					{ TEXT("fn.KismetMathLibrary.Cross"),                  TEXT("fn.KismetMathLibrary.Cross_VectorVector") },
					{ TEXT("fn.KismetMathLibrary.NormalizeVector"),        TEXT("fn.KismetMathLibrary.Normal") },
					{ TEXT("fn.KismetMathLibrary.Normalize"),              TEXT("fn.KismetMathLibrary.Normal") },
					{ TEXT("fn.KismetMathLibrary.VNorm"),                  TEXT("fn.KismetMathLibrary.Normal") },
					{ TEXT("fn.KismetMathLibrary.Lerp_Float"),             TEXT("fn.KismetMathLibrary.Lerp") },
					{ TEXT("fn.KismetMathLibrary.FLerp"),                  TEXT("fn.KismetMathLibrary.Lerp") },
					{ TEXT("fn.KismetMathLibrary.FloatLerp"),              TEXT("fn.KismetMathLibrary.Lerp") },
					{ TEXT("fn.KismetMathLibrary.Pow"),                    TEXT("fn.KismetMathLibrary.Power") },
					{ TEXT("fn.KismetMathLibrary.FPow"),                   TEXT("fn.KismetMathLibrary.Power") },
					{ TEXT("fn.KismetMathLibrary.FSqrt"),                  TEXT("fn.KismetMathLibrary.Sqrt") },

					{ TEXT("fn.KismetSystemLibrary.Concat_StrStr"),        TEXT("fn.KismetStringLibrary.Concat_StrStr") },
					{ TEXT("fn.KismetMathLibrary.Concat_StrStr"),          TEXT("fn.KismetStringLibrary.Concat_StrStr") },
					{ TEXT("k2.Concat"),                                   TEXT("fn.KismetStringLibrary.Concat_StrStr") },
					{ TEXT("k2.Append"),                                   TEXT("fn.KismetStringLibrary.Concat_StrStr") },
					{ TEXT("fn.KismetSystemLibrary.Contains"),             TEXT("fn.KismetStringLibrary.Contains") },
					{ TEXT("fn.KismetMathLibrary.Contains"),               TEXT("fn.KismetStringLibrary.Contains") },
					{ TEXT("k2.Contains"),                                 TEXT("fn.KismetStringLibrary.Contains") },
					{ TEXT("fn.KismetSystemLibrary.StartsWith"),           TEXT("fn.KismetStringLibrary.StartsWith") },
					{ TEXT("k2.StartsWith"),                               TEXT("fn.KismetStringLibrary.StartsWith") },
					{ TEXT("fn.KismetSystemLibrary.EndsWith"),             TEXT("fn.KismetStringLibrary.EndsWith") },
					{ TEXT("k2.EndsWith"),                                 TEXT("fn.KismetStringLibrary.EndsWith") },
					{ TEXT("fn.KismetSystemLibrary.Split"),                TEXT("fn.KismetStringLibrary.Split") },
					{ TEXT("k2.Split"),                                    TEXT("fn.KismetStringLibrary.Split") },
					{ TEXT("fn.KismetSystemLibrary.ToLower"),              TEXT("fn.KismetStringLibrary.ToLower") },
					{ TEXT("k2.ToLower"),                                  TEXT("fn.KismetStringLibrary.ToLower") },
					{ TEXT("fn.KismetSystemLibrary.ToUpper"),              TEXT("fn.KismetStringLibrary.ToUpper") },
					{ TEXT("k2.ToUpper"),                                  TEXT("fn.KismetStringLibrary.ToUpper") },
					{ TEXT("fn.KismetSystemLibrary.Len"),                  TEXT("fn.KismetStringLibrary.Len") },
					{ TEXT("fn.KismetSystemLibrary.GetStringLength"),      TEXT("fn.KismetStringLibrary.Len") },
					{ TEXT("k2.Len"),                                      TEXT("fn.KismetStringLibrary.Len") },
					{ TEXT("k2.StringLength"),                             TEXT("fn.KismetStringLibrary.Len") },
					{ TEXT("fn.KismetSystemLibrary.Left"),                 TEXT("fn.KismetStringLibrary.Left") },
					{ TEXT("k2.Left"),                                     TEXT("fn.KismetStringLibrary.Left") },
					{ TEXT("fn.KismetSystemLibrary.Right"),                TEXT("fn.KismetStringLibrary.Right") },
					{ TEXT("k2.Right"),                                    TEXT("fn.KismetStringLibrary.Right") },
					{ TEXT("fn.KismetSystemLibrary.Mid"),                  TEXT("fn.KismetStringLibrary.Mid") },
					{ TEXT("k2.Mid"),                                      TEXT("fn.KismetStringLibrary.Mid") },
					{ TEXT("fn.KismetSystemLibrary.TrimStartAndEnd"),      TEXT("fn.KismetStringLibrary.TrimStartAndEnd") },
					{ TEXT("k2.Trim"),                                     TEXT("fn.KismetStringLibrary.TrimStartAndEnd") },
					{ TEXT("fn.KismetSystemLibrary.Replace"),              TEXT("fn.KismetStringLibrary.Replace") },
					{ TEXT("k2.Replace"),                                  TEXT("fn.KismetStringLibrary.Replace") },

					{ TEXT("fn.KismetSystemLibrary.Array_Get"),            TEXT("fn.KismetArrayLibrary.Array_Get") },
					{ TEXT("fn.KismetSystemLibrary.Array_Add"),            TEXT("fn.KismetArrayLibrary.Array_Add") },
					{ TEXT("fn.KismetSystemLibrary.Array_Remove"),         TEXT("fn.KismetArrayLibrary.Array_Remove") },
					{ TEXT("fn.KismetSystemLibrary.Array_RemoveItem"),     TEXT("fn.KismetArrayLibrary.Array_RemoveItem") },
					{ TEXT("fn.KismetSystemLibrary.Array_Contains"),       TEXT("fn.KismetArrayLibrary.Array_Contains") },
					{ TEXT("fn.KismetSystemLibrary.Array_Length"),         TEXT("fn.KismetArrayLibrary.Array_Length") },
					{ TEXT("fn.KismetSystemLibrary.Array_Find"),           TEXT("fn.KismetArrayLibrary.Array_Find") },
					{ TEXT("fn.KismetSystemLibrary.Array_Clear"),          TEXT("fn.KismetArrayLibrary.Array_Clear") },
					{ TEXT("fn.KismetSystemLibrary.Array_Set"),            TEXT("fn.KismetArrayLibrary.Array_Set") },
					{ TEXT("fn.KismetSystemLibrary.Array_Insert"),         TEXT("fn.KismetArrayLibrary.Array_Insert") },
					{ TEXT("fn.KismetSystemLibrary.Array_Append"),         TEXT("fn.KismetArrayLibrary.Array_Append") },
					{ TEXT("fn.KismetSystemLibrary.Array_Shuffle"),        TEXT("fn.KismetArrayLibrary.Array_Shuffle") },
					{ TEXT("fn.KismetSystemLibrary.Array_Reverse"),        TEXT("fn.KismetArrayLibrary.Array_Reverse") },
					{ TEXT("fn.KismetMathLibrary.Array_Get"),              TEXT("fn.KismetArrayLibrary.Array_Get") },
					{ TEXT("fn.KismetMathLibrary.Array_Add"),              TEXT("fn.KismetArrayLibrary.Array_Add") },
					{ TEXT("fn.KismetMathLibrary.Array_Remove"),           TEXT("fn.KismetArrayLibrary.Array_Remove") },
					{ TEXT("fn.KismetMathLibrary.Array_Contains"),         TEXT("fn.KismetArrayLibrary.Array_Contains") },
					{ TEXT("fn.KismetMathLibrary.Array_Length"),           TEXT("fn.KismetArrayLibrary.Array_Length") },
					{ TEXT("fn.KismetMathLibrary.Array_Find"),             TEXT("fn.KismetArrayLibrary.Array_Find") },
					{ TEXT("fn.KismetMathLibrary.Array_Clear"),            TEXT("fn.KismetArrayLibrary.Array_Clear") },
					{ TEXT("fn.KismetMathLibrary.Array_Set"),              TEXT("fn.KismetArrayLibrary.Array_Set") },

					{ TEXT("fn.Actor.GetActorScale"),                      TEXT("fn.Actor.GetActorScale3D") },
					{ TEXT("fn.Actor.SetActorScale"),                      TEXT("fn.Actor.SetActorScale3D") },
					{ TEXT("fn.Actor.Destroy"),                            TEXT("fn.Actor.K2_DestroyActor") },
					{ TEXT("k2.DestroyActor"),                             TEXT("fn.Actor.K2_DestroyActor") },
					{ TEXT("k2.Destroy"),                                  TEXT("fn.Actor.K2_DestroyActor") },
					{ TEXT("k2.DestroyComponent"),                         TEXT("fn.ActorComponent.DestroyComponent") },

					{ TEXT("event.ComponentBeginOverlap"),                 TEXT("ev.ReceiveComponentBeginOverlap") },
					{ TEXT("event.ComponentEndOverlap"),                   TEXT("ev.ReceiveComponentEndOverlap") },
					{ TEXT("event.PointDamage"),                           TEXT("ev.ReceivePointDamage") },
					{ TEXT("event.RadialDamage"),                          TEXT("ev.ReceiveRadialDamage") },
					{ TEXT("event.Destroyed"),                             TEXT("ev.ReceiveDestroyed") },
					{ TEXT("event.InputAction"),                           TEXT("ev.InpActEvt") },
					{ TEXT("event.InputAxis"),                             TEXT("ev.InpAxisEvt") },

					{ TEXT("literal_float"),                               TEXT("fn.KismetSystemLibrary.MakeLiteralFloat") },
					{ TEXT("literal_double"),                              TEXT("fn.KismetSystemLibrary.MakeLiteralDouble") },
					{ TEXT("literal_int"),                                 TEXT("fn.KismetSystemLibrary.MakeLiteralInt") },
					{ TEXT("literal_bool"),                                TEXT("fn.KismetSystemLibrary.MakeLiteralBool") },
					{ TEXT("literal_string"),                              TEXT("fn.KismetSystemLibrary.MakeLiteralString") },
					{ TEXT("literal_name"),                                TEXT("fn.KismetSystemLibrary.MakeLiteralName") },
					{ TEXT("literal_byte"),                                TEXT("fn.KismetSystemLibrary.MakeLiteralByte") },
					{ TEXT("literal_enum"),                                TEXT("fn.KismetSystemLibrary.MakeLiteralByte") },
					{ TEXT("MakeLiteral"),                                 TEXT("fn.KismetSystemLibrary.MakeLiteralFloat") },
					{ TEXT("k2.MakeLiteralByte"),                          TEXT("fn.KismetSystemLibrary.MakeLiteralByte") },

					{ TEXT("equal_enum"),                                  TEXT("fn.KismetMathLibrary.EqualEqual_ByteByte") },
					{ TEXT("equal_byte"),                                  TEXT("fn.KismetMathLibrary.EqualEqual_ByteByte") },
					{ TEXT("equal_int"),                                   TEXT("fn.KismetMathLibrary.EqualEqual_IntInt") },
					{ TEXT("equal_string"),                                TEXT("fn.KismetStringLibrary.EqualEqual_StrStr") },
					{ TEXT("equal_name"),                                  TEXT("fn.KismetMathLibrary.EqualEqual_NameName") },
					{ TEXT("equal_object"),                                TEXT("fn.KismetMathLibrary.EqualEqual_ObjectObject") },
					{ TEXT("equal_float"),                                 TEXT("fn.KismetMathLibrary.EqualEqual_DoubleDouble") },
					{ TEXT("equal_double"),                                TEXT("fn.KismetMathLibrary.EqualEqual_DoubleDouble") },

					{ TEXT("k2.SelectFloat"),                              TEXT("fn.KismetMathLibrary.SelectFloat") },
					{ TEXT("k2.SelectInt"),                                TEXT("fn.KismetMathLibrary.SelectInt") },
					{ TEXT("k2.Select Float"),                             TEXT("fn.KismetMathLibrary.SelectFloat") },
					{ TEXT("k2.Select Int"),                               TEXT("fn.KismetMathLibrary.SelectInt") },
					{ TEXT("SelectFloat"),                                 TEXT("fn.KismetMathLibrary.SelectFloat") },
					{ TEXT("SelectInt"),                                   TEXT("fn.KismetMathLibrary.SelectInt") },

					{ TEXT("k2.Subtract"),                                 TEXT("fn.KismetMathLibrary.Subtract_DoubleDouble") },
					{ TEXT("k2.Add"),                                      TEXT("fn.KismetMathLibrary.Add_DoubleDouble") },
					{ TEXT("k2.Multiply"),                                 TEXT("fn.KismetMathLibrary.Multiply_DoubleDouble") },
					{ TEXT("k2.Divide"),                                   TEXT("fn.KismetMathLibrary.Divide_DoubleDouble") },
					{ TEXT("k2.And"),                                      TEXT("fn.KismetMathLibrary.BooleanAND") },
					{ TEXT("k2.Or"),                                       TEXT("fn.KismetMathLibrary.BooleanOR") },
					{ TEXT("k2.Not"),                                      TEXT("fn.KismetMathLibrary.Not_PreBool") },
					{ TEXT("k2.Equal"),                                    TEXT("fn.KismetMathLibrary.EqualEqual_DoubleDouble") },
					{ TEXT("k2.NotEqual"),                                 TEXT("fn.KismetMathLibrary.NotEqual_DoubleDouble") },
					{ TEXT("k2.NotEqual_IntInt"),                          TEXT("fn.KismetMathLibrary.NotEqual_IntInt") },
					{ TEXT("k2.EqualEqual_IntInt"),                        TEXT("fn.KismetMathLibrary.EqualEqual_IntInt") },
					{ TEXT("k2.Less_IntInt"),                              TEXT("fn.KismetMathLibrary.Less_IntInt") },
					{ TEXT("k2.Greater_IntInt"),                           TEXT("fn.KismetMathLibrary.Greater_IntInt") },
					{ TEXT("k2.LessEqual_IntInt"),                         TEXT("fn.KismetMathLibrary.LessEqual_IntInt") },
					{ TEXT("k2.GreaterEqual_IntInt"),                      TEXT("fn.KismetMathLibrary.GreaterEqual_IntInt") },

					{ TEXT("GreaterEqual_IntInt"),                         TEXT("fn.KismetMathLibrary.GreaterEqual_IntInt") },
					{ TEXT("LessEqual_IntInt"),                            TEXT("fn.KismetMathLibrary.LessEqual_IntInt") },
					{ TEXT("Greater_IntInt"),                              TEXT("fn.KismetMathLibrary.Greater_IntInt") },
					{ TEXT("Less_IntInt"),                                 TEXT("fn.KismetMathLibrary.Less_IntInt") },
					{ TEXT("EqualEqual_IntInt"),                           TEXT("fn.KismetMathLibrary.EqualEqual_IntInt") },
					{ TEXT("NotEqual_IntInt"),                             TEXT("fn.KismetMathLibrary.NotEqual_IntInt") },
					{ TEXT("Greater_DoubleDouble"),                        TEXT("fn.KismetMathLibrary.Greater_DoubleDouble") },
					{ TEXT("Less_DoubleDouble"),                           TEXT("fn.KismetMathLibrary.Less_DoubleDouble") },
					{ TEXT("GreaterEqual_DoubleDouble"),                   TEXT("fn.KismetMathLibrary.GreaterEqual_DoubleDouble") },
					{ TEXT("LessEqual_DoubleDouble"),                      TEXT("fn.KismetMathLibrary.LessEqual_DoubleDouble") },
					{ TEXT("EqualEqual_DoubleDouble"),                     TEXT("fn.KismetMathLibrary.EqualEqual_DoubleDouble") },
					{ TEXT("NotEqual_DoubleDouble"),                       TEXT("fn.KismetMathLibrary.NotEqual_DoubleDouble") },

					{ TEXT("fn.KismetMathLibrary.DotProduct_VectorVector"), TEXT("fn.KismetMathLibrary.Dot_VectorVector") },
					{ TEXT("fn.KismetMathLibrary.DotProduct"),             TEXT("fn.KismetMathLibrary.Dot_VectorVector") },
					{ TEXT("fn.KismetMathLibrary.CrossProduct_VectorVector"), TEXT("fn.KismetMathLibrary.Cross_VectorVector") },
					{ TEXT("fn.KismetMathLibrary.CrossProduct"),           TEXT("fn.KismetMathLibrary.Cross_VectorVector") },

					{ TEXT("fn.PlayerController.GetPawn"),                 TEXT("fn.Controller.K2_GetPawn") },
					{ TEXT("fn.PlayerController.GetControlledPawn"),       TEXT("fn.Controller.K2_GetPawn") },
					{ TEXT("fn.Controller.GetPawn"),                       TEXT("fn.Controller.K2_GetPawn") },

					{ TEXT("fn.PrimitiveComponent.SetWorldRotation"),      TEXT("fn.SceneComponent.K2_SetWorldRotation") },
					{ TEXT("fn.PrimitiveComponent.SetWorldLocation"),      TEXT("fn.SceneComponent.K2_SetWorldLocation") },
					{ TEXT("fn.PrimitiveComponent.SetRelativeRotation"),   TEXT("fn.SceneComponent.K2_SetRelativeRotation") },
					{ TEXT("fn.PrimitiveComponent.SetRelativeLocation"),   TEXT("fn.SceneComponent.K2_SetRelativeLocation") },

					{ TEXT("fn.KismetSystemLibrary.MakeLiteralColor"),     TEXT("fn.KismetMathLibrary.MakeColor") },
					{ TEXT("fn.KismetMathLibrary.MakeLiteralColor"),       TEXT("fn.KismetMathLibrary.MakeColor") },
					{ TEXT("k2.MakeLiteralColor"),                         TEXT("fn.KismetMathLibrary.MakeColor") },

					{ TEXT("ev.ReceiveInitialize"),                        TEXT("ev.ReceiveBeginPlay") },
					{ TEXT("ev.ReceiveConstruct"),                         TEXT("ev.Construct") },
					{ TEXT("ev.ReceiveOnConstruct"),                       TEXT("ev.Construct") },

					{ TEXT("fn.ComboBoxString.FindSelectedIndex"),         TEXT("fn.ComboBoxString.GetSelectedIndex") },
					{ TEXT("fn.ComboBoxString.FindSelectedOption"),        TEXT("fn.ComboBoxString.GetSelectedOption") },

					{ TEXT("fn.KismetMathLibrary.BreakStruct"),            TEXT("fn.GameplayStatics.BreakHitResult") },
					{ TEXT("fn.KismetSystemLibrary.BreakStruct"),          TEXT("fn.GameplayStatics.BreakHitResult") },

					{ TEXT("fn.PlayerController.SetInputMode_UIOnly"),     TEXT("fn.WidgetBlueprintLibrary.SetInputMode_UIOnlyEx") },
					{ TEXT("fn.PlayerController.SetInputMode_GameOnly"),   TEXT("fn.WidgetBlueprintLibrary.SetInputMode_GameOnly") },
					{ TEXT("fn.PlayerController.SetInputMode_GameAndUI"),  TEXT("fn.WidgetBlueprintLibrary.SetInputMode_GameAndUIEx") },

					{ TEXT("fn.KismetInputLibrary.MakeKey"),               TEXT("fn.InputCoreLibrary.MakeKey") },

					{ TEXT("fn.PlayerController.SetShowMouseCursor"),      TEXT("prop.set.PlayerController.bShowMouseCursor") },
					{ TEXT("fn.PlayerController.SetMouseCursor"),          TEXT("prop.set.PlayerController.bShowMouseCursor") },
				};

				if (Handle.StartsWith(TEXT("cast."), ESearchCase::CaseSensitive))
				{
					FString Fixed = TEXT("k2.Cast To ") + Handle.Mid(5);
					NodeObj->SetStringField(TEXT("handle"), Fixed);
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("handle_auto_corrected"));
					Repair->SetStringField(TEXT("node_id"), NodeId);
					Repair->SetStringField(TEXT("bad_handle"), Handle);
					Repair->SetStringField(TEXT("corrected_handle"), Fixed);
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Cast handle '%s' uses bare 'cast.' prefix — auto-corrected to '%s'. "
						     "Correct format is 'k2.Cast To ClassName' (with 'k2.' prefix and uppercase 'To')."),
						*Handle, *Fixed));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					Handle = Fixed;
				}
				else if (Handle.StartsWith(TEXT("k2.Cast to ")) && !Handle.StartsWith(TEXT("k2.Cast To ")))
				{
					FString Fixed = TEXT("k2.Cast To ") + Handle.Mid(11);
					NodeObj->SetStringField(TEXT("handle"), Fixed);
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("handle_auto_corrected"));
					Repair->SetStringField(TEXT("node_id"), NodeId);
					Repair->SetStringField(TEXT("bad_handle"), Handle);
					Repair->SetStringField(TEXT("corrected_handle"), Fixed);
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Cast handle '%s' has lowercase 'to' — auto-corrected to '%s'. "
						     "Cast handles MUST use uppercase 'To': 'k2.Cast To ClassName'."),
						*Handle, *Fixed));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					Handle = Fixed;
				}

				{
					static const TMap<FString, FString> IntSpecializations = {
						{ TEXT("k2.Less"),         TEXT("fn.KismetMathLibrary.Less_IntInt") },
						{ TEXT("k2.LessEqual"),    TEXT("fn.KismetMathLibrary.LessEqual_IntInt") },
						{ TEXT("k2.Greater"),      TEXT("fn.KismetMathLibrary.Greater_IntInt") },
						{ TEXT("k2.GreaterEqual"), TEXT("fn.KismetMathLibrary.GreaterEqual_IntInt") },
					};
					if (const FString* IntVariant = IntSpecializations.Find(Handle))
					{
						auto IsSourceInt = [&](const FString& SourceRef) -> bool
						{
							FString SourceNodeId = SourceRef;
							int32 DotPos;
							if (SourceRef.FindChar(TEXT('.'), DotPos)) SourceNodeId = SourceRef.Left(DotPos);

							if (SourceNodeId.Equals(TEXT("entry"), ESearchCase::IgnoreCase) && DotPos >= 0 && Blueprint)
							{
								const FString FieldName = SourceRef.Mid(DotPos + 1);
								for (UEdGraph* EntryGraph : Blueprint->FunctionGraphs)
								{
									if (!EntryGraph || EntryGraph->GetName() != GraphName) continue;
									for (UEdGraphNode* N : EntryGraph->Nodes)
									{
										if (UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(N))
										{
											for (const TSharedPtr<FUserPinInfo>& UP : FE->UserDefinedPins)
											{
												if (UP.IsValid() && UP->PinName == FName(*FieldName))
												{
													return UP->PinType.PinCategory == UEdGraphSchema_K2::PC_Int;
												}
											}
										}
									}
								}
								return false;
							}

							if (NodesArray)
							{
								for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArray)
								{
									TSharedPtr<FJsonObject> SourceNode = SafeAsObject(NodeVal);
									if (!SourceNode.IsValid()) continue;
									FString SrcId;
									SourceNode->TryGetStringField(TEXT("id"), SrcId);
									if (SrcId != SourceNodeId) continue;

									FString SrcHandle;
									SourceNode->TryGetStringField(TEXT("handle"), SrcHandle);

									if (SrcHandle.EndsWith(TEXT("_IntInt")) ||
										SrcHandle.EndsWith(TEXT("_Int")) ||
										SrcHandle.EndsWith(TEXT(".Array_Length")) ||
										SrcHandle.EndsWith(TEXT(".Array_LastIndex")))
									{
										return true;
									}

									if (SrcHandle.StartsWith(TEXT("var.get.")) && Blueprint)
									{
										const FString VarName = SrcHandle.Mid(8);
										for (const FBPVariableDescription& Var : Blueprint->NewVariables)
										{
											if (Var.VarName == FName(*VarName))
											{
												return Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Int;
											}
										}
									}

									if (SrcHandle.Contains(TEXT("MakeLiteralInt"))) return true;
									break;
								}
							}
							return false;
						};

						if (ConnectionsArray)
						{
							const FString ToA = NodeId + TEXT(".A");
							const FString ToB = NodeId + TEXT(".B");
							bool bAnyIntInput = false;
							for (const TSharedPtr<FJsonValue>& ConnVal : *ConnectionsArray)
							{
								TSharedPtr<FJsonObject> ConnObj = SafeAsObject(ConnVal);
								if (!ConnObj.IsValid()) continue;
								FString From, To;
								ConnObj->TryGetStringField(TEXT("from"), From);
								ConnObj->TryGetStringField(TEXT("to"),   To);
								if (!To.Equals(ToA) && !To.Equals(ToB)) continue;
								if (IsSourceInt(From)) { bAnyIntInput = true; break; }
							}
							if (bAnyIntInput)
							{
								NodeObj->SetStringField(TEXT("handle"), *IntVariant);
								TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
								Repair->SetStringField(TEXT("repair"),           TEXT("handle_auto_corrected"));
								Repair->SetStringField(TEXT("node_id"),          NodeId);
								Repair->SetStringField(TEXT("bad_handle"),       Handle);
								Repair->SetStringField(TEXT("corrected_handle"), *IntVariant);
								Repair->SetStringField(TEXT("reason"), FString::Printf(
									TEXT("Handle '%s' is ambiguous — both operands wire from int sources, so specialized to '%s'. Use this directly to skip the Conv_IntToDouble coerce."),
									*Handle, **IntVariant));
								RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
								Handle = *IntVariant;
							}
						}
					}
				}

				const FString* Corrected = HandleCorrections.Find(Handle);
				if (!Corrected)
				{
					const TMap<FString, FString>& KnowledgeRedirects = BpHandleKnowledge::GetHandleRedirects();
					Corrected = KnowledgeRedirects.Find(Handle);
				}

				FString DynamicXform;
				if (!Corrected && Handle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase))
				{
					const int32 First = Handle.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, 0);
					const int32 Second = (First != INDEX_NONE)
						? Handle.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, First + 1)
						: INDEX_NONE;
					if (First != INDEX_NONE && Second != INDEX_NONE && Second > First + 1)
					{
						const FString FnName = Handle.Mid(Second + 1);
						if (FnName.StartsWith(TEXT("Break "), ESearchCase::CaseSensitive) ||
						    FnName.StartsWith(TEXT("Make "), ESearchCase::CaseSensitive))
						{
							DynamicXform = FString::Printf(TEXT("k2.%s"), *FnName);
							Corrected = &DynamicXform;
						}
					}
				}

				FString XformedHandle;
				if (Corrected && Blueprint &&
					Corrected->StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase))
				{
					const TCHAR* RequiredParent = nullptr;
					const TCHAR* PropClass = nullptr;
					if (Handle.StartsWith(TEXT("fn.Character."), ESearchCase::IgnoreCase))
					{
						RequiredParent = TEXT("Character");
						PropClass      = TEXT("Character");
					}
					else if (Handle.StartsWith(TEXT("fn.Pawn."), ESearchCase::IgnoreCase))
					{
						RequiredParent = TEXT("Character");
						PropClass      = TEXT("Character");
					}
					else if (Handle.StartsWith(TEXT("fn.Actor."), ESearchCase::IgnoreCase))
					{
						RequiredParent = TEXT("Actor");
						PropClass      = TEXT("Actor");
					}
					if (RequiredParent)
					{
						bool bIsDescendant = false;
						for (UClass* C = Blueprint->ParentClass; C; C = C->GetSuperClass())
						{
							const FString CN = C->GetName();
							if (CN.Equals(RequiredParent, ESearchCase::IgnoreCase) ||
								CN.Equals(FString(TEXT("A")) + RequiredParent, ESearchCase::IgnoreCase) ||
								CN.Equals(FString(TEXT("U")) + RequiredParent, ESearchCase::IgnoreCase))
							{
								bIsDescendant = true;
								break;
							}
						}
						if (!bIsDescendant && PropClass)
						{
							const FString MemberName = Corrected->RightChop(8);
							XformedHandle = FString::Printf(TEXT("prop.get.%s.%s"), PropClass, *MemberName);
							Corrected = &XformedHandle;
						}
					}
				}

				if (Corrected)
				{
					NodeObj->SetStringField(TEXT("handle"), *Corrected);

					if (Handle != *Corrected)
					{
						TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
						Repair->SetStringField(TEXT("repair"), TEXT("handle_auto_corrected"));
						Repair->SetStringField(TEXT("node_id"), NodeId);
						Repair->SetStringField(TEXT("bad_handle"), Handle);
						Repair->SetStringField(TEXT("corrected_handle"), *Corrected);
						Repair->SetStringField(TEXT("reason"), FString::Printf(
							TEXT("Handle '%s' does not exist — auto-corrected to '%s'. "
							     "Use the corrected handle in future calls to avoid this repair."),
							*Handle, **Corrected));
						RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					}

					Handle = *Corrected;
				}

				if (Handle.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase) && Blueprint && Blueprint->SkeletonGeneratedClass)
				{
					FString AfterEv = Handle.RightChop(3);
					int32 DotIdx;
					if (AfterEv.FindChar(TEXT('.'), DotIdx) && DotIdx > 0)
					{
						FString FirstSeg = AfterEv.Left(DotIdx);
						FString SecondSeg = AfterEv.Mid(DotIdx + 1);
						const bool bIsKnownVerb =
							FirstSeg.Equals(TEXT("Dispatcher"),        ESearchCase::IgnoreCase) ||
							FirstSeg.Equals(TEXT("DispatcherBind"),    ESearchCase::IgnoreCase) ||
							FirstSeg.Equals(TEXT("DispatcherUnbind"),  ESearchCase::IgnoreCase) ||
							FirstSeg.Equals(TEXT("DispatcherClear"),   ESearchCase::IgnoreCase) ||
							FirstSeg.Equals(TEXT("DispatcherAssign"),  ESearchCase::IgnoreCase);
						if (!bIsKnownVerb && !SecondSeg.IsEmpty() &&
							FirstSeg.Equals(SecondSeg, ESearchCase::IgnoreCase))
						{
							bool bIsDispatcher = false;
							for (TFieldIterator<FMulticastDelegateProperty> PropIt(
									Blueprint->SkeletonGeneratedClass); PropIt; ++PropIt)
							{
								if (PropIt->GetName().Equals(FirstSeg, ESearchCase::IgnoreCase))
								{
									bIsDispatcher = true;
									break;
								}
							}
							if (bIsDispatcher)
							{
								FString Fixed = FString::Printf(TEXT("ev.Dispatcher.%s"), *FirstSeg);
								NodeObj->SetStringField(TEXT("handle"), Fixed);
								TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
								Repair->SetStringField(TEXT("repair"), TEXT("handle_auto_corrected"));
								Repair->SetStringField(TEXT("node_id"), NodeId);
								Repair->SetStringField(TEXT("bad_handle"), Handle);
								Repair->SetStringField(TEXT("corrected_handle"), Fixed);
								Repair->SetStringField(TEXT("reason"), FString::Printf(
									TEXT("Dispatcher broadcast handle '%s' follows the wrong pattern — "
									     "auto-corrected to '%s'. The correct format for calling/broadcasting "
									     "a dispatcher is 'ev.Dispatcher.<DispatcherName>' (the verb is literally "
									     "'Dispatcher', not the dispatcher's name). Use 'ev.DispatcherBind.<Name>' "
									     "to bind, 'ev.DispatcherAssign.<Name>' to assign."),
									*Handle, *Fixed));
								RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
								Handle = Fixed;
							}
						}
					}

					if (Handle.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase)
						&& Blueprint && Blueprint->SkeletonGeneratedClass)
					{
						FString AfterEv4c = Handle.RightChop(3);
						int32 DotIdx4c;
						if (!AfterEv4c.FindChar(TEXT('.'), DotIdx4c) && !AfterEv4c.IsEmpty())
						{
							const bool bLooksLikeReceive = AfterEv4c.StartsWith(TEXT("Receive"), ESearchCase::IgnoreCase);
							const bool bLooksLikeCustomEvent = AfterEv4c.Equals(TEXT("CustomEvent"), ESearchCase::IgnoreCase);
							if (!bLooksLikeReceive && !bLooksLikeCustomEvent)
							{
								for (TFieldIterator<FMulticastDelegateProperty> PropIt(
										Blueprint->SkeletonGeneratedClass); PropIt; ++PropIt)
								{
									if (PropIt->GetName().Equals(AfterEv4c, ESearchCase::IgnoreCase))
									{
										FString Fixed = FString::Printf(TEXT("ev.Dispatcher.%s"), *AfterEv4c);
										NodeObj->SetStringField(TEXT("handle"), Fixed);
										TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
										Repair->SetStringField(TEXT("repair"), TEXT("handle_auto_corrected"));
										Repair->SetStringField(TEXT("node_id"), NodeId);
										Repair->SetStringField(TEXT("bad_handle"), Handle);
										Repair->SetStringField(TEXT("corrected_handle"), Fixed);
										Repair->SetStringField(TEXT("reason"), FString::Printf(
											TEXT("'%s' matched a multicast delegate on this blueprint — auto-corrected "
											     "to '%s'. Calling/broadcasting a dispatcher uses the literal verb "
											     "'Dispatcher' as the second segment: 'ev.Dispatcher.<DispatcherName>'."),
											*Handle, *Fixed));
										RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
										Handle = Fixed;
										break;
									}
								}
							}
						}
					}
				}

				if (Handle.StartsWith(TEXT("fn.Self.Broadcast "), ESearchCase::IgnoreCase) && Blueprint && Blueprint->SkeletonGeneratedClass)
				{
					FString DispName = Handle.Mid(18);
					if (!DispName.IsEmpty())
					{
						for (TFieldIterator<FMulticastDelegateProperty> PropIt(Blueprint->SkeletonGeneratedClass); PropIt; ++PropIt)
						{
							if (PropIt->GetName().Equals(DispName, ESearchCase::IgnoreCase))
							{
								FString Fixed4d = FString::Printf(TEXT("ev.Dispatcher.%s"), *PropIt->GetName());
								NodeObj->SetStringField(TEXT("handle"), Fixed4d);
								TSharedPtr<FJsonObject> Repair4d = MakeShared<FJsonObject>();
								Repair4d->SetStringField(TEXT("repair"), TEXT("handle_auto_corrected"));
								Repair4d->SetStringField(TEXT("node_id"), NodeId);
								Repair4d->SetStringField(TEXT("bad_handle"), Handle);
								Repair4d->SetStringField(TEXT("corrected_handle"), Fixed4d);
								Repair4d->SetStringField(TEXT("reason"), FString::Printf(
									TEXT("'%s' is not valid — event dispatchers broadcast via "
									     "'ev.Dispatcher.<Name>', not 'fn.Self.Broadcast <Name>'. "
									     "Auto-corrected to '%s'."),
									*Handle, *Fixed4d));
								RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair4d)));
								Handle = Fixed4d;
								break;
							}
						}
					}
				}
			}
		}

		PreFlightVariableHandles(Blueprint, Graph, GraphName, Args, PreFlightIssues, RepairsMade);
		bHasNodes = Args->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray && NodesArray->Num() > 0;
		Args->TryGetArrayField(TEXT("connections"), ConnectionsArray);

		if (NodesArray)
		{
			for (int32 PFi = 0; PFi < NodesArray->Num(); PFi++)
			{
				TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[PFi]);
				if (!NodeObj.IsValid()) continue;
				FString NId, NHandle;
				NodeObj->TryGetStringField(TEXT("id"), NId);
				NodeObj->TryGetStringField(TEXT("handle"), NHandle);
				if (NHandle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), NHandle);
				if (NHandle.IsEmpty()) continue;
				if (!NHandle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase)) continue;

				int32 SecondDot = INDEX_NONE;
				const FString AfterFn = NHandle.Mid(3);
				if (!AfterFn.FindChar(TEXT('.'), SecondDot) || SecondDot <= 0) continue;
				FString ClassNamePart = AfterFn.Left(SecondDot);
				const FString MemberName = AfterFn.Mid(SecondDot + 1);
				if (ClassNamePart.IsEmpty() || MemberName.IsEmpty()) continue;

				FString StrippedClassName = ClassNamePart;
				if (StrippedClassName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
				{
					StrippedClassName = StrippedClassName.LeftChop(2);
				}

				UClass* TargetClass = nullptr;
				const TArray<FString> SearchNames = { ClassNamePart, ClassNamePart + TEXT("_C"),
				                                       StrippedClassName, StrippedClassName + TEXT("_C") };
				for (const FString& SN : SearchNames)
				{
					UClass* C = FindFirstObject<UClass>(*SN);
					if (C && GraphEditHelpers::IsTransientBpClass(C))
					{
						C = nullptr;
						for (TObjectIterator<UClass> It; It; ++It)
						{
							if (It->GetName() == SN && !GraphEditHelpers::IsTransientBpClass(*It))
							{ C = *It; break; }
						}
					}
					if (C && C->ClassGeneratedBy != nullptr)
					{
						TargetClass = C;
						break;
					}
				}
				if (!TargetClass) continue;

				auto FunctionExistsOnBPClass = [&MemberName](UClass* C) -> bool
				{
					if (!C) return false;
					if (C->FindFunctionByName(FName(*MemberName)) != nullptr) return true;
					UBlueprint* BP = Cast<UBlueprint>(C->ClassGeneratedBy);
					if (!BP) return false;
					if (BP->SkeletonGeneratedClass && BP->SkeletonGeneratedClass != C
					    && BP->SkeletonGeneratedClass->FindFunctionByName(FName(*MemberName)) != nullptr)
					{
						return true;
					}
					for (UEdGraph* G : BP->FunctionGraphs)
					{
						if (G && G->GetName().Equals(MemberName, ESearchCase::IgnoreCase)) return true;
					}
					return false;
				};
				if (FunctionExistsOnBPClass(TargetClass)) continue;

				FString StrippedMember = MemberName;
				bool bAccessorGuess = false;
				if (StrippedMember.StartsWith(TEXT("get"), ESearchCase::IgnoreCase) && StrippedMember.Len() > 3)
				{
					StrippedMember = StrippedMember.RightChop(3);
					bAccessorGuess = true;
				}
				else if (StrippedMember.StartsWith(TEXT("set"), ESearchCase::IgnoreCase) && StrippedMember.Len() > 3)
				{
					StrippedMember = StrippedMember.RightChop(3);
					bAccessorGuess = true;
				}

				FProperty* MatchedProp = nullptr;
				auto IsUserDeclaredProperty = [](const FProperty* P) -> bool
				{
					if (!P) return false;
					UClass* OwnerC = P->GetOwnerClass();
					if (!OwnerC) return false;
					return OwnerC->ClassGeneratedBy != nullptr;
				};
				for (TFieldIterator<FProperty> It(TargetClass); It; ++It)
				{
					if (!IsUserDeclaredProperty(*It)) continue;
					if (It->GetName().Equals(StrippedMember, ESearchCase::IgnoreCase)
					 || It->GetName().Equals(MemberName, ESearchCase::IgnoreCase))
					{ MatchedProp = *It; break; }
				}
				if (!MatchedProp) continue;

				if (!NId.IsEmpty()) PreFlightDropNodeIds.Add(NId);

				TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("type"), TEXT("user_bp_property_as_function"));
				Issue->SetStringField(TEXT("node_id"), NId);
				Issue->SetStringField(TEXT("bad_handle"), NHandle);
				Issue->SetStringField(TEXT("blueprint_class"), ClassNamePart);
				Issue->SetStringField(TEXT("property_name"), MatchedProp->GetName());
				Issue->SetStringField(TEXT("hint"), FString::Printf(
					TEXT("Handle '%s' references variable '%s' on user Blueprint '%s' as if it were a function — "
					     "%sthat is not a valid pattern. To access this variable on a casted result, do ONE of:\n"
					     "  (A) Add a public getter function on '%s' (e.g. 'Get%s' returning %s) via "
					     "blueprint(action='add_function', blueprint_path='/Game/.../%s', function_name='Get%s', "
					     "outputs=[{name:'ReturnValue', type:'...'}]). Then call fn.%s.Get%s after a cast.\n"
					     "  (B) If you control both blueprints in the same call: cast first "
					     "(k2.Cast To %s), then connect cast.As %s → the impure node that needs the value, "
					     "and read the variable through that connection chain instead of as a separate node.\n"
					     "Node DROPPED — fix and rebuild."),
					*NHandle, *MatchedProp->GetName(), *StrippedClassName,
					bAccessorGuess ? TEXT("UE does NOT auto-generate get/set accessor functions for user BP variables — ")
					               : TEXT(""),
					*StrippedClassName, *MatchedProp->GetName(), *MatchedProp->GetCPPType(),
					*StrippedClassName, *MatchedProp->GetName(),
					*StrippedClassName, *MatchedProp->GetName(),
					*StrippedClassName, *StrippedClassName));
				PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
			}
		}

		if (NodesArray)
		{
			static const TSet<FString> AllowedEngineBaseClassesLower = {
				TEXT("actor"), TEXT("pawn"), TEXT("character"), TEXT("playercontroller"),
				TEXT("aicontroller"), TEXT("controller"), TEXT("gamemode"), TEXT("gamemodebase"),
				TEXT("gamestate"), TEXT("gamestatebase"), TEXT("gameinstance"), TEXT("playerstate"),
				TEXT("hud"), TEXT("playercameramanager"), TEXT("worldsettings"),
				TEXT("actorcomponent"), TEXT("scenecomponent"), TEXT("primitivecomponent"),
				TEXT("staticmeshcomponent"), TEXT("skeletalmeshcomponent"),
				TEXT("characterMovementcomponent"), TEXT("charactermovementcomponent"),
				TEXT("userwidget"), TEXT("widget"), TEXT("animinstance"),
				TEXT("savegame"), TEXT("object")
			};
			for (int32 PFi = 0; PFi < NodesArray->Num(); PFi++)
			{
				TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[PFi]);
				if (!NodeObj.IsValid()) continue;
				FString NId, NHandle;
				NodeObj->TryGetStringField(TEXT("id"), NId);
				NodeObj->TryGetStringField(TEXT("handle"), NHandle);
				if (NHandle.IsEmpty()) continue;
				if (!NHandle.StartsWith(TEXT("k2.Cast To "), ESearchCase::IgnoreCase)) continue;

				FString TargetClassName = NHandle.RightChop(11).TrimStartAndEnd();
				if (TargetClassName.IsEmpty()) continue;

				FString CompareName = TargetClassName;
				CompareName.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);
				if (AllowedEngineBaseClassesLower.Contains(CompareName.ToLower())) continue;

				UClass* TargetClass = FindFirstObject<UClass>(*TargetClassName);
				if (!TargetClass) TargetClass = FindFirstObject<UClass>(*(TargetClassName + TEXT("_C")));
				if (!TargetClass) continue;
				if (TargetClass->ClassGeneratedBy == nullptr) continue;

				{
					bool bExemptByNativeParent = false;
					for (UClass* Walk = TargetClass; Walk; Walk = Walk->GetSuperClass())
					{
						if (Walk->ClassGeneratedBy != nullptr) continue;
						const FString NativeName = Walk->GetName().ToLower();
						if (NativeName == TEXT("gamemodebase") ||
						    NativeName == TEXT("gamemode") ||
						    NativeName == TEXT("gameinstance"))
						{
							bExemptByNativeParent = true;
							break;
						}
					}
					if (bExemptByNativeParent) continue;
				}

				TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>();
				Hint->SetStringField(TEXT("type"), TEXT("cast_to_user_bp_hint"));
				Hint->SetStringField(TEXT("node_id"), NId);
				Hint->SetStringField(TEXT("handle"), NHandle);
				Hint->SetStringField(TEXT("target_class"), CompareName);
				Hint->SetStringField(TEXT("hint"), FString::Printf(
					TEXT("Casting to user Blueprint '%s' is an anti-pattern in BP — it hard-loads "
					     "'%s' into every consumer's package and silently dies on Cast Failed when a "
					     "sibling class shows up. Pick the loosest-coupling alternative:\n"
					     "  • If you want a COMPONENT off any actor (stats, inventory, health) — "
					     "'fn.Actor.GetComponentByClass(ComponentClass=BP_YourComponent)'. Works regardless of actor class.\n"
					     "  • If you want a BEHAVIOUR contract (Interact, TakeDamage, OnPickedUp) — "
					     "create a Blueprint Interface, implement_interface on each participant, then "
					     "call 'fn.<Interface>.<Method>' polymorphically. Gate via 'fn.KismetSystemLibrary.DoesImplementInterface'.\n"
					     "  • If you want to SIGNAL listeners (OnHealthChanged, OnInventoryChanged) — "
					     "use an event dispatcher on the target ('ev.DispatcherBind.<X>').\n"
					     "Cast-To is fine for engine base types (Character, PlayerController, GameMode, "
					     "ActorComponent, UserWidget). NOT for authored BPs. The cast WILL still build "
					     "if you keep it — this is a nudge, not a block."),
					*CompareName, *CompareName));
				PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Hint)));
			}
		}

		if (NodesArray && Blueprint && Blueprint->SkeletonGeneratedClass)
		{
			TSet<FString> DispatcherNames;
			for (TFieldIterator<FMulticastDelegateProperty> PropIt(
					Blueprint->SkeletonGeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
			{
				DispatcherNames.Add(PropIt->GetName().ToLower());
			}
			if (DispatcherNames.Num() > 0)
			{
				for (int32 ci = 0; ci < NodesArray->Num(); ci++)
				{
					TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[ci]);
					if (!NodeObj.IsValid()) continue;
					FString NHandle, CustomName, NId;
					NodeObj->TryGetStringField(TEXT("handle"), NHandle);
					if (!NHandle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase)) continue;
					NodeObj->TryGetStringField(TEXT("custom_name"), CustomName);
					if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("custom_event_name"), CustomName);
					if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("event_name"), CustomName);
					if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("name"), CustomName);
					if (CustomName.IsEmpty()) continue;
					if (!DispatcherNames.Contains(CustomName.ToLower())) continue;

					NodeObj->TryGetStringField(TEXT("id"), NId);
					if (!NId.IsEmpty()) PreFlightDropNodeIds.Add(NId);

					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("custom_event_dispatcher_collision_dropped"));
					Repair->SetStringField(TEXT("node_id"), NId);
					Repair->SetStringField(TEXT("custom_name"), CustomName);
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("ev.CustomEvent{custom_name='%s'} was DROPPED because a Dispatcher named '%s' "
						     "already exists on this blueprint. UE creates a skeleton function named after the "
						     "Dispatcher, so a CustomEvent of the same name would cause "
						     "'Tried to create a property %s ... already exists' at compile. "
						     "If you meant to BROADCAST the dispatcher, use `ev.Dispatcher.%s` (already correct elsewhere in this batch). "
						     "If you meant to HANDLE the dispatcher firing, bind a separate custom-named event via "
						     "`ev.DispatcherAssign.%s` or rename this CustomEvent to something like '%s_Handler'."),
						*CustomName, *CustomName, *CustomName, *CustomName, *CustomName, *CustomName));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				}
			}
		}

		if (NodesArray)
		{
			static const TArray<FString> SpawnHandles = {
				TEXT("fn.GameplayStatics.BeginDeferredActorSpawnFromClass"),
				TEXT("fn.GameplayStatics.FinishSpawningActor"),
			};

			TSet<FString> SpawnNodeIds;
			for (int32 i = 0; i < NodesArray->Num(); i++)
			{
				TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[i]);
				if (!NodeObj.IsValid()) continue;
				FString NHandle, NId;
				NodeObj->TryGetStringField(TEXT("handle"), NHandle);
				if (NHandle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), NHandle);
				NodeObj->TryGetStringField(TEXT("id"), NId);
				if (NId.IsEmpty()) continue;
				for (const FString& H : SpawnHandles)
				{
					if (NHandle.Equals(H, ESearchCase::IgnoreCase)) { SpawnNodeIds.Add(NId); break; }
				}
			}

			if (SpawnNodeIds.Num() > 0)
			{
				const TArray<TSharedPtr<FJsonValue>>* ConnArr = nullptr;
				Args->TryGetArrayField(TEXT("connections"), ConnArr);
				if (ConnArr)
				{
					for (const TSharedPtr<FJsonValue>& CVal : *ConnArr)
					{
						TSharedPtr<FJsonObject> CObj = SafeAsObject(CVal);
						if (!CObj.IsValid()) continue;
						FString To, ToPin;
						CObj->TryGetStringField(TEXT("to"), To);
						CObj->TryGetStringField(TEXT("to_pin"), ToPin);
						if (ToPin.IsEmpty())
						{
							int32 Dot = INDEX_NONE;
							if (To.FindLastChar(TEXT('.'), Dot) && Dot > 0)
							{
								ToPin = To.Mid(Dot + 1);
								To = To.Left(Dot);
							}
						}
						if (ToPin.Equals(TEXT("SpawnTransform"), ESearchCase::IgnoreCase))
						{
							SpawnNodeIds.Remove(To);
						}
					}
				}
			}

			if (SpawnNodeIds.Num() > 0)
			{
				const FString XformId = TEXT("__auto_spawn_xform");

				TArray<TSharedPtr<FJsonValue>> NewNodes;
				NewNodes.Reserve(NodesArray->Num() + 1);
				for (const TSharedPtr<FJsonValue>& V : *NodesArray) NewNodes.Add(V);
				TSharedPtr<FJsonObject> XformObj = MakeShared<FJsonObject>();
				XformObj->SetStringField(TEXT("id"),     XformId);
				XformObj->SetStringField(TEXT("handle"), TEXT("fn.KismetMathLibrary.MakeTransform"));
				NewNodes.Add(MakeShared<FJsonValueObject>(XformObj));
				Args->SetArrayField(TEXT("nodes"), NewNodes);

				const TArray<TSharedPtr<FJsonValue>>* ExistingConns = nullptr;
				TArray<TSharedPtr<FJsonValue>> AllConns;
				if (Args->TryGetArrayField(TEXT("connections"), ExistingConns) && ExistingConns)
				{
					for (const TSharedPtr<FJsonValue>& V : *ExistingConns) AllConns.Add(V);
				}
				for (const FString& TargetId : SpawnNodeIds)
				{
					TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
					CObj->SetStringField(TEXT("from"),     XformId);
					CObj->SetStringField(TEXT("from_pin"), TEXT("ReturnValue"));
					CObj->SetStringField(TEXT("to"),       TargetId);
					CObj->SetStringField(TEXT("to_pin"),   TEXT("SpawnTransform"));
					AllConns.Add(MakeShared<FJsonValueObject>(CObj));
				}
				Args->SetArrayField(TEXT("connections"), AllConns);

				Args->TryGetArrayField(TEXT("nodes"), NodesArray);
				Args->TryGetArrayField(TEXT("connections"), ConnectionsArray);

				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"),       TEXT("by_ref_struct_pin_auto_wired"));
				Repair->SetStringField(TEXT("injected_node"), XformId);
				Repair->SetStringField(TEXT("injected_handle"), TEXT("fn.KismetMathLibrary.MakeTransform"));
				TArray<TSharedPtr<FJsonValue>> WiredArr;
				for (const FString& Id : SpawnNodeIds) WiredArr.Add(MakeShared<FJsonValueString>(Id));
				Repair->SetArrayField(TEXT("wired_into"), WiredArr);
				Repair->SetStringField(TEXT("reason"),
					TEXT("BeginDeferredActorSpawnFromClass / FinishSpawningActor have a by-ref SpawnTransform pin "
					     "— UE's compiler rejects defaults on by-ref struct pins. Auto-injected a shared "
					     "MakeTransform node (identity transform) and wired it to every Spawn-family node that "
					     "was missing the connection. To override: declare your own transform source and wire it "
					     "directly to .SpawnTransform on each Spawn node."));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
			}
		}

		if (NodesArray)
		{
			TMap<FString, FString> CustomNameToFirstId;
			for (int32 ci = 0; ci < NodesArray->Num(); ci++)
			{
				TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[ci]);
				if (!NodeObj.IsValid()) continue;
				FString NHandle, CustomName, NId;
				NodeObj->TryGetStringField(TEXT("handle"), NHandle);
				if (NHandle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), NHandle);
				if (!NHandle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase)) continue;
				NodeObj->TryGetStringField(TEXT("custom_name"), CustomName);
				if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("custom_event_name"), CustomName);
				if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("event_name"), CustomName);
				if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("name"), CustomName);
				if (CustomName.IsEmpty()) continue;
				NodeObj->TryGetStringField(TEXT("id"), NId);
				if (NId.IsEmpty()) continue;
				if (PreFlightDropNodeIds.Contains(NId)) continue;

				const FString NameKey = CustomName.ToLower();
				if (FString* FirstId = CustomNameToFirstId.Find(NameKey))
				{
					PreFlightDropNodeIds.Add(NId);
					PreFlightIdAliases.Add(NId, *FirstId);

					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("duplicate_custom_event_compressed"));
					Repair->SetStringField(TEXT("dropped_id"), NId);
					Repair->SetStringField(TEXT("aliased_to_id"), *FirstId);
					Repair->SetStringField(TEXT("custom_name"), CustomName);
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Two ev.CustomEvent rows share custom_name='%s' (ids '%s' and '%s'). "
						     "Kept '%s'; dropped '%s' and redirected its connections to '%s' so UE doesn't "
						     "auto-suffix the duplicate at compile time and leave a '%s_2' orphan behind."),
						*CustomName, **FirstId, *NId, **FirstId, *NId, **FirstId, *CustomName));
					RepairsMade.Add(MakeShared<FJsonValueObject>(Repair));
				}
				else
				{
					CustomNameToFirstId.Add(NameKey, NId);
				}
			}
		}

		for (const TPair<FString, int32>& Pair : IdCounts)
		{
			if (Pair.Value > 1)
			{
				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("duplicate_node_id_skipped"));
				Repair->SetStringField(TEXT("node_id"), Pair.Key);
				Repair->SetStringField(TEXT("count"), FString::FromInt(Pair.Value));
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("node_id '%s' appears %d times in nodes[]. Each node must have a unique id — "
					     "the first occurrence is kept; extras are skipped. Assign distinct ids."),
					*Pair.Key, Pair.Value));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				PreFlightSkipNodeIds.Add(Pair.Key);
			}
		}

		if (NodesArray)
		{
			for (int32 PFli = 0; PFli < NodesArray->Num(); PFli++)
			{
				TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[PFli]);
				if (!NodeObj.IsValid()) continue;
				FString NId, NHandle;
				NodeObj->TryGetStringField(TEXT("id"), NId);
				NodeObj->TryGetStringField(TEXT("handle"), NHandle);
				if (NHandle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), NHandle);
				if (NHandle.IsEmpty()) continue;

				const FString CastPrefix = TEXT("k2.Cast To ");
				if (!NHandle.StartsWith(CastPrefix, ESearchCase::IgnoreCase)) continue;
				const FString TargetClass = NHandle.Mid(CastPrefix.Len());
				if (!TargetClass.EndsWith(TEXT("Library"), ESearchCase::IgnoreCase)) continue;

				UClass* LibClass = FindFirstObject<UClass>(*TargetClass, EFindFirstObjectOptions::None);
				if (!LibClass) LibClass = FindFirstObject<UClass>(*(TargetClass + TEXT("_C")), EFindFirstObjectOptions::None);
				if (LibClass && !LibClass->IsChildOf(UBlueprintFunctionLibrary::StaticClass())) continue;

				TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("type"), TEXT("cast_to_function_library"));
				Issue->SetStringField(TEXT("node_id"), NId);
				Issue->SetStringField(TEXT("bad_handle"), NHandle);
				Issue->SetStringField(TEXT("hint"), FString::Printf(
					TEXT("'%s' attempts to cast to a BlueprintFunctionLibrary — libraries are static helpers "
					     "with no instances, so this cast can never succeed. "
					     "Instead of casting, call the library function directly: e.g. "
					     "fn.%s.FunctionName(...). No cast or Target pin needed."),
					*NHandle, *TargetClass));
				PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
			}
		}

		if (ConnectionsArray)
		{
			TSet<FString> ValidDeclaredIds;
			ValidDeclaredIds.Add(TEXT("entry"));
			ValidDeclaredIds.Add(TEXT("return"));
			for (int32 PFi = 0; PFi < NodesArray->Num(); PFi++)
			{
				TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[PFi]);
				if (!NodeObj.IsValid()) continue;
				FString NodeId;
				NodeObj->TryGetStringField(TEXT("id"), NodeId);
				if (!NodeId.IsEmpty() && !PreFlightSkipNodeIds.Contains(NodeId))
					ValidDeclaredIds.Add(NodeId);
			}

			TMap<FString, FString> NodeIdToHandle;
			TMap<FString, int32> SequenceNodeOutputs;
			NodeIdToHandle.Add(TEXT("entry"), TEXT("entry"));
			NodeIdToHandle.Add(TEXT("return"), TEXT("return"));
			for (int32 PFi2 = 0; PFi2 < NodesArray->Num(); PFi2++)
			{
				TSharedPtr<FJsonObject> NodeObj2 = SafeAsObject((*NodesArray)[PFi2]);
				if (!NodeObj2.IsValid()) continue;
				FString NId2, NHandle2;
				NodeObj2->TryGetStringField(TEXT("id"), NId2);
				NodeObj2->TryGetStringField(TEXT("handle"), NHandle2);
				if (NHandle2.IsEmpty()) NodeObj2->TryGetStringField(TEXT("type"), NHandle2);
				if (!NId2.IsEmpty() && !NHandle2.IsEmpty())
					NodeIdToHandle.Add(NId2, NHandle2);

				if (NHandle2.Equals(TEXT("k2.Sequence"), ESearchCase::IgnoreCase) && !NId2.IsEmpty())
				{
					int32 NumOutputs = 2;
					double NumOutputsDouble = 0.0;
					if (NodeObj2->TryGetNumberField(TEXT("num_outputs"), NumOutputsDouble))
						NumOutputs = (int32)NumOutputsDouble;
					SequenceNodeOutputs.Add(NId2, NumOutputs);
				}
			}

			TSet<FString> ForEachLoopNodeIds;
			TSet<FString> ForEachLoopExecWired;
			for (int32 PFi3 = 0; PFi3 < NodesArray->Num(); PFi3++)
			{
				TSharedPtr<FJsonObject> NodeObj3 = SafeAsObject((*NodesArray)[PFi3]);
				if (!NodeObj3.IsValid()) continue;
				FString NId3, NHandle3;
				NodeObj3->TryGetStringField(TEXT("id"), NId3);
				NodeObj3->TryGetStringField(TEXT("handle"), NHandle3);
				if (NHandle3.IsEmpty()) NodeObj3->TryGetStringField(TEXT("type"), NHandle3);
				if (!NId3.IsEmpty() && (
					NHandle3.Equals(TEXT("k2.ForEachLoop"), ESearchCase::IgnoreCase) ||
					NHandle3.Equals(TEXT("k2.foreachloop"), ESearchCase::IgnoreCase) ||
					NHandle3.Equals(TEXT("k2.ForEachLoopWithBreak"), ESearchCase::IgnoreCase) ||
					NHandle3.Equals(TEXT("k2.foreachloopwithbreak"), ESearchCase::IgnoreCase)))
				{
					ForEachLoopNodeIds.Add(NId3);
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* DefaultsArrayForSeq = nullptr;
			if (Args->TryGetArrayField(TEXT("defaults"), DefaultsArrayForSeq) && DefaultsArrayForSeq)
			{
				for (const auto& DefVal : *DefaultsArrayForSeq)
				{
					TSharedPtr<FJsonObject> Def = SafeAsObject(DefVal);
					if (!Def.IsValid()) continue;
					FString DefNodeId, DefPin;
					Def->TryGetStringField(TEXT("node_id"), DefNodeId);
					Def->TryGetStringField(TEXT("pin_name"), DefPin);
					if (DefPin.Equals(TEXT("num_outputs"), ESearchCase::IgnoreCase)
						&& SequenceNodeOutputs.Contains(DefNodeId))
					{
						int32 NewCount = 2;
						double NewCountD = 0.0;
						if (Def->TryGetNumberField(TEXT("value"), NewCountD))
							NewCount = (int32)NewCountD;
						else
						{
							FString StrVal;
							if (Def->TryGetStringField(TEXT("value"), StrVal))
								NewCount = FCString::Atoi(*StrVal);
						}
						for (const auto& NVal : *NodesArray)
						{
							TSharedPtr<FJsonObject> NObj = SafeAsObject(NVal);
							if (!NObj.IsValid()) continue;
							FString NId;
							NObj->TryGetStringField(TEXT("id"), NId);
							if (NId == DefNodeId)
							{
								NObj->SetNumberField(TEXT("num_outputs"), NewCount);
								break;
							}
						}
						SequenceNodeOutputs[DefNodeId] = NewCount;
						TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
						Repair->SetStringField(TEXT("repair"), TEXT("sequence_num_outputs_promoted"));
						Repair->SetStringField(TEXT("node_id"), DefNodeId);
						Repair->SetNumberField(TEXT("num_outputs"), NewCount);
						Repair->SetStringField(TEXT("reason"),
							TEXT("num_outputs belongs in the node definition, not defaults[]. "
							     "Auto-promoted to the Sequence node. In future calls, put "
							     "\"num_outputs\":N directly on the Sequence node object."));
						RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					}
				}
			}

			TSet<FString> ValidReturnPinNames;
			ValidReturnPinNames.Add(TEXT("execute"));
			{
				TArray<UK2Node_FunctionResult*> ResultNodes;
				Graph->GetNodesOfClass<UK2Node_FunctionResult>(ResultNodes);
				if (ResultNodes.Num() > 0 && ResultNodes[0])
				{
					for (UEdGraphPin* Pin : ResultNodes[0]->Pins)
					{
						if (!Pin || Pin->Direction != EGPD_Input) continue;
						const FString PinName = Pin->PinName.ToString();
						if (PinName.IsEmpty()) continue;
						ValidReturnPinNames.Add(PinName);
					}
					for (const TSharedPtr<FUserPinInfo>& PinInfo : ResultNodes[0]->UserDefinedPins)
					{
						if (PinInfo.IsValid())
							ValidReturnPinNames.Add(PinInfo->PinName.ToString());
					}
				}
				else
				{
					TArray<UK2Node_Tunnel*> Tunnels;
					Graph->GetNodesOfClass<UK2Node_Tunnel>(Tunnels);
					for (UK2Node_Tunnel* T : Tunnels)
					{
						if (!T || !T->bCanHaveInputs || T->bCanHaveOutputs) continue;
						for (UEdGraphPin* Pin : T->Pins)
						{
							if (!Pin || Pin->Direction != EGPD_Input) continue;
							const FString PinName = Pin->PinName.ToString();
							if (PinName.IsEmpty()) continue;
							ValidReturnPinNames.Add(PinName);
						}
						for (const TSharedPtr<FUserPinInfo>& PinInfo : T->UserDefinedPins)
							if (PinInfo.IsValid()) ValidReturnPinNames.Add(PinInfo->PinName.ToString());
					}
				}
			}

			TSet<FString> ValidEntryPinNames;
			ValidEntryPinNames.Add(TEXT("then"));
			{
				TArray<UK2Node_FunctionEntry*> EntryNodes;
				Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
				if (EntryNodes.Num() > 0 && EntryNodes[0])
				{
					for (const TSharedPtr<FUserPinInfo>& PinInfo : EntryNodes[0]->UserDefinedPins)
					{
						if (PinInfo.IsValid())
							ValidEntryPinNames.Add(PinInfo->PinName.ToString());
					}
					for (UEdGraphPin* P : EntryNodes[0]->Pins)
					{
						if (P && P->Direction == EGPD_Output && !P->bHidden)
							ValidEntryPinNames.Add(P->PinName.ToString());
					}
				}
				else
				{
					TArray<UK2Node_Tunnel*> Tunnels;
					Graph->GetNodesOfClass<UK2Node_Tunnel>(Tunnels);
					for (UK2Node_Tunnel* T : Tunnels)
					{
						if (!T || !T->bCanHaveOutputs || T->bCanHaveInputs) continue;
						for (const TSharedPtr<FUserPinInfo>& PinInfo : T->UserDefinedPins)
							if (PinInfo.IsValid()) ValidEntryPinNames.Add(PinInfo->PinName.ToString());
					}
				}
			}

			auto GetBaseNodeId = [](const FString& Id) -> FString
			{
				static const TCHAR* HandlePrefixes[] = {
					TEXT("var.get."), TEXT("var.set."), TEXT("fn."), TEXT("k2."),
					TEXT("ev."), TEXT("prop."), TEXT("entry."), TEXT("return.")
				};
				for (const TCHAR* Prefix : HandlePrefixes)
					if (Id.StartsWith(Prefix, ESearchCase::IgnoreCase)) return Id;
				int32 DotIdx;
				if (Id.FindChar(TEXT('.'), DotIdx)) return Id.Left(DotIdx);
				return Id;
			};

			TSet<FString> SeenExecOutputs;

			TSet<FString> NodesReceivingAnyConnection;
			TSet<FString> NodesReceivingExecWire;

			for (int32 PFi = 0; PFi < ConnectionsArray->Num(); PFi++)
			{
				TSharedPtr<FJsonObject> Conn = SafeAsObject((*ConnectionsArray)[PFi]);
				if (!Conn.IsValid()) continue;

				FString FromNodeId, ToNodeId, FromPin, ToPin;
				Conn->TryGetStringField(TEXT("from_node"), FromNodeId);
				if (FromNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("from"), FromNodeId);
				Conn->TryGetStringField(TEXT("to_node"), ToNodeId);
				if (ToNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("to"), ToNodeId);
				Conn->TryGetStringField(TEXT("from_pin"), FromPin);
				Conn->TryGetStringField(TEXT("to_pin"), ToPin);

				if (!FromNodeId.IsEmpty() && ToNodeId.IsEmpty())
				{
					static const TSet<FString> StdConnKeys = {
						TEXT("from"), TEXT("from_node"), TEXT("from_node_id"),
						TEXT("to"),   TEXT("to_node"),   TEXT("to_node_id"),
						TEXT("from_pin"), TEXT("to_pin")
					};
					FString ExtraKey, ExtraVal;
					int32 ExtraCount = 0;
					for (const auto& KVPair : Conn->Values)
					{
						const FString KVKey(*KVPair.Key);
						if (!StdConnKeys.Contains(KVKey.ToLower()) && KVPair.Value->Type == EJson::String)
						{
							KVPair.Value->TryGetString(ExtraVal);
							ExtraKey = KVKey;
							ExtraCount++;
						}
					}
					if (ExtraCount == 1 && !ExtraVal.IsEmpty())
					{
						int32 LastDot;
						if (ExtraVal.FindLastChar(TEXT('.'), LastDot))
						{
							if (FromPin.IsEmpty()) FromPin = ExtraKey;
							ToNodeId = ExtraVal.Left(LastDot);
							ToPin    = ExtraVal.Mid(LastDot + 1);
						}
					}
				}

				FString BaseFrom = GetBaseNodeId(FromNodeId);
				FString BaseTo   = GetBaseNodeId(ToNodeId);

				if (!BaseTo.IsEmpty() && !BaseTo.Contains(TEXT(".")))
				{
					NodesReceivingAnyConnection.Add(BaseTo);
					const bool bExecToPin = ToPin.IsEmpty()
						|| ToPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase)
						|| ToPin.Equals(TEXT("exec"), ESearchCase::IgnoreCase);
					if (bExecToPin)
						NodesReceivingExecWire.Add(BaseTo);
				}

				if (FromPin.IsEmpty() && BaseFrom != FromNodeId)
				{
					int32 DotIdx;
					if (FromNodeId.FindChar(TEXT('.'), DotIdx))
						FromPin = FromNodeId.Mid(DotIdx + 1);
				}
				if (ToPin.IsEmpty() && BaseTo != ToNodeId)
				{
					int32 DotIdx;
					if (ToNodeId.FindChar(TEXT('.'), DotIdx))
						ToPin = ToNodeId.Mid(DotIdx + 1);
				}

				auto ExistsInGraphByGeid = [Graph](const FString& Id) -> bool
				{
					if (!Graph || Id.IsEmpty()) return false;
					if (BlueprintNodeIdentity::FindNodeInGraphByLogicalId(Graph, Id, /*bCaseSensitive*/ false)) return true;
					for (UEdGraphNode* Node : Graph->Nodes)
					{
						if (!Node) continue;
						if (Node->NodeGuid.IsValid() && Node->NodeGuid.ToString(EGuidFormats::Digits).Equals(Id, ESearchCase::IgnoreCase))
							return true;
					}
					return false;
				};

				auto TryAutoRepairNodeId = [&](const FString& UnknownId, const FString& Role) -> bool
				{
					if (UnknownId.Len() < 3) return false;
					if (NodeIdFuzzyRewrites.Contains(UnknownId)) return true;
					TArray<FString> CandidateIds = ValidDeclaredIds.Array();
					if (CandidateIds.Num() == 0) return false;
					TArray<BpFuzzyResolver::FResolvedCandidate> Top =
						BpFuzzyResolver::RankCandidates(UnknownId, CandidateIds, 2);
					if (Top.Num() == 0 || Top[0].Confidence < 0.85f) return false;
					if (Top.Num() > 1 && Top[1].Confidence > Top[0].Confidence - 0.08f) return false;
					NodeIdFuzzyRewrites.Add(UnknownId, Top[0].CandidateKey);
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("connection_node_id_fuzzy_resolved"));
					Repair->SetNumberField(TEXT("connection_index"), (double)PFi);
					Repair->SetStringField(TEXT("role"), Role);
					Repair->SetStringField(TEXT("bad_id"), UnknownId);
					Repair->SetStringField(TEXT("corrected_id"), Top[0].CandidateKey);
					Repair->SetNumberField(TEXT("confidence"), Top[0].Confidence);
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Connection[%d] %s='%s' not in nodes[]; fuzzy-resolved to declared id '%s' (conf %.2f). "
						     "Applied at connection-resolution time without mutating the input JSON."),
						PFi, *Role, *UnknownId, *Top[0].CandidateKey, Top[0].Confidence));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					return true;
				};

				auto RepairRetAlias = [&](FString& NodeId)
				{
					if (!NodeId.Equals(TEXT("ret"), ESearchCase::IgnoreCase)) return;
					NodeIdFuzzyRewrites.FindOrAdd(NodeId) = TEXT("return");
					TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
					R->SetStringField(TEXT("repair"),       TEXT("implicit_node_alias"));
					R->SetNumberField(TEXT("connection_index"), (double)PFi);
					R->SetStringField(TEXT("bad_id"),       NodeId);
					R->SetStringField(TEXT("corrected_id"), TEXT("return"));
					R->SetStringField(TEXT("reason"),       TEXT("'ret' resolved to implicit 'return' node. Use 'return' in future calls."));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(R)));
					NodeId = TEXT("return");
				};
				RepairRetAlias(BaseFrom);
				RepairRetAlias(BaseTo);

				if (!BaseFrom.IsEmpty() && !ValidDeclaredIds.Contains(BaseFrom)
					&& !BaseFrom.Contains(TEXT(".")) && BaseFrom != TEXT("entry") && BaseFrom != TEXT("return")
					&& !ExistsInGraphByGeid(BaseFrom)
					&& !TryAutoRepairNodeId(BaseFrom, TEXT("from_node")))
				{
					TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
					Issue->SetStringField(TEXT("type"), TEXT("unknown_from_node"));
					Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
					Issue->SetStringField(TEXT("unknown_id"), BaseFrom);
					Issue->SetStringField(TEXT("hint"), FString::Printf(
						TEXT("Connection[%d] from_node='%s' has no matching entry in nodes[]. "
						     "This connection will fail. Add this node to nodes[], or fix the id spelling."),
						PFi, *BaseFrom));
					PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
				}

				if (!BaseTo.IsEmpty() && !ValidDeclaredIds.Contains(BaseTo)
					&& !BaseTo.Contains(TEXT(".")) && BaseTo != TEXT("entry") && BaseTo != TEXT("return")
					&& !ExistsInGraphByGeid(BaseTo)
					&& !TryAutoRepairNodeId(BaseTo, TEXT("to_node")))
				{
					TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
					Issue->SetStringField(TEXT("type"), TEXT("unknown_to_node"));
					Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
					Issue->SetStringField(TEXT("unknown_id"), BaseTo);
					Issue->SetStringField(TEXT("hint"), FString::Printf(
						TEXT("Connection[%d] to_node='%s' has no matching entry in nodes[]. "
						     "This connection will fail. Add this node to nodes[], or fix the id spelling."),
						PFi, *BaseTo));
					PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
				}

				if (!FromPin.IsEmpty() && !BaseFrom.IsEmpty())
				{
					const FString* FromHandle = NodeIdToHandle.Find(BaseFrom);
					if (FromHandle && FromHandle->StartsWith(TEXT("var.get.")))
					{
						const FString VarName = FromHandle->Mid(8);
						static const TArray<FString> WrongGenericPins = {
							TEXT("Value"), TEXT("ReturnValue"), TEXT("Output"), TEXT("Array"),
							TEXT("Result"), TEXT("Out"), TEXT("X")
						};
						if (WrongGenericPins.Contains(FromPin) && !FromPin.Equals(VarName, ESearchCase::IgnoreCase))
						{
							const FString WrongPin = FromPin;
							Conn->SetStringField(TEXT("from_pin"), VarName);
							FromPin = VarName;
							TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
							Repair->SetStringField(TEXT("repair"),         TEXT("var_get_pin_auto_corrected"));
							Repair->SetNumberField(TEXT("connection_index"), (double)PFi);
							Repair->SetStringField(TEXT("wrong_pin"),      WrongPin);
							Repair->SetStringField(TEXT("corrected_pin"),  VarName);
							Repair->SetStringField(TEXT("reason"), FString::Printf(
								TEXT("var.get.%s output pin is '%s' — NOT '%s'. Auto-corrected. Use from_pin=\"%s\" in future calls."),
								*VarName, *VarName, *WrongPin, *VarName));
							RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
						}
					}
				}

				if (BaseFrom == TEXT("entry") && FromPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
				{
					Conn->SetStringField(TEXT("from_pin"), TEXT("then"));
					FromPin = TEXT("then");
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("entry_exec_pin_auto_corrected"));
					Repair->SetNumberField(TEXT("connection_index"), (double)PFi);
					Repair->SetStringField(TEXT("reason"),
						TEXT("Entry node's exec OUTPUT pin is named 'then', not 'execute'. Use from_pin=\"then\" "
						     "when wiring 'entry.<X>' into the first impure body node. 'execute' is the exec INPUT "
						     "pin on impure call nodes (the target side of a wire)."));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				}
				if (BaseTo == TEXT("return") && ToPin.Equals(TEXT("then"), ESearchCase::IgnoreCase))
				{
					Conn->SetStringField(TEXT("to_pin"), TEXT("execute"));
					ToPin = TEXT("execute");
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("return_exec_pin_auto_corrected"));
					Repair->SetNumberField(TEXT("connection_index"), (double)PFi);
					Repair->SetStringField(TEXT("reason"),
						TEXT("Return node's exec INPUT pin is named 'execute', not 'then'. Use to_pin=\"execute\" "
						     "when wiring the last impure node's `then` into the return terminator."));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				}

				if (BaseTo == TEXT("return") && !ToPin.IsEmpty() && ToPin != TEXT("execute"))
				{
					if (!ValidReturnPinNames.Contains(ToPin))
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("type"), TEXT("void_function_return_pin"));
						Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
						Issue->SetStringField(TEXT("to_pin"), ToPin);
						const bool bIsVoid = ValidReturnPinNames.Num() == 1;
						if (bIsVoid)
						{
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] wires to 'return.%s' but this function has NO output parameters (it is void). "
								     "Remove this connection — the return node only accepts 'execute'. "
								     "For multi-path bool return: add a class variable (bool BoolResult = false), "
								     "set it true on the success path, then wire var.get.BoolResult → return.ReturnValue. "
								     "Both exec paths still wire their last node → return.execute."),
								PFi, *ToPin));
						}
						else
						{
							FString ValidPinList;
							for (const FString& P : ValidReturnPinNames)
							{
								if (P != TEXT("execute")) { ValidPinList += P + TEXT(", "); }
							}
							ValidPinList.RemoveFromEnd(TEXT(", "));
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] wires to 'return.%s' but that pin does not exist. "
								     "Valid return pin names for this function: [%s]. "
								     "Pin names must match the exact output parameter names declared in add_function."),
								PFi, *ToPin, *ValidPinList));
						}
						PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
					}
				}

				if (BaseFrom == TEXT("entry") && !FromPin.IsEmpty() && FromPin != TEXT("then"))
				{
					if (!ValidEntryPinNames.Contains(FromPin))
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("type"), TEXT("entry_pin_not_found"));
						Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
						Issue->SetStringField(TEXT("from_pin"), FromPin);
						FString ValidPinList;
						for (const FString& P : ValidEntryPinNames)
						{
							if (P != TEXT("then")) { ValidPinList += P + TEXT(", "); }
						}
						ValidPinList.RemoveFromEnd(TEXT(", "));
						const bool bNoUserInputs = ValidEntryPinNames.Num() == 1;
						if (bNoUserInputs)
						{
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] wires from 'entry.%s' but this function has NO input parameters. "
								     "Declare the input first via blueprint(action='add_function', function_name='%s', "
								     "params=[{name:'%s', type:'...'}, ...]) — or, if the function already exists, call "
								     "blueprint(action='add_function_param', function_name='%s', param_name='%s', param_type='...'). "
								     "Then re-run build_blueprint_graph. Connection will fail until the entry pin exists."),
								PFi, *FromPin, *GraphName, *FromPin, *GraphName, *FromPin));
						}
						else
						{
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] wires from 'entry.%s' but that pin does not exist on this function's entry. "
								     "Valid entry pin names: [%s]. Pin names must match the exact param names declared in add_function. "
								     "Either fix the from_pin spelling, or add the missing param via blueprint(action='add_function_param', "
								     "function_name='%s', param_name='%s', param_type='...')."),
								PFi, *FromPin, *ValidPinList, *GraphName, *FromPin));
						}
						PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
					}
				}

				if (FromPin == TEXT("Is Valid") || FromPin == TEXT("Is Not Valid")
					|| FromPin == TEXT("IsValid") || FromPin == TEXT("IsNotValid"))
				{
					static const TArray<FString> KnownExecInputPins = {
						TEXT("execute"), TEXT("exec"), TEXT("then"), TEXT("LoopBody")
					};
					const bool bTargetIsExecPin = KnownExecInputPins.Contains(ToPin)
						|| ToPin.IsEmpty();
					if (!bTargetIsExecPin)
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("type"), TEXT("exec_pin_to_data_input"));
						Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
						Issue->SetStringField(TEXT("from_pin"), FromPin);
						Issue->SetStringField(TEXT("to_pin"), ToPin);
						Issue->SetStringField(TEXT("hint"), FString::Printf(
							TEXT("Connection[%d] '%s' is an EXEC output of k2.IsValid — it routes execution flow, "
							     "NOT bool data. Wire it ONLY to exec input pins ('execute') on the NEXT impure node. "
							     "CORRECT PATTERN: isValid.Is Valid → castPawn.execute (exec; no Branch needed). "
							     "isValid.Is Not Valid → failNode.execute. "
							     "WRONG: isValid.IsValid → branch.Condition. "
							     "Note: the pin name has a SPACE: 'Is Valid' not 'IsValid'. "
							     "Wiring from entry param to both isValid AND cast: "
							     "{from:entry.TargetActor,to:isValid.InputObject} AND {from:entry.TargetActor,to:castPawn.Object}."),
							PFi, *FromPin));
						PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						ConnectionsWithPreFlightIssue.Add(PFi);
					}
				}

				{
					static const TArray<FString> KnownExecPins = {
						TEXT("then"), TEXT("execute"), TEXT("exec"),
						TEXT("LoopBody"), TEXT("Completed"),
						TEXT("Is Valid"), TEXT("Is Not Valid"),
						TEXT("CastFailed"), TEXT("Else"),
						TEXT("True"), TEXT("False"),
						TEXT("then_0"), TEXT("then_1"), TEXT("then_2"), TEXT("then_3"),
						TEXT("then_4"), TEXT("then_5"), TEXT("then_6"), TEXT("then_7"),
					};

					auto IsKnownPureHandle = [](const FString& H) -> bool {
						if (BpHandleKnowledge::IsKnownImpure(H)) return false;
						if (BpHandleKnowledge::IsKnownPure(H))   return true;
						return H.StartsWith(TEXT("fn.KismetMathLibrary."), ESearchCase::IgnoreCase)
							|| H.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase)
							|| H.Equals(TEXT("fn.KismetSystemLibrary.IsValid"), ESearchCase::IgnoreCase)
							|| H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralBool"), ESearchCase::IgnoreCase)
							|| H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralInt"), ESearchCase::IgnoreCase)
							|| H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralFloat"), ESearchCase::IgnoreCase)
							|| H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralString"), ESearchCase::IgnoreCase)
							|| H.StartsWith(TEXT("fn.AnimInstance.TryGetPawnOwner"), ESearchCase::IgnoreCase);
					};

					if (KnownExecPins.Contains(FromPin) && !BaseTo.IsEmpty())
					{
						FString* ToHandlePtr = NodeIdToHandle.Find(BaseTo);
						if (ToHandlePtr && IsKnownPureHandle(*ToHandlePtr))
						{
							TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
							Issue->SetStringField(TEXT("type"), TEXT("exec_to_pure_node"));
							Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
							Issue->SetStringField(TEXT("from_pin"), FromPin);
							Issue->SetStringField(TEXT("to_node"), BaseTo);
							Issue->SetStringField(TEXT("to_handle"), *ToHandlePtr);
							const TCHAR* FixLead = bClearFirst
								? TEXT("FIX — fix this wire in the rebuild. Route exec to the next IMPURE node ")
								: TEXT("FIX — do NOT clear the graph. Use connect_pins to route exec to the next IMPURE node ");
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] wires exec pin '%s' into pure node '%s' (handle '%s'). "
								     "Pure nodes (fn.KismetMathLibrary.*, var.get.*, Break*, Make*, SelectFloat) have NO exec pins. "
								     "%sdownstream (e.g. the var.set.* that consumes this pure node's ReturnValue). "
								     "Pattern: cast.then → setAccum.execute (impure), getAccum.X → add.A (data), add.ReturnValue → setAccum.X (data)."),
								PFi, *FromPin, *BaseTo, **ToHandlePtr, FixLead));
							PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						}
					}

					if (KnownExecPins.Contains(FromPin) && !BaseFrom.IsEmpty()
						&& BaseFrom != TEXT("entry") && BaseFrom != TEXT("return"))
					{
						FString* FromHandlePtr = NodeIdToHandle.Find(BaseFrom);
						if (FromHandlePtr && IsKnownPureHandle(*FromHandlePtr))
						{
							TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
							Issue->SetStringField(TEXT("type"), TEXT("exec_from_pure_node"));
							Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
							Issue->SetStringField(TEXT("from_node"), BaseFrom);
							Issue->SetStringField(TEXT("from_pin"), FromPin);
							Issue->SetStringField(TEXT("from_handle"), *FromHandlePtr);
							const TCHAR* FixLead = bClearFirst
								? TEXT("FIX — fix this wire in the rebuild. Wire exec between impure nodes only, skipping this pure node. ")
								: TEXT("FIX — do NOT clear the graph. Wire exec between impure nodes only, skipping this pure node. ");
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] expects exec pin '%s' as OUTPUT of pure node '%s' (handle '%s'). "
								     "Pure nodes have NO exec outputs (no 'then', 'True', 'False', etc.). "
								     "%s"
								     "Use the pure node's ReturnValue as a DATA input to the next impure node (var.set.*). "
								     "Correct: impureA.then → impureB.execute (exec); pureNode.ReturnValue → impureB.InputPin (data)."),
								PFi, *FromPin, *BaseFrom, **FromHandlePtr, FixLead));
							PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						}
					}
				}

				if (FromPin.StartsWith(TEXT("As "), ESearchCase::CaseSensitive))
				{
					const FString ToPinLower = ToPin.ToLower();
					if (ToPinLower == TEXT("condition") || ToPinLower == TEXT("index"))
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("type"), TEXT("cast_data_to_bool_pin"));
						Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
						Issue->SetStringField(TEXT("from_pin"), FromPin);
						Issue->SetStringField(TEXT("to_pin"), ToPin);
						Issue->SetStringField(TEXT("hint"), FString::Printf(
							TEXT("Connection[%d] wires cast output '%s' (an OBJECT REFERENCE data pin) to '%s'. "
							     "cast.As X is a typed object ref — NOT a bool or int. "
							     "Use cast.then (exec, cast succeeded) / cast.CastFailed (exec, cast failed) for branching. "
							     "Wire cast.As X only to object/Target input pins on function calls or var.set nodes."),
							PFi, *FromPin, *ToPin));
						PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
					}
				}

				if (!FromPin.IsEmpty() && !ToPin.IsEmpty())
				{
					static const TArray<FString> ExecOnlyOutputPins = {
						TEXT("LoopBody"), TEXT("Completed"),
						TEXT("True"), TEXT("False"),
						TEXT("CastFailed"), TEXT("Else"),
						TEXT("then_0"), TEXT("then_1"), TEXT("then_2"), TEXT("then_3"),
						TEXT("then_4"), TEXT("then_5"), TEXT("then_6"), TEXT("then_7"),
					};
					static const TArray<FString> ExecInputPinNames = {
						TEXT("execute"), TEXT("exec"), TEXT("then"),
					};
					if (ExecOnlyOutputPins.Contains(FromPin) && !ExecInputPinNames.Contains(ToPin))
					{
						if (!ExecOnlyOutputPins.Contains(ToPin))
						{
							TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
							Issue->SetStringField(TEXT("type"), TEXT("exec_output_to_data_input"));
							Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
							Issue->SetStringField(TEXT("from_node"), BaseFrom);
							Issue->SetStringField(TEXT("from_pin"), FromPin);
							Issue->SetStringField(TEXT("to_node"), BaseTo);
							Issue->SetStringField(TEXT("to_pin"), ToPin);
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] wires exec OUTPUT pin '%s' (from '%s') to data INPUT pin '%s' (on '%s'). "
								     "Exec outputs can ONLY connect to exec input pins ('execute'). "
								     "Data outputs (ReturnValue, ArrayElement, object refs, etc.) connect to data inputs. "
								     "ForEachLoop example: forEach.LoopBody → castPawn.execute (exec chain), "
								     "forEach.ArrayElement → castPawn.Object (data chain). "
								     "These are SEPARATE connections — exec and data must not be mixed."),
								PFi, *FromPin, *BaseFrom, *ToPin, *BaseTo));
							PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						}
					}
				}

				if (!BaseTo.IsEmpty() && ForEachLoopNodeIds.Contains(BaseTo)
					&& ToPin.Equals(TEXT("Completed"), ESearchCase::CaseSensitive))
				{
					TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
					Issue->SetStringField(TEXT("type"), TEXT("foreach_completed_as_exec_destination"));
					Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
					Issue->SetStringField(TEXT("to_node"), BaseTo);
					Issue->SetStringField(TEXT("hint"), FString::Printf(
						TEXT("Connection[%d] wires exec INTO '%s.Completed' — but 'Completed' is an exec OUTPUT of ForEachLoop. "
						     "The engine silently ignores this connection and loop execution breaks. "
						     "Correct pattern: wire post-loop logic FROM '%s.Completed' (not to it). "
						     "If you meant to wire exec into the loop, target '%s.execute' (the loop's exec input)."),
						PFi, *BaseTo, *BaseTo, *BaseTo));
					PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
				}

				if (!BaseTo.IsEmpty() && ForEachLoopNodeIds.Contains(BaseTo))
				{
					const bool bIsExecTarget = ToPin.IsEmpty()
						|| ToPin.Equals(TEXT("execute"), ESearchCase::IgnoreCase)
						|| ToPin.Equals(TEXT("exec"), ESearchCase::IgnoreCase);
					if (bIsExecTarget)
						ForEachLoopExecWired.Add(BaseTo);
				}

				if (!BaseTo.IsEmpty() && !ToPin.IsEmpty() && !ConnectionsWithPreFlightIssue.Contains(PFi))
				{
					if (FString* ToHandlePtr = NodeIdToHandle.Find(BaseTo))
					{
						FString ForbiddenReason;
						if (BpHandleKnowledge::IsForbiddenInputPin(*ToHandlePtr, ToPin, ForbiddenReason))
						{
							TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
							Issue->SetStringField(TEXT("type"), TEXT("forbidden_input_pin"));
							Issue->SetNumberField(TEXT("connection_index"), (double)PFi);
							Issue->SetStringField(TEXT("to_node"), BaseTo);
							Issue->SetStringField(TEXT("to_handle"), *ToHandlePtr);
							Issue->SetStringField(TEXT("to_pin"), ToPin);
							Issue->SetStringField(TEXT("hint"), FString::Printf(
								TEXT("Connection[%d] targets '%s.%s' — this pin is not a valid wire TARGET. %s"),
								PFi, *BaseTo, *ToPin, *ForbiddenReason));
							PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
							ConnectionsWithPreFlightIssue.Add(PFi);
						}
					}
				}

				static const TArray<FString> ExecOutputPins = {
					TEXT("then"), TEXT("execute"),
					TEXT("True"), TEXT("False"),
					TEXT("Is Valid"), TEXT("Is Not Valid"),
					TEXT("CastFailed"),
					TEXT("LoopBody"), TEXT("Completed"),
					TEXT("then_0"), TEXT("then_1"), TEXT("then_2"), TEXT("then_3"),
					TEXT("then_4"), TEXT("then_5"), TEXT("then_6"), TEXT("then_7"),
				};
				bool bSourceIsPureHandle = false;
				auto IsPureHandleStr = [](const FString& H) -> bool
				{
					if (BpHandleKnowledge::IsKnownImpure(H)) return false;
					if (BpHandleKnowledge::IsKnownPure(H))   return true;
					return H.StartsWith(TEXT("fn.KismetMathLibrary."), ESearchCase::IgnoreCase) ||
					       H.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase) ||
					       H.StartsWith(TEXT("k2.Self"), ESearchCase::IgnoreCase) ||
					       H.StartsWith(TEXT("k2.self"), ESearchCase::IgnoreCase) ||
					       H.Equals(TEXT("fn.KismetSystemLibrary.IsValid"), ESearchCase::IgnoreCase) ||
					       H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralBool"), ESearchCase::IgnoreCase) ||
					       H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralInt"), ESearchCase::IgnoreCase) ||
					       H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralFloat"), ESearchCase::IgnoreCase) ||
					       H.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralString"), ESearchCase::IgnoreCase);
				};
				if (!BaseFrom.IsEmpty())
				{
					if (FString* FromHandlePtrFO = NodeIdToHandle.Find(BaseFrom))
					{
						bSourceIsPureHandle = IsPureHandleStr(*FromHandlePtrFO);
					}
					if (!bSourceIsPureHandle)
					{
						bSourceIsPureHandle = IsPureHandleStr(BaseFrom);
					}
				}
				bool bTargetIsPureHandle = false;
				if (!BaseTo.IsEmpty())
				{
					if (FString* ToHandlePtrFO = NodeIdToHandle.Find(BaseTo))
					{
						const FString& HTO = *ToHandlePtrFO;
						bTargetIsPureHandle =
							BpHandleKnowledge::IsKnownPure(HTO) ||
							(!BpHandleKnowledge::IsKnownImpure(HTO) && (
								HTO.StartsWith(TEXT("fn.KismetMathLibrary."), ESearchCase::IgnoreCase) ||
								HTO.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase) ||
								HTO.Equals(TEXT("fn.KismetSystemLibrary.IsValid"), ESearchCase::IgnoreCase) ||
								HTO.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralBool"), ESearchCase::IgnoreCase) ||
								HTO.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralInt"), ESearchCase::IgnoreCase) ||
								HTO.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralFloat"), ESearchCase::IgnoreCase) ||
								HTO.Equals(TEXT("fn.KismetSystemLibrary.MakeLiteralString"), ESearchCase::IgnoreCase)
							));
					}
				}
				if (!bSourceIsPureHandle && !bTargetIsPureHandle
					&& !BaseFrom.IsEmpty() && BaseFrom != TEXT("entry") && BaseFrom != TEXT("return")
					&& !BaseFrom.StartsWith(TEXT("entry."), ESearchCase::IgnoreCase)
					&& !BaseFrom.StartsWith(TEXT("return."), ESearchCase::IgnoreCase))
				{
					const FString EffectiveFromPin = FromPin.IsEmpty() ? TEXT("then") : FromPin;
					if (ExecOutputPins.Contains(EffectiveFromPin))
					{
						const FString ExecKey = BaseFrom + TEXT(".") + EffectiveFromPin;
						if (SeenExecOutputs.Contains(ExecKey))
						{
							PreFlightDropConnectionIndices.Add(PFi);

							TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
							Repair->SetStringField(TEXT("repair"), TEXT("exec_fanout_second_wire_dropped"));
							Repair->SetNumberField(TEXT("connection_index"), (double)PFi);
							Repair->SetStringField(TEXT("from_node"), BaseFrom);
							Repair->SetStringField(TEXT("from_pin"), EffectiveFromPin);
							const bool bIsLoopBodyFanout = EffectiveFromPin.Equals(TEXT("LoopBody"), ESearchCase::IgnoreCase);
							if (bIsLoopBodyFanout)
							{
								Repair->SetStringField(TEXT("reason"), FString::Printf(
									TEXT("Connection[%d] was the SECOND exec wire from 'forEach.LoopBody' — DROPPED. "
									     "LoopBody fires into exactly ONE exec destination. "
									     "Chain exec through that destination's branches: "
									     "castPawn.then → nextImpure.execute AND castPawn.CastFailed → nextImpure.execute. "
									     "Add the missing destination via connect_pins."),
									PFi));
							}
							else
							{
								Repair->SetStringField(TEXT("reason"), FString::Printf(
									TEXT("Connection[%d] was the SECOND exec wire from '%s.%s' — DROPPED to preserve the first wire. "
									     "Exec outputs connect to ONE destination; a second wire would replace the first silently. "
									     "To route to BOTH destinations add k2.Sequence: "
									     "wire '%s.%s' → seq.execute, then seq.then_0 → first dest, seq.then_1 → second dest."),
									PFi, *BaseFrom, *EffectiveFromPin, *BaseFrom, *EffectiveFromPin));
							}
							RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
						}
						else
						{
							SeenExecOutputs.Add(ExecKey);
						}
					}
				}

				if (FromPin.StartsWith(TEXT("then_"), ESearchCase::CaseSensitive) && !BaseFrom.IsEmpty())
				{
					int32* SeqOutputs = SequenceNodeOutputs.Find(BaseFrom);
					if (SeqOutputs)
					{
						const FString NumStr = FromPin.RightChop(5);
						const int32 RequestedIdx = FCString::Atoi(*NumStr);
						if (RequestedIdx >= *SeqOutputs)
						{
							const int32 NewCount = RequestedIdx + 1;
							for (const auto& NVal : *NodesArray)
							{
								TSharedPtr<FJsonObject> NObj = SafeAsObject(NVal);
								if (!NObj.IsValid()) continue;
								FString NId;
								NObj->TryGetStringField(TEXT("id"), NId);
								if (NId == BaseFrom)
								{
									NObj->SetNumberField(TEXT("num_outputs"), NewCount);
									break;
								}
							}
							TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
							Repair->SetStringField(TEXT("repair"), TEXT("sequence_auto_grown"));
							Repair->SetStringField(TEXT("node_id"), BaseFrom);
							Repair->SetNumberField(TEXT("old_num_outputs"), (double)*SeqOutputs);
							Repair->SetNumberField(TEXT("new_num_outputs"), (double)NewCount);
							Repair->SetStringField(TEXT("reason"), FString::Printf(
								TEXT("Sequence '%s' was declared with num_outputs=%d but connection references 'then_%d'. "
								     "Auto-grown to num_outputs=%d. In future calls, set num_outputs explicitly on the Sequence node."),
								*BaseFrom, *SeqOutputs, RequestedIdx, NewCount));
							RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
							*SeqOutputs = NewCount;
						}
					}
				}
			}

			for (const FString& FeNodeId : ForEachLoopNodeIds)
			{
				if (!ForEachLoopExecWired.Contains(FeNodeId))
				{
					TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
					Issue->SetStringField(TEXT("type"), TEXT("foreach_no_exec_entry"));
					Issue->SetStringField(TEXT("node_id"), FeNodeId);
					Issue->SetStringField(TEXT("hint"), FString::Printf(
						TEXT("ForEachLoop node '%s' has no exec wire into its 'execute' input — the loop will never run. "
						     "Add a connection from the preceding node's exec output (e.g. 'prevNode.then') "
						     "to '%s.execute'. This connection is separate from the Array data input."),
						*FeNodeId, *FeNodeId));
					PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
				}
			}

			{
				auto IsPureSelfCall = [&Blueprint](const FString& Handle) -> bool
				{
					if (!Handle.StartsWith(TEXT("fn.Self."), ESearchCase::IgnoreCase)) return false;
					const FString FuncName = Handle.Mid(8);
					const FName FuncFName(*FuncName);
					UClass* Classes[] = { Blueprint->SkeletonGeneratedClass, Blueprint->GeneratedClass };
					for (UClass* CheckClass : Classes)
					{
						if (!CheckClass) continue;
						UFunction* Func = CheckClass->FindFunctionByName(FuncFName);
						if (Func && Func->HasAnyFunctionFlags(FUNC_BlueprintPure))
							return true;
					}
					return false;
				};

				auto IsImpureNodeHandle = [&IsPureSelfCall](const FString& H) -> bool {
					return H.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase)
						|| H.Equals(TEXT("k2.Branch"), ESearchCase::IgnoreCase)
						|| (H.StartsWith(TEXT("fn.Self."), ESearchCase::IgnoreCase) && !IsPureSelfCall(H));
				};

				for (const auto& HandleEntry : NodeIdToHandle)
				{
					const FString& NodeId  = HandleEntry.Key;
					const FString& Handle  = HandleEntry.Value;
					if (NodeId == TEXT("entry") || NodeId == TEXT("return")) continue;
					if (!IsImpureNodeHandle(Handle)) continue;
					if (!NodesReceivingAnyConnection.Contains(NodeId)) continue;
					if (NodesReceivingExecWire.Contains(NodeId)) continue;

					TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
					Issue->SetStringField(TEXT("type"), TEXT("impure_node_no_exec_wire"));
					Issue->SetStringField(TEXT("node_id"), NodeId);
					Issue->SetStringField(TEXT("node_handle"), Handle);
					Issue->SetStringField(TEXT("hint"), FString::Printf(
						TEXT("Node '%s' (handle '%s') has incoming data connections but NO exec wire into its "
						     "'execute' input — it will never run. "
						     "FUNCTION EXEC GAP: impure nodes (var.set, Branch, user function calls) always need BOTH "
						     "exec and data wires. Add: {\"from\":\"<prevNode>\",\"from_pin\":\"then\","
						     "\"to\":\"%s\",\"to_pin\":\"execute\"}. "
						     "For var.set that stores a function return: wire fn.then → set.execute AND fn.ReturnValue → set.InputPin. "
						     "For k2.Branch: wire the preceding impure node's .then → branch.execute."),
						*NodeId, *Handle, *NodeId));
					PreFlightIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
				}
			}
		}
	}

	bool bNeedsCompile = false;
	for (const auto& NodeVal : *NodesArray)
	{
		TSharedPtr<FJsonObject> NodeObj = SafeAsObject(NodeVal);
		if (!NodeObj.IsValid()) continue;
		FString Handle;
		NodeObj->TryGetStringField(TEXT("handle"), Handle);
		if (Handle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), Handle);
		if (Handle.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase) ||
			Handle.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase))
		{
			bNeedsCompile = true;
			break;
		}
	}
	if (bNeedsCompile)
	{
		for (int32 i = Blueprint->ImplementedInterfaces.Num() - 1; i >= 0; --i)
		{
			if (!Blueprint->ImplementedInterfaces[i].Interface ||
				!Blueprint->ImplementedInterfaces[i].Interface->HasAnyClassFlags(CLASS_Interface))
			{
				Blueprint->ImplementedInterfaces.RemoveAt(i);
			}
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);

		Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("Graph '%s' not found after pre-compile step"), *GraphName);
			return;
		}
	}

	TMap<FString, UBlueprintNodeSpawner*> SpawnerCache;
	GraphEditHelpers::BuildSpawnerCache(Blueprint, SpawnerCache);

	TMap<FString, UClass*> SelfWireContextByNodeId;
	{
		const TArray<TSharedPtr<FJsonValue>>* SelfWireConns = nullptr;
		if (Args->TryGetArrayField(TEXT("connections"), SelfWireConns))
			BuildSelfWireContextMap(SelfWireConns, Blueprint, Graph, SelfWireContextByNodeId, NodesArray);
	}

	if (bEnablePreFlight && NodesArray && BpFuzzyResolver::GetPreFlightMode() == BpFuzzyResolver::EPreFlightMode::Aggressive)
	{
		const float MinConfidence = BpFuzzyResolver::GetMinConfidence();

		TArray<FString> CacheKeys;
		SpawnerCache.GenerateKeyArray(CacheKeys);

		for (int32 i = 0; i < NodesArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[i]);
			if (!NodeObj.IsValid()) continue;

			FString NodeId;
			NodeObj->TryGetStringField(TEXT("id"), NodeId);
			if (PreFlightSkipNodeIds.Contains(NodeId) || PreFlightDropNodeIds.Contains(NodeId))
			{
				continue;
			}

			FString Handle;
			NodeObj->TryGetStringField(TEXT("handle"), Handle);
			if (Handle.IsEmpty()) continue;

			if (Handle.StartsWith(TEXT("k2.K2Node_"), ESearchCase::IgnoreCase))
			{
				PreFlightDropNodeIds.Add(NodeId);
				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("k2node_classname_handle_dropped"));
				Repair->SetStringField(TEXT("node_id"), NodeId);
				Repair->SetStringField(TEXT("bad_handle"), Handle);
				FString Hint = TEXT("`k2.K2Node_*` is a class name, not a callable handle. Use the per-instance form discoverable via discover_nodes — e.g. `k2.Debug Key E` (one spawner per key) for input debug events, `k2.Cast To <ClassName>` for casts, `k2.Branch` / `k2.Sequence` for flow control.");
				if (Handle.Contains(TEXT("InputDebugKey"), ESearchCase::IgnoreCase))
				{
					Hint = TEXT("Use `k2.Debug Key <Letter>` instead (e.g. `k2.Debug Key E`). There is one spawner per key — the bare class name has no key bound.");
				}
				else if (Handle.Contains(TEXT("DynamicCast"), ESearchCase::IgnoreCase))
				{
					Hint = TEXT("Use `k2.Cast To <ClassName>` instead (e.g. `k2.Cast To Character`). The bare class name has no target class bound.");
				}
				Repair->SetStringField(TEXT("reason"), Hint);
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				continue;
			}

			const FString HandleLower = Handle.ToLower();
			if (HandleLower.StartsWith(TEXT("var.")) ||
			    HandleLower.StartsWith(TEXT("ev.")) ||
			    HandleLower.StartsWith(TEXT("k2.cast")) ||
			    HandleLower.StartsWith(TEXT("k2.self")) ||
			    HandleLower.StartsWith(TEXT("k2.break ")) ||
			    HandleLower.StartsWith(TEXT("k2.make ")) ||
			    HandleLower.StartsWith(TEXT("k2.spawn")) ||
			    HandleLower.StartsWith(TEXT("k2.select")) ||
			    HandleLower.StartsWith(TEXT("k2.makearray")) ||
			    HandleLower.StartsWith(TEXT("k2.formattext")) ||
			    HandleLower.StartsWith(TEXT("k2.format text")) ||
			    HandleLower.StartsWith(TEXT("k2.input")) ||
			    HandleLower.StartsWith(TEXT("k2.get ")) ||
			    HandleLower.StartsWith(TEXT("k2.getsubsystem")) ||
			    HandleLower.StartsWith(TEXT("k2.get subsystem")) ||
			    HandleLower == TEXT("k2.branch") ||
			    HandleLower == TEXT("k2.sequence") ||
			    HandleLower == TEXT("k2.ifthenelse") ||
			    HandleLower == TEXT("k2.if") ||
			    HandleLower.StartsWith(TEXT("fn.self.")) ||
			    HandleLower == TEXT("entry") ||
			    HandleLower == TEXT("return"))
			{
				continue;
			}

			if (SpawnerCache.Contains(HandleLower))
			{
				continue;
			}

			if (Handle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase))
			{
				int32 SecondDot = INDEX_NONE;
				const int32 PrefixLen = 3;
				const FString AfterPrefix = Handle.Mid(PrefixLen);
				if (AfterPrefix.FindChar(TEXT('.'), SecondDot) && SecondDot > 0)
				{
					const FString ClassName = AfterPrefix.Left(SecondDot);
					const FString FuncName = AfterPrefix.Mid(SecondDot + 1);
					if (!ClassName.IsEmpty() && !FuncName.IsEmpty())
					{
						bool bFoundReflective = false;
						const TArray<FString> SearchNames = {
							ClassName, ClassName + TEXT("_C"),
							TEXT("U") + ClassName, TEXT("A") + ClassName
						};
						for (const FString& SearchName : SearchNames)
						{
							UClass* FoundClass = FindFirstObject<UClass>(*SearchName);
							if (FoundClass && GraphEditHelpers::IsTransientBpClass(FoundClass))
							{
								FoundClass = nullptr;
								for (TObjectIterator<UClass> It; It; ++It)
								{
									if (It->GetName() == SearchName && !GraphEditHelpers::IsTransientBpClass(*It))
									{ FoundClass = *It; break; }
								}
							}
							if (FoundClass && FoundClass->FindFunctionByName(FName(*FuncName)) != nullptr)
							{
								bFoundReflective = true;
								break;
							}
						}
						if (bFoundReflective) continue;
					}
				}
			}

			if (Handle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase))
			{
				int32 Dot2 = INDEX_NONE;
				const FString AfterFn = Handle.Mid(3);
				if (AfterFn.FindChar(TEXT('.'), Dot2) && Dot2 > 0)
				{
					const FString WrongLib = AfterFn.Left(Dot2);
					const FString FuncName = AfterFn.Mid(Dot2 + 1);
					const FString CanonLib = BpHandleKnowledge::CanonicalLibraryFor(FuncName);
					if (!CanonLib.IsEmpty() && !CanonLib.Equals(WrongLib, ESearchCase::IgnoreCase))
					{
						const FString CandidateHandle = FString::Printf(TEXT("fn.%s.%s"), *CanonLib, *FuncName);
						if (SpawnerCache.Contains(CandidateHandle.ToLower()))
						{
							NodeObj->SetStringField(TEXT("handle"), CandidateHandle);
							TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
							Repair->SetStringField(TEXT("repair"), TEXT("handle_library_canonicalized"));
							Repair->SetStringField(TEXT("node_id"), NodeId);
							Repair->SetStringField(TEXT("bad_handle"), Handle);
							Repair->SetStringField(TEXT("corrected_handle"), CandidateHandle);
							Repair->SetStringField(TEXT("reason"), FString::Printf(
								TEXT("Function '%s' canonically lives in '%s', not '%s'. Auto-rewrote handle to '%s'."),
								*FuncName, *CanonLib, *WrongLib, *CandidateHandle));
							RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
							continue;
						}
					}

					if (!WrongLib.IsEmpty() && !FuncName.IsEmpty())
					{
						const FString FuncSuffixLower = FString::Printf(TEXT(".%s"), *FuncName).ToLower();
						const FString WrongLibLower = WrongLib.ToLower();
						TArray<FString> Matches;
						for (const TPair<FString, UBlueprintNodeSpawner*>& Entry : SpawnerCache)
						{
							const FString& Key = Entry.Key;
							if (!Key.StartsWith(TEXT("fn."))) continue;
							if (!Key.EndsWith(FuncSuffixLower)) continue;
							Matches.Add(Key);
						}
						if (Matches.Num() > 0)
						{
							FString BestKey;
							int32 BestDist = INT32_MAX;
							for (const FString& Key : Matches)
							{
								int32 KeyDot2 = INDEX_NONE;
								const FString KeyAfterFn = Key.Mid(3);
								if (!KeyAfterFn.FindChar(TEXT('.'), KeyDot2) || KeyDot2 <= 0) continue;
								const FString KeyLib = KeyAfterFn.Left(KeyDot2);
								const int32 MinLen = FMath::Min(KeyLib.Len(), WrongLibLower.Len());
								int32 Mismatch = FMath::Abs(KeyLib.Len() - WrongLibLower.Len());
								for (int32 c = 0; c < MinLen; ++c)
								{
									if (KeyLib[c] != WrongLibLower[c]) Mismatch++;
								}
								if (Mismatch < BestDist) { BestDist = Mismatch; BestKey = Key; }
							}
							const int32 MaxAllowedDist = FMath::Max(2, WrongLib.Len() / 3);
							if (!BestKey.IsEmpty() && BestDist <= MaxAllowedDist)
							{
								FString CandidateHandle;
								if (UBlueprintNodeSpawner* MatchedSpawner = SpawnerCache.FindRef(BestKey))
								{
									CandidateHandle = GraphEditHelpers::BuildHandle(MatchedSpawner);
								}
								if (CandidateHandle.IsEmpty())
								{
									int32 BKDot2 = INDEX_NONE;
									const FString BKAfterFn = BestKey.Mid(3);
									BKAfterFn.FindChar(TEXT('.'), BKDot2);
									const FString ResolvedLib = BKAfterFn.Left(BKDot2);
									const FString ResolvedFn = BKAfterFn.Mid(BKDot2 + 1);
									CandidateHandle = FString::Printf(TEXT("fn.%s.%s"), *ResolvedLib, *ResolvedFn);
								}
								int32 ResolvedDot = INDEX_NONE;
								const FString CandAfter = CandidateHandle.Mid(3);
								CandAfter.FindChar(TEXT('.'), ResolvedDot);
								const FString ResolvedLibForMsg = ResolvedDot > 0 ? CandAfter.Left(ResolvedDot) : FString();
								NodeObj->SetStringField(TEXT("handle"), CandidateHandle);
								TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
								Repair->SetStringField(TEXT("repair"), TEXT("handle_library_typo_corrected"));
								Repair->SetStringField(TEXT("node_id"), NodeId);
								Repair->SetStringField(TEXT("bad_handle"), Handle);
								Repair->SetStringField(TEXT("corrected_handle"), CandidateHandle);
								Repair->SetStringField(TEXT("reason"), FString::Printf(
									TEXT("Library '%s' does not exist; function '%s' lives in '%s' (edit distance %d). Auto-rewrote handle."),
									*WrongLib, *FuncName, *ResolvedLibForMsg, BestDist));
								RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
								continue;
							}
						}
					}
				}
			}

			TArray<BpFuzzyResolver::FResolvedCandidate> Top =
				BpFuzzyResolver::RankCandidates(Handle, CacheKeys, 3);

			const FString OrigHandleLower = Handle.ToLower();
			auto HasToken = [](const FString& Needle, const FString& Hay)
			{
				return Hay.Contains(Needle, ESearchCase::IgnoreCase);
			};
			const bool bOriginalIsForEach = HasToken(TEXT("foreach"), OrigHandleLower) || HasToken(TEXT("for each"), OrigHandleLower);
			const bool bOriginalIsForLoop = !bOriginalIsForEach && (HasToken(TEXT("forloop"), OrigHandleLower) || HasToken(TEXT("for loop"), OrigHandleLower) || OrigHandleLower == TEXT("loop"));
			const bool bOriginalMentionsEditor = HasToken(TEXT("editor"), OrigHandleLower);

			auto ExtractClassPart = [](const FString& Handle) -> FString
			{
				if (!Handle.StartsWith(TEXT("fn."))) return FString();
				const FString Stripped = Handle.RightChop(3);
				int32 Dot = INDEX_NONE;
				if (!Stripped.FindChar(TEXT('.'), Dot)) return FString();
				return Stripped.Left(Dot);
			};
			const FString OrigClass = ExtractClassPart(Handle);
			const bool bOrigEndsInC = OrigClass.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive);
			const bool bOrigIsUserAsset = !OrigClass.IsEmpty() && (
				bOrigEndsInC ||
				OrigClass.StartsWith(TEXT("BI_")) || OrigClass.StartsWith(TEXT("BP_")) ||
				OrigClass.StartsWith(TEXT("BPC_")) || OrigClass.StartsWith(TEXT("WBP_")) ||
				OrigClass.StartsWith(TEXT("S_")) || OrigClass.StartsWith(TEXT("E_")) ||
				OrigClass.StartsWith(TEXT("DA_")) || OrigClass.StartsWith(TEXT("DT_")) ||
				OrigClass.StartsWith(TEXT("NS_")) || OrigClass.StartsWith(TEXT("M_")) ||
				OrigClass.StartsWith(TEXT("MI_")) || OrigClass.StartsWith(TEXT("T_")) ||
				OrigClass.StartsWith(TEXT("AB_")) || OrigClass.StartsWith(TEXT("ABP_")) ||
				OrigClass.StartsWith(TEXT("AC_")) || OrigClass.StartsWith(TEXT("WC_")) ||
				OrigClass.StartsWith(TEXT("GA_")) || OrigClass.StartsWith(TEXT("GE_")) ||
				OrigClass.StartsWith(TEXT("GC_")));

			while (Top.Num() > 0)
			{
				const FString& K = Top[0].CandidateKey;
				const bool bSkelClass = K.Contains(TEXT("fn.SKEL_"), ESearchCase::IgnoreCase);
				const bool bReinstClass = K.Contains(TEXT("fn.REINST_"), ESearchCase::IgnoreCase);

				const FString KLower = K.ToLower();
				const bool bCandidateIsForEach = HasToken(TEXT("foreach"), KLower) || HasToken(TEXT("for each"), KLower);
				const bool bCandidateIsForLoop = !bCandidateIsForEach && (HasToken(TEXT("forloop"), KLower) || HasToken(TEXT("for loop"), KLower));
				const bool bLoopMismatch =
					(bOriginalIsForEach && bCandidateIsForLoop) ||
					(bOriginalIsForLoop && bCandidateIsForEach);

				const bool bCandidateIsEditor =
					HasToken(TEXT("fn.editorlevellibrary"), KLower) ||
					HasToken(TEXT("fn.editoractorsubsystem"), KLower) ||
					HasToken(TEXT("fn.editorutility"), KLower) ||
					HasToken(TEXT("fn.editorassetlibrary"), KLower) ||
					HasToken(TEXT("fn.editorstaticmeshlibrary"), KLower) ||
					HasToken(TEXT("fn.editorskeletalmeshlibrary"), KLower);
				const bool bEditorMismatch = bCandidateIsEditor && !bOriginalMentionsEditor;

				bool bClassSwapOnUserAsset = false;
				if (bOrigIsUserAsset)
				{
					const FString CandClass = ExtractClassPart(K);
					if (!CandClass.IsEmpty() && !CandClass.Equals(OrigClass, ESearchCase::IgnoreCase))
						bClassSwapOnUserAsset = true;
				}

				bool bCrossNativeClassSwap = false;
				if (!bOrigIsUserAsset && !OrigClass.IsEmpty())
				{
					static const TSet<FString> StrictClassMatchOrigins = {
						TEXT("Actor"), TEXT("Pawn"), TEXT("Character"), TEXT("Controller"),
						TEXT("AIController"), TEXT("PlayerController"), TEXT("GameMode"),
						TEXT("GameInstance"), TEXT("ActorComponent"), TEXT("SceneComponent"),
						TEXT("PrimitiveComponent"), TEXT("StaticMeshComponent"), TEXT("SkeletalMeshComponent"),
						TEXT("CharacterMovementComponent"), TEXT("CapsuleComponent"),
						TEXT("UserWidget"), TEXT("Widget"), TEXT("PlayerCameraManager"),
					};
					if (StrictClassMatchOrigins.Contains(OrigClass))
					{
						const FString CandClass = ExtractClassPart(K);
						if (!CandClass.IsEmpty() && !CandClass.Equals(OrigClass, ESearchCase::IgnoreCase))
							bCrossNativeClassSwap = true;
					}
				}

				const bool bAbstractStubHandle =
					KLower == TEXT("k2.make") ||
					KLower == TEXT("k2.break") ||
					KLower == TEXT("k2.make ") ||
					KLower == TEXT("k2.break ");

				bool bVerbCrossing = false;
				{
					auto FuncVerb = [](const FString& FullHandle) -> FString
					{
						FString Fn = FullHandle;
						int32 Dot;
						if (Fn.FindLastChar(TEXT('.'), Dot)) Fn = Fn.RightChop(Dot + 1);
						if (Fn.StartsWith(TEXT("K2_"), ESearchCase::CaseSensitive)) Fn = Fn.RightChop(3);
						if (Fn.StartsWith(TEXT("Get"), ESearchCase::CaseSensitive)) return TEXT("get");
						if (Fn.StartsWith(TEXT("Set"), ESearchCase::CaseSensitive)) return TEXT("set");
						return FString();
					};
					const FString OrigVerb = FuncVerb(Handle);
					const FString CandVerb = FuncVerb(K);
					if (!OrigVerb.IsEmpty() && !CandVerb.IsEmpty() && OrigVerb != CandVerb)
						bVerbCrossing = true;
				}

				if (bSkelClass || bReinstClass || bLoopMismatch || bEditorMismatch || bClassSwapOnUserAsset || bCrossNativeClassSwap || bAbstractStubHandle || bVerbCrossing)
				{
					Top.RemoveAt(0);
					continue;
				}
				break;
			}

			if (Top.Num() == 0 || Top[0].Confidence < MinConfidence)
			{
				continue;
			}

			UBlueprintNodeSpawner* WinningSpawner = SpawnerCache.FindRef(Top[0].CandidateKey);
			FString ResolvedHandle = WinningSpawner ? GraphEditHelpers::BuildHandle(WinningSpawner) : Top[0].CandidateKey;
			if (ResolvedHandle.IsEmpty()) ResolvedHandle = Top[0].CandidateKey;

			NodeObj->SetStringField(TEXT("handle"), ResolvedHandle);

			TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
			Repair->SetStringField(TEXT("repair"), TEXT("handle_semantic_resolved"));
			Repair->SetStringField(TEXT("node_id"), NodeId);
			Repair->SetStringField(TEXT("bad_handle"), Handle);
			Repair->SetStringField(TEXT("corrected_handle"), ResolvedHandle);
			Repair->SetNumberField(TEXT("confidence"), Top[0].Confidence);

			TArray<TSharedPtr<FJsonValue>> AltArray;
			for (int32 A = 1; A < Top.Num(); A++)
			{
				TSharedPtr<FJsonObject> AltObj = MakeShared<FJsonObject>();
				UBlueprintNodeSpawner* AltSpawner = SpawnerCache.FindRef(Top[A].CandidateKey);
				const FString AltHandle = AltSpawner ? GraphEditHelpers::BuildHandle(AltSpawner) : Top[A].CandidateKey;
				AltObj->SetStringField(TEXT("handle"), AltHandle);
				AltObj->SetNumberField(TEXT("confidence"), Top[A].Confidence);
				AltArray.Add(MakeShareable(new FJsonValueObject(AltObj)));
			}
			Repair->SetArrayField(TEXT("alternatives"), AltArray);
			Repair->SetStringField(TEXT("reason"), FString::Printf(
				TEXT("Handle '%s' not found; fuzzy-resolved to '%s' (confidence %.2f). "
				     "Use the corrected handle in future calls."),
				*Handle, *ResolvedHandle, Top[0].Confidence));
			RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
		}
	}

	UK2Node_FunctionEntry* FuncEntryNode = nullptr;
	UK2Node_FunctionResult* FuncResultNode = nullptr;
	UEdGraphNode* MacroEntryTunnel = nullptr;
	UEdGraphNode* MacroExitTunnel  = nullptr;
	{
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		Graph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
		if (EntryNodes.Num() > 0)
		{
			FuncEntryNode = EntryNodes[0];
		}
		TArray<UK2Node_FunctionResult*> ResultNodes;
		Graph->GetNodesOfClass<UK2Node_FunctionResult>(ResultNodes);
		if (ResultNodes.Num() > 0)
		{
			FuncResultNode = ResultNodes[0];
		}
		if (!FuncEntryNode || !FuncResultNode)
		{
			TArray<UK2Node_Tunnel*> Tunnels;
			Graph->GetNodesOfClass<UK2Node_Tunnel>(Tunnels);
			for (UK2Node_Tunnel* T : Tunnels)
			{
				if (!T) continue;
				if (!MacroEntryTunnel && T->bCanHaveOutputs && !T->bCanHaveInputs) MacroEntryTunnel = T;
				else if (!MacroExitTunnel && T->bCanHaveInputs && !T->bCanHaveOutputs) MacroExitTunnel = T;
			}
		}
	}
	const bool bIsMacroGraph = (MacroEntryTunnel != nullptr || MacroExitTunnel != nullptr);

	int32 StaleNodesPurged = 0;
	{
		TSet<FString> NewNodeIds;
		for (int32 i = 0; i < NodesArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[i]);
			if (NodeObj.IsValid())
			{
				FString LocalId;
				NodeObj->TryGetStringField(TEXT("id"), LocalId);
				if (!LocalId.IsEmpty())
					NewNodeIds.Add(LocalId);
			}
		}

		TArray<UEdGraphNode*> NodesToRemove;

		if (FuncEntryNode || bIsMacroGraph)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node)) continue;
				if (Node->IsA(UEdGraphNode_Comment::StaticClass())) continue;
				if (Node == FuncEntryNode || Node == FuncResultNode) continue;
				if (Node == MacroEntryTunnel || Node == MacroExitTunnel) continue;

				const FString GEID = BlueprintNodeIdentity::GetLogicalId(Blueprint, Node);
				if (GEID.IsEmpty()) continue;

				if (NewNodeIds.Contains(GEID))
				{
					NodesToRemove.Add(Node);
				}
				else
				{
					bool bHasAnyLink = false;
					for (UEdGraphPin* Pin : Node->Pins)
					{
						if (Pin && Pin->LinkedTo.Num() > 0) { bHasAnyLink = true; break; }
					}
					if (!bHasAnyLink)
						NodesToRemove.Add(Node);
				}
			}
		}
		else
		{
			TSet<FString> EventNamesBeingRebuilt;
			for (int32 i = 0; i < NodesArray->Num(); i++)
			{
				TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[i]);
				if (!NodeObj.IsValid()) continue;
				FString Handle;
				NodeObj->TryGetStringField(TEXT("handle"), Handle);
				if (Handle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), Handle);

				if (Handle.StartsWith(TEXT("ev.")))
				{
					if (Handle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase))
					{
						FString CustomName;
						NodeObj->TryGetStringField(TEXT("custom_name"), CustomName);
						if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("custom_event_name"), CustomName);
						if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("event_name"), CustomName);
						if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("name"), CustomName);
						if (!CustomName.IsEmpty())
							EventNamesBeingRebuilt.Add(CustomName);
					}
					else
					{
						EventNamesBeingRebuilt.Add(Handle.Mid(3));
					}
				}
				else if (Handle.StartsWith(TEXT("ia.")))
				{
					EventNamesBeingRebuilt.Add(TEXT("ia:") + Handle.Mid(3));
				}
			}

			if (EventNamesBeingRebuilt.Num() > 0)
			{
				TArray<UEdGraphNode*> RebuiltEventNodes;
				for (const FString& EvtName : EventNamesBeingRebuilt)
				{
					UEdGraphNode* ExistingEvent = nullptr;
					for (UEdGraphNode* Node : Graph->Nodes)
					{
						if (EvtName.StartsWith(TEXT("ia:")))
						{
							FString IAName = EvtName.Mid(3);
							UK2Node_EnhancedInputAction* IANode = Cast<UK2Node_EnhancedInputAction>(Node);
							if (IANode && IANode->InputAction && IANode->InputAction->GetName().Equals(IAName, ESearchCase::IgnoreCase))
							{ ExistingEvent = IANode; break; }
							continue;
						}
						UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node);
						if (CE && CE->CustomFunctionName.ToString().Equals(EvtName, ESearchCase::IgnoreCase))
						{ ExistingEvent = CE; break; }
						UK2Node_Event* BuiltinEvt = Cast<UK2Node_Event>(Node);
						if (BuiltinEvt && BuiltinEvt->EventReference.GetMemberName().ToString().Equals(EvtName, ESearchCase::IgnoreCase))
						{ ExistingEvent = BuiltinEvt; break; }
					}
					if (!ExistingEvent) continue;
					RebuiltEventNodes.Add(ExistingEvent);

					TSet<UEdGraphNode*> Reached;
					TQueue<UEdGraphNode*> ExecQ;
					ExecQ.Enqueue(ExistingEvent);
					Reached.Add(ExistingEvent);
					while (!ExecQ.IsEmpty())
					{
						UEdGraphNode* Cur;
						ExecQ.Dequeue(Cur);
						for (UEdGraphPin* Pin : Cur->Pins)
						{
							if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
							for (UEdGraphPin* Linked : Pin->LinkedTo)
							{
								if (!Linked) continue;
								UEdGraphNode* Other = Linked->GetOwningNodeUnchecked();
								if (Other && !Reached.Contains(Other) && !Other->IsA(UEdGraphNode_Comment::StaticClass()))
								{ Reached.Add(Other); ExecQ.Enqueue(Other); }
							}
						}
					}
					TQueue<UEdGraphNode*> DataQ;
					for (UEdGraphNode* N : Reached) DataQ.Enqueue(N);
					while (!DataQ.IsEmpty())
					{
						UEdGraphNode* Cur;
						DataQ.Dequeue(Cur);
						for (UEdGraphPin* Pin : Cur->Pins)
						{
							if (!Pin || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
							for (UEdGraphPin* Linked : Pin->LinkedTo)
							{
								if (!Linked) continue;
								UEdGraphNode* Supplier = Linked->GetOwningNodeUnchecked();
								if (Supplier && !Reached.Contains(Supplier) && !Supplier->IsA(UEdGraphNode_Comment::StaticClass()))
								{ Reached.Add(Supplier); DataQ.Enqueue(Supplier); }
							}
						}
					}

					for (UEdGraphNode* N : Reached)
					{
						if (N == ExistingEvent) continue;
						const FString GEID = BlueprintNodeIdentity::GetLogicalId(Blueprint, N);
						if (!GEID.IsEmpty() && !NewNodeIds.Contains(GEID))
							NodesToRemove.Add(N);
					}
				}

			}
		}

		TSet<FString> PreservedRefIds;
		if (ConnectionsArray)
		{
			auto AddRefId = [&PreservedRefIds](const FString& Raw)
			{
				if (Raw.IsEmpty()) return;
				FString Base = Raw;
				int32 Dot;
				if (Base.FindChar(TEXT('.'), Dot)) Base = Base.Left(Dot);
				if (Base.IsEmpty()) return;
				PreservedRefIds.Add(Base);
				PreservedRefIds.Add(Base.ToLower());
			};
			for (const TSharedPtr<FJsonValue>& CV : *ConnectionsArray)
			{
				TSharedPtr<FJsonObject> CObj = SafeAsObject(CV);
				if (!CObj.IsValid()) continue;
				FString FromStr; CObj->TryGetStringField(TEXT("from"), FromStr); AddRefId(FromStr);
				FString ToStr;   CObj->TryGetStringField(TEXT("to"),   ToStr);   AddRefId(ToStr);
				FString FromNodeStr; CObj->TryGetStringField(TEXT("from_node"), FromNodeStr); AddRefId(FromNodeStr);
				FString ToNodeStr;   CObj->TryGetStringField(TEXT("to_node"),   ToNodeStr);   AddRefId(ToNodeStr);
			}
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node) || NodesToRemove.Contains(Node)) continue;
			if (Node->IsA(UEdGraphNode_Comment::StaticClass())) continue;
			if (IsClearProtectedNode(Node)) continue;
			const FString NodeGEID = BlueprintNodeIdentity::GetLogicalId(Blueprint, Node);
			if (NodeGEID.IsEmpty()) continue;

			bool bHasAnyLink = false;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->LinkedTo.Num() > 0) { bHasAnyLink = true; break; }
			}
			if (bHasAnyLink) continue;

			if (PreservedRefIds.Num() > 0)
			{
				bool bPreserved = false;
				if (PreservedRefIds.Contains(NodeGEID) || PreservedRefIds.Contains(NodeGEID.ToLower()))
				{
					bPreserved = true;
				}
				if (!bPreserved && Node->NodeGuid.IsValid())
				{
					const FString GuidHex = Node->NodeGuid.ToString();
					if (PreservedRefIds.Contains(GuidHex) || PreservedRefIds.Contains(GuidHex.ToLower()))
					{
						bPreserved = true;
					}
				}
				if (bPreserved) continue;
			}

			NodesToRemove.Add(Node);
		}

		if (NodesToRemove.Num() > 0)
		{
			for (UEdGraphNode* Node : NodesToRemove)
			{
				BlueprintNodeIdentity::ClearLogicalId(Blueprint, Node);
				Graph->RemoveNode(Node);
				StaleNodesPurged++;
			}
		}
	}

	TMap<FString, UEdGraphNode*> LocalIdMap;
	if (FuncEntryNode) LocalIdMap.Add(TEXT("entry"), FuncEntryNode);
	else if (MacroEntryTunnel) LocalIdMap.Add(TEXT("entry"), MacroEntryTunnel);
	if (FuncResultNode) LocalIdMap.Add(TEXT("return"), FuncResultNode);
	else if (MacroExitTunnel) LocalIdMap.Add(TEXT("return"), MacroExitTunnel);
	TSet<FString> FailedNodeIds;
	TArray<UEdGraphNode*> NewNodes;
	TArray<TSharedPtr<FJsonValue>> NodeFailures;
	int32 NodeSuccessCount = 0;
	int32 NodeFailCount = 0;

	int32 ExistingMaxBottom = MIN_int32;
	for (UEdGraphNode* ExNode : Graph->Nodes)
	{
		if (!IsValid(ExNode) || ExNode->IsA(UEdGraphNode_Comment::StaticClass())) continue;
		int32 EstH = FMath::Max(80, 36 + ExNode->Pins.Num() * 18);
		ExistingMaxBottom = FMath::Max(ExistingMaxBottom, ExNode->NodePosY + EstH);
	}
	int32 NewMinY = MAX_int32;
	for (int32 i = 0; i < NodesArray->Num(); i++)
	{
		TSharedPtr<FJsonObject> TmpObj = SafeAsObject((*NodesArray)[i]);
		if (!TmpObj.IsValid()) continue;
		double TmpY = 0;
		TmpObj->TryGetNumberField(TEXT("y"), TmpY);
		NewMinY = FMath::Min(NewMinY, (int32)TmpY);
	}
	int32 YOffset = 0;
	if (ExistingMaxBottom != MIN_int32 && NewMinY != MAX_int32 && NewMinY < ExistingMaxBottom + 400)
	{
		YOffset = (ExistingMaxBottom + 400) - NewMinY;
	}

	for (int32 i = 0; i < NodesArray->Num(); i++)
	{
		TSharedPtr<FJsonObject> NodeObj = SafeAsObject((*NodesArray)[i]);
		TSharedPtr<FJsonObject> NodeResult = MakeShared<FJsonObject>();

		if (!NodeObj.IsValid())
		{
			NodeResult->SetBoolField(TEXT("success"), false);
			NodeResult->SetStringField(TEXT("error"), TEXT("Invalid node object"));
			NodeFailCount++;
			NodeFailures.Add(MakeShared<FJsonValueObject>(NodeResult));
			continue;
		}

		FString LocalId, Handle, CustomName;
		NodeObj->TryGetStringField(TEXT("id"), LocalId);
		if (LocalId.IsEmpty()) NodeObj->TryGetStringField(TEXT("node_id"), LocalId);
		if (LocalId.IsEmpty()) NodeObj->TryGetStringField(TEXT("node"), LocalId);
		NodeObj->TryGetStringField(TEXT("handle"), Handle);
		if (Handle.IsEmpty()) NodeObj->TryGetStringField(TEXT("type"), Handle);
		if (Handle.StartsWith(TEXT("disp."), ESearchCase::IgnoreCase))
			Handle = TEXT("ev.Dispatcher.") + Handle.RightChop(5);

		{
			FString H = Handle;
			if (H.StartsWith(TEXT("k2."), ESearchCase::IgnoreCase)) H = H.RightChop(3);
			auto TryStruct = [&](const TCHAR* Bad, const TCHAR* GoodVerb) -> bool
			{
				if (H.StartsWith(Bad, ESearchCase::IgnoreCase))
				{
					const FString Rest = H.RightChop(FCString::Strlen(Bad)).TrimStartAndEnd();
					if (!Rest.IsEmpty()) { Handle = FString(GoodVerb) + TEXT(" ") + Rest; return true; }
				}
				return false;
			};
			TryStruct(TEXT("BreakStruct "),  TEXT("k2.Break"))
				|| TryStruct(TEXT("Break Struct "), TEXT("k2.Break"))
				|| TryStruct(TEXT("MakeStruct "),   TEXT("k2.Make"))
				|| TryStruct(TEXT("Make Struct "),  TEXT("k2.Make"));
		}

		if (Handle.Equals(TEXT("k2.Get"), ESearchCase::IgnoreCase)
			|| Handle.Equals(TEXT("k2.Get Copy"), ESearchCase::IgnoreCase)
			|| Handle.Equals(TEXT("k2.GetCopy"), ESearchCase::IgnoreCase))
			Handle = TEXT("k2.Get (a copy)");
		if (Handle.Equals(TEXT("k2.InputKey"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.Input Key"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.Key"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.InputDebugKey"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.K2Node_InputDebugKey"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.Debug Key"), ESearchCase::IgnoreCase)
		 || Handle.Equals(TEXT("k2.DebugKey"), ESearchCase::IgnoreCase))
		{
			FString KeyName;
			if (NodeObj->TryGetStringField(TEXT("key"), KeyName) && !KeyName.IsEmpty())
				Handle = TEXT("k2.Debug Key ") + KeyName.ToUpper();
		}
		NodeObj->TryGetStringField(TEXT("custom_name"), CustomName);
		if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("custom_event_name"), CustomName);
		if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("event_name"), CustomName);
		if (CustomName.IsEmpty()) NodeObj->TryGetStringField(TEXT("name"), CustomName);

		if (CustomName.IsEmpty() && Handle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase) && !LocalId.IsEmpty())
		{
			FString Prefix = LocalId.ToLower();
			while (Prefix.Len() > 0 && FChar::IsDigit(Prefix[Prefix.Len()-1]))
				Prefix.LeftChopInline(1);
			while (Prefix.Len() > 0 && Prefix[Prefix.Len()-1] == TEXT('_'))
				Prefix.LeftChopInline(1);
			bool bGenericId = (Prefix == TEXT("ev") || Prefix == TEXT("event") ||
				Prefix == TEXT("n") || Prefix == TEXT("node") ||
				Prefix == TEXT("ce") || Prefix == TEXT("custom") ||
				Prefix == TEXT("customevent") || Prefix == TEXT("custom_event") ||
				Prefix.IsEmpty());
			if (!bGenericId)
			{
				FString IdLower = LocalId.ToLower();
				bGenericId = IdLower.StartsWith(TEXT("ev_")) ||
					IdLower.StartsWith(TEXT("ce_")) ||
					IdLower.StartsWith(TEXT("event_")) ||
					IdLower.StartsWith(TEXT("custom_")) ||
					LocalId.Len() <= 3;
			}
			if (!bGenericId)
				CustomName = LocalId;
		}

		if (LocalId.IsEmpty() || Handle.IsEmpty())
		{
			NodeResult->SetBoolField(TEXT("success"), false);

			FString Library, Function;
			const bool bHasLib  = NodeObj->TryGetStringField(TEXT("library"), Library)  && !Library.IsEmpty();
			const bool bHasFunc = NodeObj->TryGetStringField(TEXT("function"), Function) && !Function.IsEmpty();
			const bool bHasInputs = NodeObj->HasField(TEXT("inputs"));

			if (bHasLib && bHasFunc && Handle.IsEmpty())
			{
				NodeResult->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Missing 'handle'. Combine library+function: handle=\"%s.%s\". Drop library/function keys."),
					*Library, *Function));
			}
			else if (bHasInputs)
			{
				NodeResult->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Missing 'handle'%s. Drop 'inputs' (use top-level connections[]/defaults[])."),
					LocalId.IsEmpty() ? TEXT(" and 'id'") : TEXT("")));
			}
			else if (Handle.IsEmpty())
			{
				NodeResult->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Missing 'handle'%s. Required: handle=\"k2.Branch\" | \"fn.Lib.Func\" | \"var.get.X\"."),
					LocalId.IsEmpty() ? TEXT(" and 'id'") : TEXT("")));
			}
			else
			{
				NodeResult->SetStringField(TEXT("error"),
					TEXT("Missing 'id' (any unique string used by connections[])."));
			}

			NodeFailCount++;
			NodeFailures.Add(MakeShared<FJsonValueObject>(NodeResult));
			continue;
		}

		if (!LocalId.IsEmpty() && PreFlightDropNodeIds.Contains(LocalId))
		{
			continue;
		}

		if (PreFlightSkipNodeIds.Contains(LocalId))
		{
			const bool bAlwaysSkip =
				(Handle.StartsWith(TEXT("entry."), ESearchCase::IgnoreCase) && !Handle.Equals(TEXT("entry"), ESearchCase::IgnoreCase))
				|| Handle.Equals(TEXT("return"), ESearchCase::IgnoreCase)
				|| Handle.Equals(TEXT("entry"), ESearchCase::IgnoreCase);
			if (bAlwaysSkip)
			{
				NodeResult->SetBoolField(TEXT("success"), false);
				NodeResult->SetStringField(TEXT("id"), LocalId);
				NodeResult->SetStringField(TEXT("handle"), Handle);
				NodeResult->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Skipped by pre-flight: '%s' is a reserved/invalid handle. See repairs_made[]."), *Handle));
				NodeFailCount++;
				NodeFailures.Add(MakeShared<FJsonValueObject>(NodeResult));
				continue;
			}
			else
			{
				PreFlightSkipNodeIds.Remove(LocalId);
			}
		}

		if (LocalIdMap.Contains(LocalId))
		{
			NodeSuccessCount++;
			continue;
		}

		if (FuncEntryNode == nullptr &&
			(LocalId.Equals(TEXT("entry"), ESearchCase::IgnoreCase) ||
			 LocalId.Equals(TEXT("return"), ESearchCase::IgnoreCase)))
		{
			NodeResult->SetBoolField(TEXT("success"), false);
			NodeResult->SetStringField(TEXT("id"), LocalId);
			NodeResult->SetStringField(TEXT("error"),
				TEXT("'entry'/'return' reserved for function graphs only. Use a unique id (e.g. 'beginPlay')."));
			NodeFailCount++;
			NodeFailures.Add(MakeShared<FJsonValueObject>(NodeResult));
			continue;
		}

		if (Handle.Equals(TEXT("return"), ESearchCase::IgnoreCase) && FuncEntryNode && !FuncResultNode)
		{
			UK2Node_FunctionResult* NewResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(FuncEntryNode);
			if (NewResultNode)
			{
				FuncResultNode = NewResultNode;
				LocalIdMap.Add(LocalId, FuncResultNode);
				NodeSuccessCount++;
				continue;
			}
		}

		{
			const bool bIsExtraReturn =
				Handle.Equals(TEXT("return.new"), ESearchCase::IgnoreCase) ||
				Handle.Equals(TEXT("return2"), ESearchCase::IgnoreCase) ||
				Handle.Equals(TEXT("k2.Return Node"), ESearchCase::IgnoreCase) ||
				Handle.Equals(TEXT("k2.FunctionResult"), ESearchCase::IgnoreCase);
			if (bIsExtraReturn && FuncEntryNode && Graph)
			{
				double XR = 0, YR = 0;
				NodeObj->TryGetNumberField(TEXT("x"), XR);
				NodeObj->TryGetNumberField(TEXT("y"), YR);

				UK2Node_FunctionResult* NewResult = nullptr;
				if (!FuncResultNode)
				{
					NewResult = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(FuncEntryNode);
					FuncResultNode = NewResult;
				}
				else
				{
					NewResult = NewObject<UK2Node_FunctionResult>(Graph);
					if (NewResult)
					{
						NewResult->FunctionReference = FuncResultNode->FunctionReference;
						NewResult->CreateNewGuid();
						NewResult->NodePosX = (int32)XR;
						NewResult->NodePosY = (int32)YR + YOffset;
						for (const TSharedPtr<FUserPinInfo>& Pin : FuncResultNode->UserDefinedPins)
						{
							if (Pin.IsValid())
								NewResult->UserDefinedPins.Add(MakeShared<FUserPinInfo>(*Pin));
						}
						Graph->AddNode(NewResult,  false,  false);
						NewResult->PostPlacedNewNode();
					}
				}

				if (NewResult)
				{
					LocalIdMap.Add(LocalId, NewResult);
					NodeSuccessCount++;
					continue;
				}
				NodeResult->SetBoolField(TEXT("success"), false);
				NodeResult->SetStringField(TEXT("id"), LocalId);
				NodeResult->SetStringField(TEXT("error"),
					TEXT("Failed to create additional return node — graph or function entry missing."));
				NodeFailCount++;
				NodeFailures.Add(MakeShared<FJsonValueObject>(NodeResult));
				continue;
			}
		}

		double JsonX = 0, JsonY = 0;
		NodeObj->TryGetNumberField(TEXT("x"), JsonX);
		NodeObj->TryGetNumberField(TEXT("y"), JsonY);
		int32 PosX = (int32)JsonX;
		int32 PosY = (int32)JsonY + YOffset;

		const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
		TArray<TSharedPtr<FJsonValue>> ParsedInputs;
		if (!NodeObj->TryGetArrayField(TEXT("inputs"), InputsArray))
		{
			static const TCHAR* InputsAliases[] = {
				TEXT("params"), TEXT("parameters"), TEXT("event_params"), TEXT("pin_params"), TEXT("user_pins")
			};
			for (const TCHAR* Alias : InputsAliases)
			{
				if (NodeObj->TryGetArrayField(Alias, InputsArray)) break;
			}
		}
		if (!InputsArray)
		{
			FString InputsStr;
			const TCHAR* StringAliases[] = {
				TEXT("inputs"), TEXT("params"), TEXT("parameters"), TEXT("event_params"), TEXT("pin_params"), TEXT("user_pins")
			};
			for (const TCHAR* Alias : StringAliases)
			{
				if (NodeObj->TryGetStringField(Alias, InputsStr) && !InputsStr.IsEmpty())
				{
					TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(InputsStr);
					if (FJsonSerializer::Deserialize(R, ParsedInputs) && ParsedInputs.Num() > 0)
					{
						InputsArray = &ParsedInputs;
					}
					break;
				}
			}
		}

		if (Handle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[build_graph] ev.CustomEvent id='%s' custom_name='%s' inputs=%d"),
				*LocalId, *CustomName, InputsArray ? InputsArray->Num() : -1);
		}

		UClass* const SelfCtx = SelfWireContextByNodeId.FindRef(LocalId);

		FString CreateError;
		TArray<FString> NodeWarnings;
		UEdGraphNode* NewNode = GraphEditHelpers::CreateNodeFromHandle(
			Blueprint, Graph, Handle, PosX, PosY, CustomName, InputsArray, CreateError, &SpawnerCache, SelfCtx, &NodeWarnings);

		for (const FString& W : NodeWarnings)
		{
			TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
			Repair->SetStringField(TEXT("repair"), TEXT("input_action_ambiguous"));
			Repair->SetStringField(TEXT("node_id"), LocalId);
			Repair->SetStringField(TEXT("reason"), W);
			RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
		}

		if (NewNode)
		{
			if (UK2Node_DynamicCast* CastN = Cast<UK2Node_DynamicCast>(NewNode))
			{
				bool bPureHint = false;
				if (NodeObj->TryGetBoolField(TEXT("is_pure"), bPureHint) && bPureHint)
				{
					CastN->SetPurity(true);
					CastN->ReconstructNode();
				}
			}
		}

		if (NewNode && InputsArray && InputsArray->Num() > 0 &&
			Handle.Equals(TEXT("ev.CustomEvent"), ESearchCase::IgnoreCase))
		{
			if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(NewNode))
			{
				bool bAddedAny = false;
				for (const TSharedPtr<FJsonValue>& InputVal : *InputsArray)
				{
					TSharedPtr<FJsonObject> InputObj = SafeAsObject(InputVal);
					if (!InputObj.IsValid()) continue;
					FString PinName, PinTypeStr;
					InputObj->TryGetStringField(TEXT("name"), PinName);
					InputObj->TryGetStringField(TEXT("type"), PinTypeStr);
					if (PinName.IsEmpty() || PinTypeStr.IsEmpty()) continue;

					bool bAlreadyPresent = false;
					for (const TSharedPtr<FUserPinInfo>& UP : CE->UserDefinedPins)
					{
						if (UP.IsValid() && UP->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
						{
							bAlreadyPresent = true;
							break;
						}
					}
					if (bAlreadyPresent) continue;

					FEdGraphPinType PinType = GraphEditHelpers::StringToPinType(PinTypeStr);
					CE->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output);
					bAddedAny = true;
				}
				if (bAddedAny) CE->ReconstructNode();
			}
		}

		if (NewNode && (NewNode->NodePosX != PosX || NewNode->NodePosY != PosY))
		{
			NewNode->NodePosX = PosX;
			NewNode->NodePosY = PosY;
		}

		if (!NewNode)
		{
			NodeResult->SetBoolField(TEXT("success"), false);
			NodeResult->SetStringField(TEXT("id"), LocalId);
			NodeResult->SetStringField(TEXT("error"), CreateError);
			NodeResult->SetStringField(TEXT("handle"), Handle);
			FailedNodeIds.Add(LocalId);
			NodeFailCount++;
			NodeFailures.Add(MakeShared<FJsonValueObject>(NodeResult));
			continue;
		}

		if (NewNode->Pins.Num() == 0 && Handle.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase))
		{
			if (UK2Node_CallFunction* FnNode = Cast<UK2Node_CallFunction>(NewNode))
			{
				const FName MemberName = FnNode->FunctionReference.GetMemberName();
				UClass* MemberParent = FnNode->FunctionReference.GetMemberParentClass();
				const bool bUnresolved = MemberName.IsNone()
					|| !MemberParent
					|| !MemberParent->FindFunctionByName(MemberName);
				if (bUnresolved)
				{
					Graph->RemoveNode(NewNode);
					NodeResult->SetBoolField(TEXT("success"), false);
					NodeResult->SetStringField(TEXT("id"), LocalId);
					NodeResult->SetStringField(TEXT("handle"), Handle);
					NodeResult->SetStringField(TEXT("error"), FString::Printf(
						TEXT("Function reference for handle '%s' did not resolve — placed node had zero pins. Qualify the handle: `fn.Self.<Func>` for own-BP, `fn.<Class>_C.<Func>` for cross-BP, or `fn.<Library>.<Func>` for library functions."),
						*Handle));
					FailedNodeIds.Add(LocalId);
					NodeFailCount++;
					NodeFailures.Add(MakeShared<FJsonValueObject>(NodeResult));
					continue;
				}
			}
		}

		if (Handle.Equals(TEXT("k2.Sequence"), ESearchCase::IgnoreCase))
		{
			UK2Node_ExecutionSequence* SeqNode = Cast<UK2Node_ExecutionSequence>(NewNode);
			if (SeqNode)
			{
				double NumOutputsDbl = 2.0;
				if (NodeObj->TryGetNumberField(TEXT("num_outputs"), NumOutputsDbl))
				{
					int32 NumOutputs = FMath::Max(2, (int32)NumOutputsDbl);
					for (int32 SeqIdx = 2; SeqIdx < NumOutputs; SeqIdx++)
						SeqNode->AddInputPin();
				}
			}
		}

		if (Handle.Equals(TEXT("k2.MakeArray"), ESearchCase::IgnoreCase))
		{
			UK2Node_MakeArray* ArrNode = Cast<UK2Node_MakeArray>(NewNode);
			if (ArrNode)
			{
				int32 MaxIdx = 0;

				double NumInputsDbl = 0.0;
				if (NodeObj->TryGetNumberField(TEXT("num_inputs"), NumInputsDbl))
					MaxIdx = FMath::Max(MaxIdx, (int32)NumInputsDbl - 1);

				auto ExtractBracketIdx = [](const FString& PinName) -> int32
				{
					if (!PinName.StartsWith(TEXT("["))) return -1;
					int32 CloseBracket = INDEX_NONE;
					if (!PinName.FindChar(TEXT(']'), CloseBracket)) return -1;
					if (CloseBracket <= 1) return -1;
					const FString Mid = PinName.Mid(1, CloseBracket - 1);
					if (!Mid.IsNumeric()) return -1;
					return FCString::Atoi(*Mid);
				};

				if (ConnectionsArray)
				{
					for (const TSharedPtr<FJsonValue>& CVal : *ConnectionsArray)
					{
						TSharedPtr<FJsonObject> Conn = SafeAsObject(CVal);
						if (!Conn.IsValid()) continue;
						FString ToId, ToPin;
						Conn->TryGetStringField(TEXT("to"), ToId);
						Conn->TryGetStringField(TEXT("to_pin"), ToPin);
						if (ToPin.IsEmpty() && ToId.Contains(TEXT(".")))
						{
							int32 Dot = INDEX_NONE;
							if (ToId.FindChar(TEXT('.'), Dot))
							{
								ToPin = ToId.Mid(Dot + 1);
								ToId = ToId.Left(Dot);
							}
						}
						if (!ToId.Equals(LocalId, ESearchCase::IgnoreCase)) continue;
						const int32 Idx = ExtractBracketIdx(ToPin);
						if (Idx > MaxIdx) MaxIdx = Idx;
					}
				}

				if (DefaultsArray)
				{
					for (const TSharedPtr<FJsonValue>& DVal : *DefaultsArray)
					{
						TSharedPtr<FJsonObject> Def = SafeAsObject(DVal);
						if (!Def.IsValid()) continue;
						FString DefNodeId, DefPin;
						Def->TryGetStringField(TEXT("node_id"), DefNodeId);
						Def->TryGetStringField(TEXT("pin_name"), DefPin);
						if (!DefNodeId.Equals(LocalId, ESearchCase::IgnoreCase)) continue;
						const int32 Idx = ExtractBracketIdx(DefPin);
						if (Idx > MaxIdx) MaxIdx = Idx;
					}
				}

				for (int32 ArrIdx = 1; ArrIdx <= MaxIdx; ArrIdx++)
				{
					ArrNode->AddInputPin();
				}
			}
		}

		BlueprintNodeIdentity::SetLogicalId(Blueprint, NewNode, LocalId);

		LocalIdMap.Add(LocalId, NewNode);
		if (!Handle.Equals(LocalId, ESearchCase::IgnoreCase) && !LocalIdMap.Contains(Handle))
			LocalIdMap.Add(Handle, NewNode);
		NewNodes.Add(NewNode);
		NodeSuccessCount++;
	}

	int32 DefaultSuccessCount = 0;
	int32 DefaultFailCount = 0;
	TArray<TSharedPtr<FJsonValue>> DefaultResults;

	if (DefaultsArray && DefaultsArray->Num() > 0)
	{
		for (int32 i = 0; i < DefaultsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Def = SafeAsObject((*DefaultsArray)[i]);
			TSharedPtr<FJsonObject> DefResult = MakeShared<FJsonObject>();

			if (!Def.IsValid())
			{
				DefResult->SetBoolField(TEXT("success"), false);
				DefResult->SetStringField(TEXT("error"), TEXT("Invalid default object"));
				DefaultFailCount++;
				DefaultResults.Add(MakeShared<FJsonValueObject>(DefResult));
				continue;
			}

			FString NodeId, PinName, Value;
			if (!Def->TryGetStringField(TEXT("node_id"), NodeId)) Def->TryGetStringField(TEXT("node"), NodeId);
			if (!Def->TryGetStringField(TEXT("pin_name"), PinName)) Def->TryGetStringField(TEXT("pin"), PinName);
			Def->TryGetStringField(TEXT("value"), Value);

			if (FString* AliasNode = PreFlightIdAliases.Find(NodeId)) NodeId = *AliasNode;

			if (PinName.IsEmpty() && !NodeId.IsEmpty())
			{
				const bool bLooksLikeHandle =
					NodeId.StartsWith(TEXT("var.")) || NodeId.StartsWith(TEXT("fn.")) ||
					NodeId.StartsWith(TEXT("k2.")) || NodeId.StartsWith(TEXT("ev.")) ||
					NodeId.StartsWith(TEXT("cast.")) || NodeId.StartsWith(TEXT("entry.")) ||
					NodeId.StartsWith(TEXT("return.")) || NodeId.StartsWith(TEXT("self."));
				int32 LastDot = INDEX_NONE;
				NodeId.FindLastChar(TEXT('.'), LastDot);
				if (!bLooksLikeHandle && LastDot != INDEX_NONE && LastDot > 0 && LastDot < NodeId.Len() - 1)
				{
					PinName = NodeId.Mid(LastDot + 1);
					NodeId = NodeId.Left(LastDot);
				}
			}

			if (!PinName.IsEmpty())
			{
				const bool bIsKnownOutput =
					PinName.Equals(TEXT("ReturnValue"),  ESearchCase::IgnoreCase) ||
					PinName.Equals(TEXT("then"),         ESearchCase::IgnoreCase) ||
					PinName.Equals(TEXT("CastFailed"),   ESearchCase::CaseSensitive) ||
					PinName.Equals(TEXT("Completed"),    ESearchCase::CaseSensitive) ||
					PinName.Equals(TEXT("LoopBody"),     ESearchCase::CaseSensitive) ||
					PinName.Equals(TEXT("ArrayElement"), ESearchCase::CaseSensitive) ||
					PinName.Equals(TEXT("ArrayIndex"),   ESearchCase::CaseSensitive);
				if (bIsKnownOutput)
				{
					UEdGraphNode* CheckNode = LocalIdMap.Contains(NodeId)
						? LocalIdMap[NodeId]
						: GraphEditHelpers::FindNodeInGraph(Graph, NodeId);
					const bool bOnReturnNode = CheckNode && CheckNode->IsA<UK2Node_FunctionResult>();
					if (!bOnReturnNode)
					{
						DefResult->SetBoolField(TEXT("success"), false);
						DefResult->SetStringField(TEXT("error"), FString::Printf(
							TEXT("'%s' is an OUTPUT pin — defaults[] can only set INPUT pin values. "
							     "Output pins produce values; they cannot be assigned a literal default. "
							     "Remove this defaults[] entry. To use this value, wire it via connections[]."),
							*PinName));
						DefaultFailCount++;
						DefaultResults.Add(MakeShared<FJsonValueObject>(DefResult));
						continue;
					}
				}
			}

			if (FailedNodeIds.Contains(NodeId))
			{
				DefResult->SetBoolField(TEXT("success"), false);
				DefResult->SetStringField(TEXT("error"), TEXT("Skipped — references a failed node"));
				DefaultFailCount++;
				DefaultResults.Add(MakeShared<FJsonValueObject>(DefResult));
				continue;
			}

			UEdGraphNode* Node = LocalIdMap.Contains(NodeId) ? LocalIdMap[NodeId] : GraphEditHelpers::FindNodeInGraph(Graph, NodeId);
			if (!Node)
			{
				DefResult->SetBoolField(TEXT("success"), false);
				DefResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Node not found: '%s'"), *NodeId));
				DefaultFailCount++;
				DefaultResults.Add(MakeShared<FJsonValueObject>(DefResult));
				continue;
			}

			UEdGraphPin* Pin = GraphEditHelpers::FindPinByName(Node, PinName, EGPD_Input);
			if (!Pin)
			{
				DefResult->SetBoolField(TEXT("success"), false);
				FString Avail = GraphEditHelpers::GetAvailablePinNames(Node, EGPD_Input);
				DefResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Pin '%s' not found on '%s'. Available inputs: [%s]"), *PinName, *NodeId, *Avail));
				DefaultFailCount++;
				DefaultResults.Add(MakeShared<FJsonValueObject>(DefResult));
				continue;
			}

			FString ResolvedDefValue = Value;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && Pin->PinType.PinSubCategoryObject.IsValid())
			{
				if (UEnum* PinEnum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					for (int32 Ei = 0; Ei < PinEnum->NumEnums() - 1; ++Ei)
					{
						FString FullName = PinEnum->GetNameStringByIndex(Ei);
						FString DisplayName = PinEnum->GetDisplayNameTextByIndex(Ei).ToString();
						FString ShortName = FullName.Contains(TEXT("::")) ? FullName.RightChop(FullName.Find(TEXT("::")) + 2) : FullName;
						if (Value.Equals(ShortName, ESearchCase::IgnoreCase) ||
							Value.Equals(DisplayName, ESearchCase::IgnoreCase) ||
							Value.Equals(FullName, ESearchCase::IgnoreCase))
						{
							ResolvedDefValue = Cast<UUserDefinedEnum>(PinEnum) ? DisplayName : FullName;
							break;
						}
					}
				}
			}

			const bool bIsDefClassOrObjPin =
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;
			bool bClassPinUnresolved = false;
			if (bIsDefClassOrObjPin && !Value.IsEmpty())
			{
				const bool bClassPin = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
				                       Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;
				bool bResolved = false;
				if (Value.StartsWith(TEXT("/")) && !Value.Contains(TEXT(".")))
				{
					if (UEditorAssetLibrary::DoesAssetExist(Value))
					{
						const FString ShortName = FPackageName::GetShortName(Value);
						ResolvedDefValue = bClassPin
							? FString::Printf(TEXT("%s.%s_C"), *Value, *ShortName)
							: FString::Printf(TEXT("%s.%s"),   *Value, *ShortName);
						bResolved = true;
					}
				}
				else if (Value.StartsWith(TEXT("/")) && Value.Contains(TEXT(".")))
				{
					bResolved = true;
				}
				else
				{
					IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
					FARFilter Filter;
					Filter.bRecursivePaths = true;
					Filter.PackagePaths.Add(FName(TEXT("/Game")));
					TArray<FAssetData> Candidates;
					AR.GetAssets(Filter, Candidates);
					for (const FAssetData& AD : Candidates)
					{
						if (AD.AssetName.ToString().Equals(Value, ESearchCase::IgnoreCase))
						{
							ResolvedDefValue = bClassPin
								? FString::Printf(TEXT("%s.%s_C"), *AD.PackageName.ToString(), *AD.AssetName.ToString())
								: FString::Printf(TEXT("%s.%s"),   *AD.PackageName.ToString(), *AD.AssetName.ToString());
							bResolved = true;
							break;
						}
					}
				}

				if (!bResolved && bClassPin)
				{
					DefResult->SetBoolField(TEXT("success"), false);
					DefResult->SetStringField(TEXT("error"), FString::Printf(
						TEXT("Class-ref pin '%s' on '%s': asset '%s' not found in /Game. Either (a) create the referenced Blueprint first, ")
						TEXT("(b) pass the full /Game/Path.Asset_C form, or (c) use a class variable on this BP and wire var.get.YourClassVar to %s instead of using a default."),
						*PinName, *NodeId, *Value, *PinName));
					DefaultFailCount++;
					DefaultResults.Add(MakeShared<FJsonValueObject>(DefResult));
					bClassPinUnresolved = true;
				}
			}
			if (bClassPinUnresolved) continue;

			Schema->TrySetDefaultValue(*Pin, ResolvedDefValue);
			Node->PinDefaultValueChanged(Pin);

			DefResult->SetBoolField(TEXT("success"), true);
			DefResult->SetStringField(TEXT("node_id"), NodeId);
			DefResult->SetStringField(TEXT("pin"), PinName);
			DefaultSuccessCount++;
			DefaultResults.Add(MakeShared<FJsonValueObject>(DefResult));
		}
	}

	if (ConnectionsArray && ConnectionsArray->Num() > 0)
	{
		TMap<UEdGraphNode*, TArray<FName>> SwitchPinAdds;
		for (int32 i = 0; i < ConnectionsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Conn = SafeAsObject((*ConnectionsArray)[i]);
			if (!Conn.IsValid()) continue;
			FString FromId, ToId, FromPin, ToPin;
			Conn->TryGetStringField(TEXT("from_node"), FromId);
			if (FromId.IsEmpty()) Conn->TryGetStringField(TEXT("from"), FromId);
			Conn->TryGetStringField(TEXT("from_pin"), FromPin);
			Conn->TryGetStringField(TEXT("to_node"), ToId);
			if (ToId.IsEmpty()) Conn->TryGetStringField(TEXT("to"), ToId);
			Conn->TryGetStringField(TEXT("to_pin"), ToPin);

			auto CollectSwitchPin = [&](const FString& NodeId, const FString& PinName)
			{
				if (NodeId.IsEmpty() || PinName.IsEmpty()) return;
				UEdGraphNode* const* NodePtr = LocalIdMap.Find(NodeId);
				if (!NodePtr || !*NodePtr) return;
				UEdGraphNode* Node = *NodePtr;
				for (UEdGraphPin* P : Node->Pins) if (P && P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)) return;
				if (PinName.Equals(TEXT("Selection"), ESearchCase::IgnoreCase) ||
					PinName.Equals(TEXT("Default"), ESearchCase::IgnoreCase) ||
					PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
					PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase)) return;
				if (Node->IsA<UK2Node_SwitchString>() || Node->IsA<UK2Node_SwitchName>() || Node->IsA<UK2Node_SwitchInteger>())
				{
					SwitchPinAdds.FindOrAdd(Node).AddUnique(FName(*PinName));
				}
			};
			CollectSwitchPin(FromId, FromPin);
			CollectSwitchPin(ToId,   ToPin);
		}
		for (auto& Pair : SwitchPinAdds)
		{
			UEdGraphNode* Node = Pair.Key;
			TArray<FName>& Cases = Pair.Value;
			if (UK2Node_SwitchString* SS = Cast<UK2Node_SwitchString>(Node))
			{
				for (const FName& N : Cases) SS->PinNames.AddUnique(N);
				SS->ReconstructNode();
			}
			else if (UK2Node_SwitchName* SN = Cast<UK2Node_SwitchName>(Node))
			{
				for (const FName& N : Cases) SN->PinNames.AddUnique(N);
				SN->ReconstructNode();
			}
			else if (UK2Node_SwitchInteger* SI = Cast<UK2Node_SwitchInteger>(Node))
			{
				for (const FName& N : Cases)
				{
					SI->AddPinToSwitchNode();
				}
				SI->ReconstructNode();
			}
		}
	}

	int32 ConnSuccessCount = 0;
	int32 ConnFailCount = 0;
	TArray<TSharedPtr<FJsonValue>> ConnResults;
	bool bConnShapeRepairLogged = false;

	if (ConnectionsArray && ConnectionsArray->Num() > 0)
	{
		for (int32 i = 0; i < ConnectionsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Conn = SafeAsObject((*ConnectionsArray)[i]);

			if (!Conn.IsValid() && (*ConnectionsArray)[i].IsValid() && (*ConnectionsArray)[i]->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Arr = (*ConnectionsArray)[i]->AsArray();
				auto EndpointFromObj = [](const TSharedPtr<FJsonValue>& V, FString& OutNode, FString& OutPin) -> bool
				{
					TSharedPtr<FJsonObject> O = (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : nullptr;
					if (!O.IsValid()) return false;
					if (!O->TryGetStringField(TEXT("node"), OutNode))
						if (!O->TryGetStringField(TEXT("id"), OutNode))
							O->TryGetStringField(TEXT("node_id"), OutNode);
					if (!O->TryGetStringField(TEXT("pin"), OutPin))
						O->TryGetStringField(TEXT("pin_name"), OutPin);
					return !OutNode.IsEmpty();
				};
				TSharedPtr<FJsonObject> Synth;
				if (Arr.Num() == 2 && Arr[0].IsValid() && Arr[0]->Type == EJson::Object)
				{
					FString FN, FP, TN, TP;
					if (EndpointFromObj(Arr[0], FN, FP) && EndpointFromObj(Arr[1], TN, TP))
					{
						Synth = MakeShared<FJsonObject>();
						Synth->SetStringField(TEXT("from"), FN);
						Synth->SetStringField(TEXT("from_pin"), FP);
						Synth->SetStringField(TEXT("to"), TN);
						Synth->SetStringField(TEXT("to_pin"), TP);
					}
				}
				else if (Arr.Num() == 4 && Arr[0].IsValid() && Arr[0]->Type == EJson::String)
				{
					Synth = MakeShared<FJsonObject>();
					Synth->SetStringField(TEXT("from"),     Arr[0]->AsString());
					Synth->SetStringField(TEXT("from_pin"), Arr[1]->AsString());
					Synth->SetStringField(TEXT("to"),       Arr[2]->AsString());
					Synth->SetStringField(TEXT("to_pin"),   Arr[3]->AsString());
				}
				if (Synth.IsValid())
				{
					Conn = Synth;
					if (!bConnShapeRepairLogged)
					{
						bConnShapeRepairLogged = true;
						TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
						Repair->SetStringField(TEXT("repair"), TEXT("connection_shape_coerced"));
						Repair->SetStringField(TEXT("reason"), TEXT("connections[] used an array shape ([{node,pin},{node,pin}] or [from,from_pin,to,to_pin]) — coerced to {from,from_pin,to,to_pin}. Use the object shape next time to avoid this."));
						RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					}
				}
			}

			TSharedPtr<FJsonObject> ConnResult = MakeShared<FJsonObject>();
			ConnResult->SetNumberField(TEXT("index"), i);

			if (!Conn.IsValid())
			{
				ConnResult->SetBoolField(TEXT("success"), false);
				const bool bIsNumericArray = (*ConnectionsArray)[i]->Type == EJson::Array;
				ConnResult->SetStringField(TEXT("error"), bIsNumericArray
					? TEXT("Connection is a numeric array [A,B,C,D], not an object. "
					       "REQUIRED format: {\"from\":\"nodeId\",\"from_pin\":\"pinName\",\"to\":\"nodeId\",\"to_pin\":\"pinName\"}. "
					       "Fetch get_tool_docs(categories=['blueprint_graph']) for the correct format.")
					: TEXT("Invalid connection object — expected {from, from_pin, to, to_pin}"));
				ConnFailCount++;
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			FString FromNodeId, FromPinName, ToNodeId, ToPinName;
			Conn->TryGetStringField(TEXT("from_node"), FromNodeId);
			if (FromNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("from"), FromNodeId);
			Conn->TryGetStringField(TEXT("from_pin"), FromPinName);
			Conn->TryGetStringField(TEXT("to_node"), ToNodeId);
			if (ToNodeId.IsEmpty()) Conn->TryGetStringField(TEXT("to"), ToNodeId);
			Conn->TryGetStringField(TEXT("to_pin"), ToPinName);

			if (FromNodeId.IsEmpty() && ToNodeId.IsEmpty() && FromPinName.IsEmpty() && ToPinName.IsEmpty())
			{
				ConnResult->SetBoolField(TEXT("success"), false);
				ConnResult->SetStringField(TEXT("error"),
					TEXT("Connection has no recognized keys (from/from_pin/to/to_pin all empty). "
					     "REQUIRED format: {\"from\":\"nodeId\",\"from_pin\":\"pinName\",\"to\":\"nodeId\",\"to_pin\":\"pinName\"}. "
					     "Fetch get_tool_docs(categories=['blueprint_graph']) to see the correct build_blueprint_graph format."));
				ConnFailCount++;
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			if (!FromNodeId.IsEmpty() && ToNodeId.IsEmpty() && ToPinName.IsEmpty())
			{
				ConnResult->SetBoolField(TEXT("success"), true);
				ConnResult->SetStringField(TEXT("status"), TEXT("skipped_dangling_exec"));
				ConnResult->SetStringField(TEXT("note"),
					TEXT("Connection has empty 'to' and 'to_pin' — treated as no-op. "
					     "Omit the connection entirely instead of sending an empty target."));
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			{
				auto IsEntryReturnId = [](const FString& Id) {
					return Id.Equals(TEXT("entry"), ESearchCase::IgnoreCase)
						|| Id.Equals(TEXT("return"), ESearchCase::IgnoreCase)
						|| Id.StartsWith(TEXT("entry."), ESearchCase::IgnoreCase)
						|| Id.StartsWith(TEXT("return."), ESearchCase::IgnoreCase);
				};
				if (Graph && (IsEntryReturnId(FromNodeId) || IsEntryReturnId(ToNodeId)))
				{
					bool bHasFunctionEntry = false;
					for (UEdGraphNode* GN : Graph->Nodes)
						if (GN && GN->IsA<UK2Node_FunctionEntry>()) { bHasFunctionEntry = true; break; }
					if (!bHasFunctionEntry)
					{
						ConnResult->SetBoolField(TEXT("success"), false);
						ConnResult->SetStringField(TEXT("error"),
							TEXT("'entry'/'return' are implicit nodes that exist ONLY in function graphs. "
							     "This is an EventGraph - there is no 'entry'/'return' node. Wire from your event "
							     "node directly (e.g. {from:'<EventNodeId>', from_pin:'then', to:'n1', to_pin:'execute'}), "
							     "using the event node id from get_blueprint_graph."));
						ConnFailCount++;
						ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
						continue;
					}
				}
			}

			if (!FromNodeId.IsEmpty() && ToNodeId.IsEmpty())
			{
				static const TSet<FString> StdConnKeysExec = {
					TEXT("from"), TEXT("from_node"), TEXT("from_node_id"),
					TEXT("to"),   TEXT("to_node"),   TEXT("to_node_id"),
					TEXT("from_pin"), TEXT("to_pin")
				};
				FString ExtraKey, ExtraVal;
				int32 ExtraCount = 0;
				for (const auto& KVPair : Conn->Values)
				{
					const FString KVKey(*KVPair.Key);
					if (!StdConnKeysExec.Contains(KVKey.ToLower()) && KVPair.Value->Type == EJson::String)
					{
						KVPair.Value->TryGetString(ExtraVal);
						ExtraKey = KVKey;
						ExtraCount++;
					}
				}
				if (ExtraCount == 1 && !ExtraVal.IsEmpty())
				{
					int32 LastDot;
					if (ExtraVal.FindLastChar(TEXT('.'), LastDot))
					{
						if (FromPinName.IsEmpty()) FromPinName = ExtraKey;
						ToNodeId  = ExtraVal.Left(LastDot);
						ToPinName = ExtraVal.Mid(LastDot + 1);
					}
				}
			}

			auto IsHandlePrefix = [](const FString& Id) -> bool {
				return Id.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("fn."),    ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("k2."),    ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("ev."),    ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("prop."),  ESearchCase::IgnoreCase);
			};
			if (FromPinName.IsEmpty() && FromNodeId.Contains(TEXT(".")) && !IsHandlePrefix(FromNodeId))
			{
				int32 DotIdx;
				if (FromNodeId.FindChar(TEXT('.'), DotIdx))
				{ FromPinName = FromNodeId.Mid(DotIdx + 1); FromNodeId = FromNodeId.Left(DotIdx); }
			}
			if (ToPinName.IsEmpty() && ToNodeId.Contains(TEXT(".")) && !IsHandlePrefix(ToNodeId))
			{
				int32 DotIdx;
				if (ToNodeId.FindChar(TEXT('.'), DotIdx))
				{ ToPinName = ToNodeId.Mid(DotIdx + 1); ToNodeId = ToNodeId.Left(DotIdx); }
			}

			if (PreFlightDropConnectionIndices.Contains(i))
			{
				ConnResult->SetBoolField(TEXT("success"), true);
				ConnResult->SetStringField(TEXT("status"), TEXT("skipped_exec_fanout_repair"));
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			if (FString* AliasFrom = PreFlightIdAliases.Find(FromNodeId)) FromNodeId = *AliasFrom;
			if (FString* AliasTo   = PreFlightIdAliases.Find(ToNodeId))   ToNodeId   = *AliasTo;

			if (PreFlightDropNodeIds.Contains(FromNodeId) || PreFlightDropNodeIds.Contains(ToNodeId))
			{
				ConnResult->SetBoolField(TEXT("success"), true);
				ConnResult->SetStringField(TEXT("status"), TEXT("skipped_dropped_node"));
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			if (FailedNodeIds.Contains(FromNodeId) || FailedNodeIds.Contains(ToNodeId))
			{
				ConnResult->SetBoolField(TEXT("success"), false);
				ConnResult->SetStringField(TEXT("error"), TEXT("Skipped — references a failed node"));
				ConnFailCount++;
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			if (FromNodeId.Equals(TEXT("k2.Self.self"), ESearchCase::IgnoreCase) && FromPinName.IsEmpty())
			{ FromNodeId = TEXT("k2.Self"); FromPinName = TEXT("self"); }
			if (ToNodeId.Equals(TEXT("k2.Self.self"), ESearchCase::IgnoreCase) && ToPinName.IsEmpty())
			{ ToNodeId = TEXT("k2.Self"); ToPinName = TEXT("self"); }

			auto StripRedundantVarSuffix = [](FString& NodeId, FString& PinName) {
				if (!NodeId.StartsWith(TEXT("var."), ESearchCase::IgnoreCase)) return;
				int32 LastDot;
				if (!NodeId.FindLastChar(TEXT('.'), LastDot) || LastDot < 8) return;
				const FString Prefix = NodeId.Left(LastDot);
				const FString Suffix = NodeId.Mid(LastDot + 1);
				if (Suffix.IsEmpty()) return;
				int32 PrefixLastDot;
				if (!Prefix.FindLastChar(TEXT('.'), PrefixLastDot)) return;
				const FString VarName = Prefix.Mid(PrefixLastDot + 1);
				if (Suffix.Equals(VarName, ESearchCase::IgnoreCase))
				{
					if (PinName.IsEmpty()) PinName = Suffix;
					NodeId = Prefix;
				}
			};
			StripRedundantVarSuffix(FromNodeId, FromPinName);
			StripRedundantVarSuffix(ToNodeId, ToPinName);

			if (const FString* RewrittenFrom = NodeIdFuzzyRewrites.Find(FromNodeId))
				FromNodeId = *RewrittenFrom;
			if (const FString* RewrittenTo = NodeIdFuzzyRewrites.Find(ToNodeId))
				ToNodeId = *RewrittenTo;

			UEdGraphNode* FromNode = LocalIdMap.Contains(FromNodeId) ? LocalIdMap[FromNodeId] : GraphEditHelpers::FindNodeInGraph(Graph, FromNodeId);
			UEdGraphNode* ToNode = LocalIdMap.Contains(ToNodeId) ? LocalIdMap[ToNodeId] : GraphEditHelpers::FindNodeInGraph(Graph, ToNodeId);

			auto LooksLikeHandle = [](const FString& Id) -> bool {
				return Id.StartsWith(TEXT("var.get."), ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("var.set."), ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("fn."), ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("k2."), ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("ev."), ESearchCase::IgnoreCase)
					|| Id.StartsWith(TEXT("prop."), ESearchCase::IgnoreCase);
			};
			if (!FromNode && LooksLikeHandle(FromNodeId))
			{
				FString InlineErr;
				UEdGraphNode* InlineNode = GraphEditHelpers::CreateNodeFromHandle(Blueprint, Graph, FromNodeId, 0, 0, TEXT(""), nullptr, InlineErr, &SpawnerCache);
				if (InlineNode) { LocalIdMap.Add(FromNodeId, InlineNode); FromNode = InlineNode; }
			}
			if (!ToNode && LooksLikeHandle(ToNodeId))
			{
				FString InlineErr;
				UEdGraphNode* InlineNode = GraphEditHelpers::CreateNodeFromHandle(Blueprint, Graph, ToNodeId, 0, 0, TEXT(""), nullptr, InlineErr, &SpawnerCache);
				if (InlineNode) { LocalIdMap.Add(ToNodeId, InlineNode); ToNode = InlineNode; }
			}

			if (!FromNode || !ToNode)
			{
				const FString& MissingId = !FromNode ? FromNodeId : ToNodeId;
				FString ErrMsg;
				if (MissingId.Contains(TEXT(".")) && !LooksLikeHandle(MissingId))
				{
					int32 LastDot;
					MissingId.FindLastChar(TEXT('.'), LastDot);
					const FString GuessedNode = MissingId.Left(LastDot);
					const FString GuessedPin = MissingId.Mid(LastDot + 1);
					ErrMsg = FString::Printf(
						TEXT("'%s' looks like the flat 'node.pin' form. Use {from:'%s', from_pin:'%s', to:'...', to_pin:'...'} instead."),
						*MissingId, *GuessedNode, *GuessedPin);
				}
				else
				{
					ErrMsg = FString::Printf(TEXT("Node not found: %s"), *MissingId);
				}
				ConnResult->SetBoolField(TEXT("success"), false);
				ConnResult->SetStringField(TEXT("error"), ErrMsg);
				ConnFailCount++;
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			if (FromPinName.IsEmpty() && ToPinName.IsEmpty() && FromNode && ToNode)
			{
				UEdGraphPin* FromExec = nullptr;
				UEdGraphPin* ToExec = nullptr;
				for (UEdGraphPin* P : FromNode->Pins)
				{
					if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{ FromExec = P; break; }
				}
				for (UEdGraphPin* P : ToNode->Pins)
				{
					if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{ ToExec = P; break; }
				}
				if (FromExec && ToExec)
				{
					FromPinName = FromExec->PinName.ToString();
					ToPinName = ToExec->PinName.ToString();
				}
			}

			if (FromPinName.IsEmpty() && FromNode)
			{
				TArray<UEdGraphPin*> DataOutputs;
				for (UEdGraphPin* P : FromNode->Pins)
				{
					if (P && P->Direction == EGPD_Output && !P->bHidden &&
						P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
						DataOutputs.Add(P);
				}
				if (DataOutputs.Num() == 1)
					FromPinName = DataOutputs[0]->PinName.ToString();
			}

			const bool bFromIsExec = FromPinName.Equals(TEXT("then"), ESearchCase::IgnoreCase) ||
				FromPinName.Equals(TEXT("out"), ESearchCase::IgnoreCase) ||
				FromPinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase);
			if (ToPinName.IsEmpty() && bFromIsExec && ToNode)
			{
				for (UEdGraphPin* P : ToNode->Pins)
				{
					if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						ToPinName = P->PinName.ToString();
						break;
					}
				}
			}
			if (FromPinName.IsEmpty() && !ToPinName.IsEmpty() && FromNode)
			{
				bool bWantExecSource = (ToPinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
				                        ToPinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase) ||
				                        ToPinName.Equals(TEXT("then"), ESearchCase::IgnoreCase));
				if (bWantExecSource)
				{
					for (UEdGraphPin* P : FromNode->Pins)
					{
						if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{
							FromPinName = P->PinName.ToString();
							break;
						}
					}
				}
			}

			bool bExecToPureDropped = false;
			if (ToNode)
			{
				bool bTargetHasExecInput = false;
				for (UEdGraphPin* P : ToNode->Pins)
				{
					if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{ bTargetHasExecInput = true; break; }
				}
				if (!bTargetHasExecInput)
				{
					const bool bAToPinIsExecName =
						ToPinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
						ToPinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase);
					bool bFromPinIsExec = false;
					if (!bAToPinIsExecName && FromNode && !FromPinName.IsEmpty())
					{
						if (UEdGraphPin* MaybeExecOut = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Output))
						{
							if (MaybeExecOut->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
								bFromPinIsExec = true;
						}
					}
					if (bAToPinIsExecName || bFromPinIsExec)
					{
						const FString PureToNodeId = ToNodeId;
						UEdGraphNode* RedirectTarget = nullptr;
						FString RedirectTargetId;

						if (ConnectionsArray)
						{
							for (int32 ScanIdx = 0; ScanIdx < ConnectionsArray->Num() && !RedirectTarget; ScanIdx++)
							{
								TSharedPtr<FJsonObject> SC = SafeAsObject((*ConnectionsArray)[ScanIdx]);
								if (!SC.IsValid()) continue;

								FString ScanFrom, ScanTo;
								SC->TryGetStringField(TEXT("from_node"), ScanFrom);
								if (ScanFrom.IsEmpty()) SC->TryGetStringField(TEXT("from"), ScanFrom);
								SC->TryGetStringField(TEXT("to_node"), ScanTo);
								if (ScanTo.IsEmpty()) SC->TryGetStringField(TEXT("to"), ScanTo);

								auto StripSuffix = [&](FString& Id) {
									if (Id.Contains(TEXT(".")) && !IsHandlePrefix(Id))
									{ int32 D; if (Id.FindChar(TEXT('.'), D)) Id = Id.Left(D); }
								};
								StripSuffix(ScanFrom);
								StripSuffix(ScanTo);

								if (!ScanFrom.Equals(PureToNodeId, ESearchCase::IgnoreCase)) continue;
								if (ScanTo.IsEmpty() || ScanTo.Equals(PureToNodeId, ESearchCase::IgnoreCase)) continue;
								if (ScanTo.Equals(FromNodeId, ESearchCase::IgnoreCase)) continue;

								UEdGraphNode* ScanToNode = LocalIdMap.Contains(ScanTo) ? LocalIdMap[ScanTo] : nullptr;
								if (!ScanToNode) continue;

								for (UEdGraphPin* P : ScanToNode->Pins)
								{
									if (P && P->Direction == EGPD_Input
										&& P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
										&& P->LinkedTo.Num() == 0)
									{
										RedirectTarget = ScanToNode;
										RedirectTargetId = ScanTo;
										break;
									}
								}
							}
						}

						TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
						if (RedirectTarget)
						{
							Repair->SetStringField(TEXT("repair"), TEXT("exec_to_pure_node_redirected"));
							Repair->SetStringField(TEXT("from_node"), FromNodeId);
							Repair->SetStringField(TEXT("to_node"), PureToNodeId);
							Repair->SetStringField(TEXT("redirected_to"), RedirectTargetId);
							Repair->SetStringField(TEXT("reason"), FString::Printf(
								TEXT("Exec wire %s.%s -> %s (PURE — no exec input) auto-redirected to %s.execute "
								     "(first impure downstream consumer of %s). "
								     "Wire exec to impure nodes directly — pure nodes compute on-demand."),
								*FromNodeId, *FromPinName, *PureToNodeId, *RedirectTargetId, *PureToNodeId));
							RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));

							ToNode = RedirectTarget;
							ToNodeId = RedirectTargetId;
							ToPinName = TEXT("execute");
						}
						else
						{
							Repair->SetStringField(TEXT("repair"), TEXT("exec_to_pure_node_dropped"));
							Repair->SetStringField(TEXT("from_node"), FromNodeId);
							Repair->SetStringField(TEXT("to_node"), ToNodeId);
							Repair->SetStringField(TEXT("reason"), FString::Printf(
								TEXT("Dropped exec wire %s.%s -> %s — target node is PURE (no exec input). "
								     "Pure nodes (var.get, Map_Find, getters, break struct, math, fn.Self pure funcs) "
								     "compute on-demand when their output is read. Wire exec directly to the next IMPURE node."),
								*FromNodeId, *FromPinName, *ToNodeId));
							RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
							ConnResult->SetBoolField(TEXT("success"), true);
							ConnResult->SetStringField(TEXT("status"), TEXT("exec_to_pure_node_dropped"));
							ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
							bExecToPureDropped = true;
						}
					}
				}
			}
			if (bExecToPureDropped) continue;

			if (FromNode && (FromPinName.Equals(TEXT("then"), ESearchCase::IgnoreCase)
			                 || FromPinName.Equals(TEXT("out"), ESearchCase::IgnoreCase)))
			{
				bool bSourceHasExecOutput = false;
				for (UEdGraphPin* P : FromNode->Pins)
				{
					if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{ bSourceHasExecOutput = true; break; }
				}
				if (!bSourceHasExecOutput)
				{
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("pure_node_data_to_exec_dropped"));
					Repair->SetStringField(TEXT("from_node"), FromNodeId);
					Repair->SetStringField(TEXT("to_node"), ToNodeId);
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Dropped wire %s.%s -> %s — source is a PURE node and has no exec output. "
						     "Wire the previous IMPURE node's `then` pin directly to %s, and let the pure "
						     "node's data flow into a separate data input."),
						*FromNodeId, *FromPinName, *ToNodeId, *ToNodeId));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					ConnResult->SetBoolField(TEXT("success"), true);
					ConnResult->SetStringField(TEXT("status"), TEXT("pure_node_data_to_exec_dropped"));
					ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
					continue;
				}
			}

			UEdGraphPin* FromPin = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Output);
			UEdGraphPin* ToPin = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Input);

			auto ReportPinResolution = [&](const FString& Role, const FString& NodeId, const FString& Requested, UEdGraphPin* Resolved)
			{
				if (!Resolved) return;
				const FString Actual = Resolved->PinName.ToString();
				if (Actual.Equals(Requested, ESearchCase::IgnoreCase)) return;
				if (Actual.StartsWith(Requested + TEXT("_"), ESearchCase::CaseSensitive))
				{
					const FString Suffix = Actual.RightChop(Requested.Len() + 1);
					int32 FirstUnderscore = INDEX_NONE;
					if (Suffix.FindChar(TEXT('_'), FirstUnderscore))
					{
						const FString Digits = Suffix.Left(FirstUnderscore);
						const FString Hex = Suffix.RightChop(FirstUnderscore + 1);
						auto IsDecimal = [](const FString& S){ if (S.IsEmpty()) return false; for (TCHAR C : S) if (!FChar::IsDigit(C)) return false; return true; };
						auto IsHex32 = [](const FString& S){ if (S.Len() != 32) return false; for (TCHAR C : S) if (!FChar::IsHexDigit(C)) return false; return true; };
						if (IsDecimal(Digits) && IsHex32(Hex)) return;
					}
				}
				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("pin_name_resolved"));
				Repair->SetStringField(TEXT("node_id"), NodeId);
				Repair->SetStringField(TEXT("role"), Role);
				Repair->SetStringField(TEXT("requested_pin"), Requested);
				Repair->SetStringField(TEXT("actual_pin"), Actual);
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("Pin name '%s' on '%s' (%s) didn't match exactly — fuzzy-resolved to '%s'. "
					     "Use the exact pin name next time to avoid this lookup."),
					*Requested, *NodeId, *Role, *Actual));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
			};
			ReportPinResolution(TEXT("from_pin"), FromNodeId, FromPinName, FromPin);
			ReportPinResolution(TEXT("to_pin"), ToNodeId, ToPinName, ToPin);

			if (!FromPin && FromPinName.Equals(TEXT("ReturnValue"), ESearchCase::IgnoreCase))
			{
				for (UEdGraphPin* P : FromNode->Pins)
				{
					if (P && P->Direction == EGPD_Output && !P->bHidden &&
						P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						FromPin = P;
						break;
					}
				}
			}
			if (!ToPin && ToPinName.Equals(TEXT("ReturnValue"), ESearchCase::IgnoreCase))
			{
				for (UEdGraphPin* P : ToNode->Pins)
				{
					if (P && P->Direction == EGPD_Input && !P->bHidden &&
						P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						ToPin = P;
						break;
					}
				}
			}

			if (!ToPin && ToPinName.Equals(TEXT("Value"), ESearchCase::IgnoreCase))
			{
				for (UEdGraphPin* P : ToNode->Pins)
				{
					if (P && P->Direction == EGPD_Input && !P->bHidden &&
						P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
						!P->PinName.ToString().Equals(TEXT("self"), ESearchCase::IgnoreCase))
					{
						ToPin = P;
						break;
					}
				}
			}

			if (!ToPin && ToPinName.Equals(TEXT("self"), ESearchCase::IgnoreCase))
			{
				if (UEdGraphPin* Alt = GraphEditHelpers::FindPinByName(ToNode, TEXT("Target"), EGPD_Input))
				{
					ToPin = Alt;
				}
			}
			if (!ToPin && ToPinName.Equals(TEXT("Target"), ESearchCase::IgnoreCase))
			{
				if (UEdGraphPin* Alt = GraphEditHelpers::FindPinByName(ToNode, TEXT("self"), EGPD_Input))
				{
					ToPin = Alt;
				}
			}

			if (!ToPin && ToPinName.Equals(TEXT("Toggle"), ESearchCase::IgnoreCase))
			{
				for (UEdGraphPin* P : ToNode->Pins)
				{
					if (P && P->Direction == EGPD_Input &&
						P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						ToPin = P;
						break;
					}
				}
			}

			if (!FromPin && FromPinName.Equals(TEXT("bBlockingHit"), ESearchCase::IgnoreCase))
				FromPin = GraphEditHelpers::FindPinByName(FromNode, TEXT("ReturnValue"), EGPD_Output);

			if (!FromPin || !ToPin)
			{
				UEdGraphPin* AltFrom = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Input);
				UEdGraphPin* AltTo = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Output);
				if (AltFrom && AltTo)
				{
					FromPin = AltTo; ToPin = AltFrom;
				}
			}

			if (!FromPin && ToPin)
			{
				if (UEdGraphPin* TypeHintPin = GraphEditHelpers::FindPinByTypeHint(FromNode, EGPD_Output, ToPin->PinType, ToPin))
				{
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("pin_type_hint_resolved"));
					Repair->SetStringField(TEXT("node_id"), FromNodeId);
					Repair->SetStringField(TEXT("bad_pin"), FromPinName);
					Repair->SetStringField(TEXT("corrected_pin"), TypeHintPin->PinName.ToString());
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Pin '%s' not found on node '%s' — resolved to '%s' as the only type-compatible output."),
						*FromPinName, *FromNodeId, *TypeHintPin->PinName.ToString()));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					FromPin = TypeHintPin;
				}
			}
			if (!ToPin && FromPin)
			{
				if (UEdGraphPin* TypeHintPin = GraphEditHelpers::FindPinByTypeHint(ToNode, EGPD_Input, FromPin->PinType, FromPin))
				{
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("pin_type_hint_resolved"));
					Repair->SetStringField(TEXT("node_id"), ToNodeId);
					Repair->SetStringField(TEXT("bad_pin"), ToPinName);
					Repair->SetStringField(TEXT("corrected_pin"), TypeHintPin->PinName.ToString());
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Pin '%s' not found on node '%s' — resolved to '%s' as the only type-compatible input."),
						*ToPinName, *ToNodeId, *TypeHintPin->PinName.ToString()));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					ToPin = TypeHintPin;
				}
			}

			if (!FromPin || !ToPin)
			{
				ConnResult->SetBoolField(TEXT("success"), false);
				FString ErrMsg;
				if (!FromPin)
				{
					UEdGraphPin* WrongDir = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Input);
					if (WrongDir)
					{
						FString Avail = GraphEditHelpers::GetAvailablePinNames(FromNode, EGPD_Output);
						ErrMsg = FString::Printf(TEXT("Pin '%s' is an INPUT on '%s', not an output. Available outputs: [%s]"), *FromPinName, *FromNodeId, *Avail);
					}
					else
					{
						FString Avail = GraphEditHelpers::GetAvailablePinNames(FromNode, EGPD_Output);
						ErrMsg = FString::Printf(TEXT("Pin '%s' not found on '%s'. Available outputs: [%s]"), *FromPinName, *FromNodeId, *Avail);
					}
				}
				else
				{
					UEdGraphPin* WrongDir = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Output);
					if (WrongDir)
					{
						FString Avail = GraphEditHelpers::GetAvailablePinNames(ToNode, EGPD_Input);
						if (ToPinName.Equals(TEXT("LoopBody"), ESearchCase::IgnoreCase) ||
							ToPinName.Equals(TEXT("Completed"), ESearchCase::IgnoreCase))
						{
							ErrMsg = FString::Printf(TEXT("Pin '%s' is an OUTPUT exec pin on '%s' — you cannot wire something TO it. ")
								TEXT("ForEachLoop fires '%s' automatically once per array element; there is NO 'continue' input. ")
								TEXT("To skip an iteration just leave the conditional branch's else side unconnected — the next iteration triggers automatically. ")
								TEXT("To run logic per-element, wire FROM '%s' (it is an OUTPUT) into your per-element node chain."),
								*ToPinName, *ToNodeId, *ToPinName, *ToPinName);
						}
						else
						{
							ErrMsg = FString::Printf(TEXT("Pin '%s' is an OUTPUT on '%s', not an input. Available inputs: [%s]"), *ToPinName, *ToNodeId, *Avail);
						}
					}
					else
					{
						FString Avail = GraphEditHelpers::GetAvailablePinNames(ToNode, EGPD_Input);
						ErrMsg = FString::Printf(TEXT("Pin '%s' not found on '%s'. Available inputs: [%s]"), *ToPinName, *ToNodeId, *Avail);
					}
				}
				ConnResult->SetStringField(TEXT("error"), ErrMsg);
				ConnFailCount++;
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			if (FromPin && ToPin && FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
				&& ToPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				bool bTargetHasExecInput = false;
				for (UEdGraphPin* P : ToNode->Pins)
				{
					if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						bTargetHasExecInput = true;
						break;
					}
				}
				if (!bTargetHasExecInput)
				{
					TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
					Repair->SetStringField(TEXT("repair"), TEXT("exec_to_pure_node_dropped"));
					Repair->SetStringField(TEXT("from_node"), FromNodeId);
					Repair->SetStringField(TEXT("to_node"), ToNodeId);
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Dropped exec wire %s.%s -> %s.%s — target node is PURE (no exec input, data-flow only). "
						     "Pure nodes like Map_Find / Array_Get / getters / math don't need an exec wire; "
						     "they compute on-demand when their output is read. Route exec directly to the next impure node."),
						*FromNodeId, *FromPin->PinName.ToString(),
						*ToNodeId, *ToPin->PinName.ToString()));
					RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
					ConnResult->SetBoolField(TEXT("success"), true);
					ConnResult->SetStringField(TEXT("status"), TEXT("exec_to_pure_node_dropped"));
					ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
					continue;
				}
			}

			if (FromPin && ToPin && FromPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& ToPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				bool bSourceHasExecOutput = false;
				for (UEdGraphPin* P : FromNode->Pins)
				{
					if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						bSourceHasExecOutput = true;
						break;
					}
				}
				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("data_to_exec_wire_dropped"));
				Repair->SetStringField(TEXT("from_node"), FromNodeId);
				Repair->SetStringField(TEXT("from_pin"), FromPin->PinName.ToString());
				Repair->SetStringField(TEXT("to_node"), ToNodeId);
				Repair->SetStringField(TEXT("to_pin"), ToPin->PinName.ToString());
				if (!bSourceHasExecOutput)
				{
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Dropped %s.%s → %s.%s — source is PURE (no exec output). "
						     "Pure data outputs do NOT carry execution. Wire the previous IMPURE node's "
						     "`then` pin directly to %s.execute instead."),
						*FromNodeId, *FromPin->PinName.ToString(),
						*ToNodeId, *ToPin->PinName.ToString(),
						*ToNodeId));
				}
				else
				{
					Repair->SetStringField(TEXT("reason"), FString::Printf(
						TEXT("Dropped %s.%s → %s.%s — '%s' is a DATA output, not exec. "
						     "Wire data to a data input pin (e.g. Target, Value, Object). "
						     "To continue execution wire %s.then → %s.execute."),
						*FromNodeId, *FromPin->PinName.ToString(),
						*ToNodeId, *ToPin->PinName.ToString(),
						*FromPin->PinName.ToString(),
						*FromNodeId, *ToNodeId));
				}
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
				ConnResult->SetBoolField(TEXT("success"), true);
				ConnResult->SetStringField(TEXT("status"), TEXT("data_to_exec_wire_dropped"));
				ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
				continue;
			}

			FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
			bool bConnected = false;
			switch (Response.Response.GetValue())
			{
			case CONNECT_RESPONSE_MAKE:
			case CONNECT_RESPONSE_BREAK_OTHERS_A:
			case CONNECT_RESPONSE_BREAK_OTHERS_B:
			case CONNECT_RESPONSE_BREAK_OTHERS_AB:
			case CONNECT_RESPONSE_MAKE_WITH_PROMOTION:
			case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
				bConnected = Schema->TryCreateConnection(FromPin, ToPin);
				break;
			default:
				bConnected = TryConnectWithSkelBypass(FromPin, ToPin, Schema, Response);
				break;
			}

			if (!bConnected && FromPin && ToPin
			    && ToPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			{
				const FName FromCat = FromPin->PinType.PinCategory;
				const bool bFromFloat = (FromCat == UEdGraphSchema_K2::PC_Real
				                         || FromCat == UEdGraphSchema_K2::PC_Float
				                         || FromCat == UEdGraphSchema_K2::PC_Double);
				const bool bFromInt = (FromCat == UEdGraphSchema_K2::PC_Int);
				if (bFromFloat || bFromInt)
				{
					const FString CmpHandle = bFromFloat
						? TEXT("fn.KismetMathLibrary.NotEqual_DoubleDouble")
						: TEXT("fn.KismetMathLibrary.NotEqual_IntInt");
					FString CreateErr;
					UEdGraphNode* CmpNode = GraphEditHelpers::CreateNodeFromHandle(
						Blueprint, Graph, CmpHandle,
						FromNode->NodePosX + 200, FromNode->NodePosY + 60,
						FString(), nullptr, CreateErr, &SpawnerCache);
					if (CmpNode)
					{
						UEdGraphPin* CmpA = nullptr, *CmpB = nullptr, *CmpReturn = nullptr;
						for (UEdGraphPin* P : CmpNode->Pins)
						{
							if (!P) continue;
							if (P->Direction == EGPD_Input && P->PinName.ToString().Equals(TEXT("A"), ESearchCase::IgnoreCase)) CmpA = P;
							else if (P->Direction == EGPD_Input && P->PinName.ToString().Equals(TEXT("B"), ESearchCase::IgnoreCase)) CmpB = P;
							else if (P->Direction == EGPD_Output && P->PinName.ToString().Equals(TEXT("ReturnValue"), ESearchCase::IgnoreCase)) CmpReturn = P;
						}
						if (CmpA && CmpB && CmpReturn)
						{
							FromPin->MakeLinkTo(CmpA);
							const UEdGraphSchema* Sch2 = Graph->GetSchema();
							if (Sch2) Sch2->TrySetDefaultValue(*CmpB, bFromFloat ? TEXT("0.0") : TEXT("0"));
							else CmpB->DefaultValue = bFromFloat ? TEXT("0.0") : TEXT("0");
							CmpReturn->MakeLinkTo(ToPin);
							bConnected = true;
							TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
							Repair->SetStringField(TEXT("repair"), TEXT("numeric_to_bool_coerced"));
							Repair->SetStringField(TEXT("from_node"), FromNodeId);
							Repair->SetStringField(TEXT("to_node"), ToNodeId);
							Repair->SetStringField(TEXT("reason"), FString::Printf(
								TEXT("Auto-inserted %s(val, 0) between %s.%s (%s) and %s.%s (bool). "
								     "Non-zero values now branch true. If you wanted a different threshold (e.g. > max), "
								     "replace this with an explicit comparison node."),
								bFromFloat ? TEXT("NotEqual_DoubleDouble") : TEXT("NotEqual_IntInt"),
								*FromNodeId, *FromPin->PinName.ToString(), *FromCat.ToString(),
								*ToNodeId, *ToPin->PinName.ToString()));
							RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
						}
					}
				}
			}

			if (!bConnected && FromPin && ToPin
			    && FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
			    && ToPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				UClass* FromClass = Cast<UClass>(FromPin->PinType.PinSubCategoryObject.Get());
				const bool bIsSelfPin = FromPin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self;
				if (!FromClass && bIsSelfPin && Blueprint)
				{
					FromClass = Blueprint->GeneratedClass
						? Blueprint->GeneratedClass
						: Blueprint->ParentClass;
				}
				const bool bFromActor = FromClass && FromClass->IsChildOf(AActor::StaticClass());
				if (bFromActor)
				{
					UScriptStruct* ToStruct = Cast<UScriptStruct>(ToPin->PinType.PinSubCategoryObject.Get());
					const FName ToStructName = ToStruct ? ToStruct->GetFName() : NAME_None;

					FString GetterHandle;
					FString GetterName;
					if (ToStructName == NAME_Vector || ToStructName == TEXT("Vector3f"))
					{
						GetterHandle = TEXT("fn.Actor.K2_GetActorLocation");
						GetterName = TEXT("GetActorLocation");
					}
					else if (ToStructName == NAME_Rotator)
					{
						GetterHandle = TEXT("fn.Actor.K2_GetActorRotation");
						GetterName = TEXT("GetActorRotation");
					}
					else if (ToStructName == NAME_Transform)
					{
						GetterHandle = TEXT("fn.Actor.GetActorTransform");
						GetterName = TEXT("GetActorTransform");
					}

					if (!GetterHandle.IsEmpty())
					{
						FString CreateErr;
						UEdGraphNode* GetterNode = GraphEditHelpers::CreateNodeFromHandle(
							Blueprint, Graph, GetterHandle,
							FromNode->NodePosX + 200, FromNode->NodePosY + 60,
							FString(), nullptr, CreateErr, &SpawnerCache);
						if (GetterNode)
						{
							UEdGraphPin* SelfIn = nullptr;
							UEdGraphPin* GetterReturn = nullptr;
							for (UEdGraphPin* P : GetterNode->Pins)
							{
								if (!P) continue;
								if (P->Direction == EGPD_Input && P->PinName.ToString().Equals(TEXT("self"), ESearchCase::IgnoreCase))
									SelfIn = P;
								else if (P->Direction == EGPD_Output && P->PinName.ToString().Equals(TEXT("ReturnValue"), ESearchCase::IgnoreCase))
									GetterReturn = P;
							}
							if (SelfIn && GetterReturn)
							{
								FromPin->MakeLinkTo(SelfIn);
								GetterReturn->MakeLinkTo(ToPin);
								bConnected = true;
								TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
								Repair->SetStringField(TEXT("repair"), TEXT("actor_to_struct_coerced"));
								Repair->SetStringField(TEXT("from_node"), FromNodeId);
								Repair->SetStringField(TEXT("to_node"), ToNodeId);
								Repair->SetStringField(TEXT("inserted_node"), GetterName);
								Repair->SetStringField(TEXT("reason"), FString::Printf(
									TEXT("Auto-inserted %s between %s.%s (Actor ref) and %s.%s (%s). "
									     "Actor references do not auto-promote to Vector/Rotator/Transform — you must call the "
									     "matching getter. Use %s explicitly next time to avoid this repair."),
									*GetterName,
									*FromNodeId, *FromPin->PinName.ToString(),
									*ToNodeId, *ToPin->PinName.ToString(),
									*ToStructName.ToString(),
									*GetterName));
								RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
							}
						}
					}
				}
			}

			ConnResult->SetBoolField(TEXT("success"), bConnected);
			if (bConnected)
			{
				ConnResult->SetStringField(TEXT("status"), TEXT("connected"));
				ConnSuccessCount++;
			}
			else
			{
				FString ConnErr = FString::Printf(TEXT("Cannot connect: %s"), *Response.Message.ToString());

				if (FromPin && ToPin
					&& FromPin->PinType.IsMap()
					&& ToPin->PinType.IsArray()
					&& ToNode && ToNode->IsA<UK2Node_MacroInstance>())
				{
					const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(ToNode);
					const FString MacroName = MacroNode && MacroNode->GetMacroGraph()
						? MacroNode->GetMacroGraph()->GetName() : FString();
					if (MacroName.Contains(TEXT("ForEach")))
					{
						ConnErr += FString::Printf(
							TEXT(" ForEachLoop iterates ARRAYS, not Maps. Pattern: place 'fn.BlueprintMapLibrary.Map_Keys' "
							     "(impure — needs exec), wire %s -> mapKeys.TargetMap (data) AND prevImpure.then -> mapKeys.execute (exec), "
							     "then mapKeys.Keys -> %s.Array. Inside the loop body use mapKeys.Keys via Array Element to look the value back up with Map_Find."),
							*FromNodeId, *ToNodeId);
					}
				}

				if (FromPin && ToPin
					&& FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
					&& ToPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
				{
					UClass* FromClass = Cast<UClass>(FromPin->PinType.PinSubCategoryObject.Get());
					UClass* ToClass   = Cast<UClass>(ToPin->PinType.PinSubCategoryObject.Get());
					if (FromClass && ToClass && FromClass != ToClass)
					{
						const bool bUpcast   = FromClass->IsChildOf(ToClass);
						const bool bDowncast = ToClass->IsChildOf(FromClass);
						if (bUpcast || bDowncast)
						{
							UClass* TargetClass = bDowncast ? ToClass : FromClass;
							const FString TargetName = TargetClass->GetName();

							const bool bUserBP = TargetClass->ClassGeneratedBy != nullptr;
							if (bUserBP)
							{
								ConnErr += FString::Printf(
									TEXT(" Type mismatch: source is %s, target wants %s (a USER Blueprint). "
									     "DON'T just insert k2.Cast To %s — hard-loads the target class into every consumer and silently dies on Cast Failed. "
									     "Use one of these instead: "
									     "(1) Create a BlueprintInterface (BPI_X) with the method you need, implement it on %s, then call fn.<BPI>.<Method> polymorphically with the Actor ref as Target — gate with fn.KismetSystemLibrary.DoesImplementInterface; "
									     "(2) If %s exposes a component (BP_StatsComponent etc.) use fn.Actor.GetComponentByClass(ComponentClass=...) and read the component's API; "
									     "(3) If you're signalling, broadcast via an event dispatcher and let %s bind via ev.DispatcherBind.<X> at BeginPlay. "
									     "Cast IS fine for engine base types (Character, PlayerController, GameMode, ActorComponent, UserWidget) — just not for authored BPs."),
									*FromClass->GetName(), *TargetName,
									*TargetName, *TargetName, *TargetName, *TargetName);
							}
							else
							{
								ConnErr += FString::Printf(
									TEXT(" Insert k2.Cast To %s between %s and %s — Object pins on cross-class calls "
									     "need exact type matching. Wire %s -> cast.Object, then cast.As %s -> %s."),
									*TargetName, *FromNodeId, *ToNodeId,
									*FromNodeId, *TargetName, *ToNodeId);
							}
						}
					}
				}

				ConnResult->SetStringField(TEXT("error"), ConnErr);
				ConnFailCount++;
			}
			ConnResults.Add(MakeShared<FJsonValueObject>(ConnResult));
		}
	}

	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node);
			if (!SelectNode) continue;

			UEdGraphPin* IndexPin = SelectNode->GetIndexPin();
			if (!IndexPin || IndexPin->LinkedTo.Num() == 0) continue;

			UEdGraphPin* SourcePin = IndexPin->LinkedTo[0];
			if (!SourcePin) continue;

			UEnum* SourceEnum = nullptr;
			if (SourcePin->PinType.PinSubCategoryObject.IsValid())
				SourceEnum = Cast<UEnum>(SourcePin->PinType.PinSubCategoryObject.Get());

			if (SourceEnum)
			{
				TArray<UEdGraphPin*> OptionPins;
				SelectNode->GetOptionPins(OptionPins);
				int32 CurrentOptions = OptionPins.Num();
				int32 NeededOptions = SourceEnum->NumEnums() - 1;

				if (CurrentOptions < NeededOptions)
				{
					FObjectPropertyBase* EnumProp = FindFProperty<FObjectPropertyBase>(SelectNode->GetClass(), TEXT("Enum"));
					if (EnumProp)
					{
						EnumProp->SetObjectPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(SelectNode), SourceEnum);
					}
					FIntProperty* NumProp = FindFProperty<FIntProperty>(SelectNode->GetClass(), TEXT("NumOptionPins"));
					if (NumProp)
					{
						NumProp->SetPropertyValue(NumProp->ContainerPtrToValuePtr<void>(SelectNode), NeededOptions);
					}
					SelectNode->ReconstructNode();
				}
			}
		}
	}

	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !IsValid(Node)) continue;
			bool bChanged = false;

			FEdGraphPinType ResolvedScalarType;
			bool bHasResolvedType = false;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard) continue;
				if (Pin->LinkedTo.Num() == 0) continue;
				UEdGraphPin* SourcePin = Pin->LinkedTo[0];
				if (!SourcePin || SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard) continue;

				EPinContainerType OrigContainer = Pin->PinType.ContainerType;
				Pin->PinType = SourcePin->PinType;
				if (OrigContainer == EPinContainerType::Array && SourcePin->PinType.ContainerType == EPinContainerType::None)
					Pin->PinType.ContainerType = EPinContainerType::Array;
				bChanged = true;

				if (!bHasResolvedType)
				{
					ResolvedScalarType = SourcePin->PinType;
					if (ResolvedScalarType.ContainerType == EPinContainerType::Array)
						ResolvedScalarType.ContainerType = EPinContainerType::None;
					bHasResolvedType = true;
				}
			}

			if (bHasResolvedType)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard) continue;
					if (Pin->LinkedTo.Num() > 0) continue;
					if (Pin->PinType.ContainerType == EPinContainerType::Array)
					{
						Pin->PinType = ResolvedScalarType;
						Pin->PinType.ContainerType = EPinContainerType::Array;
					}
					else
					{
						Pin->PinType = ResolvedScalarType;
					}
					bChanged = true;
				}
			}

			if (bChanged)
				Node->NodeConnectionListChanged();
		}
	}

	if (FuncEntryNode)
	{
		UEdGraphPin* EntryExecOut = nullptr;
		for (UEdGraphPin* P : FuncEntryNode->Pins)
		{
			if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{ EntryExecOut = P; break; }
		}
		if (EntryExecOut && EntryExecOut->LinkedTo.Num() == 0)
		{
			for (UEdGraphNode* N : NewNodes)
			{
				if (!N) continue;
				UEdGraphPin* ExecIn = nullptr;
				for (UEdGraphPin* P : N->Pins)
				{
					if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{ ExecIn = P; break; }
				}
				if (ExecIn)
				{
					EntryExecOut->MakeLinkTo(ExecIn);
					break;
				}
			}
		}
	}

	if (FuncResultNode && FuncResultNode->UserDefinedPins.Num() > 0)
	{
		UEdGraphPin* ReturnExecIn = nullptr;
		for (UEdGraphPin* P : FuncResultNode->Pins)
		{
			if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{ ReturnExecIn = P; break; }
		}
		if (ReturnExecIn && ReturnExecIn->LinkedTo.Num() == 0)
		{
			UEdGraphPin* BestExecOut = nullptr;
			for (int32 Ni = NewNodes.Num() - 1; Ni >= 0; --Ni)
			{
				UEdGraphNode* N = NewNodes[Ni];
				if (!N || N == FuncResultNode) continue;

				int32 ExecOutCount = 0;
				UEdGraphPin* CandidateThen = nullptr;
				for (UEdGraphPin* P : N->Pins)
				{
					if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						++ExecOutCount;
						if (P->LinkedTo.Num() == 0 && P->PinName.ToString() == TEXT("then"))
							CandidateThen = P;
					}
				}
				if (ExecOutCount == 1 && CandidateThen)
				{
					BestExecOut = CandidateThen;
					break;
				}
			}
			if (BestExecOut)
			{
				BestExecOut->MakeLinkTo(ReturnExecIn);
			}
		}
	}

	if (FuncResultNode)
	{
		UEdGraphPin* ReturnExecIn = nullptr;
		for (UEdGraphPin* P : FuncResultNode->Pins)
		{
			if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{ ReturnExecIn = P; break; }
		}
		if (ReturnExecIn && ReturnExecIn->LinkedTo.Num() > 0)
		{
			auto FindNodeExecPin = [](UEdGraphNode* N, EEdGraphPinDirection Dir) -> UEdGraphPin*
			{
				for (UEdGraphPin* P : N->Pins)
				{
					if (P && P->Direction == Dir && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
						&& P->PinName.ToString() == (Dir == EGPD_Output ? TEXT("then") : TEXT("execute")))
					{ return P; }
				}
				for (UEdGraphPin* P : N->Pins)
				{
					if (P && P->Direction == Dir && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{ return P; }
				}
				return nullptr;
			};

			auto IsStrandedImpure = [&](UEdGraphNode* N) -> bool
			{
				if (!N || N == FuncResultNode || N == FuncEntryNode) return false;
				if (N->IsA(UK2Node_FunctionResult::StaticClass())) return false;
				if (N->IsA(UK2Node_FunctionEntry::StaticClass())) return false;
				UEdGraphPin* ExecIn  = FindNodeExecPin(N, EGPD_Input);
				UEdGraphPin* ExecOut = FindNodeExecPin(N, EGPD_Output);
				if (!ExecIn || !ExecOut) return false;
				if (ExecIn->LinkedTo.Num() > 0) return false;
				bool bHasWiredDataIn = false;
				for (UEdGraphPin* P : N->Pins)
				{
					if (!P || P->Direction != EGPD_Input) continue;
					if (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
					if (P->LinkedTo.Num() > 0) { bHasWiredDataIn = true; break; }
				}
				if (!bHasWiredDataIn) return false;
				return true;
			};

			TArray<UEdGraphNode*> Strands;
			for (UEdGraphNode* N : NewNodes)
			{
				if (IsStrandedImpure(N)) Strands.Add(N);
			}

			auto BuildExecOrder = [&]() -> TArray<UEdGraphNode*>
			{
				TArray<UEdGraphNode*> Order;
				TSet<UEdGraphNode*> Visited;
				TArray<UEdGraphNode*> Stack;
				if (FuncEntryNode) Stack.Add(FuncEntryNode);
				while (Stack.Num() > 0)
				{
					UEdGraphNode* N = Stack.Pop(EAllowShrinking::No);
					if (!N || Visited.Contains(N)) continue;
					Visited.Add(N);
					Order.Add(N);
					for (UEdGraphPin* P : N->Pins)
					{
						if (!P || P->Direction != EGPD_Output) continue;
						if (P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
						for (UEdGraphPin* Linked : P->LinkedTo)
						{
							if (Linked)
							{
								if (UEdGraphNode* Next = Linked->GetOwningNodeUnchecked())
									Stack.Push(Next);
							}
						}
					}
				}
				return Order;
			};

			auto FindEarliestImpureConsumer = [&](UEdGraphNode* StrandNode, const TArray<UEdGraphNode*>& ExecOrder) -> UEdGraphNode*
			{
				TSet<UEdGraphNode*> ImpureConsumers;
				TSet<UEdGraphNode*> Visited;
				TArray<UEdGraphNode*> Queue;
				for (UEdGraphPin* OutPin : StrandNode->Pins)
				{
					if (!OutPin || OutPin->Direction != EGPD_Output) continue;
					if (OutPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
					for (UEdGraphPin* Linked : OutPin->LinkedTo)
					{
						if (!Linked) continue;
						if (UEdGraphNode* Consumer = Linked->GetOwningNodeUnchecked())
							Queue.Add(Consumer);
					}
				}
				while (Queue.Num() > 0)
				{
					UEdGraphNode* N = Queue.Pop(EAllowShrinking::No);
					if (!N || Visited.Contains(N)) continue;
					Visited.Add(N);
					bool bIsImpure = false;
					for (UEdGraphPin* P : N->Pins)
					{
						if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{ bIsImpure = true; break; }
					}
					if (bIsImpure)
					{
						ImpureConsumers.Add(N);
						continue;
					}
					for (UEdGraphPin* OutPin : N->Pins)
					{
						if (!OutPin || OutPin->Direction != EGPD_Output) continue;
						for (UEdGraphPin* Linked : OutPin->LinkedTo)
						{
							if (!Linked) continue;
							if (UEdGraphNode* Next = Linked->GetOwningNodeUnchecked())
								Queue.Add(Next);
						}
					}
				}
				UEdGraphNode* Earliest = nullptr;
				int32 EarliestIdx = MAX_int32;
				for (UEdGraphNode* C : ImpureConsumers)
				{
					const int32 Idx = ExecOrder.IndexOfByKey(C);
					if (Idx != INDEX_NONE && Idx < EarliestIdx)
					{
						EarliestIdx = Idx;
						Earliest = C;
					}
				}
				return Earliest;
			};

			const TArray<UEdGraphNode*> ExecOrder = BuildExecOrder();

			for (UEdGraphNode* Strand : Strands)
			{
				UEdGraphPin* StrandIn = FindNodeExecPin(Strand, EGPD_Input);
				UEdGraphPin* StrandOut = FindNodeExecPin(Strand, EGPD_Output);
				if (!StrandIn || !StrandOut) continue;
				if (StrandOut->LinkedTo.Num() > 0) continue;

				UEdGraphNode* TargetConsumer = FindEarliestImpureConsumer(Strand, ExecOrder);
				FString InsertionReason;
				bool bSpliced = false;
				if (TargetConsumer)
				{
					UEdGraphPin* ConsumerExecIn = FindNodeExecPin(TargetConsumer, EGPD_Input);
					if (ConsumerExecIn && ConsumerExecIn->LinkedTo.Num() > 0)
					{
						UEdGraphPin* UpstreamOut = ConsumerExecIn->LinkedTo[0];
						if (UpstreamOut)
						{
							UpstreamOut->BreakLinkTo(ConsumerExecIn);
							UpstreamOut->MakeLinkTo(StrandIn);
							StrandOut->MakeLinkTo(ConsumerExecIn);
							bSpliced = true;
							InsertionReason = FString::Printf(
								TEXT("Spliced before '%s' (earliest impure consumer of its data output)."),
								*TargetConsumer->GetName());
						}
					}
				}
				if (!bSpliced)
				{
					if (ReturnExecIn->LinkedTo.Num() == 0) break;
					UEdGraphPin* TailOut = ReturnExecIn->LinkedTo[0];
					if (!TailOut) break;
					TailOut->BreakLinkTo(ReturnExecIn);
					TailOut->MakeLinkTo(StrandIn);
					StrandOut->MakeLinkTo(ReturnExecIn);
					InsertionReason = TEXT("Spliced just before return (no impure consumer of its output found).");
				}

				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("stranded_impure_inserted"));
				Repair->SetStringField(TEXT("inserted_node"), Strand->GetName());
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("Impure node '%s' had wired data inputs but a free exec input. %s "
					     "Wire its execute explicitly next time to control ordering."),
					*Strand->GetName(), *InsertionReason));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
			}
		}
	}

	{
		auto FindOrPlaceSelfNode = [&]() -> UK2Node_Self*
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (UK2Node_Self* SN = Cast<UK2Node_Self>(N)) return SN;
			}
			UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
			SelfNode->SetFlags(RF_Transactional);
			Graph->AddNode(SelfNode, false, false);
			SelfNode->AllocateDefaultPins();
			NewNodes.Add(SelfNode);
			return SelfNode;
		};

		for (UEdGraphNode* Node : NewNodes)
		{
			if (!IsValid(Node)) continue;
			UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
			if (!CallNode) continue;
			const UFunction* Func = CallNode->GetTargetFunction();
			if (!Func) continue;
			const UClass* OwnerClass = Func->GetOwnerClass();
			if (!OwnerClass) continue;

			const FString NodeHandle = FString::Printf(TEXT("fn.%s.%s"), *OwnerClass->GetName(), *Func->GetName());
			const TArray<BpHandleKnowledge::FCompanionRule> Rules = BpHandleKnowledge::GetCompanionRules(NodeHandle);
			if (Rules.Num() == 0) continue;

			for (const BpHandleKnowledge::FCompanionRule& Rule : Rules)
			{
				UEdGraphPin* RequiredPin = GraphEditHelpers::FindPinByName(Node, Rule.RequiredPinName, EGPD_Input);
				if (!RequiredPin || RequiredPin->LinkedTo.Num() > 0) continue;

				UEdGraphPin* SourcePin = nullptr;
				if (Rule.DefaultSourceHandle.Equals(TEXT("k2.Self"), ESearchCase::IgnoreCase))
				{
					UK2Node_Self* SelfNode = FindOrPlaceSelfNode();
					if (!SelfNode) continue;
					SourcePin = GraphEditHelpers::FindPinByName(SelfNode, Rule.DefaultSourcePin, EGPD_Output);
					if (!SourcePin)
					{
						for (UEdGraphPin* P : SelfNode->Pins)
						{
							if (P && P->Direction == EGPD_Output) { SourcePin = P; break; }
						}
					}
				}
				if (!SourcePin) continue;
				SourcePin->MakeLinkTo(RequiredPin);

				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("companion_rule_fired"));
				Repair->SetStringField(TEXT("handle"), NodeHandle);
				Repair->SetStringField(TEXT("required_pin"), Rule.RequiredPinName);
				Repair->SetStringField(TEXT("auto_wired_from"),
					FString::Printf(TEXT("%s.%s"), *Rule.DefaultSourceHandle, *Rule.DefaultSourcePin));
				Repair->SetStringField(TEXT("reason"), Rule.Reason);
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
			}
		}
	}

	{
		UClass* SelfClass = nullptr;
		if (UBlueprint* BPForSelf = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
		{
			SelfClass = BPForSelf->SkeletonGeneratedClass
				? BPForSelf->SkeletonGeneratedClass
				: BPForSelf->GeneratedClass;
		}
		auto IsExternalMember = [SelfClass](const FMemberReference& Ref) -> bool
		{
			UClass* Owner = Ref.GetMemberParentClass();
			if (!Owner || Ref.IsSelfContext()) return false;
			if (SelfClass && SelfClass->IsChildOf(Owner)) return false;
			return true;
		};

		if (SelfClass)
		{
			for (UEdGraphNode* N : NewNodes)
			{
				if (!IsValid(N)) continue;
				FMemberReference* Ref = nullptr;
				FString VarNameForLog;
				if (UK2Node_VariableSet* SetN = Cast<UK2Node_VariableSet>(N))
				{
					Ref = &SetN->VariableReference;
					VarNameForLog = Ref->GetMemberName().ToString();
				}
				else if (UK2Node_VariableGet* GetN = Cast<UK2Node_VariableGet>(N))
				{
					Ref = &GetN->VariableReference;
					VarNameForLog = Ref->GetMemberName().ToString();
				}
				if (!Ref) continue;
				if (Ref->IsSelfContext()) continue;
				UClass* Owner = Ref->GetMemberParentClass();
				if (!Owner) continue;
				if (!SelfClass->IsChildOf(Owner)) continue;

				Ref->SetSelfMember(Ref->GetMemberName());
				if (UK2Node* K2N = Cast<UK2Node>(N))
				{
					K2N->ReconstructNode();
				}

				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("inherited_var_promoted_to_self"));
				Repair->SetStringField(TEXT("variable"), VarNameForLog);
				Repair->SetStringField(TEXT("owning_class"), Owner->GetName());
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("Variable '%s' (owned by '%s') is reachable via inheritance on this Blueprint. "
					     "Flipped the reference to self-context so the Target pin doesn't need an explicit wire. "
					     "Use the bare var.get.%s / var.set.%s handle (without the owning-class prefix) "
					     "to skip this repair on future calls."),
					*VarNameForLog, *Owner->GetName(), *VarNameForLog, *VarNameForLog));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
			}
		}

		auto FindUniqueCastResult = [Graph](UClass* TargetType) -> UEdGraphPin*
		{
			if (!TargetType) return nullptr;
			UEdGraphPin* MatchPin = nullptr;
			int32 Count = 0;
			for (UEdGraphNode* N : Graph->Nodes)
			{
				UK2Node_DynamicCast* Cast = ::Cast<UK2Node_DynamicCast>(N);
				if (!Cast || Cast->TargetType != TargetType) continue;
				UEdGraphPin* AsPin = Cast->GetCastResultPin();
				if (AsPin)
				{
					MatchPin = AsPin;
					Count++;
					if (Count > 1) return nullptr;
				}
			}
			return Count == 1 ? MatchPin : nullptr;
		};

		auto FindTargetPin = [](UEdGraphNode* N) -> UEdGraphPin*
		{
			for (UEdGraphPin* P : N->Pins)
			{
				if (P && P->Direction == EGPD_Input
				 && P->PinName == UEdGraphSchema_K2::PSC_Self)
				{
					return P;
				}
			}
			return nullptr;
		};

		for (UEdGraphNode* N : NewNodes)
		{
			if (!IsValid(N)) continue;
			UClass* OwnerClass = nullptr;
			FString VarNameForLog;

			if (UK2Node_VariableSet* SetN = Cast<UK2Node_VariableSet>(N))
			{
				if (!IsExternalMember(SetN->VariableReference)) continue;
				OwnerClass = SetN->VariableReference.GetMemberParentClass();
				VarNameForLog = SetN->VariableReference.GetMemberName().ToString();
			}
			else if (UK2Node_VariableGet* GetN = Cast<UK2Node_VariableGet>(N))
			{
				if (!IsExternalMember(GetN->VariableReference)) continue;
				OwnerClass = GetN->VariableReference.GetMemberParentClass();
				VarNameForLog = GetN->VariableReference.GetMemberName().ToString();
			}
			else continue;

			if (!OwnerClass) continue;
			UEdGraphPin* TargetPin = FindTargetPin(N);
			if (!TargetPin || TargetPin->LinkedTo.Num() > 0) continue;

			UEdGraphPin* CastResult = FindUniqueCastResult(OwnerClass);
			if (!CastResult) continue;

			TargetPin->MakeLinkTo(CastResult);
			TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
			Repair->SetStringField(TEXT("repair"), TEXT("cross_bp_target_auto_wired"));
			Repair->SetStringField(TEXT("variable"), VarNameForLog);
			Repair->SetStringField(TEXT("owning_class"), OwnerClass->GetName());
			Repair->SetStringField(TEXT("auto_wired_from"),
				FString::Printf(TEXT("%s.%s"), *CastResult->GetOwningNode()->GetName(), *CastResult->PinName.ToString()));
			Repair->SetStringField(TEXT("reason"), FString::Printf(
				TEXT("Variable '%s' (owned by '%s') has an explicit Target pin that was unwired — "
				     "auto-wired to the unique Cast To %s result in this graph. "
				     "If you intended a different target, wire it explicitly to avoid this auto-fix."),
				*VarNameForLog, *OwnerClass->GetName(), *OwnerClass->GetName()));
			RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
		}
	}

	{
		auto FindExecPin = [](UEdGraphNode* N, EEdGraphPinDirection Dir) -> UEdGraphPin*
		{
			for (UEdGraphPin* P : N->Pins)
			{
				if (P && P->Direction == Dir
				 && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					return P;
				}
			}
			return nullptr;
		};

		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			int32 ChangedThisPass = 0;
			for (UEdGraphNode* Consumer : NewNodes)
			{
				if (!IsValid(Consumer)) continue;
				if (Consumer->IsA(UK2Node_FunctionResult::StaticClass())) continue;
				if (Consumer->IsA(UK2Node_FunctionEntry::StaticClass())) continue;

				UEdGraphPin* ExecIn = FindExecPin(Consumer, EGPD_Input);
				if (!ExecIn || ExecIn->LinkedTo.Num() > 0) continue;

				UEdGraphNode* ChosenProducer = nullptr;
				for (UEdGraphPin* InP : Consumer->Pins)
				{
					if (!InP || InP->Direction != EGPD_Input) continue;
					if (InP->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
					if (InP->LinkedTo.Num() == 0) continue;
					UEdGraphPin* SrcPin = InP->LinkedTo[0];
					if (!SrcPin) continue;
					UEdGraphNode* Producer = SrcPin->GetOwningNode();
					if (!Producer || Producer == Consumer) continue;
					UEdGraphPin* ProducerExecOut = FindExecPin(Producer, EGPD_Output);
					if (!ProducerExecOut || ProducerExecOut->LinkedTo.Num() > 0) continue;
					if (!FindExecPin(Producer, EGPD_Input)) continue;
					if (ChosenProducer && ChosenProducer != Producer)
					{
						ChosenProducer = nullptr;
						break;
					}
					ChosenProducer = Producer;
				}
				if (!ChosenProducer) continue;

				UEdGraphPin* ProducerExecOut = FindExecPin(ChosenProducer, EGPD_Output);
				if (!ProducerExecOut || ProducerExecOut->LinkedTo.Num() > 0) continue;
				ProducerExecOut->MakeLinkTo(ExecIn);
				ChangedThisPass++;

				TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
				Repair->SetStringField(TEXT("repair"), TEXT("dangling_exec_auto_wired"));
				Repair->SetStringField(TEXT("from_node"), ChosenProducer->GetName());
				Repair->SetStringField(TEXT("to_node"), Consumer->GetName());
				Repair->SetStringField(TEXT("reason"), FString::Printf(
					TEXT("'%s' has a free exec OUTPUT and data-feeds '%s' which has a free exec INPUT — "
					     "auto-wired the exec to complete the chain. Pure data wires alone do NOT carry execution; "
					     "every impure consumer needs an exec wire from somewhere upstream."),
					*ChosenProducer->GetName(), *Consumer->GetName()));
				RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));
			}
			if (ChangedThisPass == 0) break;
		}
	}

	if (DefaultsArray && DefaultsArray->Num() > 0)
	{
		for (int32 i = 0; i < DefaultsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Def = SafeAsObject((*DefaultsArray)[i]);
			if (!Def.IsValid()) continue;

			FString NodeId, PinName, Value;
			if (!Def->TryGetStringField(TEXT("node_id"), NodeId)) Def->TryGetStringField(TEXT("node"), NodeId);
			if (!Def->TryGetStringField(TEXT("pin_name"), PinName)) Def->TryGetStringField(TEXT("pin"), PinName);
			Def->TryGetStringField(TEXT("value"), Value);
			if (Value.IsEmpty()) continue;

			if (PinName.IsEmpty() && !NodeId.IsEmpty())
			{
				const bool bLooksLikeHandle =
					NodeId.StartsWith(TEXT("var.")) || NodeId.StartsWith(TEXT("fn.")) ||
					NodeId.StartsWith(TEXT("k2.")) || NodeId.StartsWith(TEXT("ev.")) ||
					NodeId.StartsWith(TEXT("cast.")) || NodeId.StartsWith(TEXT("entry.")) ||
					NodeId.StartsWith(TEXT("return.")) || NodeId.StartsWith(TEXT("self."));
				int32 LastDot = INDEX_NONE;
				NodeId.FindLastChar(TEXT('.'), LastDot);
				if (!bLooksLikeHandle && LastDot != INDEX_NONE && LastDot > 0 && LastDot < NodeId.Len() - 1)
				{
					PinName = NodeId.Mid(LastDot + 1);
					NodeId = NodeId.Left(LastDot);
				}
			}

			UEdGraphNode* Node = LocalIdMap.Contains(NodeId) ? LocalIdMap[NodeId] : GraphEditHelpers::FindNodeInGraph(Graph, NodeId);
			if (!Node) continue;
			UEdGraphPin* Pin = GraphEditHelpers::FindPinByName(Node, PinName, EGPD_Input);
			if (!Pin) continue;

			if (Pin->DefaultValue != Value)
			{
				FString ResolvedDefValue = Value;
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && Pin->PinType.PinSubCategoryObject.IsValid())
				{
					if (UEnum* PinEnum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
					{
						for (int32 Ei = 0; Ei < PinEnum->NumEnums() - 1; ++Ei)
						{
							FString FullName = PinEnum->GetNameStringByIndex(Ei);
							FString DisplayName = PinEnum->GetDisplayNameTextByIndex(Ei).ToString();
							FString ShortName = FullName.Contains(TEXT("::")) ? FullName.RightChop(FullName.Find(TEXT("::")) + 2) : FullName;
							if (Value.Equals(ShortName, ESearchCase::IgnoreCase) ||
								Value.Equals(DisplayName, ESearchCase::IgnoreCase) ||
								Value.Equals(FullName, ESearchCase::IgnoreCase))
							{
								ResolvedDefValue = Cast<UUserDefinedEnum>(PinEnum) ? DisplayName : FullName;
								break;
							}
						}
					}
				}
				Schema->TrySetDefaultValue(*Pin, ResolvedDefValue);
				Node->PinDefaultValueChanged(Pin);
				DefaultSuccessCount++;
				DefaultFailCount = FMath::Max(0, DefaultFailCount - 1);
			}
		}
	}

	{
		TArray<UK2Node_DynamicCast*> RedundantCasts;
		for (const auto& IdPair : LocalIdMap)
		{
			UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(IdPair.Value);
			if (!CastNode) continue;
			UClass* TargetClass = CastNode->TargetType;
			if (!TargetClass) continue;

			UEdGraphPin* ObjectIn = CastNode->FindPin(TEXT("Object"), EGPD_Input);
			if (!ObjectIn || ObjectIn->LinkedTo.Num() != 1) continue;
			UEdGraphPin* SourcePin = ObjectIn->LinkedTo[0];
			if (!SourcePin) continue;

			UClass* SourceClass = Cast<UClass>(SourcePin->PinType.PinSubCategoryObject.Get());
			if (!SourceClass) continue;
			if (!SourceClass->IsChildOf(TargetClass)) continue;

			RedundantCasts.Add(CastNode);
		}

		for (UK2Node_DynamicCast* CastNode : RedundantCasts)
		{
			UClass* TargetClass = CastNode->TargetType;
			if (!TargetClass) continue;
			UEdGraphPin* ObjectIn = CastNode->FindPin(TEXT("Object"), EGPD_Input);
			UEdGraphPin* SourcePin = (ObjectIn && ObjectIn->LinkedTo.Num() > 0) ? ObjectIn->LinkedTo[0] : nullptr;
			if (!SourcePin) continue;

			UEdGraphPin* AsPin = nullptr;
			for (UEdGraphPin* P : CastNode->Pins)
			{
				if (!P || P->Direction != EGPD_Output) continue;
				if (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
				AsPin = P;
				break;
			}

			if (AsPin)
			{
				for (UEdGraphPin* Consumer : TArray<UEdGraphPin*>(AsPin->LinkedTo))
				{
					AsPin->BreakLinkTo(Consumer);
					SourcePin->MakeLinkTo(Consumer);
				}
			}

			UEdGraphPin* CastExecIn = CastNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			UEdGraphPin* CastThenOut = CastNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			if (CastExecIn && CastThenOut && CastExecIn->LinkedTo.Num() == 1)
			{
				UEdGraphPin* UpstreamThen = CastExecIn->LinkedTo[0];
				for (UEdGraphPin* DownstreamExec : TArray<UEdGraphPin*>(CastThenOut->LinkedTo))
				{
					CastThenOut->BreakLinkTo(DownstreamExec);
					UpstreamThen->MakeLinkTo(DownstreamExec);
				}
			}

			TSharedPtr<FJsonObject> Repair = MakeShared<FJsonObject>();
			Repair->SetStringField(TEXT("repair"), TEXT("redundant_cast_stripped"));
			Repair->SetStringField(TEXT("dropped_target"), TargetClass->GetName());
			Repair->SetStringField(TEXT("reason"), FString::Printf(
				TEXT("Cast To %s was redundant — the source pin already produces "
				     "a %s. Skip the cast next time: wire the source directly to "
				     "whatever was reading the As-%s output. The compiler flags this "
				     "with 'ReturnValue is already a %s, you don't need Cast To %s'."),
				*TargetClass->GetName(), *TargetClass->GetName(),
				*TargetClass->GetName(), *TargetClass->GetName(), *TargetClass->GetName()));
			RepairsMade.Add(MakeShareable(new FJsonValueObject(Repair)));

			CastNode->BreakAllNodeLinks();
			Graph->RemoveNode(CastNode);
		}
	}

	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (RepairsMade.Num() > 0)
	{
		TSet<FString> ReasonSeenForType;
		for (TSharedPtr<FJsonValue>& V : RepairsMade)
		{
			TSharedPtr<FJsonObject> R = SafeAsObject(V);
			if (!R.IsValid()) continue;
			FString RepairType;
			if (!R->TryGetStringField(TEXT("repair"), RepairType) || RepairType.IsEmpty()) continue;
			if (ReasonSeenForType.Contains(RepairType))
			{
				R->RemoveField(TEXT("reason"));
			}
			else
			{
				ReasonSeenForType.Add(RepairType);
			}
		}
		ResultObj->SetArrayField(TEXT("repairs_made"), RepairsMade);
	}
	if (PreFlightIssues.Num() > 0)
	{
		ResultObj->SetArrayField(TEXT("pre_flight_issues"), PreFlightIssues);
	}

	TSharedPtr<FJsonObject> NodesSummary = MakeShared<FJsonObject>();
	NodesSummary->SetNumberField(TEXT("total"), NodesArray->Num());
	NodesSummary->SetNumberField(TEXT("created"), NodeSuccessCount);
	NodesSummary->SetNumberField(TEXT("failed"), NodeFailCount);
	if (NodeFailCount > 0)
	{
		TSet<FString> SeenErrors;
		TArray<TSharedPtr<FJsonValue>> Deduped;
		for (const TSharedPtr<FJsonValue>& F : NodeFailures)
		{
			TSharedPtr<FJsonObject> FObj = SafeAsObject(F);
			FString ErrMsg;
			if (FObj.IsValid()) FObj->TryGetStringField(TEXT("error"), ErrMsg);
			if (!ErrMsg.IsEmpty() && SeenErrors.Contains(ErrMsg))
			{
				if (Deduped.Num() > 0)
				{
					TSharedPtr<FJsonObject> Last = SafeAsObject(Deduped.Last());
					if (Last.IsValid())
					{
						int32 RepeatCount = 0;
						Last->TryGetNumberField(TEXT("repeated"), RepeatCount);
						Last->SetNumberField(TEXT("repeated"), RepeatCount + 1);
					}
				}
				continue;
			}
			SeenErrors.Add(ErrMsg);
			Deduped.Add(F);
		}
		ResultObj->SetArrayField(TEXT("node_failures"), Deduped);

		int32 VarNotFoundCount = 0;
		TSet<FString> MissingVarNames;
		for (const TSharedPtr<FJsonValue>& F : Deduped)
		{
			TSharedPtr<FJsonObject> FObj = SafeAsObject(F);
			if (!FObj.IsValid()) continue;
			FString ErrMsg;
			FObj->TryGetStringField(TEXT("error"), ErrMsg);
			int32 RepeatCount = 0;
			FObj->TryGetNumberField(TEXT("repeated"), RepeatCount);
			if (ErrMsg.Contains(TEXT("Variable '")) && ErrMsg.Contains(TEXT("' not found")))
			{
				VarNotFoundCount += 1 + RepeatCount;
				int32 Quote1 = INDEX_NONE;
				if (ErrMsg.FindChar(TEXT('\''), Quote1))
				{
					const FString After = ErrMsg.Mid(Quote1 + 1);
					int32 Quote2 = INDEX_NONE;
					if (After.FindChar(TEXT('\''), Quote2))
						MissingVarNames.Add(After.Left(Quote2));
				}
			}
		}
		if (VarNotFoundCount >= 2)
		{
			TArray<FString> NameList = MissingVarNames.Array();
			const FString NameSample = FString::Join(NameList, TEXT(", "));
			ResultObj->SetStringField(TEXT("cross_bp_variable_hint"), FString::Printf(
				TEXT("%d missing vars (%s). var.get/set only resolve THIS Blueprint. For other BPs: wire the ref then var.get.<Ref>->fn.<Class>.Get<Name>. Else add_variable here."),
				VarNotFoundCount, *NameSample));
		}
	}
	ResultObj->SetObjectField(TEXT("nodes"), NodesSummary);

	if (NodeFailCount > 0 && NodeSuccessCount == 0 && NodesArray->Num() > 0)
	{
		bool bSawLegacySchema = false;
		for (const TSharedPtr<FJsonValue>& NV : *NodesArray)
		{
			const TSharedPtr<FJsonObject>* NObj = nullptr;
			if (!NV.IsValid() || !NV->TryGetObject(NObj) || !NObj) continue;
			FString H;
			(*NObj)->TryGetStringField(TEXT("handle"), H);
			if (!H.IsEmpty()) continue;
			if ((*NObj)->HasField(TEXT("function_name")) || (*NObj)->HasField(TEXT("event_type"))
				|| (*NObj)->HasField(TEXT("class")) || (*NObj)->HasField(TEXT("type")))
			{
				bSawLegacySchema = true;
				break;
			}
		}

		ResultObj->SetStringField(TEXT("node_creation_hint"), bSawLegacySchema
			? TEXT("ALL nodes failed: wrong node schema. 'type'/'function_name'/'event_type'/'class' are NOT valid node fields — each node needs an 'id' + a 'handle'. Events: handle=\"ev.<Name>\" (Begin Play = \"ev.ReceiveBeginPlay\"). Functions: handle=\"fn.<Class>.<Func>\" (Print String = \"fn.KismetSystemLibrary.PrintString\"). Run discover_nodes(query='...') to find the exact handle. Place events in the EventGraph via graph_name=\"EventGraph\" (NOT function_name=\"BeginPlay\"), and connect from the event's exec-out pin named \"then\" (NOT \"exec\").")
			: TEXT("ALL nodes failed. Required shape: {\"id\":\"n1\",\"handle\":\"...\"}. ONE graph per call (use graph_name=...). Don't switch to place_node. Read get_tool_docs(['blueprint_graph','blueprint_nodes']) and retry."));
	}

	if (ConnectionsArray && ConnectionsArray->Num() > 0)
	{
		TSharedPtr<FJsonObject> ConnSummary = MakeShared<FJsonObject>();
		ConnSummary->SetNumberField(TEXT("total"), ConnectionsArray->Num());
		ConnSummary->SetNumberField(TEXT("connected"), ConnSuccessCount);
		ConnSummary->SetNumberField(TEXT("failed"), ConnFailCount);
		if (ConnFailCount > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ConnFailures;
			for (const TSharedPtr<FJsonValue>& V : ConnResults)
			{
				TSharedPtr<FJsonObject> Obj = SafeAsObject(V);
				if (Obj.IsValid() && Obj->HasField(TEXT("error")))
					ConnFailures.Add(V);
			}
			if (ConnFailures.Num() > 0)
				ConnSummary->SetArrayField(TEXT("failures"), ConnFailures);
		}
		ResultObj->SetObjectField(TEXT("connections"), ConnSummary);
	}

	if (DefaultsArray && DefaultsArray->Num() > 0)
	{
		TSharedPtr<FJsonObject> DefSummary = MakeShared<FJsonObject>();
		DefSummary->SetNumberField(TEXT("total"), DefaultsArray->Num());
		DefSummary->SetNumberField(TEXT("set"), DefaultSuccessCount);
		DefSummary->SetNumberField(TEXT("failed"), DefaultFailCount);
		if (DefaultFailCount > 0) DefSummary->SetArrayField(TEXT("details"), DefaultResults);
		ResultObj->SetObjectField(TEXT("defaults"), DefSummary);
	}

	{
		TArray<TSharedPtr<FJsonValue>> UnconnectedList;
		bool bAnySilentDefault = false;
		TSet<UEdGraphNode*> SeenNodes;
		TArray<TPair<FString, UEdGraphNode*>> OrderedEntries;
		OrderedEntries.Reserve(LocalIdMap.Num());
		for (auto& Pair : LocalIdMap)
		{
			if (!Pair.Value) continue;
			if (Pair.Key.Contains(TEXT("."), ESearchCase::CaseSensitive)) continue;
			if (SeenNodes.Contains(Pair.Value)) continue;
			SeenNodes.Add(Pair.Value);
			OrderedEntries.Add({ Pair.Key, Pair.Value });
		}
		for (auto& Pair : LocalIdMap)
		{
			if (!Pair.Value || SeenNodes.Contains(Pair.Value)) continue;
			SeenNodes.Add(Pair.Value);
			OrderedEntries.Add({ Pair.Key, Pair.Value });
		}

		for (const TPair<FString, UEdGraphNode*>& EntryPair : OrderedEntries)
		{
			UEdGraphNode* N = EntryPair.Value;
			if (!N) continue;

			bool bHasExecPins = false;
			bool bHasExecIn = false, bHasExecOut = false;
			bool bHasDataIn = false;
			bool bHasConnectedDataOut = false;
			TArray<FString> FreeExecInputs, FreeExecOutputs, FreeDataInputs;

			for (UEdGraphPin* P : N->Pins)
			{
				if (!P || P->bHidden || P->bOrphanedPin) continue;

				if (P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
					&& P->Direction == EGPD_Output && P->LinkedTo.Num() > 0)
					bHasConnectedDataOut = true;

				if (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					bHasExecPins = true;
					if (P->Direction == EGPD_Input && P->LinkedTo.Num() == 0)
						FreeExecInputs.Add(P->PinName.ToString());
					if (P->Direction == EGPD_Input && P->LinkedTo.Num() > 0)
						bHasExecIn = true;
					if (P->Direction == EGPD_Output && P->LinkedTo.Num() == 0)
						FreeExecOutputs.Add(P->PinName.ToString());
					if (P->Direction == EGPD_Output && P->LinkedTo.Num() > 0)
						bHasExecOut = true;
				}
				else if (P->Direction == EGPD_Input && P->LinkedTo.Num() == 0
				&& P->DefaultValue.IsEmpty() && P->AutogeneratedDefaultValue.IsEmpty() && P->DefaultObject == nullptr)
				{
					FString PName = P->PinName.ToString();
					if (PName == TEXT("self") || PName == TEXT("WorldContextObject") || PName == TEXT("ClassFilter")
						|| PName == TEXT("DamageTypeClass") || PName == TEXT("EventInstigator")
						|| PName == TEXT("DamageCauser") || PName == TEXT("bPropagateToChildren")
						|| PName == TEXT("WorldContext") || PName == TEXT("LatentInfo")
						|| PName == TEXT("Target") || PName == TEXT("ReturnValueCache")
						|| P->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
						continue;
					FreeDataInputs.Add(PName);
					bHasDataIn = true;
				}
			}

			bool bCompletelyDisconnected = true;
			for (UEdGraphPin* P : N->Pins)
			{
				if (P && !P->bHidden && !P->bOrphanedPin && P->LinkedTo.Num() > 0)
				{ bCompletelyDisconnected = false; break; }
			}

			bool bIsEvent = IsClearProtectedNode(N);
			bool bOrphanedExec = bHasExecPins && !bHasExecIn && !bIsEvent;

			if (bOrphanedExec || FreeDataInputs.Num() > 0 || (bCompletelyDisconnected && !bIsEvent))
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("id"), EntryPair.Key);
				Entry->SetStringField(TEXT("node"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
				if (bCompletelyDisconnected && !bIsEvent)
					Entry->SetStringField(TEXT("issue"), TEXT("completely_disconnected"));
				else if (bOrphanedExec)
					Entry->SetStringField(TEXT("issue"), TEXT("no_exec_input"));
				if (FreeExecInputs.Num() > 0)
					Entry->SetStringField(TEXT("free_exec_in"), FString::Join(FreeExecInputs, TEXT(", ")));
				if (FreeDataInputs.Num() > 0)
					Entry->SetStringField(TEXT("free_data_in"), FString::Join(FreeDataInputs, TEXT(", ")));
				if (bOrphanedExec && bHasConnectedDataOut)
				{
					bAnySilentDefault = true;
					Entry->SetBoolField(TEXT("will_be_pruned"), true);
					Entry->SetStringField(TEXT("silent_default_risk"),
						TEXT("Impure node with a wired data output but no exec input — UE will PRUNE it at compile and read its output as a default (0/null). Wire its `execute` pin into the exec chain, or make it a pure getter (set_function_pure)."));
				}
				UnconnectedList.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}

		if (UnconnectedList.Num() > 0)
		{
			ResultObj->SetArrayField(TEXT("unconnected_nodes"), UnconnectedList);
			ResultObj->SetStringField(TEXT("unconnected_nodes_hint"),
				TEXT("completely_disconnected: orphan, delete_nodes if unintended. no_exec_input: wire prior `then`. free_data_in: defaults used (not an error). will_be_pruned: SILENT failure — node will be pruned and its output read as a default; see silent_default_risk."));

			if (bAnySilentDefault)
			{
				ResultObj->SetStringField(TEXT("rebuild_recommended"),
					TEXT("A node has a wired data output but no exec input — it will be PRUNED at compile and its value read as a default (0/null): a silent logic failure even though it compiles. Rebuild with clear_before_build=true clear_force=true, routing exec through that node (prior node's `then` -> its `execute`), or mark a simple getter pure via set_function_pure. See unconnected_nodes[].silent_default_risk."));
			}
			else if (RepairsMade.Num() > 0)
			{
				ResultObj->SetStringField(TEXT("rebuild_recommended"),
					TEXT("Pre-flight repair dropped/redirected wires AND the build left unconnected nodes — the function body is partial. Rebuild from scratch in the next call with clear_before_build=true clear_force=true and corrected wiring (route exec around pure nodes, see repairs_made[])."));
			}
		}

	}

	struct FBuildCommentSpec { FString Text; FLinearColor Color; TArray<FString> MemberIds; };
	TArray<FBuildCommentSpec> CommentSpecs;

	int32 CommentsCreated = 0;
	int32 CommentsFailed = 0;
	{
		const TArray<TSharedPtr<FJsonValue>>* CommentsArray = nullptr;
		if (Args->TryGetArrayField(TEXT("comments"), CommentsArray) && CommentsArray)
		{
			TSet<FString> NewCommentTexts;
			for (const TSharedPtr<FJsonValue>& CV : *CommentsArray)
			{
				TSharedPtr<FJsonObject> CO = SafeAsObject(CV);
				if (CO.IsValid())
				{
					FString CT;
					CO->TryGetStringField(TEXT("text"), CT);
					if (!CT.IsEmpty()) NewCommentTexts.Add(CT);
				}
			}
			TArray<UEdGraphNode*> StaleComments;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
				{
					if (NewCommentTexts.Contains(Comment->NodeComment))
						StaleComments.Add(Node);
				}
			}
			for (UEdGraphNode* Node : StaleComments)
				Graph->RemoveNode(Node);

			for (const TSharedPtr<FJsonValue>& CommentVal : *CommentsArray)
			{
				TSharedPtr<FJsonObject> CommentObj = SafeAsObject(CommentVal);
				if (!CommentObj.IsValid()) { CommentsFailed++; continue; }

				FString Text = TEXT("Comment");
				CommentObj->TryGetStringField(TEXT("text"), Text);

				const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
				TArray<UEdGraphNode*> MemberNodes;
				if (CommentObj->TryGetArrayField(TEXT("node_ids"), NodeIdsArray) && NodeIdsArray)
				{
					for (const TSharedPtr<FJsonValue>& IdVal : *NodeIdsArray)
					{
						FString NodeId = IdVal->AsString();
						if (UEdGraphNode** Found = LocalIdMap.Find(NodeId))
							MemberNodes.Add(*Found);
					}
				}

				if (MemberNodes.Num() == 0) { CommentsFailed++; continue; }

				constexpr int32 CPad = 40;
				int32 MinX = INT32_MAX, MinY = INT32_MAX, MaxX = INT32_MIN, MaxY = INT32_MIN;
				for (UEdGraphNode* N : MemberNodes)
				{
					FVector2D EstSize = FBpLayoutConverter::EstimateNodeSize(N);
					MinX = FMath::Min(MinX, N->NodePosX);
					MinY = FMath::Min(MinY, N->NodePosY);
					MaxX = FMath::Max(MaxX, N->NodePosX + (int32)EstSize.X);
					MaxY = FMath::Max(MaxY, N->NodePosY + (int32)EstSize.Y);
				}

				FLinearColor Color(0.15f, 0.25f, 0.5f, 0.5f);
				const TSharedPtr<FJsonObject>* ColorObj = nullptr;
				if (CommentObj->TryGetObjectField(TEXT("color"), ColorObj) && ColorObj)
				{
					double R, G, B, A;
					if ((*ColorObj)->TryGetNumberField(TEXT("r"), R)) Color.R = (float)R;
					if ((*ColorObj)->TryGetNumberField(TEXT("g"), G)) Color.G = (float)G;
					if ((*ColorObj)->TryGetNumberField(TEXT("b"), B)) Color.B = (float)B;
					if ((*ColorObj)->TryGetNumberField(TEXT("a"), A)) Color.A = (float)A;
				}

				{
					FBuildCommentSpec Spec;
					Spec.Text = Text;
					Spec.Color = Color;
					if (NodeIdsArray)
						for (const TSharedPtr<FJsonValue>& IdVal : *NodeIdsArray)
							if (!IdVal->AsString().IsEmpty()) Spec.MemberIds.Add(IdVal->AsString());
					CommentSpecs.Add(MoveTemp(Spec));
				}

				UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
				CommentNode->CreateNewGuid();
				CommentNode->PostPlacedNewNode();
				CommentNode->NodePosX = MinX - CPad;
				CommentNode->NodePosY = MinY - CPad - 30;
				CommentNode->NodeWidth = (MaxX - MinX) + CPad * 2;
				CommentNode->NodeHeight = (MaxY - MinY) + CPad * 2 + 30;
				CommentNode->NodeComment = Text;
				CommentNode->CommentColor = Color;
				CommentNode->FontSize = 16;
				CommentNode->MoveMode = ECommentBoxMode::GroupMovement;
				CommentNode->bCommentBubbleVisible = false;
				Graph->AddNode(CommentNode, false, false);

				CommentsCreated++;
			}
		}
	}

	if (CommentsCreated > 0 || CommentsFailed > 0)
	{
		TSharedPtr<FJsonObject> CommentStats = MakeShared<FJsonObject>();
		CommentStats->SetNumberField(TEXT("created"), CommentsCreated);
		CommentStats->SetNumberField(TEXT("failed"), CommentsFailed);
		ResultObj->SetObjectField(TEXT("comments"), CommentStats);
	}

	if (StaleNodesPurged > 0)
	{
		ResultObj->SetNumberField(TEXT("stale_nodes_cleared"), StaleNodesPurged);
		const TCHAR* StaleSeverity =
			(StaleNodesPurged >= 6 && !bClearFirst) ? TEXT("warning") :
			(StaleNodesPurged >= 3)                 ? TEXT("notice")  :
			                                          TEXT("info");
		ResultObj->SetStringField(TEXT("stale_severity"), StaleSeverity);
		FString Note = FString::Printf(TEXT("%d orphan GEID node(s) auto-cleared."), StaleNodesPurged);
		if (StaleNodesPurged >= 6 && !bClearFirst)
		{
			Note += TEXT(" Substantial prior work discarded — pass clear_before_build:true if intentional. Discarded nodes are not recoverable.");
		}
		else if (StaleNodesPurged >= 3)
		{
			Note += TEXT(" Orphan GEID nodes from prior turns are auto-cleared by design.");
		}
		ResultObj->SetStringField(TEXT("stale_note"), Note);
	}

	{
		TSet<UEdGraphNode*> GeidNodes;
		TMap<FString, UEdGraphNode*> GeidToNode;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node) || Node->IsA(UEdGraphNode_Comment::StaticClass())) continue;
			const FString NodeLogicalId = BlueprintNodeIdentity::GetLogicalId(Blueprint, Node);
			if (!NodeLogicalId.IsEmpty())
			{
				GeidNodes.Add(Node);
				GeidToNode.Add(NodeLogicalId, Node);
			}
		}

		if (GeidNodes.Num() >= 2)
		{
			TSet<FString> AiCommentTexts;
			for (const FBuildCommentSpec& Spec : CommentSpecs)
				AiCommentTexts.Add(Spec.Text);

			struct FPreservedComment
			{
				FString Text;
				FLinearColor Color;
				int32 PosX, PosY, Width, Height, FontSize;
				bool bBubbleVisible;
			};
			TArray<FPreservedComment> UserComments;

			TArray<UEdGraphNode*> CommentNodesToRemove;
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!IsValid(N) || !N->IsA(UEdGraphNode_Comment::StaticClass())) continue;
				UEdGraphNode_Comment* C = Cast<UEdGraphNode_Comment>(N);
				if (AiCommentTexts.Contains(C->NodeComment))
				{
					CommentNodesToRemove.Add(N);
				}
				else
				{
					FPreservedComment PC;
					PC.Text           = C->NodeComment;
					PC.Color          = C->CommentColor;
					PC.PosX           = C->NodePosX;
					PC.PosY           = C->NodePosY;
					PC.Width          = C->NodeWidth;
					PC.Height         = C->NodeHeight;
					PC.FontSize       = C->FontSize;
					PC.bBubbleVisible = C->bCommentBubbleVisible;
					UserComments.Add(PC);
					CommentNodesToRemove.Add(N);
				}
			}
			for (UEdGraphNode* N : CommentNodesToRemove)
				Graph->RemoveNode(N);

			FBpLayoutConfig ArrangeConfig;
			ArrangeConfig.NodeSpacing    = FVector2D(80.0, 40.0);
			ArrangeConfig.ExecSpacingX   = 140.0;
			ArrangeConfig.ExecBranchGap  = 200.0;
			ArrangeConfig.InterChainGap  = 250.0;
			ArrangeConfig.CommentPadding = FVector2D(34.0, 24.0);
			ArrangeConfig.CommentSpacingX = 40.0;
			ArrangeConfig.CommentSpacingY = 30.0;
			ArrangeConfig.CommentHeaderH  = 30.0;
			ArrangeConfig.bSelectedOnly   = true;

			FBpLayoutConversion Conv = FBpLayoutConverter::ConvertGraph(Graph, ArrangeConfig, GeidNodes);
			int32 ArrangedCount = 0;
			if (Conv.Graph.Nodes.Num() > 0)
			{
				FBpLayoutResult LayoutResult = FBpLayoutEngine::ArrangeGraph(Conv.Graph);
				ArrangedCount = FBpLayoutApplier::Apply(Conv.NodeMap, LayoutResult);
			}

			constexpr int32 CPad9 = 40;
			int32 RebuildCount = 0;
			for (const FBuildCommentSpec& Spec : CommentSpecs)
			{
				TArray<UEdGraphNode*> ValidMembers;
				for (const FString& Id : Spec.MemberIds)
					if (UEdGraphNode** Found = GeidToNode.Find(Id))
						ValidMembers.Add(*Found);
				if (ValidMembers.Num() < Spec.MemberIds.Num())
					for (const FString& Id : Spec.MemberIds)
						if (!GeidToNode.Contains(Id))
							if (UEdGraphNode** Found = LocalIdMap.Find(Id))
								ValidMembers.Add(*Found);
				if (ValidMembers.Num() == 0) continue;

				int32 MinX9 = INT32_MAX, MinY9 = INT32_MAX, MaxX9 = INT32_MIN, MaxY9 = INT32_MIN;
				for (UEdGraphNode* N : ValidMembers)
				{
					FVector2D EstSize = FBpLayoutConverter::EstimateNodeSize(N);
					MinX9 = FMath::Min(MinX9, N->NodePosX);
					MinY9 = FMath::Min(MinY9, N->NodePosY);
					MaxX9 = FMath::Max(MaxX9, N->NodePosX + (int32)EstSize.X);
					MaxY9 = FMath::Max(MaxY9, N->NodePosY + (int32)EstSize.Y);
				}

				UEdGraphNode_Comment* CNode = NewObject<UEdGraphNode_Comment>(Graph);
				CNode->CreateNewGuid();
				CNode->PostPlacedNewNode();
				CNode->NodePosX  = MinX9 - CPad9;
				CNode->NodePosY  = MinY9 - CPad9 - 30;
				CNode->NodeWidth  = (MaxX9 - MinX9) + CPad9 * 2;
				CNode->NodeHeight = (MaxY9 - MinY9) + CPad9 * 2 + 30;
				CNode->NodeComment = Spec.Text;
				CNode->CommentColor = Spec.Color;
				CNode->FontSize = 16;
				CNode->MoveMode = ECommentBoxMode::GroupMovement;
				CNode->bCommentBubbleVisible = false;
				Graph->AddNode(CNode, false, false);
				RebuildCount++;
			}

			for (const FPreservedComment& PC : UserComments)
			{
				UEdGraphNode_Comment* CNode = NewObject<UEdGraphNode_Comment>(Graph);
				CNode->CreateNewGuid();
				CNode->PostPlacedNewNode();
				CNode->NodePosX             = PC.PosX;
				CNode->NodePosY             = PC.PosY;
				CNode->NodeWidth            = PC.Width;
				CNode->NodeHeight           = PC.Height;
				CNode->NodeComment          = PC.Text;
				CNode->CommentColor         = PC.Color;
				CNode->FontSize             = PC.FontSize;
				CNode->bCommentBubbleVisible = PC.bBubbleVisible;
				CNode->MoveMode             = ECommentBoxMode::GroupMovement;
				Graph->AddNode(CNode, false, false);
			}

			if (ArrangedCount > 0 || RebuildCount > 0)
			{
				TSharedPtr<FJsonObject> ArrangeStats = MakeShared<FJsonObject>();
				ArrangeStats->SetNumberField(TEXT("nodes_arranged"), ArrangedCount);
				ArrangeStats->SetNumberField(TEXT("comments_rebuilt"), RebuildCount);
				ResultObj->SetObjectField(TEXT("auto_arrange"), ArrangeStats);
			}
		}
	}

	if (FuncResultNode && FuncEntryNode)
	{
		int32 MaxLogicX = FuncEntryNode->NodePosX + 400;
		int32 SumY = 0;
		int32 YCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node)) continue;
			if (Node->IsA(UEdGraphNode_Comment::StaticClass())) continue;
			if (!BlueprintNodeIdentity::HasLogicalId(Blueprint, Node)) continue;
			FVector2D EstSize = FBpLayoutConverter::EstimateNodeSize(Node);
			int32 NodeRight = Node->NodePosX + (int32)EstSize.X;
			if (NodeRight > MaxLogicX)
				MaxLogicX = NodeRight;
			SumY += Node->NodePosY;
			YCount++;
		}

		bool bIsVoidFunction = true;
		for (UEdGraphPin* Pin : FuncResultNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				bIsVoidFunction = false;
				break;
			}
		}

		int32 TargetX = MaxLogicX + 300;
		int32 TargetY = FuncEntryNode->NodePosY;
		if (YCount > 0 && !bIsVoidFunction)
			TargetY = SumY / YCount;

		if (bIsVoidFunction)
		{
			FuncResultNode->NodePosX = FuncEntryNode->NodePosX;
			FuncResultNode->NodePosY = FuncEntryNode->NodePosY + 600;
		}
		else
		{
			FuncResultNode->NodePosX = TargetX;
			FuncResultNode->NodePosY = TargetY;
		}
	}

	if (Blueprint && Blueprint->ParentClass)
	{
		bool bHasDirectInputEvent = false;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node)) continue;
			const FString CN = Node->GetClass()->GetName();
			if (CN.Contains(TEXT("InputKey")) || CN.Contains(TEXT("InputDebug")) || CN.Contains(TEXT("InputTouch")))
			{
				bHasDirectInputEvent = true;
				break;
			}
		}
		const bool bAutoReceivingClass =
			Blueprint->ParentClass->IsChildOf(APawn::StaticClass()) ||
			Blueprint->ParentClass->IsChildOf(APlayerController::StaticClass());
		if (bHasDirectInputEvent && !bAutoReceivingClass && Blueprint->GeneratedClass)
		{
			if (AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
			{
				FByteProperty* AutoRecvProp = FindFProperty<FByteProperty>(AActor::StaticClass(), TEXT("AutoReceiveInput"));
				if (AutoRecvProp)
				{
					const uint8 Current = AutoRecvProp->GetPropertyValue_InContainer(CDO);
					if (Current == (uint8)EAutoReceiveInput::Disabled)
					{
						AutoRecvProp->SetPropertyValue_InContainer(CDO, (uint8)EAutoReceiveInput::Player0);
						FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
						ResultObj->SetStringField(TEXT("auto_receive_input_applied"),
							TEXT("Set AutoReceiveInput=Player0 on the BP's CDO — direct input events (Debug Key / Input Key / Input Touch) on a non-Pawn actor only fire when this is configured. Without this auto-fix the input would compile cleanly but never trigger at runtime, requiring a manual fix in the Details panel."));
					}
				}
			}
		}
	}

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);

	if (NodeFailCount > 0 || ConnFailCount > 0 || DefaultFailCount > 0)
	{
		OutError = FString::Printf(
			TEXT("BUILD INCOMPLETE: %d node(s) failed, %d connection(s) failed, %d default(s) failed. ")
			TEXT("This call is REPORTED AS FAILED — do NOT proceed as if the graph is built. ")
			TEXT("Inspect node_failures / connections.details / defaults.details in the result JSON, ")
			TEXT("then PATCH the specific issues with connect_pins / set_pin_default / place_node ")
			TEXT("(do NOT rebuild the entire graph from scratch — that just creates more partial state). ")
			TEXT("Common causes: (1) wrong handle name → discover_nodes / get_handle_reference, ")
			TEXT("(2) wrong pin name → check available pins in the failure details, ")
			TEXT("(3) class-ref pin default uses bare name for an asset that does not exist yet — ")
			TEXT("create the referenced asset first, OR pass a class variable, OR use the full /Game/Foo.Foo_C path. ")
			TEXT("(4) wiring an exec output as a 'to' pin (e.g. to_pin:'LoopBody' — LoopBody is an OUTPUT, not a continue label). ")
			TEXT("After patching, re-verify with get_blueprint_skeleton (or get_blueprint_graph on the affected graph) BEFORE moving on."),
			NodeFailCount, ConnFailCount, DefaultFailCount);
	}
	return;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

void HandleBuildGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError, FString& OutSummary)
{
	HandleBuildGraph(Args, OutJsonString, OutError);
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> FullResult;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJsonString);
	if (!FJsonSerializer::Deserialize(Reader, FullResult) || !FullResult.IsValid()) return;

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

	const TSharedPtr<FJsonObject>* NodesObj = nullptr;
	if (FullResult->TryGetObjectField(TEXT("nodes"), NodesObj))
	{
		TSharedPtr<FJsonObject> NS = MakeShared<FJsonObject>();
		NS->SetNumberField(TEXT("created"), (*NodesObj)->GetIntegerField(TEXT("created")));
		NS->SetNumberField(TEXT("failed"), (*NodesObj)->GetIntegerField(TEXT("failed")));
		Summary->SetObjectField(TEXT("nodes"), NS);
	}

	const TSharedPtr<FJsonObject>* ConnsObj = nullptr;
	if (FullResult->TryGetObjectField(TEXT("connections"), ConnsObj))
	{
		TSharedPtr<FJsonObject> CS = MakeShared<FJsonObject>();
		CS->SetNumberField(TEXT("connected"), (*ConnsObj)->GetIntegerField(TEXT("connected")));
		int32 ConnFailed = (*ConnsObj)->GetIntegerField(TEXT("failed"));
		CS->SetNumberField(TEXT("failed"), ConnFailed);
		if (ConnFailed > 0)
		{
			const TArray<TSharedPtr<FJsonValue>>* Details = nullptr;
			if ((*ConnsObj)->TryGetArrayField(TEXT("details"), Details) && Details)
			{
				TArray<TSharedPtr<FJsonValue>> TopFailures;
				for (int32 i = 0; i < FMath::Min(5, Details->Num()); ++i)
					TopFailures.Add((*Details)[i]);
				CS->SetArrayField(TEXT("failures"), TopFailures);
				if (Details->Num() > 5)
					CS->SetNumberField(TEXT("more_failures"), Details->Num() - 5);
			}
		}
		Summary->SetObjectField(TEXT("connections"), CS);
	}

	const TSharedPtr<FJsonObject>* DefsObj = nullptr;
	if (FullResult->TryGetObjectField(TEXT("defaults"), DefsObj))
	{
		int32 DefFailed = (*DefsObj)->GetIntegerField(TEXT("failed"));
		if (DefFailed > 0)
		{
			TSharedPtr<FJsonObject> DS = MakeShared<FJsonObject>();
			DS->SetNumberField(TEXT("set"), (*DefsObj)->GetIntegerField(TEXT("set")));
			DS->SetNumberField(TEXT("failed"), DefFailed);
			Summary->SetObjectField(TEXT("defaults"), DS);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* UnconnArray = nullptr;
	if (FullResult->TryGetArrayField(TEXT("unconnected_nodes"), UnconnArray) && UnconnArray && UnconnArray->Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TopUnconn;
		for (int32 i = 0; i < FMath::Min(10, UnconnArray->Num()); ++i)
			TopUnconn.Add((*UnconnArray)[i]);
		Summary->SetArrayField(TEXT("unconnected_nodes"), TopUnconn);
	}

	double StaleCleared = 0.0;
	if (FullResult->TryGetNumberField(TEXT("stale_nodes_cleared"), StaleCleared) && StaleCleared > 0)
		Summary->SetNumberField(TEXT("stale_nodes_cleared"), (int32)StaleCleared);

	const TArray<TSharedPtr<FJsonValue>>* PFIssues = nullptr;
	if (FullResult->TryGetArrayField(TEXT("pre_flight_issues"), PFIssues) && PFIssues && PFIssues->Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Capped;
		for (int32 i = 0; i < FMath::Min(3, PFIssues->Num()); ++i)
			Capped.Add((*PFIssues)[i]);
		Summary->SetArrayField(TEXT("pre_flight_issues"), Capped);
	}

	OutSummary = GraphEditHelpers::JsonToString(Summary);
}

namespace
{
	FString NodeIdOf(const UEdGraphNode* Node)
	{
		return BlueprintNodeIdentity::GetNodeIdOrGuid(nullptr, Node);
	}

	FString HandleOf(const UEdGraphNode* Node)
	{
		if (!Node) return FString();

		if (const UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(Node))
		{
			if (!VG->VariableReference.IsSelfContext())
			{
				if (UClass* OwnerClass = VG->VariableReference.GetMemberParentClass())
				{
					return FString::Printf(TEXT("prop.get.%s.%s"),
						*OwnerClass->GetName(), *VG->VariableReference.GetMemberName().ToString());
				}
			}
			return TEXT("var.get.") + VG->VariableReference.GetMemberName().ToString();
		}
		if (const UK2Node_VariableSet* VS = Cast<UK2Node_VariableSet>(Node))
		{
			if (!VS->VariableReference.IsSelfContext())
			{
				if (UClass* OwnerClass = VS->VariableReference.GetMemberParentClass())
				{
					return FString::Printf(TEXT("prop.set.%s.%s"),
						*OwnerClass->GetName(), *VS->VariableReference.GetMemberName().ToString());
				}
			}
			return TEXT("var.set.") + VS->VariableReference.GetMemberName().ToString();
		}

		if (const UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(Node))
		{
			const FName FnName = CF->FunctionReference.GetMemberName();
			UClass* OwnerClass = CF->FunctionReference.GetMemberParentClass();
			if (CF->FunctionReference.IsSelfContext()) return TEXT("fn.Self.") + FnName.ToString();
			if (OwnerClass) return TEXT("fn.") + OwnerClass->GetName() + TEXT(".") + FnName.ToString();
			return TEXT("fn.") + FnName.ToString();
		}
		if (const UK2Node_DynamicCast* DC = Cast<UK2Node_DynamicCast>(Node))
		{
			return DC->TargetType ? (TEXT("cast.") + DC->TargetType->GetName()) : FString(TEXT("cast"));
		}
		if (const UK2Node_CallDelegate* CD = Cast<UK2Node_CallDelegate>(Node))
			return TEXT("ev.Dispatcher.") + CD->GetPropertyName().ToString();
		auto DelegateHandle = [](const TCHAR* Prefix, const UK2Node_BaseMCDelegate* D) -> FString
		{
			if (!D->DelegateReference.IsSelfContext())
			{
				if (UClass* OwnerClass = D->DelegateReference.GetMemberParentClass())
				{
					return FString::Printf(TEXT("%s.%s.%s"),
						Prefix, *OwnerClass->GetName(), *D->GetPropertyName().ToString());
				}
			}
			return FString::Printf(TEXT("%s.%s"), Prefix, *D->GetPropertyName().ToString());
		};
		if (const UK2Node_AddDelegate* AD = Cast<UK2Node_AddDelegate>(Node))
			return DelegateHandle(TEXT("ev.DispatcherBind"), AD);
		if (const UK2Node_RemoveDelegate* RD = Cast<UK2Node_RemoveDelegate>(Node))
			return DelegateHandle(TEXT("ev.DispatcherUnbind"), RD);
		if (const UK2Node_ClearDelegate* ClrD = Cast<UK2Node_ClearDelegate>(Node))
			return DelegateHandle(TEXT("ev.DispatcherClear"), ClrD);
		if (const UK2Node_AssignDelegate* AsgD = Cast<UK2Node_AssignDelegate>(Node))
			return DelegateHandle(TEXT("ev.DispatcherAssign"), AsgD);
		if (const UK2Node_ComponentBoundEvent* CBE = Cast<UK2Node_ComponentBoundEvent>(Node))
			return TEXT("ev.") + CBE->ComponentPropertyName.ToString() + TEXT(".") + CBE->DelegatePropertyName.ToString();
		if (const UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node))
			return TEXT("ev.CustomEvent");
		if (const UK2Node_Event* Ev = Cast<UK2Node_Event>(Node))
			return TEXT("ev.") + Ev->EventReference.GetMemberName().ToString();

		if (Cast<UK2Node_FunctionEntry>(Node))     return TEXT("entry");
		if (Cast<UK2Node_FunctionResult>(Node))    return TEXT("return");
		if (Cast<UK2Node_IfThenElse>(Node))        return TEXT("k2.Branch");
		if (Cast<UK2Node_ExecutionSequence>(Node)) return TEXT("k2.Sequence");
		if (Cast<UK2Node_Self>(Node))              return TEXT("k2.Self");
		if (Cast<UK2Node_Select>(Node))            return TEXT("k2.Select");
		if (Cast<UK2Node_MakeArray>(Node))         return TEXT("k2.MakeArray");

		if (const UK2Node_MakeStruct* MS = Cast<UK2Node_MakeStruct>(Node))
			return MS->StructType ? (TEXT("k2.Make ") + MS->StructType->GetName()) : FString(TEXT("k2.MakeStruct"));
		if (const UK2Node_BreakStruct* BS = Cast<UK2Node_BreakStruct>(Node))
			return BS->StructType ? (TEXT("k2.Break ") + BS->StructType->GetName()) : FString(TEXT("k2.BreakStruct"));

		if (const UK2Node_MacroInstance* MI = Cast<UK2Node_MacroInstance>(Node))
		{
			if (UEdGraph* Macro = MI->GetMacroGraph()) return TEXT("macro.") + Macro->GetName();
			return TEXT("macro");
		}

		FString ClassName = Node->GetClass()->GetName();
		if (ClassName.StartsWith(TEXT("K2Node_"))) return TEXT("k2.") + ClassName.RightChop(7);
		return ClassName;
	}

	FString PinTypeFromGraphPinType(const FEdGraphPinType& T)
	{
		auto Resolve = [](const FName& Cat, const FName& SubCat, const TWeakObjectPtr<UObject>& SubObj) -> FString
		{
			FString CatStr = Cat.ToString();
			if (SubObj.IsValid())  return CatStr + TEXT(":") + SubObj->GetName();
			if (!SubCat.IsNone())  return CatStr + TEXT(":") + SubCat.ToString();
			return CatStr;
		};
		const FString Inner = Resolve(T.PinCategory, T.PinSubCategory, T.PinSubCategoryObject);
		if (T.ContainerType == EPinContainerType::Array) return TEXT("TArray<") + Inner + TEXT(">");
		if (T.ContainerType == EPinContainerType::Set)   return TEXT("TSet<") + Inner + TEXT(">");
		if (T.ContainerType == EPinContainerType::Map)
		{
			const FString Value = Resolve(T.PinValueType.TerminalCategory,
				T.PinValueType.TerminalSubCategory, T.PinValueType.TerminalSubCategoryObject);
			return TEXT("TMap<") + Inner + TEXT(",") + Value + TEXT(">");
		}
		return Inner;
	}

	FString PinTypeShort(const UEdGraphPin* Pin)
	{
		return Pin ? PinTypeFromGraphPinType(Pin->PinType) : FString(TEXT("?"));
	}

	void GetExecOutTargets(const UEdGraphNode* Node, TArray<TSharedPtr<FJsonValue>>& OutExec)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
			if (Pin->Direction != EGPD_Output) continue;
			if (Pin->LinkedTo.Num() == 0) continue;

			TArray<TSharedPtr<FJsonValue>> Targets;
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
					Targets.Add(MakeShared<FJsonValueString>(NodeIdOf(Linked->GetOwningNode())));
			}
			if (Targets.Num() == 0) continue;

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("pin"), Pin->PinName.ToString());
			if (Targets.Num() == 1)
				Out->SetField(TEXT("to"), Targets[0]);
			else
				Out->SetArrayField(TEXT("to"), Targets);
			OutExec.Add(MakeShared<FJsonValueObject>(Out));
		}
	}

	FString NotABlueprintHint(const FString& Path)
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
		if (!Asset) return FString::Printf(TEXT("Failed to load Blueprint: %s"), *Path);

		const FString ClassName = Asset->GetClass()->GetName();
		const TCHAR* Suggestion = nullptr;
		if (ClassName.Contains(TEXT("Niagara")))                      Suggestion = TEXT("Use get_niagara_detailed_summary.");
		else if (ClassName.Contains(TEXT("Material")))                Suggestion = TEXT("Use get_material_nodes.");
		else if (ClassName.Contains(TEXT("WidgetBlueprint")))         Suggestion = TEXT("Use get_widget_summary.");
		else if (ClassName.Contains(TEXT("BehaviorTree")))            Suggestion = TEXT("Use get_behavior_tree_summary.");
		else if (ClassName.Contains(TEXT("AnimBlueprint")))           Suggestion = TEXT("AnimBlueprints work with this tool — check the path.");
		else if (ClassName.Contains(TEXT("AnimMontage")) ||
				 ClassName.Contains(TEXT("AnimSequence")))            Suggestion = TEXT("Use animation(action='get_montage_summary') / animation umbrella.");
		else if (ClassName.Contains(TEXT("DataTable")))               Suggestion = TEXT("Use data(action='get_data_table_rows').");
		else if (ClassName.Contains(TEXT("UserDefinedStruct")))       Suggestion = TEXT("Use data(action='get_struct_summary').");
		else if (ClassName.Contains(TEXT("UserDefinedEnum")))         Suggestion = TEXT("Use data(action='get_enum_values').");

		if (Suggestion)
			return FString::Printf(TEXT("Asset at '%s' is a %s, not a Blueprint. %s"), *Path, *ClassName, Suggestion);
		return FString::Printf(TEXT("Asset at '%s' is a %s, not a Blueprint."), *Path, *ClassName);
	}
}

void HandleGetGraphNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{

	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	int32 MaxNodes = 100;
	if (Args->HasField(TEXT("max_nodes")))
		MaxNodes = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_nodes")), 1, 200);

	FString Filter = TEXT("all");
	Args->TryGetStringField(TEXT("filter"), Filter);

	bool bIncludeUnconnectedPins = false;
	Args->TryGetBoolField(TEXT("include_unconnected_pins"), bIncludeUnconnectedPins);

	FString Detail = TEXT("outline");
	Args->TryGetStringField(TEXT("detail"), Detail);
	const bool bOutline = !Detail.Equals(TEXT("full"), ESearchCase::IgnoreCase);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		if (UObject* Asset = UEditorAssetLibrary::LoadAsset(BlueprintPath))
		{
			OutError = FString::Printf(TEXT("Asset at '%s' is a %s, not a Blueprint. For Niagara systems use get_niagara_detailed_summary; for materials use get_material_nodes; for widgets use get_widget_summary."), *BlueprintPath, *Asset->GetClass()->GetName());
		}
		else
		{
			OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BlueprintPath);
		}
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> GraphNamesArray;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* G : AllGraphs)
	{
		if (G) GraphNamesArray.Add(MakeShared<FJsonValueString>(G->GetName()));
	}

	TArray<UEdGraphNode*> FilteredNodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!IsValid(Node)) continue;
		if (Node->IsA(UEdGraphNode_Comment::StaticClass())) continue;

		if (Filter.Equals(TEXT("exec_chain"), ESearchCase::IgnoreCase))
		{
			bool bHasExec = false;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{ bHasExec = true; break; }
			}
			if (!bHasExec) continue;
		}
		else if (Filter.Equals(TEXT("connected"), ESearchCase::IgnoreCase))
		{
			bool bHasConn = false;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden && Pin->LinkedTo.Num() > 0)
				{ bHasConn = true; break; }
			}
			if (!bHasConn) continue;
		}

		FilteredNodes.Add(Node);
	}

	int32 Offset = 0;
	if (Args->HasField(TEXT("offset")))
		Offset = FMath::Max(0, (int32)Args->GetNumberField(TEXT("offset")));

	int32 TotalCount = FilteredNodes.Num();

	if (Offset > 0 && Offset < FilteredNodes.Num())
		FilteredNodes.RemoveAt(0, Offset);
	else if (Offset >= FilteredNodes.Num())
		FilteredNodes.Empty();

	bool bTruncated = FilteredNodes.Num() > MaxNodes;
	if (bTruncated)
		FilteredNodes.SetNum(MaxNodes);

	TArray<TSharedPtr<FJsonValue>> NodesJsonArray;
	for (UEdGraphNode* Node : FilteredNodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("id"), NodeIdOf(Node));
		NodeObj->SetStringField(TEXT("handle"), HandleOf(Node));
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

		if (const UK2Node_EnhancedInputAction* IANode = Cast<UK2Node_EnhancedInputAction>(Node))
		{
			if (IANode->InputAction)
				NodeObj->SetStringField(TEXT("input_action"), IANode->InputAction->GetPathName());
		}

		bool bIsPure = true;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{ bIsPure = false; break; }
		}
		if (bIsPure) NodeObj->SetBoolField(TEXT("is_pure"), true);

		TArray<TSharedPtr<FJsonValue>> ExecOuts;
		GetExecOutTargets(Node, ExecOuts);
		if (ExecOuts.Num() > 0) NodeObj->SetArrayField(TEXT("exec_to"), ExecOuts);

		if (!bOutline)
		{
			TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
			PosObj->SetNumberField(TEXT("x"), Node->NodePosX);
			PosObj->SetNumberField(TEXT("y"), Node->NodePosY);
			NodeObj->SetObjectField(TEXT("position"), PosObj);
			NodeObj->SetArrayField(TEXT("pins"),
				GraphEditHelpers::GetPinInfoArrayWithConnections(Node, bIncludeUnconnectedPins));
		}

		NodesJsonArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("graph_name"), Graph->GetName());
	ResultObj->SetNumberField(TEXT("node_count"), NodesJsonArray.Num());
	ResultObj->SetNumberField(TEXT("total_node_count"), TotalCount);
	ResultObj->SetBoolField(TEXT("truncated"), bTruncated);
	if (Offset > 0) ResultObj->SetNumberField(TEXT("offset"), Offset);
	ResultObj->SetArrayField(TEXT("graphs_available"), GraphNamesArray);
	ResultObj->SetArrayField(TEXT("nodes"), NodesJsonArray);

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

void HandleDisconnectPins(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{

	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		if (UObject* Asset = UEditorAssetLibrary::LoadAsset(BlueprintPath))
		{
			OutError = FString::Printf(TEXT("Asset at '%s' is a %s, not a Blueprint. For Niagara systems use get_niagara_detailed_summary; for materials use get_material_nodes; for widgets use get_widget_summary."), *BlueprintPath, *Asset->GetClass()->GetName());
		}
		else
		{
			OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BlueprintPath);
		}
		return;
	}

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* BatchArray = nullptr;
	if ((Args->TryGetArrayField(TEXT("connections"), BatchArray) && BatchArray && BatchArray->Num() > 0) ||
		(Args->TryGetArrayField(TEXT("items"), BatchArray) && BatchArray && BatchArray->Num() > 0))
	{
		TArray<TSharedPtr<FJsonValue>> BatchResults;
		int32 SuccessCount = 0, FailCount = 0, TotalBroken = 0;
		for (int32 i = 0; i < BatchArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = SafeAsObject((*BatchArray)[i]);
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);
			if (!Item.IsValid())
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), TEXT("Invalid connection object"));
				FailCount++;
				BatchResults.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			TSharedPtr<FJsonObject> SubArgs = MakeShared<FJsonObject>();
			SubArgs->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			SubArgs->SetStringField(TEXT("graph_name"), GraphName);
			FString S;
			if (Item->TryGetStringField(TEXT("from_node"), S) || Item->TryGetStringField(TEXT("from_node_id"), S) || Item->TryGetStringField(TEXT("from"), S)) SubArgs->SetStringField(TEXT("from_node"), S);
			if (Item->TryGetStringField(TEXT("from_pin"), S))  SubArgs->SetStringField(TEXT("from_pin"), S);
			if (Item->TryGetStringField(TEXT("to_node"), S)   || Item->TryGetStringField(TEXT("to_node_id"), S)   || Item->TryGetStringField(TEXT("to"), S))   SubArgs->SetStringField(TEXT("to_node"), S);
			if (Item->TryGetStringField(TEXT("to_pin"), S))    SubArgs->SetStringField(TEXT("to_pin"), S);
			if (Item->TryGetStringField(TEXT("node_id"), S))   SubArgs->SetStringField(TEXT("node_id"), S);
			if (Item->TryGetStringField(TEXT("pin_name"), S))  SubArgs->SetStringField(TEXT("pin_name"), S);
			bool bDA = false;
			if (Item->TryGetBoolField(TEXT("disconnect_all"), bDA)) SubArgs->SetBoolField(TEXT("disconnect_all"), bDA);
			FString SubOut, SubErr;
			HandleDisconnectPins(SubArgs, SubOut, SubErr);
			if (!SubErr.IsEmpty())
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), SubErr);
				FailCount++;
			}
			else
			{
				Entry->SetBoolField(TEXT("success"), true);
				TSharedPtr<FJsonObject> SubObj;
				TSharedRef<TJsonReader<>> SubReader = TJsonReaderFactory<>::Create(SubOut);
				if (FJsonSerializer::Deserialize(SubReader, SubObj) && SubObj.IsValid())
				{
					FString Status; int32 Broken = 0;
					if (SubObj->TryGetStringField(TEXT("status"), Status)) Entry->SetStringField(TEXT("status"), Status);
					if (SubObj->TryGetNumberField(TEXT("broken_links"), Broken))
					{
						Entry->SetNumberField(TEXT("broken_links"), Broken);
						TotalBroken += Broken;
					}
				}
				SuccessCount++;
			}
			BatchResults.Add(MakeShared<FJsonValueObject>(Entry));
		}
		TSharedPtr<FJsonObject> BatchObj = MakeShared<FJsonObject>();
		BatchObj->SetNumberField(TEXT("total"), BatchArray->Num());
		BatchObj->SetNumberField(TEXT("disconnected"), SuccessCount);
		BatchObj->SetNumberField(TEXT("failed"), FailCount);
		BatchObj->SetNumberField(TEXT("broken_links"), TotalBroken);
		BatchObj->SetArrayField(TEXT("results"), BatchResults);
		OutJsonString = GraphEditHelpers::JsonToString(BatchObj);
		return;
	}

	bool bDisconnectAll = false;
	Args->TryGetBoolField(TEXT("disconnect_all"), bDisconnectAll);

	if (!bDisconnectAll)
	{
		FString TestToNode;
		FString TestNodeId;
		bool bHasNodeId = Args->TryGetStringField(TEXT("node_id"), TestNodeId) || Args->TryGetStringField(TEXT("from_node"), TestNodeId);
		bool bHasToNode = Args->TryGetStringField(TEXT("to_node"), TestToNode) && !TestToNode.IsEmpty();
		FString TestPinName;
		bool bHasPinName = Args->TryGetStringField(TEXT("pin_name"), TestPinName) ||
		                   Args->TryGetStringField(TEXT("from_pin"), TestPinName);
		if (bHasNodeId && bHasPinName && !bHasToNode)
			bDisconnectAll = true;
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (bDisconnectAll)
	{
		FString NodeId, PinName;
		if (!Args->TryGetStringField(TEXT("node_id"), NodeId))
			Args->TryGetStringField(TEXT("from_node"), NodeId);
		if (!Args->TryGetStringField(TEXT("pin_name"), PinName))
			Args->TryGetStringField(TEXT("from_pin"), PinName);
		if (PinName.IsEmpty())
		{
			OutError = TEXT("disconnect_all mode requires: node_id (or from_node), pin_name (alias: from_pin)");
			return;
		}
		if (NodeId.IsEmpty())
		{
			OutError = TEXT("disconnect_all mode requires: node_id (or from_node), pin_name");
			return;
		}

		UEdGraphNode* Node = GraphEditHelpers::FindNodeInGraph(Graph, NodeId);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("Node not found: %s"), *NodeId);
			return;
		}

		UEdGraphPin* Pin = GraphEditHelpers::FindPinByName(Node, PinName, EGPD_Output);
		if (!Pin) Pin = GraphEditHelpers::FindPinByName(Node, PinName, EGPD_Input);
		if (!Pin)
		{
			OutError = FString::Printf(TEXT("Pin not found: %s on node %s"), *PinName, *NodeId);
			return;
		}

		int32 BrokenCount = Pin->LinkedTo.Num();
		Pin->Modify();
		Pin->BreakAllPinLinks();

		Graph->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

		ResultObj->SetStringField(TEXT("status"), TEXT("disconnected_all"));
		ResultObj->SetNumberField(TEXT("broken_links"), BrokenCount);
		ResultObj->SetStringField(TEXT("pin"), PinName);
	}
	else
	{
		FString FromNodeId, FromPinName, ToNodeId, ToPinName;
		if (!Args->TryGetStringField(TEXT("from_node"), FromNodeId))   Args->TryGetStringField(TEXT("from_node_id"), FromNodeId);
		if (!Args->TryGetStringField(TEXT("to_node"), ToNodeId))       Args->TryGetStringField(TEXT("to_node_id"), ToNodeId);
		Args->TryGetStringField(TEXT("from_pin"), FromPinName);
		Args->TryGetStringField(TEXT("to_pin"), ToPinName);
		if (FromNodeId.IsEmpty() || FromPinName.IsEmpty() || ToNodeId.IsEmpty() || ToPinName.IsEmpty())
		{
			OutError = TEXT("Specific mode requires: from_node (alias from_node_id), from_pin, to_node (alias to_node_id), to_pin");
			return;
		}

		UEdGraphNode* FromNode = GraphEditHelpers::FindNodeInGraph(Graph, FromNodeId);
		UEdGraphNode* ToNode = GraphEditHelpers::FindNodeInGraph(Graph, ToNodeId);
		if (!FromNode)
		{
			OutError = FString::Printf(TEXT("from_node not found: %s"), *FromNodeId);
			return;
		}
		if (!ToNode)
		{
			OutError = FString::Printf(TEXT("to_node not found: %s"), *ToNodeId);
			return;
		}

		UEdGraphPin* FromPin = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Output);
		UEdGraphPin* ToPin = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Input);

		if (!FromPin || !ToPin)
		{
			UEdGraphPin* AltFrom = GraphEditHelpers::FindPinByName(FromNode, FromPinName, EGPD_Input);
			UEdGraphPin* AltTo = GraphEditHelpers::FindPinByName(ToNode, ToPinName, EGPD_Output);
			if (AltFrom && AltTo)
			{
				FromPin = AltTo;
				ToPin = AltFrom;
			}
		}

		if (!FromPin)
		{
			OutError = FString::Printf(TEXT("Pin not found: %s on node %s"), *FromPinName, *FromNodeId);
			return;
		}
		if (!ToPin)
		{
			OutError = FString::Printf(TEXT("Pin not found: %s on node %s"), *ToPinName, *ToNodeId);
			return;
		}

		if (!FromPin->LinkedTo.Contains(ToPin))
		{
			ResultObj->SetStringField(TEXT("status"), TEXT("not_connected"));
			OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
			return;
		}

		FromPin->Modify();
		FromPin->BreakLinkTo(ToPin);

		Graph->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

		ResultObj->SetStringField(TEXT("status"), TEXT("disconnected"));
		ResultObj->SetNumberField(TEXT("broken_links"), 1);
	}

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

void HandleArrangeNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{

	bool bSelectedOnly = false;
	bool bGeidOnly = false;
	Args->TryGetBoolField(TEXT("selected_only"), bSelectedOnly);
	Args->TryGetBoolField(TEXT("geid_only"), bGeidOnly);

	FString BlueprintPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);

	FString GraphName = TEXT("EventGraph");
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	TSet<UEdGraphNode*> SelectedUENodes;

	if (!BlueprintPath.IsEmpty())
	{
		Blueprint = LoadBlueprintFromPath(BlueprintPath);
		if (!Blueprint)
		{
			OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
			return;
		}
		Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
			return;
		}
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	FBlueprintEditor* BPEditor = nullptr;
	if (AssetEditorSubsystem)
	{
		IAssetEditorInstance* ActiveEditor = nullptr;
		UObject* ActiveAsset = nullptr;
		double LastActivationTime = 0.0;
		for (UObject* Asset : AssetEditorSubsystem->GetAllEditedAssets())
		{
			for (IAssetEditorInstance* Editor : AssetEditorSubsystem->FindEditorsForAsset(Asset))
			{
				if (Editor && Editor->GetLastActivationTime() > LastActivationTime)
				{
					LastActivationTime = Editor->GetLastActivationTime();
					ActiveEditor = Editor;
					ActiveAsset = Asset;
				}
			}
		}
		if (ActiveEditor)
		{
			const FName EditorName = ActiveEditor->GetEditorName();
			if (EditorName == FName(TEXT("BlueprintEditor")) || EditorName == FName(TEXT("AnimationBlueprintEditor")))
			{
				BPEditor = static_cast<FBlueprintEditor*>(ActiveEditor);

				if (bSelectedOnly && BPEditor)
				{
					for (UObject* Obj : BPEditor->GetSelectedNodes())
					{
						if (UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
							SelectedUENodes.Add(Node);
					}
				}

				if (!Blueprint)
					Blueprint = Cast<UBlueprint>(ActiveAsset);

				if (!Graph && SelectedUENodes.Num() > 0)
				{
					for (UEdGraphNode* Node : SelectedUENodes)
					{
						Graph = Node->GetGraph();
						if (Graph) break;
					}
				}

				if (!Graph && Blueprint)
				{
					Graph = GraphEditHelpers::FindGraphByName(Blueprint, GraphName);
					if (!Graph)
					{
						TArray<UEdGraph*> AllGraphs;
						Blueprint->GetAllGraphs(AllGraphs);
						for (UEdGraph* G : AllGraphs)
						{
							if (G->GetFName() == FName(TEXT("EventGraph"))) { Graph = G; break; }
						}
						if (!Graph && AllGraphs.Num() > 0) Graph = AllGraphs[0];
					}
				}
			}
		}
	}

	if (!Blueprint)
	{
		OutError = TEXT("No blueprint_path provided and no active Blueprint editor found. Open a Blueprint or provide blueprint_path.");
		return;
	}
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
		return;
	}
	if (bGeidOnly && Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (IsValid(Node) && BlueprintNodeIdentity::HasLogicalId(Blueprint, Node))
				SelectedUENodes.Add(Node);
		}
		if (SelectedUENodes.Num() >= 2)
			bSelectedOnly = true;
		else
			bGeidOnly = false;
	}
	if (bSelectedOnly && SelectedUENodes.Num() < 2)
	{
		OutError = TEXT("selected_only requires at least 2 selected nodes in the Blueprint editor");
		return;
	}

	struct FCommentRecord
	{
		FString Text;
		FLinearColor Color;
		int32 FontSize;
		ECommentBoxMode::Type MoveMode;
		TArray<UEdGraphNode*> MemberNodes;
	};
	TArray<FCommentRecord> PreservedComments;
	{
		TArray<UEdGraphNode_Comment*> CommentsToRemove;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
			if (!Comment) continue;
			if (IsOrphanCommentBox(Comment))
			{
				CommentsToRemove.Add(Comment);
				continue;
			}

			FCommentRecord Record;
			Record.Text = Comment->NodeComment;
			Record.Color = Comment->CommentColor;
			Record.FontSize = Comment->FontSize;
			Record.MoveMode = Comment->MoveMode;

			int32 CX = Comment->NodePosX;
			int32 CY = Comment->NodePosY;
			int32 CW = Comment->NodeWidth;
			int32 CH = Comment->NodeHeight;

			for (UEdGraphNode* Other : Graph->Nodes)
			{
				if (Other == Comment || Other->IsA(UEdGraphNode_Comment::StaticClass())) continue;
				FVector2D EstSize = FBpLayoutConverter::EstimateNodeSize(Other);
				float CenterX = Other->NodePosX + EstSize.X * 0.5f;
				float CenterY = Other->NodePosY + EstSize.Y * 0.5f;
				if (CenterX >= CX && CenterX <= CX + CW && CenterY >= CY && CenterY <= CY + CH)
				{
					Record.MemberNodes.Add(Other);
				}
			}

			if (Record.MemberNodes.Num() > 0)
			{
				PreservedComments.Add(MoveTemp(Record));
				CommentsToRemove.Add(Comment);
			}
		}
		for (UEdGraphNode_Comment* C : CommentsToRemove)
			Graph->RemoveNode(C);

	}

	TArray<UEdGraphNode*> OrphanNodes;
	int32 OrphansDeleted = 0;
	if (!bSelectedOnly)
	{
		TArray<UEdGraphNode*> AllNodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node) || Node->IsA(UEdGraphNode_Comment::StaticClass())) continue;
			AllNodes.Add(Node);
		}

		TSet<UEdGraphNode*> ExecReachable;
		TQueue<UEdGraphNode*> ExecQueue;
		for (UEdGraphNode* Node : AllNodes)
		{
			if (IsClearProtectedNode(Node))
			{
				ExecQueue.Enqueue(Node);
				ExecReachable.Add(Node);
			}
		}
		while (!ExecQueue.IsEmpty())
		{
			UEdGraphNode* Current;
			ExecQueue.Dequeue(Current);
			for (UEdGraphPin* Pin : Current->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked) continue;
					UEdGraphNode* Other = Linked->GetOwningNodeUnchecked();
					if (Other && !ExecReachable.Contains(Other))
					{
						ExecReachable.Add(Other);
						ExecQueue.Enqueue(Other);
					}
				}
			}
		}

		TSet<UEdGraphNode*> Reachable = ExecReachable;
		TQueue<UEdGraphNode*> DataQueue;
		for (UEdGraphNode* N : ExecReachable) DataQueue.Enqueue(N);
		while (!DataQueue.IsEmpty())
		{
			UEdGraphNode* Current;
			DataQueue.Dequeue(Current);
			for (UEdGraphPin* Pin : Current->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked) continue;
					UEdGraphNode* Supplier = Linked->GetOwningNodeUnchecked();
					if (Supplier && !Reachable.Contains(Supplier))
					{
						Reachable.Add(Supplier);
						DataQueue.Enqueue(Supplier);
					}
				}
			}
		}

		for (UEdGraphNode* Node : AllNodes)
		{
			if (!Reachable.Contains(Node))
			{
				if (IsClearProtectedNode(Node))
					continue;
				OrphanNodes.Add(Node);
			}
		}

		for (UEdGraphNode* N : OrphanNodes)
			Graph->Nodes.Remove(N);

	}

	FBpLayoutConfig Config;
	Config.NodeSpacing = FVector2D(80.0, 40.0);
	Config.ExecSpacingX = 140.0;
	Config.ExecBranchGap = 200.0;
	Config.InterChainGap = 250.0;
	Config.CommentPadding = FVector2D(34.0, 24.0);
	Config.CommentSpacingX = 40.0;
	Config.CommentSpacingY = 30.0;
	Config.CommentHeaderH = 30.0;
	Config.bSelectedOnly = bSelectedOnly;

	double SpacingX = 0, SpacingY = 0;
	if (Args->TryGetNumberField(TEXT("spacing_x"), SpacingX)) Config.NodeSpacing.X = SpacingX;
	if (Args->TryGetNumberField(TEXT("spacing_y"), SpacingY)) Config.NodeSpacing.Y = SpacingY;

	FBpLayoutConversion Conv = bSelectedOnly
		? FBpLayoutConverter::ConvertGraph(Graph, Config, SelectedUENodes)
		: FBpLayoutConverter::ConvertGraph(Graph, Config);

	if (Conv.Graph.Nodes.Num() == 0 && OrphanNodes.Num() == 0)
	{
		OutJsonString = TEXT("{\"status\":\"no_nodes\",\"message\":\"Graph has no nodes to arrange\"}");
		return;
	}

	int32 MovedCount = 0;
	if (Conv.Graph.Nodes.Num() > 0)
	{
		FBpLayoutResult LayoutResult = FBpLayoutEngine::ArrangeGraph(Conv.Graph);
		MovedCount = FBpLayoutApplier::Apply(Conv.NodeMap, LayoutResult);
	}

	if (MovedCount > 0 && bSelectedOnly)
	{
		int32 MaxNonSelectedBottom = MIN_int32;
		for (UEdGraphNode* ExNode : Graph->Nodes)
		{
			if (!IsValid(ExNode) || ExNode->IsA(UEdGraphNode_Comment::StaticClass())) continue;
			if (SelectedUENodes.Contains(ExNode)) continue;
			int32 H = 36 + ExNode->Pins.Num() * 13;
			MaxNonSelectedBottom = FMath::Max(MaxNonSelectedBottom, ExNode->NodePosY + H);
		}

		if (MaxNonSelectedBottom != MIN_int32)
		{
			int32 MinSelectedY = MAX_int32;
			for (UEdGraphNode* SelNode : SelectedUENodes)
				if (IsValid(SelNode)) MinSelectedY = FMath::Min(MinSelectedY, SelNode->NodePosY);

			if (MinSelectedY < MaxNonSelectedBottom + 100)
			{
				int32 PushDown = (MaxNonSelectedBottom + 200) - MinSelectedY;
				for (UEdGraphNode* SelNode : SelectedUENodes)
					if (IsValid(SelNode)) SelNode->NodePosY += PushDown;
			}
		}
	}

	if (MovedCount > 0 && !bSelectedOnly)
	{
		TArray<UEdGraphNode*> LiveNodes;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (IsValid(N) && !N->IsA(UEdGraphNode_Comment::StaticClass()))
				LiveNodes.Add(N);
		}

		LiveNodes.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			if (A.NodePosX != B.NodePosX) return A.NodePosX < B.NodePosX;
			return A.NodePosY < B.NodePosY;
		});

		auto EstHeight = [](UEdGraphNode* N) -> int32
		{
			int32 InC = 0, OutC = 0;
			for (UEdGraphPin* P : N->Pins)
			{
				if (!P || P->bHidden || P->bOrphanedPin) continue;
				if (P->Direction == EGPD_Input) InC++; else OutC++;
			}
			return FMath::Max(48, 36 + FMath::Max(InC, OutC) * 26);
		};

		constexpr int32 SafetyGap = 10;
		constexpr int32 ColThreshold = 200;
		for (int32 i = 0; i < LiveNodes.Num(); i++)
		{
			UEdGraphNode* Ni = LiveNodes[i];
			int32 Hi = EstHeight(Ni);
			for (int32 j = i + 1; j < LiveNodes.Num(); j++)
			{
				UEdGraphNode* Nj = LiveNodes[j];
				if (FMath::Abs(Nj->NodePosX - Ni->NodePosX) > ColThreshold) continue;
				int32 BottomI = Ni->NodePosY + Hi;
				if (Nj->NodePosY < BottomI + SafetyGap)
					Nj->NodePosY = BottomI + SafetyGap;
			}
		}
	}

	if (OrphanNodes.Num() > 0)
	{
		int32 DeletedCount = 0;
		for (UEdGraphNode* N : OrphanNodes)
		{
			if (BlueprintNodeIdentity::HasLogicalId(Blueprint, N))
			{
				for (UEdGraphPin* Pin : N->Pins)
				{
					if (Pin) Pin->BreakAllPinLinks();
				}
				BlueprintNodeIdentity::ClearLogicalId(Blueprint, N);
				N->DestroyNode();
				DeletedCount++;
			}
			else
			{
				Graph->Nodes.Add(N);
			}
		}

		OrphansDeleted = DeletedCount;
	}

	int32 CommentsPreserved = 0;
	for (const FCommentRecord& Rec : PreservedComments)
	{
		TArray<UEdGraphNode*> ValidMembers;
		for (UEdGraphNode* N : Rec.MemberNodes)
		{
			if (IsValid(N) && Graph->Nodes.Contains(N))
				ValidMembers.Add(N);
		}
		if (ValidMembers.Num() == 0) continue;

		constexpr int32 PPad = 40;
		int32 MinX = INT32_MAX, MinY = INT32_MAX, MaxX = INT32_MIN, MaxY = INT32_MIN;
		for (UEdGraphNode* N : ValidMembers)
		{
			FVector2D EstSize = FBpLayoutConverter::EstimateNodeSize(N);
			MinX = FMath::Min(MinX, N->NodePosX);
			MinY = FMath::Min(MinY, N->NodePosY);
			MaxX = FMath::Max(MaxX, N->NodePosX + (int32)EstSize.X);
			MaxY = FMath::Max(MaxY, N->NodePosY + (int32)EstSize.Y);
		}

		UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
		CommentNode->CreateNewGuid();
		CommentNode->PostPlacedNewNode();
		CommentNode->NodePosX = MinX - PPad;
		CommentNode->NodePosY = MinY - PPad - 30;
		CommentNode->NodeWidth = (MaxX - MinX) + PPad * 2;
		CommentNode->NodeHeight = (MaxY - MinY) + PPad * 2 + 30;
		CommentNode->NodeComment = Rec.Text;
		CommentNode->CommentColor = Rec.Color;
		CommentNode->FontSize = Rec.FontSize;
		CommentNode->MoveMode = Rec.MoveMode;
		CommentNode->bCommentBubbleVisible = false;
		Graph->AddNode(CommentNode, false, false);
		CommentsPreserved++;
	}
	if (CommentsPreserved > 0)
	{

		TArray<UEdGraphNode_Comment*> ArrangedComments;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UEdGraphNode_Comment* C = Cast<UEdGraphNode_Comment>(Node))
				ArrangedComments.Add(C);
		}
		ArrangedComments.Sort([](const UEdGraphNode_Comment& A, const UEdGraphNode_Comment& B) { return A.NodePosY < B.NodePosY; });
		for (int32 i = 1; i < ArrangedComments.Num(); i++)
		{
			UEdGraphNode_Comment* Prev = ArrangedComments[i - 1];
			UEdGraphNode_Comment* Curr = ArrangedComments[i];
			int32 PrevBottom = Prev->NodePosY + Prev->NodeHeight;
			if (Curr->NodePosY < PrevBottom + 40)
			{
				int32 PushDown = (PrevBottom + 40) - Curr->NodePosY;
				int32 CX = Curr->NodePosX, CY = Curr->NodePosY;
				int32 CW = Curr->NodeWidth, CH = Curr->NodeHeight;
				Curr->NodePosY += PushDown;
				for (UEdGraphNode* N : Graph->Nodes)
				{
					if (N == Curr || !IsValid(N) || N->IsA(UEdGraphNode_Comment::StaticClass())) continue;
					if (N->NodePosX >= CX && N->NodePosY >= CY &&
						N->NodePosX <= CX + CW && N->NodePosY <= CY + CH)
						N->NodePosY += PushDown;
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("status"), TEXT("arranged"));
	ResultObj->SetNumberField(TEXT("nodes_moved"), MovedCount);
	ResultObj->SetNumberField(TEXT("total_nodes"), (double)Conv.Graph.Nodes.Num());
	ResultObj->SetStringField(TEXT("graph"), GraphName);
	if (bSelectedOnly)
		ResultObj->SetNumberField(TEXT("selected_count"), (double)SelectedUENodes.Num());
	if (OrphansDeleted > 0)
		ResultObj->SetNumberField(TEXT("orphans_deleted"), OrphansDeleted);
	if (CommentsPreserved > 0)
		ResultObj->SetNumberField(TEXT("comments_preserved"), CommentsPreserved);

	OutJsonString = GraphEditHelpers::JsonToString(ResultObj);
	return;
}

void HandleAddFunction(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	{
		const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
		if (BatchToolHelper::TryGetBatchItems(Args, TEXT("functions"), ItemsArray))
		{
			HandleAddFunctionsBulk(Args, OutJsonString, OutError);
			return;
		}
	}

	FString BlueprintPath, FunctionName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}
	if (!Args->TryGetStringField(TEXT("function_name"), FunctionName) || FunctionName.IsEmpty())
	{
		if (!Args->TryGetStringField(TEXT("name"), FunctionName) || FunctionName.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: function_name");
			return;
		}
	}

	{
		FString ReplicationVal;
		if (Args->TryGetStringField(TEXT("replication"), ReplicationVal) && !ReplicationVal.IsEmpty()
			&& !ReplicationVal.Equals(TEXT("NotReplicated"), ESearchCase::IgnoreCase)
			&& !ReplicationVal.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(
				TEXT("add_function rejects replication='%s'. Blueprint functions cannot be RPCs — "
				     "only Custom Events can replicate. Workflow: "
				     "(1) Use place_node (or build_blueprint_graph) with handle='ev.CustomEvent', "
				     "custom_name='%s', inputs=[{name,type},...] in graph_name='EventGraph'. "
				     "(2) Call set_function_replication(function_name='%s', replication='Server'|'Client'|'Multicast', reliable=true|false). "
				     "Bonus: custom_name starting with 'Server_'/'Client_'/'Multicast_' auto-flags the event on placement so step 2 is optional."),
				*ReplicationVal, *FunctionName, *FunctionName);
			return;
		}
	}

	static const TCHAR* RPCPrefixes[] = { TEXT("Server_"), TEXT("Client_"), TEXT("Multicast_") };
	for (const TCHAR* Prefix : RPCPrefixes)
	{
		if (FunctionName.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(
				TEXT("'%s' looks like a Blueprint RPC (Server_/Client_/Multicast_ prefix). "
				     "RPCs must be Custom Events in EventGraph — NOT separate function graphs. "
				     "Do NOT call add_function for this. Instead, use build_blueprint_graph with "
				     "ev.CustomEvent (custom_name='%s') in EventGraph, then call "
				     "set_function_replication(function_name='%s', replication='Server'|'Client'|'Multicast', reliable=true|false) afterwards."),
				*FunctionName, *FunctionName, *FunctionName);
			return;
		}
	}

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP)
	{
		FString AssetName = FPaths::GetBaseFilename(BlueprintPath);
		BP = FindFirstObjectSafe<UBlueprint>(*AssetName);
	}
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	const bool bIsMacroLib = (BP->BlueprintType == BPTYPE_MacroLibrary);

	TArray<TObjectPtr<UEdGraph>>& SearchGraphs = bIsMacroLib ? BP->MacroGraphs : BP->FunctionGraphs;
	for (UEdGraph* ExistingGraph : SearchGraphs)
	{
		if (ExistingGraph && ExistingGraph->GetFName() == FName(*FunctionName))
		{
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("function_name"), FunctionName);
			Result->SetStringField(TEXT("message"), FString::Printf(TEXT("%s '%s' already exists. Use build_blueprint_graph with graph_name='%s' to add logic."), bIsMacroLib ? TEXT("Macro") : TEXT("Function"), *FunctionName, *FunctionName));
			OutJsonString = GraphEditHelpers::JsonToString(Result);
			return;
		}
	}

	for (UEdGraph* UG : BP->UbergraphPages)
	{
		if (!UG) continue;
		for (UEdGraphNode* Node : UG->Nodes)
		{
			UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node);
			UK2Node_Event* Ev = Cast<UK2Node_Event>(Node);
			UK2Node_ComponentBoundEvent* CBE = Cast<UK2Node_ComponentBoundEvent>(Node);
			FString ExistingName;
			FString EventKind;
			if (CE) { ExistingName = CE->CustomFunctionName.ToString(); EventKind = TEXT("custom event"); }
			else if (CBE) { ExistingName = CBE->CustomFunctionName.ToString(); EventKind = TEXT("bound event"); }
			else if (Ev) { ExistingName = Ev->EventReference.GetMemberName().ToString(); EventKind = TEXT("event"); }
			if (!ExistingName.IsEmpty() && ExistingName.Equals(FunctionName, ESearchCase::IgnoreCase))
			{
				Result->SetBoolField(TEXT("success"), true);
				Result->SetStringField(TEXT("function_name"), FunctionName);
				Result->SetStringField(TEXT("existing_kind"), EventKind);
				Result->SetStringField(TEXT("message"), FString::Printf(
					TEXT("A %s named '%s' already exists on this blueprint. Events and functions share the same name scope in UE — "
					     "creating a function with this name would produce a compile error. Call build_blueprint_graph in the "
					     "EventGraph to add logic downstream of the existing event; OR rename the event first via delete_nodes + ev.CustomEvent{custom_name:'NewName'}."),
					*EventKind, *FunctionName));
				OutJsonString = GraphEditHelpers::JsonToString(Result);
				return;
			}
		}
	}

	if (!bIsMacroLib)
	{
		for (const FBPInterfaceDescription& IfDesc : BP->ImplementedInterfaces)
		{
			if (!IfDesc.Interface) continue;
			for (UEdGraph* IGraph : IfDesc.Graphs)
			{
				if (IGraph && IGraph->GetFName() == FName(*FunctionName))
				{
					Result->SetBoolField(TEXT("success"), true);
					Result->SetStringField(TEXT("function_name"), FunctionName);
					Result->SetStringField(TEXT("interface"), IfDesc.Interface->GetName());
					Result->SetStringField(TEXT("message"), FString::Printf(
						TEXT("'%s' is already an interface implementation from '%s'. Use build_blueprint_graph(graph_name='%s') to populate its body — do NOT add a duplicate function with this name."),
						*FunctionName, *IfDesc.Interface->GetName(), *FunctionName));
					OutJsonString = GraphEditHelpers::JsonToString(Result);
					return;
				}
			}
		}
	}

	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Add %s %s"), bIsMacroLib ? TEXT("Macro") : TEXT("Function"), *FunctionName)));
	BP->Modify();

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (bIsMacroLib)
	{
		FBlueprintEditorUtils::AddMacroGraph(BP, NewGraph, true, nullptr);
	}
	else
	{
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, true, nullptr);
	}

	int32 InputCount = 0;
	int32 OutputCount = 0;

	if (bIsMacroLib)
	{
		TArray<UK2Node_Tunnel*> Tunnels;
		NewGraph->GetNodesOfClass<UK2Node_Tunnel>(Tunnels);
		UK2Node_Tunnel* EntryTunnel = nullptr;
		UK2Node_Tunnel* ExitTunnel = nullptr;
		for (UK2Node_Tunnel* T : Tunnels)
		{
			if (T->bCanHaveOutputs && !T->bCanHaveInputs)      EntryTunnel = T;
			else if (T->bCanHaveInputs && !T->bCanHaveOutputs) ExitTunnel = T;
		}
		const TArray<TSharedPtr<FJsonValue>>* InArr = nullptr;
		if (EntryTunnel && Args->TryGetArrayField(TEXT("inputs"), InArr) && InArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *InArr)
			{
				TSharedPtr<FJsonObject> Obj = SafeAsObject(V); if (!Obj) continue;
				FString PN, PT; Obj->TryGetStringField(TEXT("name"), PN); Obj->TryGetStringField(TEXT("type"), PT);
				if (PN.IsEmpty() || PT.IsEmpty()) continue;
				EntryTunnel->CreateUserDefinedPin(FName(*PN), GraphEditHelpers::StringToPinType(PT), EGPD_Output, true);
				InputCount++;
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* OutArr = nullptr;
		if (ExitTunnel && Args->TryGetArrayField(TEXT("outputs"), OutArr) && OutArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *OutArr)
			{
				TSharedPtr<FJsonObject> Obj = SafeAsObject(V); if (!Obj) continue;
				FString PN, PT; Obj->TryGetStringField(TEXT("name"), PN); Obj->TryGetStringField(TEXT("type"), PT);
				if (PN.IsEmpty() || PT.IsEmpty()) continue;
				ExitTunnel->CreateUserDefinedPin(FName(*PN), GraphEditHelpers::StringToPinType(PT), EGPD_Input, true);
				OutputCount++;
			}
		}
	}
	else
	{
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		NewGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
		UK2Node_FunctionEntry* EntryNode = EntryNodes.Num() > 0 ? EntryNodes[0] : nullptr;

		for (const TCHAR* F : { TEXT("inputs"), TEXT("outputs"), TEXT("params"), TEXT("parameters"), TEXT("arguments"), TEXT("returns"), TEXT("return_values") })
		{
			FString StrVal;
			const TArray<TSharedPtr<FJsonValue>>* CheckArr = nullptr;
			if (!Args->TryGetArrayField(F, CheckArr) && Args->TryGetStringField(F, StrVal) && !StrVal.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Parsed;
				TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(StrVal);
				if (FJsonSerializer::Deserialize(R, Parsed))
					Args->SetArrayField(F, Parsed);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
		if ((!Args->TryGetArrayField(TEXT("inputs"), InputsArray) || !InputsArray) && EntryNode)
			Args->TryGetArrayField(TEXT("params"), InputsArray);
		if ((!InputsArray) && EntryNode)
			Args->TryGetArrayField(TEXT("parameters"), InputsArray);
		if ((!InputsArray) && EntryNode)
			Args->TryGetArrayField(TEXT("arguments"), InputsArray);
		if (InputsArray && EntryNode)
		{
			TArray<TSharedPtr<FJsonObject>> ReturnParamObjs;
			for (const TSharedPtr<FJsonValue>& Val : *InputsArray)
			{
				TSharedPtr<FJsonObject> Obj = SafeAsObject(Val);
				if (!Obj) continue;
				FString PName, PType;
				Obj->TryGetStringField(TEXT("name"), PName);
				Obj->TryGetStringField(TEXT("type"), PType);
				if (PName.IsEmpty() || PType.IsEmpty()) continue;
				bool bIsReturn = false;
				Obj->TryGetBoolField(TEXT("return"), bIsReturn);
				FString DirStr;
				if (!bIsReturn && Obj->TryGetStringField(TEXT("direction"), DirStr))
				{
					if (DirStr.Equals(TEXT("output"), ESearchCase::IgnoreCase)
						|| DirStr.Equals(TEXT("out"), ESearchCase::IgnoreCase)
						|| DirStr.Equals(TEXT("return"), ESearchCase::IgnoreCase))
					{
						bIsReturn = true;
					}
				}
				if (bIsReturn) { ReturnParamObjs.Add(Obj); continue; }
				FEdGraphPinType PinType = GraphEditHelpers::StringToPinType(PType);
				EntryNode->CreateUserDefinedPin(FName(*PName), PinType, EGPD_Output, true);
				InputCount++;
			}
			if (ReturnParamObjs.Num() > 0)
			{
				UK2Node_FunctionResult* RetResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
				if (RetResultNode)
				{
					for (const TSharedPtr<FJsonObject>& Obj : ReturnParamObjs)
					{
						FString PName, PType;
						Obj->TryGetStringField(TEXT("name"), PName);
						Obj->TryGetStringField(TEXT("type"), PType);
						FEdGraphPinType PinType = GraphEditHelpers::StringToPinType(PType);
						RetResultNode->CreateUserDefinedPin(FName(*PName), PinType, EGPD_Input, true);
						OutputCount++;
					}
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* OutputsArray = nullptr;
		Args->TryGetArrayField(TEXT("outputs"), OutputsArray);
		if (!OutputsArray) Args->TryGetArrayField(TEXT("return_values"), OutputsArray);
		if (!OutputsArray) Args->TryGetArrayField(TEXT("returns"), OutputsArray);
		if (OutputsArray && EntryNode)
		{
			UK2Node_FunctionResult* ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
			if (ResultNode)
			{
				for (const TSharedPtr<FJsonValue>& Val : *OutputsArray)
				{
					TSharedPtr<FJsonObject> Obj = SafeAsObject(Val);
					if (!Obj) continue;
					FString PName, PType;
					Obj->TryGetStringField(TEXT("name"), PName);
					Obj->TryGetStringField(TEXT("type"), PType);
					if (PName.IsEmpty() || PType.IsEmpty()) continue;
					FEdGraphPinType PinType = GraphEditHelpers::StringToPinType(PType);
					ResultNode->CreateUserDefinedPin(FName(*PName), PinType, EGPD_Input, true);
					OutputCount++;
				}
			}
		}

		if (OutputCount == 0 && EntryNode)
		{
			FString ReturnTypeAlias;
			if (Args->TryGetStringField(TEXT("return_type"), ReturnTypeAlias) && !ReturnTypeAlias.IsEmpty()
				&& !ReturnTypeAlias.Equals(TEXT("void"), ESearchCase::IgnoreCase)
				&& !ReturnTypeAlias.Equals(TEXT("none"), ESearchCase::IgnoreCase))
			{
				UK2Node_FunctionResult* ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
				if (ResultNode)
				{
					FEdGraphPinType PinType = GraphEditHelpers::StringToPinType(ReturnTypeAlias);
					ResultNode->CreateUserDefinedPin(FName(TEXT("ReturnValue")), PinType, EGPD_Input, true);
					OutputCount++;
				}
			}
		}

		if (OutputCount == 0 && EntryNode)
		{
			FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
		}

		if (EntryNode) EntryNode->ReconstructNode();
		{
			TArray<UK2Node_FunctionResult*> RNodes;
			NewGraph->GetNodesOfClass<UK2Node_FunctionResult>(RNodes);
			for (UK2Node_FunctionResult* RN : RNodes) RN->ReconstructNode();
		}

		bool bPure = false;
		if (Args->TryGetBoolField(TEXT("pure"), bPure) && bPure)
		{
			GetDefault<UEdGraphSchema_K2>()->AddExtraFunctionFlags(NewGraph, FUNC_BlueprintPure);
		}
	}

	{
		FString Category;
		if (Args->TryGetStringField(TEXT("category"), Category) && !Category.IsEmpty())
		{
			FBlueprintEditorUtils::SetBlueprintFunctionOrMacroCategory(
				NewGraph, FText::FromString(Category), true);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("function_name"), FunctionName);
	Result->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Result->SetNumberField(TEXT("inputs"), InputCount);
	Result->SetNumberField(TEXT("outputs"), OutputCount);

	if (!bIsMacroLib)
	{
		TArray<TSharedPtr<FJsonValue>> InputPinNames;
		TArray<UK2Node_FunctionEntry*> ENodes;
		NewGraph->GetNodesOfClass<UK2Node_FunctionEntry>(ENodes);
		if (ENodes.Num() > 0)
		{
			for (UEdGraphPin* P : ENodes[0]->Pins)
			{
				if (P && P->Direction == EGPD_Output && !P->bHidden)
					InputPinNames.Add(MakeShared<FJsonValueString>(P->PinName.ToString()));
			}
		}
		Result->SetArrayField(TEXT("entry_pins"), InputPinNames);

		TArray<TSharedPtr<FJsonValue>> OutputPinNames;
		TArray<UK2Node_FunctionResult*> RNodes;
		NewGraph->GetNodesOfClass<UK2Node_FunctionResult>(RNodes);
		if (RNodes.Num() > 0)
		{
			for (UEdGraphPin* P : RNodes[0]->Pins)
			{
				if (P && P->Direction == EGPD_Input && !P->bHidden)
					OutputPinNames.Add(MakeShared<FJsonValueString>(P->PinName.ToString()));
			}
		}
		Result->SetArrayField(TEXT("return_pins"), OutputPinNames);
	}

	const bool bIsInterface = (BP->BlueprintType == BPTYPE_Interface);
	FString Msg = FString::Printf(TEXT("%s '%s' created. Use build_blueprint_graph with graph_name='%s' to add logic. In connections: use 'entry' node for input params, 'return' node for output/ReturnValue."),
		bIsMacroLib ? TEXT("Macro") : TEXT("Function"), *FunctionName, *FunctionName);
	if (bIsInterface)
	{
		Msg += FString::Printf(TEXT(" INTERFACE: call this from another Blueprint via msg.%s.%s (preferred — auto-no-ops if target doesn't implement) ")
			TEXT("or fn.%s_C.%s (strict). Compile the interface (compile_blueprint) before either form will resolve."),
			*BP->GetName(), *FunctionName, *BP->GetName(), *FunctionName);
	}
	Result->SetStringField(TEXT("message"), Msg);
	if (bIsInterface) Result->SetBoolField(TEXT("is_interface"), true);
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleAddFunctionsBulk(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Functions = nullptr;
	if (!BatchToolHelper::TryGetBatchItems(Args, TEXT("functions"), Functions))
	{
		OutError = TEXT("Missing or empty 'items'/'functions' array");
		return;
	}

	int32 Added = 0, Failed = 0;
	TArray<FString> Errors;
	TArray<TSharedPtr<FJsonValue>> CreatedNames;

	for (const TSharedPtr<FJsonValue>& Val : *Functions)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr))
		{
			Errors.Add(TEXT("Skipped entry: not a JSON object"));
			++Failed;
			continue;
		}

		TSharedPtr<FJsonObject> FuncArgs = MakeShared<FJsonObject>();
		FuncArgs->SetStringField(TEXT("blueprint_path"), BlueprintPath);
		for (auto& Pair : (*ObjPtr)->Values)
			FuncArgs->SetField(FString(*Pair.Key), Pair.Value);

		if (!FuncArgs->HasField(TEXT("function_name")))
		{
			FString NameAlias;
			if ((*ObjPtr)->TryGetStringField(TEXT("name"), NameAlias))
				FuncArgs->SetStringField(TEXT("function_name"), NameAlias);
		}

		FString FuncOut, FuncErr;
		HandleAddFunction(FuncArgs, FuncOut, FuncErr);
		if (FuncErr.IsEmpty())
		{
			++Added;
			FString FuncName; FuncArgs->TryGetStringField(TEXT("function_name"), FuncName);
			CreatedNames.Add(MakeShared<FJsonValueString>(FuncName));
		}
		else
		{
			FString FuncName; FuncArgs->TryGetStringField(TEXT("function_name"), FuncName);
			Errors.Add(FString::Printf(TEXT("%s: %s"), *FuncName, *FuncErr));
			++Failed;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	const bool bAllFailed = (Failed > 0 && Added == 0);
	Result->SetBoolField(TEXT("success"), !bAllFailed);
	Result->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Result->SetNumberField(TEXT("added"), Added);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("created"), CreatedNames);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors) ErrArr.Add(MakeShared<FJsonValueString>(E));
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("%d function(s) ready in '%s'. Use build_blueprint_graph(graph_name='Name') for each to add logic.%s"),
		Added, *FPaths::GetBaseFilename(BlueprintPath),
		Failed > 0 ? *FString::Printf(TEXT(" %d failed — see errors array."), Failed) : TEXT("")));

	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleOverrideFunction(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, FunctionName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("function_name"), FunctionName) || FunctionName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: function_name"); return; }

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath); return; }

	for (UEdGraph* G : BP->FunctionGraphs)
	{
		if (G && G->GetFName().ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("success"), true);
			R->SetStringField(TEXT("function_name"), FunctionName);
			R->SetStringField(TEXT("message"), FString::Printf(TEXT("Override '%s' already exists. Use build_blueprint_graph with graph_name='%s' to add logic."), *FunctionName, *FunctionName));
			OutJsonString = GraphEditHelpers::JsonToString(R);
			return;
		}
	}

	UClass* ParentClass = BP->ParentClass;
	if (!ParentClass) { OutError = TEXT("Blueprint has no parent class"); return; }

	UFunction* ParentFunc = nullptr;
	for (TFieldIterator<UFunction> It(ParentClass, EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		if (It->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			ParentFunc = *It;
			break;
		}
	}
	if (!ParentFunc)
	{ OutError = FString::Printf(TEXT("Function '%s' not found in parent class hierarchy of '%s'"), *FunctionName, *ParentClass->GetName()); return; }

	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Override Function %s"), *FunctionName)));
	BP->Modify();

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, false, ParentClass);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("function_name"), FunctionName);
	Result->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Override for '%s' created. Use build_blueprint_graph(blueprint_path, graph_name='%s') to add logic. Entry/return nodes already exist — do NOT add them."),
		*FunctionName, *FunctionName));
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleAddTimeline(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, TimelineName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("timeline_name"), TimelineName) || TimelineName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: timeline_name"); return; }

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath); return; }

	UEdGraph* EventGraph = nullptr;
	for (UEdGraph* G : BP->UbergraphPages)
	{
		if (G) { EventGraph = G; break; }
	}
	if (!EventGraph) { OutError = TEXT("No EventGraph found in Blueprint"); return; }

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (UK2Node_Timeline* TL = Cast<UK2Node_Timeline>(Node))
		{
			if (TL->TimelineName.ToString().Equals(TimelineName, ESearchCase::IgnoreCase))
			{
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetBoolField(TEXT("success"), true);
				R->SetStringField(TEXT("timeline_name"), TimelineName);
				R->SetStringField(TEXT("message"), FString::Printf(TEXT("Timeline '%s' already exists. Reference with k2.Timeline.%s in build_blueprint_graph."), *TimelineName, *TimelineName));
				OutJsonString = GraphEditHelpers::JsonToString(R);
				return;
			}
		}
	}

	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Add Timeline %s"), *TimelineName)));
	BP->Modify();
	EventGraph->Modify();

	FName TLName(*TimelineName);
	UTimelineTemplate* Template = FBlueprintEditorUtils::AddNewTimeline(BP, TLName);
	if (!Template)
	{
		Template = BP->FindTimelineTemplateByVariableName(TLName);
	}
	if (!Template) { OutError = TEXT("Failed to create or find timeline template"); return; }

	bool bAutoPlay = false, bLoop = false;
	double LengthD = 1.0;
	Args->TryGetBoolField(TEXT("auto_play"), bAutoPlay);
	Args->TryGetBoolField(TEXT("loop"), bLoop);
	Args->TryGetNumberField(TEXT("length"), LengthD);
	Template->TimelineLength = (float)LengthD;

	UK2Node_Timeline* TimelineNode = nullptr;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (UK2Node_Timeline* TL = Cast<UK2Node_Timeline>(Node))
		{
			if (TL->TimelineName == TLName) { TimelineNode = TL; break; }
		}
	}
	if (!TimelineNode)
	{
		TimelineNode = NewObject<UK2Node_Timeline>(EventGraph);
		TimelineNode->TimelineName = TLName;
		TimelineNode->NodePosX = -400;
		TimelineNode->NodePosY = -200;
		TimelineNode->SetFlags(RF_Transactional);
		EventGraph->AddNode(TimelineNode, false, false);
		TimelineNode->CreateNewGuid();
		TimelineNode->PostPlacedNewNode();
	}
	TimelineNode->bAutoPlay = bAutoPlay;
	TimelineNode->bLoop = bLoop;

	const TArray<TSharedPtr<FJsonValue>>* TracksArray = nullptr;
	TArray<FString> AddedTracks;
	if (Args->TryGetArrayField(TEXT("tracks"), TracksArray) && TracksArray)
	{
		for (const TSharedPtr<FJsonValue>& TrackVal : *TracksArray)
		{
			const TSharedPtr<FJsonObject>* TrackObjPtr;
			if (!TrackVal->TryGetObject(TrackObjPtr)) continue;
			const TSharedPtr<FJsonObject>& TrackObj = *TrackObjPtr;

			FString TrackType, TrackName;
			TrackObj->TryGetStringField(TEXT("type"), TrackType);
			TrackObj->TryGetStringField(TEXT("name"), TrackName);
			if (TrackName.IsEmpty()) continue;

			if (TrackType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
			{
				UCurveFloat* Curve = NewObject<UCurveFloat>(Template, *FString::Printf(TEXT("%s_%s_Curve"), *TimelineName, *TrackName));
				const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
				if (TrackObj->TryGetArrayField(TEXT("keys"), KeysArray) && KeysArray)
				{
					for (const TSharedPtr<FJsonValue>& KeyVal : *KeysArray)
					{
						const TSharedPtr<FJsonObject>* KeyObjPtr;
						if (!KeyVal->TryGetObject(KeyObjPtr)) continue;
						double Time = 0.0, Value = 0.0;
						(*KeyObjPtr)->TryGetNumberField(TEXT("time"), Time);
						(*KeyObjPtr)->TryGetNumberField(TEXT("value"), Value);
						Curve->FloatCurve.AddKey((float)Time, (float)Value);
					}
				}
				else
				{
					Curve->FloatCurve.AddKey(0.0f, 0.0f);
					Curve->FloatCurve.AddKey(Template->TimelineLength, 1.0f);
				}
				FTTFloatTrack Track;
				Track.SetTrackName(FName(*TrackName), Template);
				Track.CurveFloat = Curve;
				Template->FloatTracks.Add(Track);
				AddedTracks.Add(FString::Printf(TEXT("float:%s"), *TrackName));
			}
			else if (TrackType.Equals(TEXT("event"), ESearchCase::IgnoreCase))
			{
				UCurveFloat* KeyCurve = NewObject<UCurveFloat>(Template, *FString::Printf(TEXT("%s_%s_Keys"), *TimelineName, *TrackName), RF_Public);
				const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
				if (TrackObj->TryGetArrayField(TEXT("keys"), KeysArray) && KeysArray)
				{
					for (const TSharedPtr<FJsonValue>& KeyVal : *KeysArray)
					{
						const TSharedPtr<FJsonObject>* KeyObjPtr;
						if (!KeyVal->TryGetObject(KeyObjPtr)) continue;
						double Time = 0.0;
						(*KeyObjPtr)->TryGetNumberField(TEXT("time"), Time);
						KeyCurve->FloatCurve.AddKey((float)Time, 1.0f);
					}
				}
				FTTEventTrack Track;
				Track.SetTrackName(FName(*TrackName), Template);
				Track.CurveKeys = KeyCurve;
				Template->EventTracks.Add(Track);
				AddedTracks.Add(FString::Printf(TEXT("event:%s"), *TrackName));
			}
			else if (TrackType.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
			{
				UCurveVector* Curve = NewObject<UCurveVector>(Template, *FString::Printf(TEXT("%s_%s_Curve"), *TimelineName, *TrackName), RF_Public);
				FTTVectorTrack Track;
				Track.SetTrackName(FName(*TrackName), Template);
				Track.CurveVector = Curve;
				Template->VectorTracks.Add(Track);
				AddedTracks.Add(FString::Printf(TEXT("vector:%s"), *TrackName));
			}
		}
	}

	TimelineNode->ReconstructNode();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipSave);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("timeline_name"), TimelineName);
	Result->SetStringField(TEXT("handle"), FString::Printf(TEXT("k2.Timeline.%s"), *TimelineName));
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Timeline '%s' created. Reference with handle 'k2.Timeline.%s' in build_blueprint_graph. Use var.get.%s for component ref (e.g. SetPlayRate target). Pins: Play/PlayFromStart/Stop/Reverse/ReverseFromEnd (exec in), Update/Finished (exec out), Direction (byte out)%s"),
		*TimelineName, *TimelineName, *TimelineName,
		AddedTracks.Num() > 0 ? *FString::Printf(TEXT(", track outputs: %s"), *FString::Join(AddedTracks, TEXT(", "))) : TEXT("")));
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleClearGraph(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, GraphName = TEXT("EventGraph"), AnchorId, FollowMode = TEXT("both");
	int32 Hops = 5;
	bool bForce = false;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	Args->TryGetStringField(TEXT("anchor_id"), AnchorId);
	Args->TryGetStringField(TEXT("follow"),    FollowMode);
	Args->TryGetBoolField  (TEXT("force"),     bForce);
	if (Args->HasField(TEXT("hops"))) Hops = FMath::Clamp((int32)Args->GetNumberField(TEXT("hops")), 1, 10);

	const bool bFollowExec = !FollowMode.Equals(TEXT("data"), ESearchCase::IgnoreCase);
	const bool bFollowData = FollowMode.Equals(TEXT("data"), ESearchCase::IgnoreCase) || FollowMode.Equals(TEXT("both"), ESearchCase::IgnoreCase);

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath); return; }

	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return; }

	TArray<UEdGraphNode*> ToRemove;
	bool bAnchorMode = !AnchorId.IsEmpty();

	if (bAnchorMode)
	{
		UEdGraphNode* Anchor = GraphEditHelpers::FindNodeInGraph(Graph, AnchorId);
		if (!Anchor)
		{
			OutError = FString::Printf(TEXT("Anchor node '%s' not found in graph '%s'. Pass a node id from get_blueprint_graph (friendly id, GEID tag, or hex GUID)."),
				*AnchorId, *GraphName);
			return;
		}
		const int32 NodeCap = 200;
		TSet<UEdGraphNode*> Reached;
		Reached.Add(Anchor);
		TArray<UEdGraphNode*> Frontier; Frontier.Add(Anchor);
		bool bTruncated = false;
		for (int32 D = 0; D < Hops && !bTruncated; ++D)
		{
			TArray<UEdGraphNode*> Next;
			for (UEdGraphNode* Node : Frontier)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->LinkedTo.Num() == 0) continue;
					const bool bIsExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
					if (bIsExec && !bFollowExec) continue;
					if (!bIsExec && !bFollowData) continue;
					for (UEdGraphPin* L : Pin->LinkedTo)
					{
						UEdGraphNode* Other = L ? L->GetOwningNode() : nullptr;
						if (!Other || Reached.Contains(Other)) continue;
						if (Reached.Num() >= NodeCap) { bTruncated = true; break; }
						Reached.Add(Other);
						Next.Add(Other);
					}
					if (bTruncated) break;
				}
				if (bTruncated) break;
			}
			Frontier = MoveTemp(Next);
		}
		for (UEdGraphNode* N : Reached) ToRemove.Add(N);
	}
	else
	{
		int32 UserNodeCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node)) continue;
			if (IsClearProtectedNode(Node)) continue;
			++UserNodeCount;
		}

		const int32 Threshold = 5;
		if (!bForce && UserNodeCount > Threshold)
		{
			TArray<FString> SampleTitles;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node) || IsClearProtectedNode(Node)) continue;
				SampleTitles.Add(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
				if (SampleTitles.Num() >= 6) break;
			}
			const FString Sample = SampleTitles.Num() > 0 ? FString::Join(SampleTitles, TEXT(", ")) : FString();
			OutError = FString::Printf(
				TEXT("REFUSED: graph '%s' has %d user nodes — bulk-clearing that much logic is almost always a mistake. ")
				TEXT("Did you mean to clear one event chain? Pass `anchor_id=<event-node-id>` (from get_blueprint_graph) to ")
				TEXT("delete only that chain via BFS — the rest of the graph stays intact. ")
				TEXT("If you genuinely want to wipe the whole graph, retry with `force=true`. ")
				TEXT("Sample of nodes that would be deleted: [%s]."),
				*GraphName, UserNodeCount, *Sample);
			return;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsValid(Node)) continue;
			if (IsClearProtectedNode(Node)) continue;
			ToRemove.Add(Node);
		}
	}

	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Clear Graph %s"), *GraphName)));
	BP->Modify();
	Graph->Modify();

	int32 ClearedCount = 0;
	TArray<TSharedPtr<FJsonValue>> ClearedNames;
	for (UEdGraphNode* Node : ToRemove)
	{
		if (!IsValid(Node)) continue;
		ClearedNames.Add(MakeShared<FJsonValueString>(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
		Node->Modify();
		Node->DestroyNode();
		ClearedCount++;
	}

	TArray<TSharedPtr<FJsonValue>> PreservedNames;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!IsValid(Node) || Node->IsA<UEdGraphNode_Comment>()) continue;
		PreservedNames.Add(MakeShared<FJsonValueString>(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("graph_name"), GraphName);
	Result->SetStringField(TEXT("mode"), bAnchorMode ? TEXT("anchored") : (bForce ? TEXT("force_full") : TEXT("full")));
	if (bAnchorMode) Result->SetStringField(TEXT("anchor_id"), AnchorId);
	Result->SetNumberField(TEXT("cleared_nodes"), ClearedCount);
	if (ClearedNames.Num()   > 0) Result->SetArrayField(TEXT("cleared"),   ClearedNames);
	if (PreservedNames.Num() > 0) Result->SetArrayField(TEXT("preserved"), PreservedNames);
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleUndoLastClear(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor unavailable — cannot undo.");
		return;
	}

	const bool bOk = GEditor->UndoTransaction( true);
	if (!bOk)
	{
		OutError = TEXT("UndoTransaction returned false — undo stack may be empty.");
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("note"), TEXT(
		"Undid the topmost editor transaction (typically the most recent clear / build). "
		"If you ran another graph-mutating tool after the unwanted clear, this rolled back "
		"that follow-up instead — call again to undo the previous step. "
		"Always verify state with get_blueprint_graph(detail='outline')."
	));
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleSetTimelineProperties(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, TimelineName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("timeline_name"), TimelineName) || TimelineName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: timeline_name"); return; }

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath); return; }

	UTimelineTemplate* Template = BP->FindTimelineTemplateByVariableName(FName(*TimelineName));
	if (!Template) { OutError = FString::Printf(TEXT("Timeline '%s' not found in Blueprint"), *TimelineName); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Set Timeline Properties")));
	BP->Modify();
	Template->Modify();

	double Length = Template->TimelineLength;
	if (Args->HasField(TEXT("length"))) Args->TryGetNumberField(TEXT("length"), Length);
	Template->TimelineLength = (float)Length;

	UK2Node_Timeline* TimelineNode = nullptr;
	for (UEdGraph* G : BP->UbergraphPages)
	{
		if (!G) continue;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UK2Node_Timeline* TL = Cast<UK2Node_Timeline>(Node))
			{
				if (TL->TimelineName.ToString().Equals(TimelineName, ESearchCase::IgnoreCase))
				{ TimelineNode = TL; break; }
			}
		}
		if (TimelineNode) break;
	}

	if (TimelineNode)
	{
		TimelineNode->Modify();
		bool bLoopVal = TimelineNode->bLoop != 0;
		bool bAutoPlayVal = TimelineNode->bAutoPlay != 0;
		if (Args->HasField(TEXT("loop")))      Args->TryGetBoolField(TEXT("loop"), bLoopVal);
		if (Args->HasField(TEXT("auto_play"))) Args->TryGetBoolField(TEXT("auto_play"), bAutoPlayVal);
		TimelineNode->bLoop     = bLoopVal ? 1 : 0;
		TimelineNode->bAutoPlay = bAutoPlayVal ? 1 : 0;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("timeline_name"), TimelineName);
	Result->SetNumberField(TEXT("length"), Template->TimelineLength);
	if (TimelineNode)
	{
		Result->SetBoolField(TEXT("loop"), TimelineNode->bLoop);
		Result->SetBoolField(TEXT("auto_play"), TimelineNode->bAutoPlay);
	}
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleAddTimelineTrack(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, TimelineName, TrackType, TrackName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("timeline_name"), TimelineName) || TimelineName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: timeline_name"); return; }
	if (!Args->TryGetStringField(TEXT("track_type"), TrackType) || TrackType.IsEmpty())
	{ OutError = TEXT("Missing required parameter: track_type (float|vector|event)"); return; }
	if (!Args->TryGetStringField(TEXT("track_name"), TrackName) || TrackName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: track_name"); return; }

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath); return; }

	UTimelineTemplate* Template = BP->FindTimelineTemplateByVariableName(FName(*TimelineName));
	if (!Template) { OutError = FString::Printf(TEXT("Timeline '%s' not found"), *TimelineName); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Add Timeline Track")));
	BP->Modify();
	Template->Modify();

	const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
	Args->TryGetArrayField(TEXT("keys"), KeysArray);

	FString AddedTrack;
	if (TrackType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
	{
		UCurveFloat* Curve = NewObject<UCurveFloat>(Template, *FString::Printf(TEXT("%s_%s_Curve"), *TimelineName, *TrackName), RF_Public);
		if (KeysArray)
		{
			for (const TSharedPtr<FJsonValue>& KV : *KeysArray)
			{
				const TSharedPtr<FJsonObject>* KO;
				if (!KV->TryGetObject(KO)) continue;
				double T = 0.0, V = 0.0;
				(*KO)->TryGetNumberField(TEXT("time"), T);
				(*KO)->TryGetNumberField(TEXT("value"), V);
				Curve->FloatCurve.AddKey((float)T, (float)V);
			}
		}
		else
		{
			Curve->FloatCurve.AddKey(0.0f, 0.0f);
			Curve->FloatCurve.AddKey(Template->TimelineLength, 1.0f);
		}
		FTTFloatTrack Track;
		Track.SetTrackName(FName(*TrackName), Template);
		Track.CurveFloat = Curve;
		Template->FloatTracks.Add(Track);
		AddedTrack = FString::Printf(TEXT("float:%s"), *TrackName);
	}
	else if (TrackType.Equals(TEXT("event"), ESearchCase::IgnoreCase))
	{
		UCurveFloat* KeyCurve = NewObject<UCurveFloat>(Template, *FString::Printf(TEXT("%s_%s_Keys"), *TimelineName, *TrackName), RF_Public);
		if (KeysArray)
		{
			for (const TSharedPtr<FJsonValue>& KV : *KeysArray)
			{
				const TSharedPtr<FJsonObject>* KO;
				if (!KV->TryGetObject(KO)) continue;
				double T = 0.0;
				(*KO)->TryGetNumberField(TEXT("time"), T);
				KeyCurve->FloatCurve.AddKey((float)T, 1.0f);
			}
		}
		FTTEventTrack Track;
		Track.SetTrackName(FName(*TrackName), Template);
		Track.CurveKeys = KeyCurve;
		Template->EventTracks.Add(Track);
		AddedTrack = FString::Printf(TEXT("event:%s"), *TrackName);
	}
	else if (TrackType.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
	{
		UCurveVector* Curve = NewObject<UCurveVector>(Template, *FString::Printf(TEXT("%s_%s_Curve"), *TimelineName, *TrackName), RF_Public);
		FTTVectorTrack Track;
		Track.SetTrackName(FName(*TrackName), Template);
		Track.CurveVector = Curve;
		Template->VectorTracks.Add(Track);
		AddedTrack = FString::Printf(TEXT("vector:%s"), *TrackName);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown track_type '%s'. Use: float|vector|event"), *TrackType);
		return;
	}

	for (UEdGraph* G : BP->UbergraphPages)
	{
		if (!G) continue;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UK2Node_Timeline* TL = Cast<UK2Node_Timeline>(Node))
			{
				if (TL->TimelineName.ToString().Equals(TimelineName, ESearchCase::IgnoreCase))
				{
					TL->ReconstructNode();
					break;
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("timeline_name"), TimelineName);
	Result->SetStringField(TEXT("added_track"), AddedTrack);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Track '%s' added to timeline '%s'. Rebuild references with k2.Timeline.%s in build_blueprint_graph."),
		*TrackName, *TimelineName, *TimelineName));
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandlePlaceNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("nodes"), ItemsArray))
	{
		FString BlueprintPath, GraphName;
		Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
		Args->TryGetStringField(TEXT("graph_name"), GraphName);

		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = SafeAsObject((*ItemsArray)[i]);
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			if (!Item->HasField(TEXT("blueprint_path")) && !BlueprintPath.IsEmpty())
				Item->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			if (!Item->HasField(TEXT("graph_name")) && !GraphName.IsEmpty())
				Item->SetStringField(TEXT("graph_name"), GraphName);
			FString ItemOut, ItemErr;
			HandlePlaceNode(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	HandlePlaceNode(Args, OutJsonString, OutError);
}

void HandleSetPinDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("defaults"), ItemsArray))
	{
		FString BlueprintPath, GraphName;
		Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
		Args->TryGetStringField(TEXT("graph_name"), GraphName);

		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = SafeAsObject((*ItemsArray)[i]);
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			if (!Item->HasField(TEXT("blueprint_path")) && !BlueprintPath.IsEmpty())
				Item->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			if (!Item->HasField(TEXT("graph_name")) && !GraphName.IsEmpty())
				Item->SetStringField(TEXT("graph_name"), GraphName);
			FString ItemOut, ItemErr;
			HandleSetPinDefault(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	HandleSetPinDefault(Args, OutJsonString, OutError);
}

void HandleGetBlueprintSkeletonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP) { OutError = NotABlueprintHint(BlueprintPath); return; }

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Root->SetStringField(TEXT("blueprint_class"), BP->GetClass()->GetName());
	if (BP->ParentClass) Root->SetStringField(TEXT("parent_class"), BP->ParentClass->GetName());

	TArray<TSharedPtr<FJsonValue>> InterfacesArr;
	if (BP->ParentClass)
	{
		for (const FImplementedInterface& Iface : BP->ParentClass->Interfaces)
		{
			if (!Iface.Class) continue;
			TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
			I->SetStringField(TEXT("name"), Iface.Class->GetName());
			I->SetStringField(TEXT("source"), TEXT("native_parent"));
			InterfacesArr.Add(MakeShared<FJsonValueObject>(I));
		}
	}
	for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
	{
		if (!Iface.Interface) continue;
		TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
		I->SetStringField(TEXT("name"), Iface.Interface->GetName());
		I->SetStringField(TEXT("source"), TEXT("blueprint"));
		InterfacesArr.Add(MakeShared<FJsonValueObject>(I));
	}
	if (InterfacesArr.Num() > 0) Root->SetArrayField(TEXT("interfaces"), InterfacesArr);

	TArray<TSharedPtr<FJsonValue>> GraphsArr;
	TSet<UEdGraph*> SeenGraphs;
	auto AddGraphEntry = [&](UEdGraph* G, const TCHAR* DefaultType)
	{
		if (!G || SeenGraphs.Contains(G)) return;
		SeenGraphs.Add(G);
		const FString ClassName = G->GetClass()->GetName();
		FString Type = DefaultType;
		if (ClassName == TEXT("AnimationGraph"))                  Type = TEXT("anim_graph");
		else if (ClassName == TEXT("AnimationStateMachineGraph")) Type = TEXT("state_machine");
		else if (ClassName == TEXT("AnimationTransitionGraph"))   Type = TEXT("transition_rule");
		else if (ClassName == TEXT("AnimationStateGraph"))        Type = TEXT("anim_state");
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), G->GetName());
		O->SetStringField(TEXT("type"), Type);
		O->SetNumberField(TEXT("node_count"), G->Nodes.Num());
		GraphsArr.Add(MakeShared<FJsonValueObject>(O));
	};
	{
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		TSet<UEdGraph*> FuncSet(BP->FunctionGraphs);
		TSet<UEdGraph*> MacroSet(BP->MacroGraphs);
		TSet<UEdGraph*> UberSet(BP->UbergraphPages);
		for (UEdGraph* G : AllGraphs)
		{
			if (!G) continue;
			if (FuncSet.Contains(G))       AddGraphEntry(G, TEXT("function"));
			else if (MacroSet.Contains(G)) AddGraphEntry(G, TEXT("macro"));
			else if (UberSet.Contains(G))  AddGraphEntry(G, TEXT("event_graph"));
			else
			{
				UEdGraph* Outer = Cast<UEdGraph>(G->GetOuter());
				AddGraphEntry(G, (Outer && UberSet.Contains(Outer)) ? TEXT("event_graph") : TEXT("function"));
			}
		}
		for (const FBPInterfaceDescription& IfDesc : BP->ImplementedInterfaces)
			for (UEdGraph* G : IfDesc.Graphs) AddGraphEntry(G, TEXT("interface_impl"));
		if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP))
			for (UEdGraph* G : AnimBP->DelegateSignatureGraphs) AddGraphEntry(G, TEXT("delegate_sig"));
	}
	Root->SetArrayField(TEXT("graphs"), GraphsArr);

	TArray<TSharedPtr<FJsonValue>> FunctionsArr, EventsArr, MacrosArr, DispatchersArr;
	TMap<UEdGraph*, FString> InterfaceImplMap;
	for (const FBPInterfaceDescription& IfDesc : BP->ImplementedInterfaces)
	{
		if (!IfDesc.Interface) continue;
		const FString IfName = IfDesc.Interface->GetName();
		for (UEdGraph* G : IfDesc.Graphs) if (G) InterfaceImplMap.Add(G, IfName);
	}
	{
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			const FString GraphClassName = Graph->GetClass()->GetName();
			if (GraphClassName == TEXT("AnimationStateMachineGraph") ||
				GraphClassName == TEXT("AnimationTransitionGraph") ||
				GraphClassName == TEXT("AnimationStateGraph") ||
				GraphClassName == TEXT("AnimationGraph"))
				continue;

			UK2Node_FunctionEntry* EntryNode = nullptr;
			UK2Node_FunctionResult* ResultNode = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (auto* E = Cast<UK2Node_FunctionEntry>(Node)) EntryNode = E;
				else if (auto* R = Cast<UK2Node_FunctionResult>(Node)) ResultNode = R;
			}

			bool bIsEventGraph = BP->UbergraphPages.Contains(Graph);
			if (!bIsEventGraph)
			{
				UEdGraph* Outer = Cast<UEdGraph>(Graph->GetOuter());
				if (Outer && BP->UbergraphPages.Contains(Outer)) bIsEventGraph = true;
			}
			const bool bIsMacro = BP->MacroGraphs.Contains(Graph);
			const bool bIsDispatcher = BP->DelegateSignatureGraphs.Contains(Graph);

			if (bIsEventGraph)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UK2Node_Event* Ev = Cast<UK2Node_Event>(Node);
					UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node);
					UK2Node_ComponentBoundEvent* CBE = Cast<UK2Node_ComponentBoundEvent>(Node);
					if (!Ev && !CE && !CBE) continue;

					TSharedPtr<FJsonObject> EvObj = MakeShared<FJsonObject>();
					FString RawName, EvType;
					if (CBE)
					{
						RawName = CBE->CustomFunctionName.ToString();
						EvType = TEXT("bound_event");
						EvObj->SetStringField(TEXT("component"), CBE->ComponentPropertyName.ToString());
						EvObj->SetStringField(TEXT("delegate"),  CBE->DelegatePropertyName.ToString());
					}
					else if (CE) { RawName = CE->CustomFunctionName.ToString(); EvType = TEXT("custom_event"); }
					else         { RawName = Ev->EventReference.GetMemberName().ToString(); EvType = TEXT("event"); }
					EvObj->SetStringField(TEXT("name"), RawName);
					EvObj->SetStringField(TEXT("type"), EvType);
					EvObj->SetStringField(TEXT("graph"), Graph->GetName());

					TArray<TSharedPtr<FJsonValue>> Params;
					UEdGraphNode* EvNode = Ev ? (UEdGraphNode*)Ev : (CE ? (UEdGraphNode*)CE : (UEdGraphNode*)CBE);
					for (UEdGraphPin* Pin : EvNode->Pins)
					{
						if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
						if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
						if (Pin->Direction != EGPD_Output) continue;
						if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate &&
							Pin->PinName == TEXT("OutputDelegate")) continue;
						TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
						P->SetStringField(TEXT("name"), Pin->PinName.ToString());
						P->SetStringField(TEXT("type"), PinTypeShort(Pin));
						Params.Add(MakeShared<FJsonValueObject>(P));
					}
					if (Params.Num() > 0) EvObj->SetArrayField(TEXT("params"), Params);

					EventsArr.Add(MakeShared<FJsonValueObject>(EvObj));
				}
				continue;
			}

			if (!EntryNode && !bIsDispatcher && !bIsMacro) continue;

			TSharedPtr<FJsonObject> Sig = MakeShared<FJsonObject>();
			Sig->SetStringField(TEXT("name"), Graph->GetName());

			if (EntryNode)
			{
				bool bIsPure = true;
				for (UEdGraphPin* Pin : EntryNode->Pins)
				{
					if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{ bIsPure = false; break; }
				}
				Sig->SetBoolField(TEXT("is_pure"), bIsPure);

				const uint32 AccessFlags = EntryNode->GetFunctionFlags();
				if (AccessFlags & FUNC_Public) Sig->SetStringField(TEXT("access"), TEXT("public"));
				else if (AccessFlags & FUNC_Protected) Sig->SetStringField(TEXT("access"), TEXT("protected"));
				else Sig->SetStringField(TEXT("access"), TEXT("private"));

				TArray<TSharedPtr<FJsonValue>> Inputs;
				for (UEdGraphPin* Pin : EntryNode->Pins)
				{
					if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
					if (Pin->Direction != EGPD_Output) continue;
					TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
					P->SetStringField(TEXT("name"), Pin->PinName.ToString());
					P->SetStringField(TEXT("type"), PinTypeShort(Pin));
					Inputs.Add(MakeShared<FJsonValueObject>(P));
				}
				if (Inputs.Num() > 0) Sig->SetArrayField(TEXT("inputs"), Inputs);
			}
			if (ResultNode)
			{
				TArray<TSharedPtr<FJsonValue>> Outputs;
				for (UEdGraphPin* Pin : ResultNode->Pins)
				{
					if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
					if (Pin->Direction != EGPD_Input) continue;
					TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
					P->SetStringField(TEXT("name"), Pin->PinName.ToString());
					P->SetStringField(TEXT("type"), PinTypeShort(Pin));
					Outputs.Add(MakeShared<FJsonValueObject>(P));
				}
				if (Outputs.Num() > 0) Sig->SetArrayField(TEXT("outputs"), Outputs);
			}

			if (bIsMacro)            MacrosArr.Add(MakeShared<FJsonValueObject>(Sig));
			else if (bIsDispatcher)  {  }
			else
			{
				if (const FString* IfaceName = InterfaceImplMap.Find(Graph))
				{
					Sig->SetStringField(TEXT("interface"), *IfaceName);
				}
				FunctionsArr.Add(MakeShared<FJsonValueObject>(Sig));
			}
		}
	}

	for (const FBPVariableDescription& V : BP->NewVariables)
	{
		if (V.VarType.PinCategory != UEdGraphSchema_K2::PC_MCDelegate) continue;
		TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("name"), V.VarName.ToString());
		TArray<TSharedPtr<FJsonValue>> Params;
		if (UEdGraph* SigGraph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(BP, V.VarName))
		{
			for (UEdGraphNode* Node : SigGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					for (const TSharedPtr<FUserPinInfo>& Pin : Entry->UserDefinedPins)
					{
						if (!Pin.IsValid()) continue;
						TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
						P->SetStringField(TEXT("name"), Pin->PinName.ToString());
						P->SetStringField(TEXT("type"), PinTypeFromGraphPinType(Pin->PinType));
						Params.Add(MakeShared<FJsonValueObject>(P));
					}
					break;
				}
			}
		}
		D->SetArrayField(TEXT("params"), Params);
		DispatchersArr.Add(MakeShared<FJsonValueObject>(D));
	}

	if (FunctionsArr.Num()   > 0) Root->SetArrayField(TEXT("functions"),   FunctionsArr);
	if (EventsArr.Num()      > 0) Root->SetArrayField(TEXT("events"),      EventsArr);
	if (MacrosArr.Num()      > 0) Root->SetArrayField(TEXT("macros"),      MacrosArr);
	if (DispatchersArr.Num() > 0) Root->SetArrayField(TEXT("dispatchers"), DispatchersArr);

	UObject* CDO = (BP->GeneratedClass && BP->GeneratedClass->GetDefaultObject(false)) ? BP->GeneratedClass->GetDefaultObject() : nullptr;

	TArray<TSharedPtr<FJsonValue>> VariablesArr;
	for (const FBPVariableDescription& V : BP->NewVariables)
	{
		if (V.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate) continue;

		TSharedPtr<FJsonObject> Var = MakeShared<FJsonObject>();
		Var->SetStringField(TEXT("name"), V.VarName.ToString());
		Var->SetStringField(TEXT("type"), PinTypeFromGraphPinType(V.VarType));

		FString DefaultStr = V.DefaultValue;
		if (DefaultStr.IsEmpty() && CDO)
		{
			if (FProperty* Prop = FindFProperty<FProperty>(BP->GeneratedClass, V.VarName))
			{
				FString Exported;
				Prop->ExportText_InContainer(0, Exported, CDO, CDO, CDO, PPF_None);
				if (!Exported.IsEmpty() && Exported != TEXT("false") && Exported != TEXT("0") &&
					Exported != TEXT("0.0") && Exported != TEXT("()") && Exported != TEXT("None"))
					DefaultStr = Exported;
			}
		}
		if (!DefaultStr.IsEmpty()) Var->SetStringField(TEXT("default"), DefaultStr);

		if (!V.Category.IsEmpty() && !V.Category.ToString().Equals(TEXT("Default"))) Var->SetStringField(TEXT("category"), V.Category.ToString());
		if (V.PropertyFlags & CPF_Net) Var->SetStringField(TEXT("replication"), TEXT("Replicated"));
		else if (V.HasMetaData(FName(TEXT("BlueprintReplicationCondition")))) Var->SetStringField(TEXT("replication"), TEXT("RepNotify"));
		if (V.PropertyFlags & CPF_ExposeOnSpawn) Var->SetBoolField(TEXT("exposed_on_spawn"), true);
		if ((V.PropertyFlags & CPF_Edit) && !(V.PropertyFlags & CPF_DisableEditOnInstance)) Var->SetBoolField(TEXT("instance_editable"), true);

		VariablesArr.Add(MakeShared<FJsonValueObject>(Var));
	}
	if (VariablesArr.Num() > 0) Root->SetArrayField(TEXT("variables"), VariablesArr);

	TArray<TSharedPtr<FJsonValue>> ComponentsArr;
	TSet<FString> SeenCompNames;
	TMap<FString, UActorComponent*> CdoCompByName;
	if (AActor* ActorCDO = Cast<AActor>(CDO))
	{
		TArray<UActorComponent*> AllCdoComps; ActorCDO->GetComponents(AllCdoComps);
		for (UActorComponent* CC : AllCdoComps) if (CC) CdoCompByName.Add(CC->GetName(), CC);
	}
	auto AddMeshAsset = [&CdoCompByName](const TSharedPtr<FJsonObject>& C, const FString& CompName, UActorComponent* Template)
	{
		UActorComponent* Eff = Template;
		if (UActorComponent** Found = CdoCompByName.Find(CompName)) Eff = *Found;
		if (const USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Eff))
		{
			if (USkeletalMesh* M = SKC->GetSkeletalMeshAsset())
			{
				C->SetStringField(TEXT("mesh"), M->GetPathName());
				if (USkeleton* S = M->GetSkeleton()) C->SetStringField(TEXT("skeleton"), S->GetPathName());
			}
		}
		else if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Eff))
		{
			if (UStaticMesh* M = SMC->GetStaticMesh()) C->SetStringField(TEXT("mesh"), M->GetPathName());
		}
	};
	if (BP->SimpleConstructionScript)
	{
		TArray<USCS_Node*> Nodes = BP->SimpleConstructionScript->GetAllNodes();
		USCS_Node* RootNode = BP->SimpleConstructionScript->GetDefaultSceneRootNode();
		for (USCS_Node* Node : Nodes)
		{
			if (!Node || !Node->ComponentTemplate) continue;
			const FString CompName = Node->GetVariableName().ToString();
			SeenCompNames.Add(CompName);
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), CompName);
			C->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
			AddMeshAsset(C, CompName, Node->ComponentTemplate);
			if (Node == RootNode) C->SetBoolField(TEXT("root"), true);
			if (USCS_Node* Parent = BP->SimpleConstructionScript->FindParentNode(Node))
				C->SetStringField(TEXT("parent"), Parent->GetVariableName().ToString());
			ComponentsArr.Add(MakeShared<FJsonValueObject>(C));
		}
	}
	if (BP->ParentClass)
	{
		if (UObject* ParentCDO = BP->ParentClass->GetDefaultObject())
		{
			TArray<UObject*> DefaultSubobjects;
			ParentCDO->GetDefaultSubobjects(DefaultSubobjects);
			for (UObject* Sub : DefaultSubobjects)
			{
				UActorComponent* Comp = Cast<UActorComponent>(Sub);
				if (!Comp) continue;
				const FString CompName = Comp->GetName();
				if (SeenCompNames.Contains(CompName)) continue;
				SeenCompNames.Add(CompName);
				TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
				C->SetStringField(TEXT("name"), CompName);
				C->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
				AddMeshAsset(C, CompName, Comp);
				C->SetBoolField(TEXT("inherited"), true);
				ComponentsArr.Add(MakeShared<FJsonValueObject>(C));
			}
		}
		for (UClass* Ancestor = BP->ParentClass; Ancestor; Ancestor = Ancestor->GetSuperClass())
		{
			UBlueprint* AncestorBP = Cast<UBlueprint>(Ancestor->ClassGeneratedBy);
			if (!AncestorBP || !AncestorBP->SimpleConstructionScript) continue;
			for (USCS_Node* Node : AncestorBP->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node || !Node->ComponentTemplate) continue;
				const FString CompName = Node->GetVariableName().ToString();
				if (SeenCompNames.Contains(CompName)) continue;
				SeenCompNames.Add(CompName);
				TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
				C->SetStringField(TEXT("name"), CompName);
				C->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
				AddMeshAsset(C, CompName, Node->ComponentTemplate);
				C->SetBoolField(TEXT("inherited"), true);
				C->SetStringField(TEXT("inherited_from"), Ancestor->GetName());
				ComponentsArr.Add(MakeShared<FJsonValueObject>(C));
			}
		}
	}
	if (ComponentsArr.Num() > 0) Root->SetArrayField(TEXT("components"), ComponentsArr);

	TArray<TSharedPtr<FJsonValue>> TimelinesArr;
	for (UTimelineTemplate* Template : BP->Timelines)
	{
		if (!Template) continue;
		TimelinesArr.Add(MakeShared<FJsonValueString>(Template->GetVariableName().ToString()));
	}
	if (TimelinesArr.Num() > 0) Root->SetArrayField(TEXT("timelines"), TimelinesArr);

	TArray<TSharedPtr<FJsonValue>> TagsArr;
	if (CDO)
	{
		TSet<FString> SeenTags;
		for (TFieldIterator<FStructProperty> It(CDO->GetClass()); It; ++It)
		{
			FStructProperty* Prop = *It;
			if (!Prop || !Prop->Struct) continue;
			if (Prop->Struct->GetFName() != FName("GameplayTagContainer")) continue;
			if (FGameplayTagContainer* Container = Prop->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO))
			{
				for (const FGameplayTag& Tag : *Container)
				{
					FString Name = Tag.GetTagName().ToString();
					if (!SeenTags.Contains(Name))
					{
						SeenTags.Add(Name);
						TagsArr.Add(MakeShared<FJsonValueString>(Name));
					}
				}
			}
		}
	}
	if (TagsArr.Num() > 0) Root->SetArrayField(TEXT("gameplay_tags"), TagsArr);

	OutJsonString = GraphEditHelpers::JsonToString(Root);
}

void HandleGetBlueprintSubgraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, GraphName, AnchorId, FollowMode = TEXT("exec");
	int32 Hops = 2;
	int32 RequestedHops = 2;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: graph_name"); return; }
	if (!Args->TryGetStringField(TEXT("anchor_id"), AnchorId) || AnchorId.IsEmpty())
	{ OutError = TEXT("Missing required parameter: anchor_id"); return; }
	if (Args->HasField(TEXT("hops")))
	{
		RequestedHops = (int32)Args->GetNumberField(TEXT("hops"));
		Hops = FMath::Clamp(RequestedHops, 1, 5);
	}
	Args->TryGetStringField(TEXT("follow"), FollowMode);
	const bool bExec = !FollowMode.Equals(TEXT("data"), ESearchCase::IgnoreCase);
	const bool bData = FollowMode.Equals(TEXT("data"), ESearchCase::IgnoreCase) || FollowMode.Equals(TEXT("both"), ESearchCase::IgnoreCase);

	UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
	if (!BP) { OutError = NotABlueprintHint(BlueprintPath); return; }
	UEdGraph* Graph = GraphEditHelpers::FindGraphByName(BP, GraphName);
	if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return; }
	UEdGraphNode* Anchor = GraphEditHelpers::FindNodeInGraph(Graph, AnchorId);
	if (!Anchor) { OutError = FString::Printf(TEXT("Anchor node '%s' not found in graph '%s'"), *AnchorId, *GraphName); return; }

	const int32 NodeCap = 50;
	TMap<UEdGraphNode*, int32> Distances;
	TArray<UEdGraphNode*> Frontier;
	Distances.Add(Anchor, 0);
	Frontier.Add(Anchor);
	bool bTruncated = false;

	for (int32 Depth = 0; Depth < Hops && !bTruncated; ++Depth)
	{
		TArray<UEdGraphNode*> NextFrontier;
		for (UEdGraphNode* Node : Frontier)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->LinkedTo.Num() == 0) continue;
				const bool bIsExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
				if (bIsExec && !bExec) continue;
				if (!bIsExec && !bData) continue;

				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked) continue;
					UEdGraphNode* Other = Linked->GetOwningNode();
					if (!Other || Distances.Contains(Other)) continue;
					if (Distances.Num() >= NodeCap) { bTruncated = true; break; }
					Distances.Add(Other, Depth + 1);
					NextFrontier.Add(Other);
				}
				if (bTruncated) break;
			}
			if (bTruncated) break;
		}
		Frontier = MoveTemp(NextFrontier);
	}

	TArray<UEdGraphNode*> Ordered;
	Distances.GetKeys(Ordered);
	Ordered.Sort([&](UEdGraphNode& A, UEdGraphNode& B) { return Distances[&A] < Distances[&B]; });

	TSet<FString> InSetIds;
	InSetIds.Reserve(Ordered.Num());
	for (UEdGraphNode* N : Ordered) InSetIds.Add(NodeIdOf(N));

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UEdGraphNode* Node : Ordered)
	{
		TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
		N->SetStringField(TEXT("id"), NodeIdOf(Node));
		N->SetStringField(TEXT("handle"), HandleOf(Node));
		N->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		N->SetNumberField(TEXT("hops_from_anchor"), Distances[Node]);

		TArray<TSharedPtr<FJsonValue>> ExecOuts;
		GetExecOutTargets(Node, ExecOuts);
		ExecOuts.RemoveAll([&InSetIds](const TSharedPtr<FJsonValue>& V)
		{
			TSharedPtr<FJsonObject> O = SafeAsObject(V);
			if (!O.IsValid()) return true;
			FString TargetId;
			if (O->TryGetStringField(TEXT("to"), TargetId))
				return !InSetIds.Contains(TargetId);
			const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
			if (O->TryGetArrayField(TEXT("to"), Targets) && Targets)
			{
				TArray<TSharedPtr<FJsonValue>> Kept;
				for (const TSharedPtr<FJsonValue>& T : *Targets)
				{
					FString Tid = T.IsValid() ? T->AsString() : FString();
					if (InSetIds.Contains(Tid)) Kept.Add(T);
				}
				if (Kept.Num() == 0) return true;
				if (Kept.Num() == 1) O->SetField(TEXT("to"), Kept[0]); else O->SetArrayField(TEXT("to"), Kept);
				return false;
			}
			return true;
		});
		if (ExecOuts.Num() > 0) N->SetArrayField(TEXT("exec_to"), ExecOuts);
		NodesArr.Add(MakeShared<FJsonValueObject>(N));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Root->SetStringField(TEXT("graph_name"), Graph->GetName());
	Root->SetStringField(TEXT("anchor_id"), NodeIdOf(Anchor));
	Root->SetNumberField(TEXT("hops"), Hops);
	if (RequestedHops != Hops) Root->SetNumberField(TEXT("hops_requested"), RequestedHops);
	Root->SetStringField(TEXT("follow"), bExec && bData ? TEXT("both") : (bData ? TEXT("data") : TEXT("exec")));
	Root->SetNumberField(TEXT("node_count"), NodesArr.Num());
	if (bTruncated) Root->SetBoolField(TEXT("truncated"), true);
	Root->SetArrayField(TEXT("nodes"), NodesArr);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = MoveTemp(Out);
}

namespace
{
	UEdGraph* ResolveGraphForCommentTool(const TSharedPtr<FJsonObject>& Args, UBlueprint*& OutBlueprint, FString& OutError)
	{
		OutBlueprint = nullptr;
		FString BlueprintPath, GraphName = TEXT("EventGraph");
		if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
		{ OutError = TEXT("Missing required parameter: blueprint_path"); return nullptr; }
		Args->TryGetStringField(TEXT("graph_name"), GraphName);

		UBlueprint* BP = LoadBlueprintFromPath(BlueprintPath);
		if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath); return nullptr; }
		UEdGraph* Graph = GraphEditHelpers::FindGraphByName(BP, GraphName);
		if (!Graph) { OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName); return nullptr; }

		OutBlueprint = BP;
		return Graph;
	}
}

void HandleAddBlueprintComment(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = nullptr;
	UEdGraph* Graph = ResolveGraphForCommentTool(Args, BP, OutError);
	if (!Graph) return;

	FString Text;
	Args->TryGetStringField(TEXT("text"), Text);
	if (Text.IsEmpty()) Args->TryGetStringField(TEXT("comment"), Text);
	if (Text.IsEmpty()) { OutError = TEXT("Missing required parameter: text"); return; }

	double X = 0, Y = 0, W = 400, H = 200;
	Args->TryGetNumberField(TEXT("x"), X);
	Args->TryGetNumberField(TEXT("y"), Y);
	Args->TryGetNumberField(TEXT("width"),  W);
	Args->TryGetNumberField(TEXT("height"), H);

	const TArray<TSharedPtr<FJsonValue>>* MemberIdsArr = nullptr;
	bool bAutoSizedFromMembers = false;
	if (Args->TryGetArrayField(TEXT("member_node_ids"), MemberIdsArr) && MemberIdsArr && MemberIdsArr->Num() > 0)
	{
		int32 MinX = INT_MAX, MinY = INT_MAX, MaxX = INT_MIN, MaxY = INT_MIN;
		bool bFoundAny = false;
		for (const TSharedPtr<FJsonValue>& IdVal : *MemberIdsArr)
		{
			if (!IdVal.IsValid()) continue;
			const FString IdStr = IdVal->AsString();
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!IsValid(N)) continue;
				if (N->IsA<UEdGraphNode_Comment>()) continue;
				const FString GuidHex = N->NodeGuid.ToString(EGuidFormats::Digits);
				const FString MemberLogicalId = BlueprintNodeIdentity::GetLogicalId(BP, N);
				const bool bGeidMatch = !MemberLogicalId.IsEmpty()
					&& MemberLogicalId.Equals(IdStr, ESearchCase::IgnoreCase);
				if (bGeidMatch || GuidHex.Equals(IdStr, ESearchCase::IgnoreCase))
				{
					MinX = FMath::Min(MinX, N->NodePosX);
					MinY = FMath::Min(MinY, N->NodePosY);
					MaxX = FMath::Max(MaxX, N->NodePosX + 200);
					MaxY = FMath::Max(MaxY, N->NodePosY + 80);
					bFoundAny = true;
					break;
				}
			}
		}
		if (bFoundAny)
		{
			constexpr int32 Pad = 40;
			X = MinX - Pad;
			Y = MinY - Pad - 30;
			W = (MaxX - MinX) + Pad * 2;
			H = (MaxY - MinY) + Pad * 2 + 30;
			bAutoSizedFromMembers = true;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Add Comment")));
	BP->Modify();
	Graph->Modify();

	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(Graph);
	Comment->CreateNewGuid();
	Comment->PostPlacedNewNode();
	Comment->NodePosX     = (int32)X;
	Comment->NodePosY     = (int32)Y;
	Comment->NodeWidth    = (int32)W;
	Comment->NodeHeight   = (int32)H;
	Comment->NodeComment  = Text;
	Comment->FontSize     = 16;
	Comment->MoveMode     = ECommentBoxMode::GroupMovement;
	Comment->bCommentBubbleVisible = false;

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Args->TryGetObjectField(TEXT("color"), ColorObj) && ColorObj && ColorObj->IsValid())
	{
		double R = 0.6, G = 0.6, B = 0.6, A = 1.0;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		(*ColorObj)->TryGetNumberField(TEXT("a"), A);
		Comment->CommentColor = FLinearColor((float)R, (float)G, (float)B, (float)A);
	}

	Graph->AddNode(Comment, false, false);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("id"), Comment->NodeGuid.ToString(EGuidFormats::Digits));
	Result->SetStringField(TEXT("text"), Comment->NodeComment);
	Result->SetNumberField(TEXT("x"), Comment->NodePosX);
	Result->SetNumberField(TEXT("y"), Comment->NodePosY);
	Result->SetNumberField(TEXT("width"), Comment->NodeWidth);
	Result->SetNumberField(TEXT("height"), Comment->NodeHeight);
	if (bAutoSizedFromMembers) Result->SetBoolField(TEXT("auto_sized_from_members"), true);
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleDeleteBlueprintComment(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = nullptr;
	UEdGraph* Graph = ResolveGraphForCommentTool(Args, BP, OutError);
	if (!Graph) return;

	TSet<FString> Wanted;
	bool bDeleteAll = false;
	Args->TryGetBoolField(TEXT("all"), bDeleteAll);

	if (!bDeleteAll)
	{
		const TArray<TSharedPtr<FJsonValue>>* IdsArr = nullptr;
		if (Args->TryGetArrayField(TEXT("comment_ids"), IdsArr) && IdsArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *IdsArr)
				if (V.IsValid()) Wanted.Add(V->AsString());
		}
		FString SingleId;
		if (Args->TryGetStringField(TEXT("comment_id"), SingleId) && !SingleId.IsEmpty())
			Wanted.Add(SingleId);

		if (Wanted.Num() == 0)
		{
			OutError = TEXT("Specify comment_id, comment_ids=[...], or all=true to choose what to delete");
			return;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Comments")));
	BP->Modify();
	Graph->Modify();

	TArray<UEdGraphNode_Comment*> ToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
		if (!Comment || !IsValid(Comment)) continue;
		if (bDeleteAll)
		{
			ToRemove.Add(Comment);
			continue;
		}
		const FString IdStr = Comment->NodeGuid.ToString(EGuidFormats::Digits);
		if (Wanted.Contains(IdStr)) ToRemove.Add(Comment);
	}

	TArray<TSharedPtr<FJsonValue>> DeletedIds;
	for (UEdGraphNode_Comment* C : ToRemove)
	{
		DeletedIds.Add(MakeShared<FJsonValueString>(C->NodeGuid.ToString(EGuidFormats::Digits)));
		C->Modify();
		C->DestroyNode();
	}
	if (ToRemove.Num() > 0) FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("deleted_count"), ToRemove.Num());
	Result->SetArrayField(TEXT("deleted"), DeletedIds);
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

void HandleUpdateBlueprintComment(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = nullptr;
	UEdGraph* Graph = ResolveGraphForCommentTool(Args, BP, OutError);
	if (!Graph) return;

	FString Id;
	if (!Args->TryGetStringField(TEXT("comment_id"), Id) || Id.IsEmpty())
	{ OutError = TEXT("Missing required parameter: comment_id (use list_blueprint_comments to find it)"); return; }

	UEdGraphNode_Comment* Target = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
		if (!Comment) continue;
		if (Comment->NodeGuid.ToString(EGuidFormats::Digits).Equals(Id, ESearchCase::IgnoreCase))
		{ Target = Comment; break; }
	}
	if (!Target) { OutError = FString::Printf(TEXT("Comment with id '%s' not found in graph"), *Id); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("Update Comment")));
	BP->Modify();
	Target->Modify();

	FString NewText;
	if (Args->TryGetStringField(TEXT("text"), NewText)) Target->NodeComment = NewText;
	double V;
	if (Args->TryGetNumberField(TEXT("x"), V))      Target->NodePosX   = (int32)V;
	if (Args->TryGetNumberField(TEXT("y"), V))      Target->NodePosY   = (int32)V;
	if (Args->TryGetNumberField(TEXT("width"),  V)) Target->NodeWidth  = (int32)V;
	if (Args->TryGetNumberField(TEXT("height"), V)) Target->NodeHeight = (int32)V;

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Args->TryGetObjectField(TEXT("color"), ColorObj) && ColorObj && ColorObj->IsValid())
	{
		double R = Target->CommentColor.R, G = Target->CommentColor.G, B = Target->CommentColor.B, A = Target->CommentColor.A;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		(*ColorObj)->TryGetNumberField(TEXT("a"), A);
		Target->CommentColor = FLinearColor((float)R, (float)G, (float)B, (float)A);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("text"), Target->NodeComment);
	OutJsonString = GraphEditHelpers::JsonToString(Result);
}

}

#undef LOCTEXT_NAMESPACE
