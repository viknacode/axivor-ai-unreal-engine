// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/MetaSoundTools.h"
#include "Misc/EngineVersionComparison.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)

#include "Managers/SettingsManager.h"

#include "MetasoundSource.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistryContainer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/Package.h"

namespace MetaSoundTools
{

void HandleCreateMetaSound(const FString& AssetName, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'"), *PackagePath);
		return;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, SavePath, UMetaSoundSource::StaticClass(), nullptr);

	if (!NewAsset)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		NewAsset = NewObject<UMetaSoundSource>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		if (NewAsset)
		{
			FAssetRegistryModule::AssetCreated(NewAsset);
			NewAsset->MarkPackageDirty();
		}
	}

	if (!NewAsset) { OutError = TEXT("Failed to create MetaSound Source asset"); return; }

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"type\":\"MetaSoundSource\",\"message\":\"MetaSound Source created. Open in editor to build the graph, or use duplicate_metasound to clone an existing MetaSound.\"}"),
		*NewAsset->GetPathName());
}

void HandleGetMetaSoundSummary(const FString& AssetPath,
	FString& OutJsonString, FString& OutError)
{

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);
	const FMetasoundFrontendDocument& Doc = Builder.GetConstDocumentChecked();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("class"), Source->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> InputsArr;
	for (const FMetasoundFrontendClassInput& In : Doc.RootGraph.Interface.Inputs)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), In.Name.ToString());
		O->SetStringField(TEXT("type"), In.TypeName.ToString());
		InputsArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("inputs"), InputsArr);

	TArray<TSharedPtr<FJsonValue>> OutputsArr;
	for (const FMetasoundFrontendClassOutput& Out : Doc.RootGraph.Interface.Outputs)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Out.Name.ToString());
		O->SetStringField(TEXT("type"), Out.TypeName.ToString());
		OutputsArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("outputs"), OutputsArr);

	const FMetasoundFrontendGraph& Graph = Builder.FindConstBuildGraphChecked();
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (const FMetasoundFrontendNode& Node : Graph.Nodes)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("node_id"), Node.GetID().ToString());
		O->SetStringField(TEXT("class_id"), Node.ClassID.ToString());
		O->SetStringField(TEXT("name"), Node.Name.ToString());
		NodesArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("nodes"), NodesArr);
	Root->SetNumberField(TEXT("node_count"), Graph.Nodes.Num());
	Root->SetNumberField(TEXT("edge_count"), Graph.Edges.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

static FName ResolveMetaSoundTypeName(const FString& InDataType)
{
	if (InDataType.IsEmpty()) return FName(TEXT("float"));
	const FString L = InDataType.ToLower();
	if (L == TEXT("float"))           return FName(TEXT("float"));
	if (L == TEXT("int") || L == TEXT("int32")) return FName(TEXT("int32"));
	if (L == TEXT("bool"))            return FName(TEXT("bool"));
	if (L == TEXT("string"))          return FName(TEXT("string"));
	if (L == TEXT("trigger"))         return FName(TEXT("Trigger"));
	if (L == TEXT("time"))            return FName(TEXT("Time"));
	if (L == TEXT("audio") || L == TEXT("audio:mono")) return FName(TEXT("Audio"));
	if (L == TEXT("audio:stereo"))    return FName(TEXT("AudioStereo"));
	if (L == TEXT("waveasset") || L == TEXT("wave")) return FName(TEXT("WaveAsset"));
	return FName(*InDataType);
}

static void ApplyTypedLiteral(FName TypeName, const FString& Value, FMetasoundFrontendLiteral& OutLiteral)
{
	const FString L = TypeName.ToString().ToLower();
	if (L == TEXT("float"))      { OutLiteral.Set(Value.IsEmpty() ? 0.0f : FCString::Atof(*Value)); return; }
	if (L == TEXT("int32"))      { OutLiteral.Set((int32)(Value.IsEmpty() ? 0 : FCString::Atoi(*Value))); return; }
	if (L == TEXT("bool"))
	{
		const bool bVal = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("1"));
		OutLiteral.Set(bVal); return;
	}
	if (L == TEXT("time"))       { OutLiteral.Set(Value.IsEmpty() ? 0.0f : FCString::Atof(*Value)); return; }
	OutLiteral.Set(Value);
}

