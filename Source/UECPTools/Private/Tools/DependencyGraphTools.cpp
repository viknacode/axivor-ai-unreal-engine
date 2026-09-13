// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/DependencyGraphTools.h"
#include "Utils/MountResolver.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "MCPToolsLog.h"

namespace DependencyGraphTools
{

static FString ClassifyAssetType(const FAssetData& Asset)
{
	const FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();

	if (ClassName == TEXT("WidgetBlueprint"))        return TEXT("widget");
	if (ClassName == TEXT("AnimBlueprint"))           return TEXT("anim_bp");
	if (ClassName == TEXT("BehaviorTree"))            return TEXT("behavior_tree");
	if (ClassName == TEXT("NiagaraSystem"))           return TEXT("niagara");
	if (ClassName == TEXT("NiagaraEmitter"))          return TEXT("niagara");
	if (ClassName == TEXT("Material"))                return TEXT("material");
	if (ClassName == TEXT("MaterialInstance") || ClassName == TEXT("MaterialInstanceConstant")) return TEXT("material");
	if (ClassName == TEXT("DataTable"))               return TEXT("data_table");
	if (ClassName == TEXT("UserDefinedStruct"))       return TEXT("struct");
	if (ClassName == TEXT("UserDefinedEnum"))         return TEXT("enum");
	if (ClassName == TEXT("SoundCue") || ClassName == TEXT("SoundWave")) return TEXT("audio");
	if (ClassName == TEXT("Texture2D") || ClassName == TEXT("TextureCube")) return TEXT("texture");
	if (ClassName == TEXT("StaticMesh"))              return TEXT("static_mesh");
	if (ClassName == TEXT("SkeletalMesh"))            return TEXT("skeletal_mesh");
	if (ClassName == TEXT("LevelSequence"))           return TEXT("sequence");
	if (ClassName == TEXT("Blueprint"))
	{
		FString ParentClass;
		Asset.GetTagValue(FName(TEXT("ParentClass")), ParentClass);
		if (ParentClass.Contains(TEXT("Character")))    return TEXT("character");
		if (ParentClass.Contains(TEXT("Pawn")))         return TEXT("pawn");
		if (ParentClass.Contains(TEXT("GameMode")))     return TEXT("game_mode");
		if (ParentClass.Contains(TEXT("PlayerController"))) return TEXT("player_controller");
		if (ParentClass.Contains(TEXT("GameState")))    return TEXT("game_state");
		if (ParentClass.Contains(TEXT("PlayerState")))  return TEXT("player_state");
		if (ParentClass.Contains(TEXT("HUD")))          return TEXT("hud");
		if (ParentClass.Contains(TEXT("ActorComponent"))) return TEXT("component_bp");
		return TEXT("actor");
	}
	return TEXT("other");
}

static FString GetParentBlueprintPath(const FAssetData& Asset)
{
	FString ParentClassPath;
	Asset.GetTagValue(FName(TEXT("ParentClass")), ParentClassPath);
	return ParentClassPath;
}

static int32 GetNodeCount(const FAssetData& Asset)
{
	int32 Count = 0;
	FString NumNodesStr;
	if (Asset.GetTagValue(FName(TEXT("NumReplicatedProperties")), NumNodesStr))
	{
		Count = FCString::Atoi(*NumNodesStr);
	}
	return Count;
}

void HandleGetDependencyGraph(
	const FString& RootPath,
	int32 MaxDepth,
	bool bIncludeEngine,
	FString& OutJsonString,
	FString& OutError)
{
	UE_LOG(LogMCPTool, Log, TEXT("DependencyGraphTools::HandleGetDependencyGraph — root='%s' depth=%d"), *RootPath, MaxDepth);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	if (MaxDepth <= 0) MaxDepth = 3;

	TArray<FAssetData> AllAssets;
	if (!RootPath.IsEmpty())
	{
		FAssetData RootAsset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(RootPath));
		if (!RootAsset.IsValid())
		{
			TArray<FAssetData> Found;
			AssetRegistry.GetAssetsByPackageName(*RootPath, Found);
			if (Found.Num() > 0)
			{
				RootAsset = Found[0];
			}
			else
			{
				OutError = FString::Printf(TEXT("Asset not found: %s"), *RootPath);
				return;
			}
		}
		AllAssets.Add(RootAsset);
	}
	else
	{
		FARFilter Filter;
		for (const FString& Mount : UECPMountResolver::GetUserContentMounts())
		{
			Filter.PackagePaths.Add(FName(*Mount));
		}
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimBlueprint")));
		AssetRegistry.GetAssets(Filter, AllAssets);
	}

	if (AllAssets.Num() == 0)
	{
		OutError = TEXT("No assets found in project");
		return;
	}

