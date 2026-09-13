// Copyright 2026, BlueprintsLab, All rights reserved

#include "McpJsonRpc.h"

#include "Serialization/JsonSerializer.h"

namespace McpJsonRpc
{

namespace
{

	FString BuildErrorEnvelope(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
		Env->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));

		if (Id.IsValid()) Env->SetField(TEXT("id"), Id);
		else              Env->SetField(TEXT("id"), MakeShared<FJsonValueNull>());

		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetNumberField(TEXT("code"), Code);
		Err->SetStringField(TEXT("message"), Message);
		Env->SetObjectField(TEXT("error"), Err);

		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Env, W);
		return Out;
	}
}

bool ParseRequest(const FString& Json, FRequest& OutReq, FString& OutErrorEnvelope)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrorEnvelope = BuildErrorEnvelope(nullptr, (int32)EError::ParseError,
			TEXT("Invalid JSON"));
		return false;
	}

	FString JsonRpcVer;
	if (!Root->TryGetStringField(TEXT("jsonrpc"), JsonRpcVer) || JsonRpcVer != TEXT("2.0"))
	{
		OutErrorEnvelope = BuildErrorEnvelope(Root->TryGetField(TEXT("id")),
			(int32)EError::InvalidRequest,
			TEXT("Missing or invalid \"jsonrpc\" field; must be \"2.0\""));
		return false;
	}

	if (!Root->TryGetStringField(TEXT("method"), OutReq.Method) || OutReq.Method.IsEmpty())
	{
		OutErrorEnvelope = BuildErrorEnvelope(Root->TryGetField(TEXT("id")),
			(int32)EError::InvalidRequest,
			TEXT("Missing \"method\" field"));
		return false;
	}

	const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj && ParamsObj->IsValid())
	{
		OutReq.Params = *ParamsObj;
	}

	OutReq.Id = Root->TryGetField(TEXT("id"));
	OutReq.bIsNotification = !OutReq.Id.IsValid();

	return true;
}

FString MakeSuccess(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
	TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
	Env->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));

	if (Id.IsValid()) Env->SetField(TEXT("id"), Id);
	else              Env->SetField(TEXT("id"), MakeShared<FJsonValueNull>());

	if (Result.IsValid())
	{
		Env->SetObjectField(TEXT("result"), Result);
	}
	else
	{
		Env->SetObjectField(TEXT("result"), MakeShared<FJsonObject>());
	}

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Env, W);
	return Out;
}

FString MakeError(const TSharedPtr<FJsonValue>& Id, EError Code, const FString& Message)
{
	return BuildErrorEnvelope(Id, (int32)Code, Message);
}

FString MakeErrorCustom(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
{
	return BuildErrorEnvelope(Id, Code, Message);
}

}
