// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/PinTypeResolver.h"
#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/UserDefinedEnum.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "WidgetBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UObjectIterator.h"

namespace UECPPinTypes
{

bool ResolvePinTypeFromString(const FString& TypeStrRaw, FEdGraphPinType& OutPinType)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	FString TypeStr = TypeStrRaw;
	{
		static const TPair<FString,FString> ColonAliases[] = {
			{ TEXT("TArray:"), TEXT("TArray<") },
			{ TEXT("Array:"),  TEXT("TArray<") },
			{ TEXT("TSet:"),   TEXT("TSet<")   },
			{ TEXT("Set:"),    TEXT("TSet<")   },
			{ TEXT("TMap:"),   TEXT("TMap<")   },
			{ TEXT("Map:"),    TEXT("TMap<")   },
		};
		for (const auto& Pair : ColonAliases)
		{
			if (TypeStr.StartsWith(Pair.Key, ESearchCase::IgnoreCase))
			{
				TypeStr = Pair.Value + TypeStr.Mid(Pair.Key.Len()) + TEXT(">");
				break;
			}
		}
	}

	FString InnerType = TypeStr;
	FString MapValueTypeStr;

	if (TypeStr.StartsWith(TEXT("TSet<")) && TypeStr.EndsWith(TEXT(">")))
	{
		InnerType = TypeStr.Mid(5, TypeStr.Len() - 6).TrimStartAndEnd();
		OutPinType.ContainerType = EPinContainerType::Set;
	}
	else if (TypeStr.StartsWith(TEXT("Set<")) && TypeStr.EndsWith(TEXT(">")))
	{
		InnerType = TypeStr.Mid(4, TypeStr.Len() - 5).TrimStartAndEnd();
		OutPinType.ContainerType = EPinContainerType::Set;
	}
	else if ((TypeStr.StartsWith(TEXT("TMap<")) || TypeStr.StartsWith(TEXT("Map<"))) && TypeStr.EndsWith(TEXT(">")))
	{
		int32 StartIdx = TypeStr.StartsWith(TEXT("TMap<")) ? 5 : 4;
		FString InnerPart = TypeStr.Mid(StartIdx, TypeStr.Len() - StartIdx - 1).TrimStartAndEnd();
		int32 Depth = 0, SplitIdx = -1;
		for (int32 Ci = 0; Ci < InnerPart.Len(); Ci++)
		{
			TCHAR Ch = InnerPart[Ci];
			if (Ch == '<') Depth++;
			else if (Ch == '>') Depth--;
			else if (Ch == ',' && Depth == 0) { SplitIdx = Ci; break; }
		}
		if (SplitIdx >= 0)
		{
			InnerType = InnerPart.Mid(0, SplitIdx).TrimStartAndEnd();
			MapValueTypeStr = InnerPart.Mid(SplitIdx + 1).TrimStartAndEnd();
			OutPinType.ContainerType = EPinContainerType::Map;
		}
	}
	else if (TypeStr.StartsWith(TEXT("TArray<")) && TypeStr.EndsWith(TEXT(">")))
	{
		InnerType = TypeStr.Mid(7, TypeStr.Len() - 8).TrimStartAndEnd();
		OutPinType.ContainerType = EPinContainerType::Array;
	}
	else if (TypeStr.StartsWith(TEXT("Array<")) && TypeStr.EndsWith(TEXT(">")))
	{
		InnerType = TypeStr.Mid(6, TypeStr.Len() - 7).TrimStartAndEnd();
		OutPinType.ContainerType = EPinContainerType::Array;
	}

		FString TypePrefix;
		{
			static const TPair<const TCHAR*, const TCHAR*> KnownPfx[] = {
				{ TEXT("object:"),      TEXT("object:")      },
				{ TEXT("obj:"),         TEXT("object:")      },
				{ TEXT("class:"),       TEXT("class:")       },
				{ TEXT("c:"),           TEXT("class:")       },
				{ TEXT("softobject:"),  TEXT("softobject:")  },
				{ TEXT("softclass:"),   TEXT("softclass:")   },
				{ TEXT("struct:"),      TEXT("struct:")      },
				{ TEXT("s:"),           TEXT("struct:")      },
				{ TEXT("interface:"),   TEXT("interface:")   },
				{ TEXT("enum:"),        TEXT("enum:")        },
				{ TEXT("byte:"),        TEXT("byte:")        },
			};
			for (const auto& Pfx : KnownPfx)
			{
				FString PfxStr(Pfx.Key);
				if (InnerType.StartsWith(PfxStr, ESearchCase::IgnoreCase))
				{
					TypePrefix = FString(Pfx.Value);
					InnerType = InnerType.Mid(PfxStr.Len()).TrimStartAndEnd();
					break;
				}
			}
		}

