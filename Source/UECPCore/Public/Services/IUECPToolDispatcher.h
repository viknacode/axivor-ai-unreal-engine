// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FUECPToolResult
{
	bool     bSuccess = false;
	FString  ResultJson;
	FString  ErrorMessage;

	FString  SummaryJson;
};

// Free-form parameter spec convention used by FUECPToolMeta::Params, e.g.
//   "mesh_path, nanite_enabled?, lod_bias?=0, actors=[{label, location?}], asset_path|asset_paths"
//   `?`         optional          `=default`   default value        `|alt`  alternative names
//   `[...]`     array             `{...}`      object               `(...)` free-text note
// The dispatcher parses this into a JSON schema per umbrella (see GetUmbrellaSchema).
struct FUECPToolMeta
{
	FName   Action;
	FName   Umbrella;
	FString Summary;
	FString Params;
	bool    bAlwaysShow = false;
};

// One parameter extracted from a FUECPToolMeta::Params spec by UECPToolDispatch::ParseParamSpec.
struct FUECPParsedParam
{
	// Cleaned identifier (decorations `?`, `=default`, `|alt`, `[]`, `{}`, `(...)` stripped).
	FString Name;
	// The raw spec token this parameter came from, kept verbatim as its human description.
	FString RawToken;
	// Inferred JSON schema type: string | number | boolean | array | object.
	FString JsonType;
	// True when the token carried a trailing `?` (or an `=default`).
	bool    bOptional = false;
	// Ready-to-embed JSON schema for this property ({type, description?, items?, properties?}).
	TSharedPtr<FJsonObject> Schema;
};

enum class EUECPToolThreadAffinity : uint8
{

	GameThread,

	AnyThread,
};

class UECPCORE_API IUECPToolDispatcher
{
public:
	virtual ~IUECPToolDispatcher() = default;

	using FToolHandler = TFunction<FUECPToolResult(const TSharedPtr<FJsonObject>& )>;

	virtual void RegisterHandler(FName ToolName, FToolHandler Handler,
		EUECPToolThreadAffinity Affinity = EUECPToolThreadAffinity::GameThread) = 0;
	virtual void UnregisterHandler(FName ToolName) = 0;

	virtual void SetRegistrantContext(FName ExtensionId) = 0;

	virtual FUECPToolResult ExecuteFromArgs(FName ToolName, const TSharedPtr<FJsonObject>& Args) = 0;

	virtual void ExecuteFromArgsAsync(FName ToolName, const TSharedPtr<FJsonObject>& Args,
		TFunction<void(FUECPToolResult)> OnComplete)
	{
		FUECPToolResult R = ExecuteFromArgs(ToolName, Args);
		if (OnComplete) OnComplete(MoveTemp(R));
	}

	virtual TArray<FName> ListTools() const = 0;

	virtual void RegisterToolMetadata(const FUECPToolMeta& ) {}

	virtual void GetAllToolMetadata(TArray<FUECPToolMeta>& ) const {}

	virtual void RemoveToolMetadataForOwner(FName ) {}

	// JSON schema derived from every FUECPToolMeta registered under `Umbrella`:
	//   { type:object,
	//     properties:{ action:{type:string, enum:[...all actions...]}, <param>:{type, description} ... },
	//     required:[action], additionalProperties:true }
	// Parameter types are inferred from the Params spec (see UECPToolDispatch::ParseParamSpec);
	// the same parameter used by several actions is merged and its description lists those actions.
	// Cached per umbrella and rebuilt when metadata changes. Returns null when no metadata exists
	// for the umbrella. The returned object is shared — treat it as immutable (use
	// FJsonObject::Duplicate before editing).
	virtual TSharedPtr<FJsonObject> GetUmbrellaSchema(FName ) const { return nullptr; }

	virtual void RemoveHandlersForOwner(FName ) {}

	// Names of the tools currently registered under a given owner (extension id).
	// Used to validate a manifest's declared owned_tools against what actually registered.
	virtual TArray<FName> GetToolsForOwner(FName ) const { return {}; }

	virtual bool IsRegistered(FName ToolName) const = 0;
};

namespace UECPToolDispatch
{

	UECPCORE_API bool IsUmbrellaName(FName Name);

	// Parses a FUECPToolMeta::Params spec into named parameters (see FUECPToolMeta for the
	// convention). Tokens that carry no identifier (e.g. "...", "<properties>") are skipped and
	// reported through OutUnparseable. Returns false when at least one token was skipped.
	// An empty spec parses successfully into an empty list.
	UECPCORE_API bool ParseParamSpec(const FString& Params, TArray<FUECPParsedParam>& OutParams,
		TArray<FString>* OutUnparseable = nullptr);

	// Name/token heuristics used by ParseParamSpec to pick a JSON schema type
	// (string | number | boolean | array | object).
	UECPCORE_API FString InferParamJsonType(const FString& Name, const FString& RawToken);
}
