// Copyright 2026, BlueprintsLab, All rights reserved

#include "ACP/FUECPACPJsonRpc.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr const TCHAR* JsonRpcVersion = TEXT("2.0");

	FString SerializeSingleLine(const TSharedRef<FJsonObject>& Root)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		Writer->Close();
		return Out;
	}
}

void FUECPACPJsonRpc::AppendBytes(const TArray<uint8>& Bytes)
{
	if (Bytes.Num() == 0) return;
	AppendUtf8(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
}

void FUECPACPJsonRpc::AppendUtf8(const ANSICHAR* Data, int32 Count)
{
	if (!Data || Count <= 0) return;
	FUTF8ToTCHAR Conv(Data, Count);
	Buffer.AppendChars(Conv.Get(), Conv.Length());
}

void FUECPACPJsonRpc::AppendFString(const FString& Chunk)
{
	Buffer.Append(Chunk);
}

bool FUECPACPJsonRpc::TakeLine(FString& OutLine)
{
	int32 LfIdx = INDEX_NONE;
	if (!Buffer.FindChar(TEXT('\n'), LfIdx)) return false;

	OutLine = Buffer.Left(LfIdx);
	Buffer.RemoveAt(0, LfIdx + 1, EAllowShrinking::No);

	if (OutLine.EndsWith(TEXT("\r"))) OutLine.LeftChopInline(1, EAllowShrinking::No);
	OutLine.TrimStartAndEndInline();
	return true;
}

FString FUECPACPJsonRpc::FormatRequest(int64 Id, const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("jsonrpc"), JsonRpcVersion);
	Root->SetNumberField(TEXT("id"), static_cast<double>(Id));
	Root->SetStringField(TEXT("method"), Method);
	if (Params.IsValid()) Root->SetObjectField(TEXT("params"), Params);
	return SerializeSingleLine(Root);
}

FString FUECPACPJsonRpc::FormatNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("jsonrpc"), JsonRpcVersion);
	Root->SetStringField(TEXT("method"), Method);
	if (Params.IsValid()) Root->SetObjectField(TEXT("params"), Params);
	return SerializeSingleLine(Root);
}

FString FUECPACPJsonRpc::FormatResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("jsonrpc"), JsonRpcVersion);
	Root->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
	Root->SetObjectField(TEXT("result"), Result.IsValid() ? Result : MakeShared<FJsonObject>());
	return SerializeSingleLine(Root);
}

FString FUECPACPJsonRpc::FormatError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("jsonrpc"), JsonRpcVersion);
	Root->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());

	const TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
	Err->SetNumberField(TEXT("code"), Code);
	Err->SetStringField(TEXT("message"), Message);
	Root->SetObjectField(TEXT("error"), Err);

	return SerializeSingleLine(Root);
}

bool FUECPACPJsonRpc::ParseFrame(const FString& Frame, TSharedPtr<FJsonObject>& OutRoot)
{
	if (Frame.IsEmpty()) return false;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Frame);
	return FJsonSerializer::Deserialize(R, OutRoot) && OutRoot.IsValid();
}

bool FUECPACPJsonRpc::IsRequest(const TSharedPtr<FJsonObject>& Root)
{
	return Root.IsValid() && Root->HasField(TEXT("method")) && Root->HasField(TEXT("id"));
}

bool FUECPACPJsonRpc::IsNotification(const TSharedPtr<FJsonObject>& Root)
{
	return Root.IsValid() && Root->HasField(TEXT("method")) && !Root->HasField(TEXT("id"));
}

bool FUECPACPJsonRpc::IsResponse(const TSharedPtr<FJsonObject>& Root)
{
	return Root.IsValid() && Root->HasField(TEXT("id")) && !Root->HasField(TEXT("method"));
}
