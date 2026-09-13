// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "Services/IUECPToolDispatcher.h"

namespace UECPExtTools
{

inline FString GetStringWithFallbacks(const TSharedPtr<FJsonObject>& Args,
	std::initializer_list<const TCHAR*> Keys)
{
	if (!Args.IsValid()) return FString();
	for (const TCHAR* Key : Keys)
	{
		FString Val;
		if (Args->TryGetStringField(Key, Val) && !Val.IsEmpty()) return Val;
	}
	return FString();
}

inline int32 GetIntWithDefault(const TSharedPtr<FJsonObject>& Args,
	const TCHAR* Key, int32 Default)
{
	if (!Args.IsValid()) return Default;
	double D = 0.0;
	if (Args->TryGetNumberField(Key, D)) return (int32)D;
	int32 I = 0;
	if (Args->TryGetNumberField(Key, I)) return I;
	return Default;
}

inline double GetNumberWithDefault(const TSharedPtr<FJsonObject>& Args,
	const TCHAR* Key, double Default)
{
	if (!Args.IsValid()) return Default;
	double D = 0.0;
	if (Args->TryGetNumberField(Key, D)) return D;
	return Default;
}

inline bool GetBoolWithDefault(const TSharedPtr<FJsonObject>& Args,
	const TCHAR* Key, bool Default)
{
	if (!Args.IsValid()) return Default;
	bool B = false;
	if (Args->TryGetBoolField(Key, B)) return B;
	FString S;
	if (Args->TryGetStringField(Key, S))
	{
		if (S.Equals(TEXT("true"), ESearchCase::IgnoreCase)) return true;
		if (S.Equals(TEXT("false"), ESearchCase::IgnoreCase)) return false;
	}
	return Default;
}

template<typename T>
T* LoadAssetTypedReporting(const FString& Path, const TCHAR* AssetTypeLabel, FString& OutErr)
{
	if (Path.IsEmpty())
	{
		OutErr = FString::Printf(TEXT("Missing %s path"), AssetTypeLabel ? AssetTypeLabel : TEXT("asset"));
		return nullptr;
	}
	T* Typed = LoadObject<T>(nullptr, *Path);
	if (Typed) { OutErr.Reset(); return Typed; }

	UObject* AnyAsset = LoadObject<UObject>(nullptr, *Path);
	if (!AnyAsset)
	{
		OutErr = FString::Printf(TEXT("Could not load %s at: %s"),
			AssetTypeLabel ? AssetTypeLabel : TEXT("asset"), *Path);
	}
	else
	{
		OutErr = FString::Printf(TEXT("Asset at '%s' is not a %s (got %s)"),
			*Path, AssetTypeLabel ? AssetTypeLabel : TEXT("expected type"),
			*AnyAsset->GetClass()->GetName());
	}
	return nullptr;
}

inline FUECPToolResult MakeOkResult(const TSharedPtr<FJsonObject>& Fields)
{
	FUECPToolResult R;
	R.bSuccess = true;
	TSharedPtr<FJsonObject> Body = Fields.IsValid() ? Fields : MakeShared<FJsonObject>();
	if (!Body->HasField(TEXT("success"))) Body->SetBoolField(TEXT("success"), true);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&R.ResultJson);
	FJsonSerializer::Serialize(Body.ToSharedRef(), W);
	return R;
}

inline FUECPToolResult MakeOkResult(const FString& BodyJson)
{
	FUECPToolResult R;
	R.bSuccess = true;
	R.ResultJson = BodyJson;
	return R;
}

inline FUECPToolResult MakeErrorResult(const FString& Message)
{
	FUECPToolResult R;
	R.bSuccess = false;
	R.ErrorMessage = Message;
	return R;
}

}