void HandleAddMetaSoundInput(const FString& AssetPath,
	const FString& InputName, const FString& DataType,
	FString& OutJsonString, FString& OutError)
{

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }
	if (InputName.IsEmpty()) { OutError = TEXT("input_name is required"); return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	if (Builder.FindGraphInput(FName(*InputName)))
	{
		OutError = FString::Printf(TEXT("Input '%s' already exists on '%s'"), *InputName, *AssetPath);
		return;
	}

	const FName TypeName = ResolveMetaSoundTypeName(DataType);
	FMetasoundFrontendClassInput ClassInput;
	ClassInput.Name = FName(*InputName);
	ClassInput.TypeName = TypeName;
	ClassInput.VertexID = FGuid::NewGuid();
	ClassInput.NodeID = FGuid::NewGuid();
	ClassInput.AccessType = EMetasoundFrontendVertexAccessType::Reference;
	ClassInput.InitDefault();

	const FMetasoundFrontendNode* NewNode = Builder.AddGraphInput(ClassInput);
	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("AddGraphInput failed for '%s' (type '%s'). Verify the type name is registered."), *InputName, *TypeName.ToString());
		return;
	}

	Source->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"input_name\":\"%s\",\"data_type\":\"%s\",\"node_id\":\"%s\"}"),
		*AssetPath, *InputName, *TypeName.ToString(), *NewNode->GetID().ToString());
}

void HandleAddMetaSoundOutput(const FString& AssetPath,
	const FString& OutputName, const FString& DataType,
	FString& OutJsonString, FString& OutError)
{

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }
	if (OutputName.IsEmpty()) { OutError = TEXT("output_name is required"); return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	if (Builder.FindGraphOutput(FName(*OutputName)))
	{
		OutError = FString::Printf(TEXT("Output '%s' already exists on '%s'"), *OutputName, *AssetPath);
		return;
	}

	const FName TypeName = ResolveMetaSoundTypeName(DataType.IsEmpty() ? TEXT("audio") : DataType);
	FMetasoundFrontendClassOutput ClassOutput;
	ClassOutput.Name = FName(*OutputName);
	ClassOutput.TypeName = TypeName;
	ClassOutput.VertexID = FGuid::NewGuid();
	ClassOutput.NodeID = FGuid::NewGuid();
	ClassOutput.AccessType = EMetasoundFrontendVertexAccessType::Reference;

	const FMetasoundFrontendNode* NewNode = Builder.AddGraphOutput(ClassOutput);
	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("AddGraphOutput failed for '%s' (type '%s'). Verify the type name is registered."), *OutputName, *TypeName.ToString());
		return;
	}

	Source->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"output_name\":\"%s\",\"data_type\":\"%s\",\"node_id\":\"%s\"}"),
		*AssetPath, *OutputName, *TypeName.ToString(), *NewNode->GetID().ToString());
}

void HandleSetMetaSoundDefaultParameter(const FString& AssetPath,
	const FString& ParameterName, const FString& Value,
	FString& OutJsonString, FString& OutError)
{

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }
	if (ParameterName.IsEmpty()) { OutError = TEXT("parameter_name is required"); return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	const FMetasoundFrontendClassInput* ClassInput = Builder.FindGraphInput(FName(*ParameterName));
	if (!ClassInput)
	{
		OutError = FString::Printf(TEXT("Graph input '%s' not found. Add it via add_metasound_input first."), *ParameterName);
		return;
	}

	FMetasoundFrontendLiteral Literal;
	ApplyTypedLiteral(ClassInput->TypeName, Value, Literal);

	if (!Builder.SetGraphInputDefault(FName(*ParameterName), MoveTemp(Literal)))
	{
		OutError = FString::Printf(TEXT("SetGraphInputDefault failed for '%s'"), *ParameterName);
		return;
	}

	Source->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"parameter\":\"%s\",\"type\":\"%s\",\"value\":\"%s\"}"),
		*ParameterName, *ClassInput->TypeName.ToString(), *Value);
}

