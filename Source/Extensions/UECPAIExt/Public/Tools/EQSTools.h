// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace EQSTools
{
	UECPAIEXT_API void HandleCreateEQSQuery(const FString& AssetName, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddEQSGenerator(const FString& QueryPath, const FString& GeneratorType,
		const FString& ContextClass, float Radius, float GridSpacing, int32 GridSize,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddEQSTest(const FString& QueryPath, const FString& TestType,
		const FString& FilterMode, float FloatFilterMin, float FloatFilterMax,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetEQSQuerySummary(const FString& QueryPath,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetEQSParam(const FString& QueryPath, const FString& Target,
		const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveEQSTest(const FString& QueryPath, int32 TestIndex,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleListEQSGeneratorTypes(FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleListEQSTestTypes(FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveEQSGenerator(const FString& QueryPath, int32 GeneratorIndex,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddEQSOption(const FString& QueryPath, const FString& GeneratorType,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetEQSTestScore(const FString& QueryPath, int32 OptionIndex, int32 TestIndex,
		const FString& ScoringEquation, float ScoringFactor, const FString& FilterType,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleCreateEQSQueryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddEQSGeneratorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddEQSTestFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetEQSQuerySummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetEQSParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveEQSTestFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleListEQSGeneratorTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleListEQSTestTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveEQSGeneratorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddEQSOptionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetEQSTestScoreFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