	TMap<FName, int32> PackageToNodeId;
	TArray<TSharedPtr<FJsonObject>> Nodes;
	TArray<TSharedPtr<FJsonObject>> Edges;
	int32 NextNodeId = 0;

	auto EnsureNode = [&](const FName& PackageName, const FAssetData* OptAssetData) -> int32
	{
		if (int32* Existing = PackageToNodeId.Find(PackageName))
			return *Existing;

		int32 Id = NextNodeId++;
		PackageToNodeId.Add(PackageName, Id);

		TSharedPtr<FJsonObject> Node = MakeShareable(new FJsonObject);
		Node->SetNumberField(TEXT("id"), Id);

		FString Label = PackageName.ToString();
		int32 LastSlash;
		if (Label.FindLastChar(TEXT('/'), LastSlash))
			Label = Label.Mid(LastSlash + 1);
		Node->SetStringField(TEXT("label"), Label);
		Node->SetStringField(TEXT("path"), PackageName.ToString());

		if (OptAssetData)
		{
			Node->SetStringField(TEXT("type"), ClassifyAssetType(*OptAssetData));
			FString ParentClass;
			OptAssetData->GetTagValue(FName(TEXT("ParentClass")), ParentClass);
			if (!ParentClass.IsEmpty())
			{
				int32 DotIdx;
				if (ParentClass.FindLastChar(TEXT('.'), DotIdx))
					ParentClass = ParentClass.Mid(DotIdx + 1);
				ParentClass.RemoveFromEnd(TEXT("'"));
				Node->SetStringField(TEXT("parent_class"), ParentClass);
			}
		}
		else
		{
			Node->SetStringField(TEXT("type"), TEXT("external"));
		}

		Nodes.Add(Node);
		return Id;
	};

	const TArray<FString> UserMounts = UECPMountResolver::GetUserContentMounts();
	TSet<FName> Visited;
	TArray<TPair<FName, int32>> Queue;

	for (const FAssetData& Asset : AllAssets)
	{
		int32 Id = EnsureNode(Asset.PackageName, &Asset);
		Queue.Add(TPair<FName, int32>(Asset.PackageName, 0));
	}

	while (Queue.Num() > 0)
	{
		auto [PkgName, Depth] = Queue[0];
		Queue.RemoveAt(0);

		if (Visited.Contains(PkgName))
			continue;
		Visited.Add(PkgName);

		if (Depth >= MaxDepth)
			continue;

		int32 FromId = PackageToNodeId.FindChecked(PkgName);

		TArray<FAssetIdentifier> Dependencies;
		AssetRegistry.GetDependencies(PkgName, Dependencies);

		for (const FAssetIdentifier& Dep : Dependencies)
		{
			if (!Dep.IsPackage())
				continue;

			const FString DepStr = Dep.PackageName.ToString();

			if (!bIncludeEngine)
			{
				if (DepStr.StartsWith(TEXT("/Script")) || DepStr.StartsWith(TEXT("/Engine")))
					continue;
				if (!UECPMountResolver::IsPathUnderMount(DepStr, UserMounts))
					continue;
			}

			if (Dep.PackageName == PkgName)
				continue;

			FAssetData DepAssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(DepStr));
			const FAssetData* DepDataPtr = DepAssetData.IsValid() ? &DepAssetData : nullptr;

			int32 ToId = EnsureNode(Dep.PackageName, DepDataPtr);

			TSharedPtr<FJsonObject> Edge = MakeShareable(new FJsonObject);
			Edge->SetNumberField(TEXT("from"), FromId);
			Edge->SetNumberField(TEXT("to"), ToId);
			Edge->SetStringField(TEXT("type"), TEXT("references"));
			Edges.Add(Edge);

			if (UECPMountResolver::IsPathUnderMount(DepStr, UserMounts) && !Visited.Contains(Dep.PackageName))
			{
				Queue.Add(TPair<FName, int32>(Dep.PackageName, Depth + 1));
			}
		}