void HandleDuplicateMetaSound(const FString& SourcePath, const FString& NewAssetName,
	const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	{
		FText Reason;
		if (!FName::IsValidXName(NewAssetName, INVALID_OBJECTNAME_CHARACTERS, &Reason))
		{
			OutError = FString::Printf(
				TEXT("new_asset_name '%s' contains characters UE's asset tools refuse (offending set: %s — note the '.'). Pick a name using only letters, digits, and underscores."),
				*NewAssetName, INVALID_OBJECTNAME_CHARACTERS);
			return;
		}
	}

	FString DestPath = SavePath;
	if (!DestPath.EndsWith(TEXT("/"))) DestPath += TEXT("/");
	DestPath += NewAssetName;

	if (UEditorAssetLibrary::DoesAssetExist(DestPath))
	{
		OutError = FString::Printf(
			TEXT("Destination asset already exists at '%s'. Pick a different new_asset_name, or delete the existing asset first via asset_management(action='delete_asset', asset_path='%s')."),
			*DestPath, *DestPath);
		return;
	}

	UObject* NewObj = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
	if (!NewObj)
	{
		OutError = FString::Printf(TEXT("Failed to duplicate MetaSound from '%s'. Ensure the source exists."), *SourcePath);
		return;
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"new_asset_path\":\"%s\",\"source_path\":\"%s\",\"message\":\"MetaSound duplicated successfully. The copy preserves all graph nodes and connections.\"}"),
		*NewObj->GetPathName(), *SourcePath);
}

void HandleAddMetaSoundNode(const FString& AssetPath, const FString& NodeClassName,
	int32 MajorVersion, FString& OutJsonString, FString& OutError)
{

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset || !Asset->IsA(UMetaSoundSource::StaticClass()))
	{
		OutError = TEXT("MetaSoundSource not found: ") + AssetPath;
		return;
	}
	UMetaSoundSource* Source = Cast<UMetaSoundSource>(Asset);

	FMetasoundFrontendClassName ClassName;
	if (!FMetasoundFrontendClassName::Parse(NodeClassName, ClassName))
	{
		TArray<FString> Parts;
		NodeClassName.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() >= 3)
			ClassName = FMetasoundFrontendClassName(FName(*Parts[0]), FName(*Parts[1]), FName(*Parts[2]));
		else if (Parts.Num() == 2)
			ClassName = FMetasoundFrontendClassName(FName(*Parts[0]), FName(*Parts[1]));
		else
			ClassName = FMetasoundFrontendClassName(FName(), FName(*NodeClassName));
	}

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	FGuid NodeID = FGuid::NewGuid();
	const FMetasoundFrontendNode* NewNode = Builder.AddNodeByClassName(ClassName, MajorVersion > 0 ? MajorVersion : 1, NodeID);

	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("Failed to add node '%s'. Class may not be registered. "
			"Try formats like 'Metasound.Math.Add_Float', 'Metasound.Engine.Oscillator.Sine', "
			"'MetasoundEngine.PlayWave.AudioFile'. Use list_metasound_node_types if available."), *NodeClassName);
		return;
	}

	FString InputsJson = TEXT("[");
	bool bFirst = true;
	for (const FMetasoundFrontendVertex& V : NewNode->Interface.Inputs)
	{
		if (!bFirst) InputsJson += TEXT(",");
		InputsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"type\":\"%s\",\"vertex_id\":\"%s\"}"),
			*V.Name.ToString(), *V.TypeName.ToString(), *V.VertexID.ToString());
		bFirst = false;
	}
	InputsJson += TEXT("]");

	FString OutputsJson = TEXT("[");
	bFirst = true;
	for (const FMetasoundFrontendVertex& V : NewNode->Interface.Outputs)
	{
		if (!bFirst) OutputsJson += TEXT(",");
		OutputsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"type\":\"%s\",\"vertex_id\":\"%s\"}"),
			*V.Name.ToString(), *V.TypeName.ToString(), *V.VertexID.ToString());
		bFirst = false;
	}
	OutputsJson += TEXT("]");

	Source->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"node_id\":\"%s\",\"class_name\":\"%s\",\"inputs\":%s,\"outputs\":%s,\"message\":\"Node added. Use node_id with connect_metasound_nodes.\"}"),
		*NewNode->GetID().ToString(), *NodeClassName, *InputsJson, *OutputsJson);
}

