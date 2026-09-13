// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BatchToolHelper
{

inline bool TryGetBatchItems(const TSharedPtr<FJsonObject>& Args,
	const TCHAR* Alias, const TArray<TSharedPtr<FJsonValue>>*& OutArray)
{
	auto UnwrapStringElements = [&](const TCHAR* Key) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args->TryGetArrayField(Key, Arr) || !Arr || Arr->Num() == 0) return false;
		bool bAnyString = false;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
			if (V.IsValid() && V->Type == EJson::String) { bAnyString = true; break; }
		if (!bAnyString) return true;

		TArray<TSharedPtr<FJsonValue>> Fixed;
		Fixed.Reserve(Arr->Num());
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				FString Str = V->AsString();
				TSharedPtr<FJsonObject> Parsed;
				TSharedRef<TJsonReader<>> Rdr = TJsonReaderFactory<>::Create(Str);
				if (FJsonSerializer::Deserialize(Rdr, Parsed) && Parsed.IsValid())
					Fixed.Add(MakeShared<FJsonValueObject>(Parsed));
				else
					Fixed.Add(V);
			}
			else
			{
				Fixed.Add(V);
			}
		}
		Args->SetArrayField(Key, Fixed);
		return true;
	};

	if (Args->TryGetArrayField(TEXT("items"), OutArray) && OutArray && OutArray->Num() > 0)
	{
		UnwrapStringElements(TEXT("items"));
		Args->TryGetArrayField(TEXT("items"), OutArray);
		return OutArray && OutArray->Num() > 0;
	}
	if (Alias && *Alias && Args->TryGetArrayField(Alias, OutArray) && OutArray && OutArray->Num() > 0)
	{
		UnwrapStringElements(Alias);
		Args->TryGetArrayField(Alias, OutArray);
		return OutArray && OutArray->Num() > 0;
	}

	auto TryParseStringifiedArray = [&](const TCHAR* Key) -> bool
	{
		FString Str;
		if (!Args->TryGetStringField(Key, Str) || Str.IsEmpty() || Str[0] != TEXT('['))
			return false;
		TArray<TSharedPtr<FJsonValue>> Parsed;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Str);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || Parsed.Num() == 0)
			return false;
		Args->SetArrayField(Key, Parsed);
		return Args->TryGetArrayField(Key, OutArray) && OutArray && OutArray->Num() > 0;
	};

	if (TryParseStringifiedArray(TEXT("items")))
		return true;
	if (Alias && *Alias && TryParseStringifiedArray(Alias))
		return true;

	OutArray = nullptr;
	return false;
}

struct FBatchResultBuilder
{
	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Completed = 0;
	int32 Failed = 0;

	void AddSuccess(int32 Index, const TSharedPtr<FJsonObject>& ExtraFields = nullptr)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), Index);
		Entry->SetBoolField(TEXT("success"), true);
		if (ExtraFields.IsValid())
		{
			for (const auto& KV : ExtraFields->Values)
				Entry->Values.Add(KV.Key, KV.Value);
		}
		Results.Add(MakeShared<FJsonValueObject>(Entry));
		Completed++;
	}

	void AddFailure(int32 Index, const FString& Error, const TSharedPtr<FJsonObject>& ExtraFields = nullptr)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), Index);
		Entry->SetBoolField(TEXT("success"), false);
		Entry->SetStringField(TEXT("error"), Error);
		if (ExtraFields.IsValid())
		{
			for (const auto& KV : ExtraFields->Values)
				Entry->Values.Add(KV.Key, KV.Value);
		}
		Results.Add(MakeShared<FJsonValueObject>(Entry));
		Failed++;
	}

	void Finalize(FString& OutJsonString)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), Completed > 0 || Failed == 0);
		Root->SetNumberField(TEXT("completed"), Completed);
		Root->SetNumberField(TEXT("failed"), Failed);
		if (Failed > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Failures;
			Failures.Reserve(Failed);
			for (const TSharedPtr<FJsonValue>& V : Results)
			{
				const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
				bool bOk = true;
				if (Obj.IsValid()) Obj->TryGetBoolField(TEXT("success"), bOk);
				if (!bOk) Failures.Add(V);
			}
			Root->SetArrayField(TEXT("results"), Failures);
		}
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}
};

inline FString GetItemString(const TSharedPtr<FJsonObject>& Item, const TCHAR* Primary, const TCHAR* Fallback = nullptr)
{
	FString Val;
	if (Item->TryGetStringField(Primary, Val) && !Val.IsEmpty())
		return Val;
	if (Fallback && Item->TryGetStringField(Fallback, Val))
		return Val;
	return Val;
}

inline FString GetStringOrBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName)
{
	FString Val;
	if (Obj->TryGetStringField(FieldName, Val))
		return Val;
	bool bVal = false;
	if (Obj->TryGetBoolField(FieldName, bVal))
		return bVal ? TEXT("true") : TEXT("false");
	return FString();
}

inline FString SerializeCompactJson(const TSharedPtr<FJsonObject>& Obj)
{
	FString Output;
	if (!Obj.IsValid()) return Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Output;
}

inline void WriteCompactJson(const TSharedPtr<FJsonObject>& Obj, FString& OutString)
{
	OutString = SerializeCompactJson(Obj);
}

}