		TArray<FAssetIdentifier> Referencers;
		AssetRegistry.GetReferencers(PkgName, Referencers);
		for (const FAssetIdentifier& Ref : Referencers)
		{
			if (!Ref.IsPackage()) continue;
			const FString RefStr = Ref.PackageName.ToString();
			if (!UECPMountResolver::IsPathUnderMount(RefStr, UserMounts)) continue;
			if (Ref.PackageName == PkgName) continue;

			TArray<FAssetData> RefAssets;
			AssetRegistry.GetAssetsByPackageName(Ref.PackageName, RefAssets);
			for (const FAssetData& RefAsset : RefAssets)
			{
				FString ParentPath;
				RefAsset.GetTagValue(FName(TEXT("ParentClass")), ParentPath);
				if (ParentPath.Contains(PkgName.ToString()))
				{
					int32 ChildId = EnsureNode(Ref.PackageName, &RefAsset);

					TSharedPtr<FJsonObject> InhEdge = MakeShareable(new FJsonObject);
					InhEdge->SetNumberField(TEXT("from"), FromId);
					InhEdge->SetNumberField(TEXT("to"), ChildId);
					InhEdge->SetStringField(TEXT("type"), TEXT("inherits"));
					Edges.Add(InhEdge);

					if (!Visited.Contains(Ref.PackageName))
						Queue.Add(TPair<FName, int32>(Ref.PackageName, Depth + 1));
				}
			}
		}
	}

	if (Nodes.Num() > 500)
	{
		UE_LOG(LogMCPTool, Warning, TEXT("DependencyGraph: %d nodes, capping at 500"), Nodes.Num());
		Nodes.SetNum(500);
		Edges.RemoveAll([](const TSharedPtr<FJsonObject>& E) {
			return E->GetNumberField(TEXT("from")) >= 500 || E->GetNumberField(TEXT("to")) >= 500;
		});
	}

	TMap<int32, int32> NodeEdgeCounts;
	for (auto& E : Edges)
	{
		int32 From = static_cast<int32>(E->GetNumberField(TEXT("from")));
		int32 To = static_cast<int32>(E->GetNumberField(TEXT("to")));
		NodeEdgeCounts.FindOrAdd(From)++;
		NodeEdgeCounts.FindOrAdd(To)++;
	}
	for (auto& N : Nodes)
	{
		int32 Id = static_cast<int32>(N->GetNumberField(TEXT("id")));
		N->SetNumberField(TEXT("connections"), NodeEdgeCounts.Contains(Id) ? NodeEdgeCounts[Id] : 0);
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject);
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetNumberField(TEXT("node_count"), Nodes.Num());
	ResultObj->SetNumberField(TEXT("edge_count"), Edges.Num());

	TArray<TSharedPtr<FJsonValue>> NodeValues;
	for (auto& N : Nodes) NodeValues.Add(MakeShareable(new FJsonValueObject(N)));
	ResultObj->SetArrayField(TEXT("nodes"), NodeValues);

	TArray<TSharedPtr<FJsonValue>> EdgeValues;
	for (auto& E : Edges) EdgeValues.Add(MakeShareable(new FJsonValueObject(E)));
	ResultObj->SetArrayField(TEXT("edges"), EdgeValues);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("DependencyGraph: %d nodes, %d edges"), Nodes.Num(), Edges.Num());
}

