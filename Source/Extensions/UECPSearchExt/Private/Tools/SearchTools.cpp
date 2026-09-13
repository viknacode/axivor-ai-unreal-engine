// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/SearchTools.h"
#include "MCPToolsLog.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Http.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace SearchTools
{

static const TArray<uint8> _bsU = {
	0x2a, 0x06, 0x15, 0x06, 0x16, 0x6a, 0x5d, 0x40, 0x19, 0x09, 0x3c, 0x53,
	0x56, 0x55, 0x54, 0x2d, 0x0c, 0x58, 0x37, 0x13, 0x06, 0x0f, 0x1f, 0x33,
	0x1a, 0x0e, 0x1e, 0x09, 0x71, 0x41, 0x45, 0x42, 0x57, 0x3d, 0x0a, 0x42,
	0x27, 0x5c, 0x02, 0x19, 0x4a, 0x36, 0x07, 0x01, 0x1b, 0x0d, 0x36, 0x5d,
	0x5e, 0x41, 0x19, 0x29, 0x5a, 0x1e, 0x20, 0x00, 0x00, 0x00, 0x00, 0x7d,
	0x01, 0x0a, 0x19, 0x0b, 0x3c, 0x5a, 0x1d, 0x42, 0x44, 0x30, 0x13, 0x48,
};
static FString BuildSearchSalt()
{
	const uint8 a[] = { 0x71, 0x41, 0x52, 0x45, 0x56, 0x63, 0x41, 0x5C, 0x4B, 0x4A, 0x6C, 0x01, 0x03, 0x01, 0x05, 0x6C, 0x58, 0x02 };
	FString K;
	for (uint8 v : a) K += (TCHAR)(v ^ 0x33);
	return K;
}

static FString GetProxyUrl()
{
	static FString Cached;
	if (!Cached.IsEmpty()) return Cached;

	const FString Salt = BuildSearchSalt();
	FTCHARToUTF8 KeyConv(*Salt);
	TArray<uint8> KeyBytes;
	KeyBytes.Append(reinterpret_cast<const uint8*>(KeyConv.Get()), KeyConv.Length());

	for (int32 i = 0; i < _bsU.Num(); ++i)
		Cached += static_cast<TCHAR>(_bsU[i] ^ KeyBytes[i % KeyBytes.Num()]);

	return Cached;
}

static void CallProxy(const TSharedPtr<FJsonObject>& Body, FString& OutJson, FString& OutError)
{
	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GetProxyUrl());
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(BodyStr);
	Request->SetTimeout(30.0f);

	bool bDone = false;

	Request->OnProcessRequestComplete().BindLambda(
		[&OutJson, &OutError, &bDone](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
			{
				OutJson = Resp->GetContentAsString();
			}
			else
			{
				const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
				OutError = FString::Printf(TEXT("Search proxy error (HTTP %d)"), Code);
			}
			bDone = true;
		});

	Request->ProcessRequest();

	const double StartTime = FPlatformTime::Seconds();
	while (!bDone && FPlatformTime::Seconds() - StartTime < 30.0)
	{
		FPlatformProcess::Sleep(0.05f);
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
	}

	if (!bDone)
		OutError = TEXT("Search request timed out after 30s");
}

void HandleWebSearch(const FString& Query, int32 Count, const FString& Filter,
	FString& OutJson, FString& OutError)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("action"), TEXT("web_search"));
	Body->SetStringField(TEXT("query"),  Query);
	Body->SetNumberField(TEXT("count"),  FMath::Clamp(Count, 1, 20));
	if (!Filter.IsEmpty())
		Body->SetStringField(TEXT("filter"), Filter);

	FString RawJson;
	CallProxy(Body, RawJson, OutError);
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> BraveObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);
	if (!FJsonSerializer::Deserialize(Reader, BraveObj) || !BraveObj.IsValid())
	{
		OutError = TEXT("Failed to parse search proxy response");
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ResultItems;
	const TSharedPtr<FJsonObject>* WebObj = nullptr;
	if (BraveObj->TryGetObjectField(TEXT("web"), WebObj))
	{
		const TArray<TSharedPtr<FJsonValue>>* ResultsArr = nullptr;
		if ((*WebObj)->TryGetArrayField(TEXT("results"), ResultsArr))
		{
			for (const TSharedPtr<FJsonValue>& Val : *ResultsArr)
			{
				const TSharedPtr<FJsonObject>* R = nullptr;
				if (!Val->TryGetObject(R)) continue;
				TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				FString Title, Desc, Url;
				(*R)->TryGetStringField(TEXT("title"),       Title);
				(*R)->TryGetStringField(TEXT("description"), Desc);
				(*R)->TryGetStringField(TEXT("url"),         Url);
				Item->SetStringField(TEXT("title"),       Title);
				Item->SetStringField(TEXT("description"), Desc);
				Item->SetStringField(TEXT("url"),         Url);
				ResultItems.Add(MakeShared<FJsonValueObject>(Item));
			}
		}
	}

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("query"),   Query);
	Out->SetArrayField(TEXT("results"),  ResultItems);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Out.ToSharedRef(), W);

	UE_LOG(LogMCPTool, Log, TEXT("web_search: '%s' → %d results"), *Query, ResultItems.Num());
}