void HandleConnectMetaSoundNodes(const FString& AssetPath,
	const FString& SourceNodeId, const FString& SourceOutputName,
	const FString& DestNodeId, const FString& DestInputName,
	FString& OutJsonString, FString& OutError)
{

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset || !Asset->IsA(UMetaSoundSource::StaticClass()))
	{
		OutError = TEXT("MetaSoundSource not found: ") + AssetPath;
		return;
	}
	UMetaSoundSource* Source = Cast<UMetaSoundSource>(Asset);

	FGuid SrcNodeID, DstNodeID;
	if (!FGuid::Parse(SourceNodeId, SrcNodeID) || !SrcNodeID.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid source_node_id '%s'. Use the node_id returned by add_metasound_node."), *SourceNodeId);
		return;
	}
	if (!FGuid::Parse(DestNodeId, DstNodeID) || !DstNodeID.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid dest_node_id '%s'. Use the node_id returned by add_metasound_node."), *DestNodeId);
		return;
	}

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	const FMetasoundFrontendVertex* SrcOutput = Builder.FindNodeOutput(SrcNodeID, FName(*SourceOutputName));
	if (!SrcOutput)
	{
		TArray<const FMetasoundFrontendVertex*> Outputs = Builder.FindNodeOutputs(SrcNodeID);
		FString Available;
		for (const FMetasoundFrontendVertex* V : Outputs)
			Available += V->Name.ToString() + TEXT(", ");
		OutError = FString::Printf(TEXT("Output '%s' not found. Available outputs: [%s]"), *SourceOutputName, *Available);
		return;
	}

	const FMetasoundFrontendVertex* DstInput = Builder.FindNodeInput(DstNodeID, FName(*DestInputName));
	if (!DstInput)
	{
		TArray<const FMetasoundFrontendVertex*> Inputs = Builder.FindNodeInputs(DstNodeID);
		FString Available;
		for (const FMetasoundFrontendVertex* V : Inputs)
			Available += V->Name.ToString() + TEXT(", ");
		OutError = FString::Printf(TEXT("Input '%s' not found. Available inputs: [%s]"), *DestInputName, *Available);
		return;
	}

	FMetasoundFrontendEdge Edge;
	Edge.FromNodeID = SrcNodeID;
	Edge.FromVertexID = SrcOutput->VertexID;
	Edge.ToNodeID = DstNodeID;
	Edge.ToVertexID = DstInput->VertexID;

	Builder.AddEdge(MoveTemp(Edge));
	Source->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"from_node\":\"%s\",\"from_output\":\"%s\",\"to_node\":\"%s\",\"to_input\":\"%s\"}"),
		*SourceNodeId, *SourceOutputName, *DestNodeId, *DestInputName);
}

void HandleRemoveMetaSoundNode(const FString& AssetPath, const FString& NodeId,
	FString& OutJsonString, FString& OutError)
{

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeId, NodeGuid) || !NodeGuid.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid node_id '%s'"), *NodeId);
		return;
	}

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	if (!Builder.RemoveNode(NodeGuid))
	{
		OutError = FString::Printf(TEXT("Failed to remove node '%s'"), *NodeId);
		return;
	}

	Source->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_node\":\"%s\"}"), *NodeId);
}