void HandleGetInheritanceTree(
	const FString& RootClass,
	const FString& FolderFilter,
	FString& OutNomnomlString,
	FString& OutError)
{
	UE_LOG(LogMCPTool, Log, TEXT("DependencyGraphTools::HandleGetInheritanceTree — root='%s' folder='%s'"), *RootClass, *FolderFilter);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	if (!FolderFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*FolderFilter));
	}
	else
	{
		for (const FString& Mount : UECPMountResolver::GetUserContentMounts())
		{
			Filter.PackagePaths.Add(FName(*Mount));
		}
	}
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint")));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimBlueprint")));

	TArray<FAssetData> AllBlueprints;
	AssetRegistry.GetAssets(Filter, AllBlueprints);

	if (AllBlueprints.Num() == 0)
	{
		OutError = TEXT("No blueprints found");
		return;
	}

	TMap<FString, TArray<FString>> ParentToChildren;
	TMap<FString, FString> BPToParent;
	TSet<FString> AllNames;

	for (const FAssetData& Asset : AllBlueprints)
	{
		FString BPName = Asset.AssetName.ToString();
		FString ParentClassStr;
		Asset.GetTagValue(FName(TEXT("ParentClass")), ParentClassStr);

		if (ParentClassStr.IsEmpty())
			continue;

		FString ParentName;
		int32 DotIdx;
		if (ParentClassStr.FindLastChar(TEXT('.'), DotIdx))
			ParentName = ParentClassStr.Mid(DotIdx + 1);
		else
			ParentName = ParentClassStr;
		ParentName.RemoveFromEnd(TEXT("'"));

		if (!RootClass.IsEmpty())
		{
			bool bMatchesRoot = ParentName.Contains(RootClass) || BPName.Contains(RootClass);
			if (!bMatchesRoot)
			{
				bool bParentChainMatch = false;
				FString CheckParent = ParentName;
				for (int32 i = 0; i < 10 && !bParentChainMatch; ++i)
				{
					if (CheckParent.Contains(RootClass))
					{
						bParentChainMatch = true;
						break;
					}
					if (FString* GrandParent = BPToParent.Find(CheckParent))
						CheckParent = *GrandParent;
					else
						break;
				}
				if (!bParentChainMatch)
					continue;
			}
		}

		ParentToChildren.FindOrAdd(ParentName).Add(BPName);
		BPToParent.Add(BPName, ParentName);
		AllNames.Add(BPName);
		AllNames.Add(ParentName);
	}

	if (ParentToChildren.Num() == 0)
	{
		OutError = TEXT("No inheritance relationships found");
		return;
	}

	FString Nomnoml;
	Nomnoml += TEXT("#direction: right\n");
	Nomnoml += TEXT("#spacing: 50\n");
	Nomnoml += TEXT("#padding: 12\n");
	Nomnoml += TEXT("#fontSize: 12\n");
	Nomnoml += TEXT("#lineWidth: 2\n");
	Nomnoml += TEXT("#edges: rounded\n");
	Nomnoml += TEXT("#fill: #2d2d30; #3c3c3c\n");
	Nomnoml += TEXT("#stroke: #b0b0b0\n");
	Nomnoml += TEXT("#ranker: longest-path\n\n");

	TSet<FString> UserBPNames;
	for (const FAssetData& Asset : AllBlueprints)
		UserBPNames.Add(Asset.AssetName.ToString());

	TSet<FString> EmittedEdges;
	for (auto& [Parent, Children] : ParentToChildren)
	{
		bool bIsNativeParent = !UserBPNames.Contains(Parent);
		if (bIsNativeParent && Children.Num() > 8)
		{
			Nomnoml += FString::Printf(TEXT("[<package> %s (%d) |\n"), *Parent, Children.Num());
			for (int32 i = 0; i < FMath::Min(Children.Num(), 20); ++i)
			{
				Nomnoml += FString::Printf(TEXT("  [%s]\n"), *Children[i]);
			}
			if (Children.Num() > 20)
				Nomnoml += FString::Printf(TEXT("  [... +%d more]\n"), Children.Num() - 20);
			Nomnoml += TEXT("]\n");
		}
		else
		{
			for (const FString& Child : Children)
			{
				FString EdgeKey = FString::Printf(TEXT("[%s] -> [%s]"), *Parent, *Child);
				if (!EmittedEdges.Contains(EdgeKey))
				{
					Nomnoml += EdgeKey + TEXT("\n");
					EmittedEdges.Add(EdgeKey);
				}
			}
		}
	}

	for (const FAssetData& Asset : AllBlueprints)
	{
		FString Interfaces;
		Asset.GetTagValue(FName(TEXT("ImplementedInterfaces")), Interfaces);
		if (!Interfaces.IsEmpty())
		{
			FString BPName = Asset.AssetName.ToString();
			TArray<FString> InterfaceList;
			Interfaces.ParseIntoArray(InterfaceList, TEXT(","));
			for (FString& Iface : InterfaceList)
			{
				Iface.TrimStartAndEndInline();
				int32 Dot;
				if (Iface.FindLastChar(TEXT('.'), Dot))
					Iface = Iface.Mid(Dot + 1);
				Iface.RemoveFromEnd(TEXT("'"));
				if (!Iface.IsEmpty())
				{
					FString EdgeKey = FString::Printf(TEXT("[<abstract>%s] <:-- [%s]"), *Iface, *BPName);
					if (!EmittedEdges.Contains(EdgeKey))
					{
						Nomnoml += EdgeKey + TEXT("\n");
						EmittedEdges.Add(EdgeKey);
					}
				}
			}
		}
	}

	OutNomnomlString = Nomnoml;
	UE_LOG(LogMCPTool, Log, TEXT("InheritanceTree: %d relationships, %d unique names"), EmittedEdges.Num(), AllNames.Num());
}

void HandleGetDependencyGraphFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString RootPath;
	Args->TryGetStringField(TEXT("root_path"), RootPath);

	int32 MaxDepth = 3;
	if (Args->HasField(TEXT("max_depth")))
		MaxDepth = static_cast<int32>(Args->GetNumberField(TEXT("max_depth")));

	bool bIncludeEngine = false;
	if (Args->HasField(TEXT("include_engine")))
		bIncludeEngine = Args->GetBoolField(TEXT("include_engine"));

	HandleGetDependencyGraph(RootPath, MaxDepth, bIncludeEngine, OutJsonString, OutError);
}

void HandleGetInheritanceTreeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString RootClass, FolderFilter;
	Args->TryGetStringField(TEXT("root_class"), RootClass);
	Args->TryGetStringField(TEXT("folder_filter"), FolderFilter);

	FString Nomnoml;
	HandleGetInheritanceTree(RootClass, FolderFilter, Nomnoml, OutError);
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("nomnoml"), Nomnoml);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
}

}