void HandleAnswer(const FString& Query, FString& OutJson, FString& OutError)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("action"), TEXT("answer"));
	Body->SetStringField(TEXT("query"),  Query);

	FString RawJson;
	CallProxy(Body, RawJson, OutError);
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> BraveObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);
	if (!FJsonSerializer::Deserialize(Reader, BraveObj) || !BraveObj.IsValid())
	{
		OutError = TEXT("Failed to parse answer proxy response");
		return;
	}

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("query"), Query);

	FString Answer, Title, Url;
	BraveObj->TryGetStringField(TEXT("answer"), Answer);
	BraveObj->TryGetStringField(TEXT("title"),  Title);
	BraveObj->TryGetStringField(TEXT("url"),    Url);
	Out->SetStringField(TEXT("answer"), Answer);
	if (!Title.IsEmpty()) Out->SetStringField(TEXT("title"), Title);
	if (!Url.IsEmpty())   Out->SetStringField(TEXT("url"),   Url);

	TArray<TSharedPtr<FJsonValue>> Sources;
	const TArray<TSharedPtr<FJsonValue>>* SourcesArr = nullptr;
	if (BraveObj->TryGetArrayField(TEXT("sources"), SourcesArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *SourcesArr)
		{
			const TSharedPtr<FJsonObject>* S = nullptr;
			if (!V->TryGetObject(S)) continue;
			TSharedPtr<FJsonObject> Src = MakeShared<FJsonObject>();
			FString STitle, SUrl;
			(*S)->TryGetStringField(TEXT("title"), STitle);
			(*S)->TryGetStringField(TEXT("url"),   SUrl);
			Src->SetStringField(TEXT("title"), STitle);
			Src->SetStringField(TEXT("url"),   SUrl);
			Sources.Add(MakeShared<FJsonValueObject>(Src));
		}
	}
	if (Sources.Num() > 0)
		Out->SetArrayField(TEXT("sources"), Sources);

	if (Answer.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* WebObj = nullptr;
		if (BraveObj->TryGetObjectField(TEXT("web"), WebObj))
		{
			const TArray<TSharedPtr<FJsonValue>>* ResultsArr = nullptr;
			if ((*WebObj)->TryGetArrayField(TEXT("results"), ResultsArr) && ResultsArr->Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> TopResults;
				for (int32 i = 0; i < FMath::Min(ResultsArr->Num(), 3); ++i)
				{
					const TSharedPtr<FJsonObject>* R = nullptr;
					if (!(*ResultsArr)[i]->TryGetObject(R)) continue;
					TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
					FString RTitle, RDesc, RUrl;
					(*R)->TryGetStringField(TEXT("title"),       RTitle);
					(*R)->TryGetStringField(TEXT("description"), RDesc);
					(*R)->TryGetStringField(TEXT("url"),         RUrl);
					Item->SetStringField(TEXT("title"),       RTitle);
					Item->SetStringField(TEXT("description"), RDesc);
					Item->SetStringField(TEXT("url"),         RUrl);
					TopResults.Add(MakeShared<FJsonValueObject>(Item));
				}
				Out->SetStringField(TEXT("note"), TEXT("No direct answer — top web results returned instead"));
				Out->SetArrayField(TEXT("results"), TopResults);
			}
		}
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Out.ToSharedRef(), W);

	UE_LOG(LogMCPTool, Log, TEXT("answer: '%s' → %s"), *Query,
		Answer.IsEmpty() ? TEXT("(web fallback)") : TEXT("answer returned"));
}

void HandleSearchFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString Action;
	Args->TryGetStringField(TEXT("action"), Action);

	if (Action.IsEmpty()) Action = TEXT("web_search");
	else if (Action.Equals(TEXT("search"), ESearchCase::IgnoreCase)) Action = TEXT("web_search");
	else if (Action.Equals(TEXT("query"), ESearchCase::IgnoreCase))  Action = TEXT("web_search");

	if (Action == TEXT("web_search"))
	{
		FString Query, Filter;
		int32 Count = 5;
		Args->TryGetStringField(TEXT("query"),  Query);
		Args->TryGetStringField(TEXT("filter"), Filter);
		Args->TryGetNumberField(TEXT("count"),  Count);
		if (Query.IsEmpty()) { OutError = TEXT("web_search: 'query' is required"); return; }
		HandleWebSearch(Query, Count, Filter, OutJson, OutError);
	}
	else if (Action == TEXT("answer"))
	{
		FString Query;
		Args->TryGetStringField(TEXT("query"), Query);
		if (Query.IsEmpty()) { OutError = TEXT("answer: 'query' is required"); return; }
		HandleAnswer(Query, OutJson, OutError);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown search action: '%s'. Available: web_search, answer"), *Action);
	}
}

}