		if (InnerType.Equals(TEXT("bool"), ESearchCase::IgnoreCase) ||
			InnerType.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Boolean;
		}
		else if (InnerType.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Byte;
		}
		else if (InnerType.Equals(TEXT("int"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("int32"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Int;
		}
		else if (InnerType.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Int64;
		}
		else if (InnerType.Equals(TEXT("real:float"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Real;
			OutPinType.PinSubCategory = K2Schema->PC_Float;
		}
		else if (InnerType.Equals(TEXT("float"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("real"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("real:double"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("double"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Real;
			OutPinType.PinSubCategory = K2Schema->PC_Double;
		}
		else if (InnerType.Equals(TEXT("string"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_String;
		}
		else if (InnerType.Equals(TEXT("text"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Text;
		}
		else if (InnerType.Equals(TEXT("name"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("FName"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Name;
		}
		else if (InnerType.Equals(TEXT("Vector"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("FVector"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (InnerType.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("FVector2D"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
		}
		else if (InnerType.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else if (InnerType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("FTransform"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		}
		else if (InnerType.Equals(TEXT("FColor"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FColor>::Get();
		}
		else if (InnerType.Equals(TEXT("Color"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("FLinearColor"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("linearcol"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("linear_color"), ESearchCase::IgnoreCase) ||
				 InnerType.Equals(TEXT("linear color"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		}
		else if (InnerType.StartsWith(TEXT("TSubclassOf<")) && InnerType.EndsWith(TEXT(">")))
		{
			FString ClassName = InnerType.Mid(12, InnerType.Len() - 13).TrimStartAndEnd();
			UClass* FoundClass = nullptr;

			FoundClass = FindFirstObjectSafe<UClass>(*ClassName);
			if (!FoundClass)
			{
				FoundClass = UClass::TryFindTypeSlow<UClass>(ClassName);
			}
			if (!FoundClass)
			{
				FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
				FoundClass = LoadObject<UClass>(nullptr, *ScriptPath);
			}
			if (!FoundClass)
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (It->GetName().Equals(ClassName, ESearchCase::IgnoreCase) ||
						It->GetFName().ToString().Equals(ClassName, ESearchCase::IgnoreCase))
					{
						FoundClass = *It;
						break;
					}
				}
			}

			if (FoundClass)
			{
				OutPinType.PinCategory = K2Schema->PC_Class;
				OutPinType.PinSubCategoryObject = FoundClass;
			}
			else
			{
				return false;
			}
		}
		else if (InnerType.StartsWith(TEXT("TSoftObjectPtr<")) && InnerType.EndsWith(TEXT(">")))
		{
			FString AssetPath = InnerType.Mid(15, InnerType.Len() - 16).TrimStartAndEnd();
			UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
			if (UClass* AssetClass = Cast<UClass>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_SoftObject;
				OutPinType.PinSubCategoryObject = AssetClass;
			}
			else if (UBlueprint* BP = Cast<UBlueprint>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_SoftObject;
				OutPinType.PinSubCategoryObject = BP->GeneratedClass;
			}
			else
			{
				return false;
			}
		}
		else if (InnerType.StartsWith(TEXT("TSoftClassPtr<")) && InnerType.EndsWith(TEXT(">")))
		{
			FString AssetPath = InnerType.Mid(14, InnerType.Len() - 15).TrimStartAndEnd();
			UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
			if (UClass* AssetClass = Cast<UClass>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_SoftClass;
				OutPinType.PinSubCategoryObject = AssetClass;
			}
			else if (UBlueprint* BP = Cast<UBlueprint>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_SoftClass;
				OutPinType.PinSubCategoryObject = BP->GeneratedClass;
			}
			else
			{
				return false;
			}
		}
		else if (InnerType.StartsWith(TEXT("/Game/")) || InnerType.StartsWith(TEXT("/Engine/")))
		{
			UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(InnerType);
			if (!LoadedAsset)
			{
				LoadedAsset = LoadObject<UObject>(nullptr, *InnerType);
			}

			if (UBlueprint* BP = Cast<UBlueprint>(LoadedAsset))
			{
				if (BP->GeneratedClass)
				{
					OutPinType.PinCategory = K2Schema->PC_Object;
					OutPinType.PinSubCategoryObject = BP->GeneratedClass;
				}
				else
				{
					OutPinType.PinCategory = K2Schema->PC_Class;
					OutPinType.PinSubCategoryObject = BP->GetClass();
				}
			}
			else if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset))
			{
				if (WidgetBP->GeneratedClass)
				{
					OutPinType.PinCategory = K2Schema->PC_Object;
					OutPinType.PinSubCategoryObject = WidgetBP->GeneratedClass;
				}
				else
				{
					return false;
				}
			}
			else if (UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_Struct;
				OutPinType.PinSubCategoryObject = UserStruct;
			}
			else if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_Struct;
				OutPinType.PinSubCategoryObject = ScriptStruct;
			}
			else if (UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_Byte;
				OutPinType.PinSubCategoryObject = UserEnum;
			}
			else if (UEnum* Enum = Cast<UEnum>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_Byte;
				OutPinType.PinSubCategoryObject = Enum;
			}
			else if (UClass* Class = Cast<UClass>(LoadedAsset))
			{
				OutPinType.PinCategory = K2Schema->PC_Object;
				OutPinType.PinSubCategoryObject = Class;
			}
			else
			{
				return false;
			}
		}
		else if (InnerType.Equals(TEXT("DataTable"), ESearchCase::IgnoreCase) || InnerType.Equals(TEXT("UDataTable"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Object;
			OutPinType.PinSubCategoryObject = UDataTable::StaticClass();
		}
		else if (InnerType.Equals(TEXT("object"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = K2Schema->PC_Object;
			OutPinType.PinSubCategoryObject = UObject::StaticClass();
		}
		else
		{
			FString CleanedInnerType = InnerType;
			if (CleanedInnerType.EndsWith(TEXT("*")))
			{
				CleanedInnerType = CleanedInnerType.LeftChop(1).TrimStartAndEnd();
			}

			UObject* FoundType = LoadObject<UObject>(nullptr, *CleanedInnerType);
			if (!FoundType)
			{
			if (TypePrefix == TEXT("class:") || TypePrefix == TEXT("softclass:"))
				FoundType = FindFirstObjectSafe<UClass>(*CleanedInnerType);
			else
				FoundType = FindFirstObjectSafe<UObject>(*CleanedInnerType);
			}

			if (!FoundType)
			{
				FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s"), *CleanedInnerType);
				FoundType = StaticFindObject(UClass::StaticClass(), nullptr, *ScriptPath);
			}

			if (!FoundType)
			{
				for (const FString& Pfx : {FString(TEXT("A")), FString(TEXT("U"))})
				{
					FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s%s"), *Pfx, *CleanedInnerType);
					FoundType = StaticFindObject(UClass::StaticClass(), nullptr, *ScriptPath);
					if (FoundType) break;
				}
			}

			if (!FoundType && CleanedInnerType.Len() > 1 && (CleanedInnerType.StartsWith(TEXT("A")) || CleanedInnerType.StartsWith(TEXT("U"))))
			{
				FString ShortName = CleanedInnerType.Mid(1);
				FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s"), *ShortName);
				FoundType = StaticFindObject(UClass::StaticClass(), nullptr, *ScriptPath);
			}

			if (!FoundType)
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					FString ClassName = It->GetName();
					if (ClassName.Equals(CleanedInnerType, ESearchCase::IgnoreCase) ||
						(ClassName.Len() > 1 && ClassName.Mid(1).Equals(CleanedInnerType, ESearchCase::IgnoreCase)) ||
						(CleanedInnerType.Len() > 1 && ClassName.Equals(TEXT("A") + CleanedInnerType, ESearchCase::IgnoreCase)) ||
						(CleanedInnerType.Len() > 1 && ClassName.Equals(TEXT("U") + CleanedInnerType, ESearchCase::IgnoreCase)))
					{
						FoundType = *It;
						break;
					}
				}
			}

			if (!FoundType)
			{
				TArray<FString> ScriptStructCandidates;
				ScriptStructCandidates.Add(CleanedInnerType);
				if (CleanedInnerType.StartsWith(TEXT("S_"), ESearchCase::IgnoreCase) && CleanedInnerType.Len() > 2)
					ScriptStructCandidates.AddUnique(CleanedInnerType.Mid(2));
				if (CleanedInnerType.StartsWith(TEXT("F"), ESearchCase::IgnoreCase) && CleanedInnerType.Len() > 1)
					ScriptStructCandidates.AddUnique(CleanedInnerType.Mid(1));
				for (const FString& C : TArray<FString>(ScriptStructCandidates))
					if (C.StartsWith(TEXT("F"), ESearchCase::IgnoreCase) && C.Len() > 1)
						ScriptStructCandidates.AddUnique(C.Mid(1));

				for (TObjectIterator<UScriptStruct> It; It; ++It)
				{
					const FString& SName = It->GetName();
					for (const FString& Candidate : ScriptStructCandidates)
					{
						if (SName.Equals(Candidate, ESearchCase::IgnoreCase))
						{
							FoundType = *It;
							break;
						}
					}
					if (FoundType) break;
				}
			}

			if (!FoundType)
			{
				TArray<FString> NameCandidates;
				NameCandidates.Add(CleanedInnerType);
				if (CleanedInnerType.StartsWith(TEXT("S_"), ESearchCase::IgnoreCase) && CleanedInnerType.Len() > 2)
					NameCandidates.AddUnique(CleanedInnerType.Mid(2));
				if (CleanedInnerType.StartsWith(TEXT("F"), ESearchCase::IgnoreCase) && CleanedInnerType.Len() > 1)
					NameCandidates.AddUnique(CleanedInnerType.Mid(1));
				for (const FString& C : TArray<FString>(NameCandidates))
				{
					if (C.StartsWith(TEXT("F"), ESearchCase::IgnoreCase) && C.Len() > 1)
						NameCandidates.AddUnique(C.Mid(1));
				}

				IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				TArray<FAssetData> StructAssets;
				AR.GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), StructAssets, true);
				for (const FAssetData& Asset : StructAssets)
				{
					const FString AssetName = Asset.AssetName.ToString();
					for (const FString& Candidate : NameCandidates)
					{
						if (AssetName.Equals(Candidate, ESearchCase::IgnoreCase))
						{
							FoundType = Asset.GetAsset();
							break;
						}
					}
					if (FoundType) break;
				}
			}

			if (!FoundType && (TypePrefix == TEXT("object:") || TypePrefix == TEXT("class:") || TypePrefix.IsEmpty()))
			{
				IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				TArray<FTopLevelAssetPath> ClassPaths = {
					UBlueprint::StaticClass()->GetClassPathName(),
					UWidgetBlueprint::StaticClass()->GetClassPathName(),
				};
				for (const FTopLevelAssetPath& ClassPath : ClassPaths)
				{
					TArray<FAssetData> BPAssets;
					AR.GetAssetsByClass(ClassPath, BPAssets, true);
					for (const FAssetData& Asset : BPAssets)
					{
						if (!Asset.AssetName.ToString().Equals(CleanedInnerType, ESearchCase::IgnoreCase)) continue;
						UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
						if (!BP) continue;
						UClass* BPClass = BP->GeneratedClass ? BP->GeneratedClass : BP->SkeletonGeneratedClass;
						if (BPClass) { FoundType = BPClass; break; }
					}
					if (FoundType) break;
				}
			}

			if (!FoundType && (TypePrefix == TEXT("enum:") || TypePrefix == TEXT("byte:") || TypePrefix.IsEmpty()))
			{
				auto StripEnumPrefix = [](const FString& In) -> FString
				{
					return In.StartsWith(TEXT("E_"), ESearchCase::IgnoreCase) ? In.RightChop(2) : In;
				};
				const FString WantNorm = StripEnumPrefix(CleanedInnerType);
				for (TObjectIterator<UEnum> It; It; ++It)
				{
					const FString EnumName = It->GetName();
					if (EnumName.Equals(CleanedInnerType, ESearchCase::IgnoreCase)
						|| StripEnumPrefix(EnumName).Equals(WantNorm, ESearchCase::IgnoreCase))
					{
						FoundType = *It;
						break;
					}
				}
			}

			if (UBlueprint* FoundBP = Cast<UBlueprint>(FoundType))
			{
				UClass* BPCls = FoundBP->GeneratedClass ? FoundBP->GeneratedClass : FoundBP->SkeletonGeneratedClass;
				if (BPCls) FoundType = BPCls;
			}

			if (UClass* FoundClass = Cast<UClass>(FoundType))
			{
				if (TypePrefix == TEXT("class:"))
					OutPinType.PinCategory = K2Schema->PC_Class;
				else if (TypePrefix == TEXT("softclass:"))
					OutPinType.PinCategory = K2Schema->PC_SoftClass;
				else if (TypePrefix == TEXT("softobject:"))
					OutPinType.PinCategory = K2Schema->PC_SoftObject;
				else if (TypePrefix == TEXT("interface:"))
				{
					if (!FoundClass->HasAnyClassFlags(CLASS_Interface))
						return false;
					OutPinType.PinCategory = K2Schema->PC_Interface;
				}
				else if (FoundClass->HasAnyClassFlags(CLASS_Interface))
				{
					OutPinType.PinCategory = K2Schema->PC_Interface;
				}
				else
					OutPinType.PinCategory = K2Schema->PC_Object;
				OutPinType.PinSubCategoryObject = FoundClass;
			}
			else if (UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(FoundType))
			{
				OutPinType.PinCategory = K2Schema->PC_Struct;
				OutPinType.PinSubCategoryObject = UserStruct;
			}
			else if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(FoundType))
			{
				OutPinType.PinCategory = K2Schema->PC_Struct;
				OutPinType.PinSubCategoryObject = ScriptStruct;
			}
			else if (UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(FoundType))
			{
				OutPinType.PinCategory = K2Schema->PC_Byte;
				OutPinType.PinSubCategoryObject = UserEnum;
			}
			else if (UEnum* Enum = Cast<UEnum>(FoundType))
			{
				OutPinType.PinCategory = K2Schema->PC_Byte;
				OutPinType.PinSubCategoryObject = Enum;
			}
			else
			{
				return false;
			}
		}

	if (OutPinType.ContainerType == EPinContainerType::Map && !MapValueTypeStr.IsEmpty())
	{
		if (MapValueTypeStr.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Boolean;
		else if (MapValueTypeStr.Equals(TEXT("int"), ESearchCase::IgnoreCase) || MapValueTypeStr.Equals(TEXT("int32"), ESearchCase::IgnoreCase) || MapValueTypeStr.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Int;
		else if (MapValueTypeStr.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Int64;
		else if (MapValueTypeStr.Equals(TEXT("float"), ESearchCase::IgnoreCase) || MapValueTypeStr.Equals(TEXT("double"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Real;
			OutPinType.PinValueType.TerminalSubCategory = K2Schema->PC_Double;
		}
		else if (MapValueTypeStr.Equals(TEXT("string"), ESearchCase::IgnoreCase) || MapValueTypeStr.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_String;
		else if (MapValueTypeStr.Equals(TEXT("text"), ESearchCase::IgnoreCase))
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Text;
		else if (MapValueTypeStr.Equals(TEXT("name"), ESearchCase::IgnoreCase))
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Name;
		else if (MapValueTypeStr.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Byte;
		else if (MapValueTypeStr.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Struct;
			OutPinType.PinValueType.TerminalSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else
		{
			UObject* ValType = FindFirstObjectSafe<UObject>(*MapValueTypeStr);
			if (!ValType)
			{
				FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s"), *MapValueTypeStr);
				ValType = StaticFindObject(UClass::StaticClass(), nullptr, *ScriptPath);
			}
			if (UClass* ValClass = Cast<UClass>(ValType))
			{
				OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Object;
				OutPinType.PinValueType.TerminalSubCategoryObject = ValClass;
			}
			else if (UScriptStruct* ValStruct = Cast<UScriptStruct>(ValType))
			{
				OutPinType.PinValueType.TerminalCategory = K2Schema->PC_Struct;
				OutPinType.PinValueType.TerminalSubCategoryObject = ValStruct;
			}
		}
	}

	return true;
}

}