void HandleDisconnectMetaSoundNodes(const FString& AssetPath,
	const FString& SourceNodeId, const FString& SourceOutputName,
	const FString& DestNodeId, const FString& DestInputName,
	FString& OutJsonString, FString& OutError)
{

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }

	FGuid SrcNodeID, DstNodeID;
	if (!FGuid::Parse(SourceNodeId, SrcNodeID)) { OutError = TEXT("Invalid source_node_id"); return; }
	if (!FGuid::Parse(DestNodeId, DstNodeID)) { OutError = TEXT("Invalid dest_node_id"); return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	const FMetasoundFrontendVertex* SrcOutput = Builder.FindNodeOutput(SrcNodeID, FName(*SourceOutputName));
	const FMetasoundFrontendVertex* DstInput = Builder.FindNodeInput(DstNodeID, FName(*DestInputName));

	if (!SrcOutput || !DstInput)
	{
		OutError = TEXT("Could not find the specified pins to disconnect");
		return;
	}

	FMetasoundFrontendEdge EdgeToRemove;
	EdgeToRemove.FromNodeID = SrcNodeID;
	EdgeToRemove.FromVertexID = SrcOutput->VertexID;
	EdgeToRemove.ToNodeID = DstNodeID;
	EdgeToRemove.ToVertexID = DstInput->VertexID;
	Builder.RemoveEdge(EdgeToRemove);
	Source->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"disconnected\":\"%s:%s -> %s:%s\"}"),
		*SourceNodeId, *SourceOutputName, *DestNodeId, *DestInputName);
}

void HandleListMetaSoundNodeTypes(const FString& Filter, FString& OutJsonString, FString& OutError)
{

	FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
	if (!Registry)
	{
		OutError = TEXT("MetaSound frontend registry not initialised");
		return;
	}

	const FString FilterLower = Filter.ToLower();
	TArray<TSharedPtr<FJsonValue>> Types;
	const int32 MaxResults = 200;

	Registry->IterateRegistry(
		[&Types, &FilterLower, MaxResults](const FMetasoundFrontendClass& Class)
		{
			if (Types.Num() >= MaxResults) return;
			const FString FullName = Class.Metadata.GetClassName().ToString();
			if (!FilterLower.IsEmpty() && !FullName.ToLower().Contains(FilterLower)) return;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("class_name"), FullName);
			O->SetNumberField(TEXT("major_version"), Class.Metadata.GetVersion().Major);
			Types.Add(MakeShared<FJsonValueObject>(O));
		},
		EMetasoundFrontendClassType::External);

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetNumberField(TEXT("count"), Types.Num());
	Res->SetBoolField(TEXT("truncated"), Types.Num() >= MaxResults);
	Res->SetArrayField(TEXT("types"), Types);
	if (Types.Num() == 0 && !Filter.IsEmpty())
	{
		Res->SetStringField(TEXT("hint"), TEXT("No matches. Try a shorter filter, or omit it to see the first 200 registered classes. Common substrings: 'Math', 'Oscillator', 'Trigger', 'Filter', 'Envelope', 'Gain', 'Mix'."));
	}
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetMetaSoundNodeInputDefault(const FString& AssetPath, const FString& NodeId,
	const FString& InputName, const FString& Value,
	FString& OutJsonString, FString& OutError)
{

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeId, NodeGuid) || !NodeGuid.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid node_id '%s'"), *NodeId);
		return;
	}

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);

	const FMetasoundFrontendVertex* Input = Builder.FindNodeInput(NodeGuid, FName(*InputName));
	if (!Input)
	{
		TArray<const FMetasoundFrontendVertex*> Inputs = Builder.FindNodeInputs(NodeGuid);
		FString Available;
		for (const FMetasoundFrontendVertex* V : Inputs)
			Available += V->Name.ToString() + TEXT(", ");
		OutError = FString::Printf(TEXT("Input '%s' not found. Available: [%s]"), *InputName, *Available);
		return;
	}

	FMetasoundFrontendLiteral Literal;
	Literal.Set(Value);
	Builder.SetNodeInputDefault(NodeGuid, Input->VertexID, Literal);

	Source->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"node_id\":\"%s\",\"input\":\"%s\",\"value\":\"%s\"}"),
		*NodeId, *InputName, *Value);
}

void HandleCreateMetaSoundFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("MS_NewSound"), SavePath;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateMetaSound(AssetName, SavePath, OutJsonString, OutError);
}

void HandleGetMetaSoundSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleGetMetaSoundSummary(AssetPath, OutJsonString, OutError);
}

void HandleAddMetaSoundInputFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, InputName, DataType;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("input_name"), InputName);
	if (!Args->TryGetStringField(TEXT("type_name"), DataType))
		Args->TryGetStringField(TEXT("data_type"), DataType);
	HandleAddMetaSoundInput(AssetPath, InputName, DataType, OutJsonString, OutError);
}

void HandleAddMetaSoundOutputFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, OutputName, DataType;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("output_name"), OutputName);
	if (!Args->TryGetStringField(TEXT("type_name"), DataType))
		Args->TryGetStringField(TEXT("data_type"), DataType);
	HandleAddMetaSoundOutput(AssetPath, OutputName, DataType, OutJsonString, OutError);
}

void HandleSetMetaSoundDefaultParameterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ParameterName, Value;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("parameter_name"), ParameterName);
	Args->TryGetStringField(TEXT("value"), Value);
	HandleSetMetaSoundDefaultParameter(AssetPath, ParameterName, Value, OutJsonString, OutError);
}

void HandleDuplicateMetaSoundFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SourcePath, NewAssetName = TEXT("MS_Duplicate"), SavePath;
	Args->TryGetStringField(TEXT("source_path"), SourcePath);
	Args->TryGetStringField(TEXT("new_asset_name"), NewAssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleDuplicateMetaSound(SourcePath, NewAssetName, SavePath, OutJsonString, OutError);
}

void HandleAddMetaSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NodeClassName;
	double MajorVersionD = 1;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("node_class_name"), NodeClassName);
	Args->TryGetNumberField(TEXT("major_version"), MajorVersionD);
	HandleAddMetaSoundNode(AssetPath, NodeClassName, (int32)MajorVersionD, OutJsonString, OutError);
}

void HandleConnectMetaSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, SourceNodeId, SourceOutputName, DestNodeId, DestInputName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("source_node_id"), SourceNodeId);
	Args->TryGetStringField(TEXT("source_output_name"), SourceOutputName);
	Args->TryGetStringField(TEXT("dest_node_id"), DestNodeId);
	Args->TryGetStringField(TEXT("dest_input_name"), DestInputName);
	HandleConnectMetaSoundNodes(AssetPath, SourceNodeId, SourceOutputName, DestNodeId, DestInputName, OutJsonString, OutError);
}

void HandleRemoveMetaSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NodeId;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("node_id"), NodeId);
	HandleRemoveMetaSoundNode(AssetPath, NodeId, OutJsonString, OutError);
}

void HandleDisconnectMetaSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, SourceNodeId, SourceOutputName, DestNodeId, DestInputName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("source_node_id"), SourceNodeId);
	Args->TryGetStringField(TEXT("source_output_name"), SourceOutputName);
	Args->TryGetStringField(TEXT("dest_node_id"), DestNodeId);
	Args->TryGetStringField(TEXT("dest_input_name"), DestInputName);
	HandleDisconnectMetaSoundNodes(AssetPath, SourceNodeId, SourceOutputName, DestNodeId, DestInputName, OutJsonString, OutError);
}

void HandleListMetaSoundNodeTypesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString Filter;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("filter"), Filter);
	HandleListMetaSoundNodeTypes(Filter, OutJsonString, OutError);
}

void HandleSetMetaSoundNodeInputDefaultFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NodeId, InputName, Value;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("node_id"), NodeId);
	Args->TryGetStringField(TEXT("input_name"), InputName);
	Args->TryGetStringField(TEXT("value"), Value);
	HandleSetMetaSoundNodeInputDefault(AssetPath, NodeId, InputName, Value, OutJsonString, OutError);
}

void HandleRemoveMetaSoundInputFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, InputName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (!Args->TryGetStringField(TEXT("input_name"), InputName))
		Args->TryGetStringField(TEXT("name"), InputName);

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }
	if (InputName.IsEmpty()) { OutError = TEXT("input_name is required"); return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);
	if (!Builder.RemoveGraphInput(FName(*InputName)))
	{
		OutError = FString::Printf(TEXT("Input '%s' not found on '%s'"), *InputName, *AssetPath);
		return;
	}
	Source->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_input\":\"%s\"}"), *InputName);
}

void HandleRemoveMetaSoundOutputFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, OutputName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (!Args->TryGetStringField(TEXT("output_name"), OutputName))
		Args->TryGetStringField(TEXT("name"), OutputName);

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }
	if (OutputName.IsEmpty()) { OutError = TEXT("output_name is required"); return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);
	if (!Builder.RemoveGraphOutput(FName(*OutputName)))
	{
		OutError = FString::Printf(TEXT("Output '%s' not found on '%s'"), *OutputName, *AssetPath);
		return;
	}
	Source->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_output\":\"%s\"}"), *OutputName);
}

void HandleListMetaSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Source) { OutError = TEXT("MetaSoundSource not found: ") + AssetPath; return; }

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Source);
	FMetaSoundFrontendDocumentBuilder& Builder =
		Metasound::Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(DocInterface);
	const FMetasoundFrontendDocument& Doc = Builder.GetConstDocumentChecked();

	TMap<FGuid, FString> ClassNameById;
	for (const FMetasoundFrontendClass& C : Doc.Dependencies)
		ClassNameById.Add(C.ID, C.Metadata.GetClassName().ToString());

	const FMetasoundFrontendGraph& Graph = Builder.FindConstBuildGraphChecked();

	TMap<FGuid, int32> InEdgeCount, OutEdgeCount;
	for (const FMetasoundFrontendEdge& E : Graph.Edges)
	{
		InEdgeCount.FindOrAdd(E.ToNodeID)++;
		OutEdgeCount.FindOrAdd(E.FromNodeID)++;
	}

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (const FMetasoundFrontendNode& Node : Graph.Nodes)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("node_id"), Node.GetID().ToString());
		O->SetStringField(TEXT("name"), Node.Name.ToString());
		const FString* CName = ClassNameById.Find(Node.ClassID);
		O->SetStringField(TEXT("class_name"), CName ? *CName : TEXT(""));
		O->SetNumberField(TEXT("input_count"), Node.Interface.Inputs.Num());
		O->SetNumberField(TEXT("output_count"), Node.Interface.Outputs.Num());
		O->SetNumberField(TEXT("edges_in"), InEdgeCount.FindRef(Node.GetID()));
		O->SetNumberField(TEXT("edges_out"), OutEdgeCount.FindRef(Node.GetID()));
		NodesArr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("asset_path"), AssetPath);
	Res->SetNumberField(TEXT("count"), NodesArr.Num());
	Res->SetArrayField(TEXT("nodes"), NodesArr);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

}

#else

#include "Dom/JsonObject.h"

namespace MetaSoundTools
{
	static const TCHAR* kRequires55 =
		TEXT("MetaSound tools require UE 5.5+ — IDocumentBuilderRegistry / FMetaSoundFrontendDocumentBuilder "
		     "didn't exist in 5.4 (the legacy 5.4 path used Doc.RootGraph.Graph directly, a fundamentally "
		     "different API).");

	void HandleCreateMetaSoundFromArgs              (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleGetMetaSoundSummaryFromArgs          (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleAddMetaSoundInputFromArgs            (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleAddMetaSoundOutputFromArgs           (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleSetMetaSoundDefaultParameterFromArgs (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleDuplicateMetaSoundFromArgs           (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleAddMetaSoundNodeFromArgs             (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleConnectMetaSoundNodesFromArgs        (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleRemoveMetaSoundNodeFromArgs          (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleDisconnectMetaSoundNodesFromArgs     (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleListMetaSoundNodeTypesFromArgs       (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleSetMetaSoundNodeInputDefaultFromArgs (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleRemoveMetaSoundInputFromArgs         (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleRemoveMetaSoundOutputFromArgs        (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleListMetaSoundNodesFromArgs           (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
}

#endif
