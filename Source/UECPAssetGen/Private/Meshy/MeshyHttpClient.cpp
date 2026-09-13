// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/MeshyHttpClient.h"
#include "UECPAssetGenModule.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"

FMeshyHttpClient& FMeshyHttpClient::Get()
{
	static FMeshyHttpClient Instance;
	return Instance;
}

FString FMeshyHttpClient::BuildAuthHeader(const FString& ApiKey)
{
	return FString::Printf(TEXT("Bearer %s"), *ApiKey);
}

bool FMeshyHttpClient::ShouldRetry(int32 HttpCode)
{
	return HttpCode == 429 || HttpCode == 503 || HttpCode == 504;
}

float FMeshyHttpClient::GetBackoffSeconds(int32 Attempt, const FString& RetryAfterHeader)
{
	if (!RetryAfterHeader.IsEmpty())
	{
		const float ServerHint = FCString::Atof(*RetryAfterHeader);
		if (ServerHint > 0.f) return FMath::Min(ServerHint, 60.f);
	}
	return FMath::Min(FMath::Pow(2.f, static_cast<float>(Attempt)), 30.f);
}

void FMeshyHttpClient::ParseError(const FString& ResponseBody, TSharedPtr<FJsonObject> Json, FMeshyError& OutError)
{
	if (!Json.IsValid())
	{
		OutError.Message = ResponseBody.Left(300);
		return;
	}

	const TSharedPtr<FJsonObject>* ErrObj = nullptr;
	if (Json->TryGetObjectField(TEXT("task_error"), ErrObj) && ErrObj && ErrObj->IsValid())
	{
		(*ErrObj)->TryGetStringField(TEXT("type"),    OutError.Type);
		(*ErrObj)->TryGetStringField(TEXT("message"), OutError.Message);
		(*ErrObj)->TryGetStringField(TEXT("doc_url"), OutError.DocUrl);
		(*ErrObj)->TryGetNumberField(TEXT("code"),    OutError.Code);
		return;
	}

	Json->TryGetStringField(TEXT("error"),   OutError.Type);
	Json->TryGetStringField(TEXT("message"), OutError.Message);
	Json->TryGetNumberField(TEXT("code"),    OutError.Code);

	if (OutError.Message.IsEmpty())
	{
		OutError.Message = ResponseBody.Left(300);
	}
}

void FMeshyHttpClient::Post(
	const FString& Url,
	const FString& ApiKey,
	TSharedPtr<FJsonObject> Payload,
	TFunction<void(const FMeshyHttpResult&)> OnDone)
{
	FString Body;
	if (Payload.IsValid())
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
	}
	SendJson(TEXT("POST"), Url, ApiKey, Body, MoveTemp(OnDone), 0);
}

void FMeshyHttpClient::Get(
	const FString& Url,
	const FString& ApiKey,
	TFunction<void(const FMeshyHttpResult&)> OnDone)
{
	SendJson(TEXT("GET"), Url, ApiKey, FString(), MoveTemp(OnDone), 0);
}

void FMeshyHttpClient::SendJson(
	const FString& Verb,
	const FString& Url,
	const FString& ApiKey,
	const FString& Body,
	TFunction<void(const FMeshyHttpResult&)> OnDone,
	int32 Attempt)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(Url);
	Request->SetTimeout(DefaultTimeoutSec);
	Request->SetHeader(TEXT("Authorization"), BuildAuthHeader(ApiKey));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	if (Verb == TEXT("POST"))
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Body);
	}

	Request->OnProcessRequestComplete().BindLambda(
		[this, Verb, Url, ApiKey, Body, OnDone = MoveTemp(OnDone), Attempt]
		(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk) mutable
		{
			FMeshyHttpResult Result;
			Result.HttpCode = Resp.IsValid() ? Resp->GetResponseCode() : 0;

			if (!bOk || !Resp.IsValid())
			{
				Result.Error.Message = TEXT("Network error or no response");
				OnDone(Result);
				return;
			}

			Result.ResponseBody = Resp->GetContentAsString();
			if (!Result.ResponseBody.IsEmpty())
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.ResponseBody);
				TSharedPtr<FJsonObject> Json;
				if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
				{
					Result.ResponseJson = Json;
				}
			}

			const bool bHttpOk = (Result.HttpCode >= 200 && Result.HttpCode < 300);
			if (!bHttpOk && ShouldRetry(Result.HttpCode) && Attempt < MaxRetries)
			{
				const FString RetryAfter = Resp->GetHeader(TEXT("Retry-After"));
				const float Delay = GetBackoffSeconds(Attempt, RetryAfter);
				UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy HTTP %d on %s — retry %d in %.1fs"), Result.HttpCode, *Url, Attempt + 1, Delay);

				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
					[this, Verb, Url, ApiKey, Body, OnDone = MoveTemp(OnDone), Attempt](float) mutable
					{
						SendJson(Verb, Url, ApiKey, Body, MoveTemp(OnDone), Attempt + 1);
						return false;
					}), Delay);
				return;
			}

			if (bHttpOk)
			{
				Result.bSuccess = true;
			}
			else
			{
				ParseError(Result.ResponseBody, Result.ResponseJson, Result.Error);
				if (Result.Error.Message.IsEmpty())
				{
					Result.Error.Message = FString::Printf(TEXT("HTTP %d"), Result.HttpCode);
				}
			}

			OnDone(Result);
		});

	Request->ProcessRequest();
}

void FMeshyHttpClient::Download(
	const FString& Url,
	const FString& DestPath,
	TFunction<void(bool, const FString&)> OnDone)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("GET"));
	Request->SetURL(Url);
	Request->SetTimeout(DefaultTimeoutSec);

	Request->OnProcessRequestComplete().BindLambda(
		[OnDone = MoveTemp(OnDone), DestPath](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
				UE_LOG(LogUECPAssetGen, Warning, TEXT("Meshy download failed (HTTP %d): %s"), Code, *Req->GetURL());
				OnDone(false, FString());
				return;
			}

			const TArray<uint8>& Data = Resp->GetContent();
			if (Data.Num() == 0)
			{
				OnDone(false, FString());
				return;
			}

			const FString Dir = FPaths::GetPath(DestPath);
			IFileManager::Get().MakeDirectory(*Dir,  true);

			if (!FFileHelper::SaveArrayToFile(Data, *DestPath))
			{
				UE_LOG(LogUECPAssetGen, Warning, TEXT("Meshy download saved 0 bytes to %s"), *DestPath);
				OnDone(false, FString());
				return;
			}

			OnDone(true, DestPath);
		});

	Request->ProcessRequest();
}
