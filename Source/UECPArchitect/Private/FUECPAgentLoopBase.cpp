// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPAgentLoopBase.h"
#include "Interfaces/IHttpResponse.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Containers/Ticker.h"
#include "Misc/ConfigCacheIni.h"
#include "Managers/SettingsManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Mcp/UECPMcpClient.h"

namespace
{
	int32 EstimateEntryChars(const TSharedPtr<FJsonValue>& Entry)
	{
		const TSharedPtr<FJsonObject> Obj = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Obj.IsValid()) return 0;
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
		W->Close();
		return Out.Len();
	}

	bool IsElidedStub(const FString& S)
	{
		return S.StartsWith(TEXT("{\"elided\":true")) || S.StartsWith(TEXT("[elided"));
	}

	FString MakeElidedToolStub(const TCHAR* IdKey, const FString& Id, int32 OriginalChars)
	{
		return FString::Printf(TEXT("{\"elided\":true,\"%s\":\"%s\",\"original_chars\":%d}"),
			IdKey, *Id.ReplaceCharWithEscapedChar(), OriginalChars);
	}

	FString MakeElidedTextStub(int32 OriginalChars)
	{
		return FString::Printf(TEXT("[elided earlier assistant text: %d chars]"), OriginalChars);
	}

	// Elides every tool result carried by one history entry. Handles the three provider shapes:
	//   OpenAI : {role:"tool", tool_call_id, content:"..."}
	//   Claude : {role:"user", content:[{type:"tool_result", tool_use_id, content:"..."}]}
	//   Gemini : {role:"user", parts:[{functionResponse:{name, response:{content:"..."}}}]}
	bool ElideToolResultsInEntry(const TSharedPtr<FJsonValue>& Entry)
	{
		const TSharedPtr<FJsonObject> Obj = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Obj.IsValid()) return false;

		FString Role;
		Obj->TryGetStringField(TEXT("role"), Role);
		bool bChanged = false;

		if (Role == TEXT("tool"))
		{
			FString Content;
			if (Obj->TryGetStringField(TEXT("content"), Content) && !IsElidedStub(Content))
			{
				FString Id;
				Obj->TryGetStringField(TEXT("tool_call_id"), Id);
				Obj->SetStringField(TEXT("content"), MakeElidedToolStub(TEXT("tool_call_id"), Id, Content.Len()));
				bChanged = true;
			}
			return bChanged;
		}

		if (Role != TEXT("user")) return false;

		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (Obj->TryGetArrayField(TEXT("content"), Content) && Content)
		{
			for (const TSharedPtr<FJsonValue>& BV : *Content)
			{
				const TSharedPtr<FJsonObject> B = BV.IsValid() ? BV->AsObject() : nullptr;
				if (!B.IsValid()) continue;
				FString Type;
				B->TryGetStringField(TEXT("type"), Type);
				if (Type != TEXT("tool_result")) continue;
				FString Text;
				if (!B->TryGetStringField(TEXT("content"), Text) || IsElidedStub(Text)) continue;
				FString Id;
				B->TryGetStringField(TEXT("tool_use_id"), Id);
				B->SetStringField(TEXT("content"), MakeElidedToolStub(TEXT("tool_use_id"), Id, Text.Len()));
				bChanged = true;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts)
		{
			for (const TSharedPtr<FJsonValue>& PV : *Parts)
			{
				const TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
				if (!P.IsValid()) continue;
				const TSharedPtr<FJsonObject>* FR = nullptr;
				if (!P->TryGetObjectField(TEXT("functionResponse"), FR) || !FR || !FR->IsValid()) continue;
				const TSharedPtr<FJsonObject>* Resp = nullptr;
				if (!(*FR)->TryGetObjectField(TEXT("response"), Resp) || !Resp || !Resp->IsValid()) continue;
				FString Text;
				if (!(*Resp)->TryGetStringField(TEXT("content"), Text) || IsElidedStub(Text)) continue;
				FString Name;
				(*FR)->TryGetStringField(TEXT("name"), Name);
				(*Resp)->SetStringField(TEXT("content"), MakeElidedToolStub(TEXT("name"), Name, Text.Len()));
				bChanged = true;
			}
		}
		return bChanged;
	}

	// Elides the assistant/model text of one history entry (tool_use / functionCall parts are kept).
	//   OpenAI : {role:"assistant", content:"..." | null, tool_calls?, reasoning_content?}
	//   Claude : {role:"assistant", content:[{type:"text", text:"..."}, {type:"tool_use", ...}]}
	//   Gemini : {role:"model", parts:[{text:"..."}, {functionCall:...}]}
	bool ElideAssistantTextInEntry(const TSharedPtr<FJsonValue>& Entry)
	{
		const TSharedPtr<FJsonObject> Obj = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Obj.IsValid()) return false;

		FString Role;
		Obj->TryGetStringField(TEXT("role"), Role);
		if (Role != TEXT("assistant") && Role != TEXT("model")) return false;

		// Only worth eliding when the stub is materially shorter than the text.
		constexpr int32 MinElideLen = 160;
		bool bChanged = false;

		FString Reasoning;
		if (Obj->TryGetStringField(TEXT("reasoning_content"), Reasoning) && Reasoning.Len() > MinElideLen)
		{
			Obj->SetStringField(TEXT("reasoning_content"), TEXT(""));
			bChanged = true;
		}

		FString ContentStr;
		if (Obj->TryGetStringField(TEXT("content"), ContentStr))
		{
			if (ContentStr.Len() > MinElideLen && !IsElidedStub(ContentStr))
			{
				Obj->SetStringField(TEXT("content"), MakeElidedTextStub(ContentStr.Len()));
				bChanged = true;
			}
			return bChanged;
		}

		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (Obj->TryGetArrayField(TEXT("content"), Content) && Content)
		{
			for (const TSharedPtr<FJsonValue>& BV : *Content)
			{
				const TSharedPtr<FJsonObject> B = BV.IsValid() ? BV->AsObject() : nullptr;
				if (!B.IsValid()) continue;
				FString Type, Text;
				B->TryGetStringField(TEXT("type"), Type);
				if (Type != TEXT("text")) continue;
				if (!B->TryGetStringField(TEXT("text"), Text) || Text.Len() <= MinElideLen || IsElidedStub(Text)) continue;
				B->SetStringField(TEXT("text"), MakeElidedTextStub(Text.Len()));
				bChanged = true;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts)
		{
			for (const TSharedPtr<FJsonValue>& PV : *Parts)
			{
				const TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
				if (!P.IsValid()) continue;
				FString Text;
				if (!P->TryGetStringField(TEXT("text"), Text) || Text.Len() <= MinElideLen || IsElidedStub(Text)) continue;
				P->SetStringField(TEXT("text"), MakeElidedTextStub(Text.Len()));
				bChanged = true;
			}
		}
		return bChanged;
	}
}

int32 FUECPAgentLoopBase::ResolveMaxOutputTokens()
{
	int32 Value = 16000;
	if (GConfig)
	{
		int32 Cfg = 0;
		if (GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxOutputTokens"), Cfg, FSettingsManager::GetGlobalConfigPath()) && Cfg > 0)
			Value = Cfg;
		else if (GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxOutputTokens"), Cfg, GEditorIni) && Cfg > 0)
			Value = Cfg;
	}
	return FMath::Clamp(Value, 1024, 64000);
}

float FUECPAgentLoopBase::ResolveRequestTimeoutSeconds()
{
	float Value = 600.0f;
	if (GConfig)
	{
		float Cfg = 0.0f;
		if (GConfig->GetFloat(TEXT("BpGeneratorUltimate"), TEXT("RequestTimeoutSeconds"), Cfg, FSettingsManager::GetGlobalConfigPath()) && Cfg > 0.0f)
			Value = Cfg;
		else if (GConfig->GetFloat(TEXT("BpGeneratorUltimate"), TEXT("RequestTimeoutSeconds"), Cfg, GEditorIni) && Cfg > 0.0f)
			Value = Cfg;
	}
	return FMath::Clamp(Value, 60.0f, 3600.0f);
}

FString FUECPAgentLoopBase::CapToolResultText(const FString& In)
{
	if (In.Len() <= MaxToolResultChars) return In;
	const int32 Omitted = In.Len() - MaxToolResultChars;
	return In.Left(MaxToolResultChars)
		+ FString::Printf(TEXT("\n... [truncated: %d more chars — call the tool again with a narrower filter]"), Omitted);
}

void FUECPAgentLoopBase::ConfigureRequestTimeout()
{
	if (!ActiveRequest.IsValid()) return;
	ActiveRequest->SetTimeout(ResolveRequestTimeoutSeconds());
}

bool FUECPAgentLoopBase::IsTransientHttpFailure(bool bWasSuccessful, int32 Code)
{
	if (!bWasSuccessful || Code == 0) return true;
	switch (Code)
	{
	case 429: case 500: case 502: case 503: case 504: case 529:
		return true;
	default:
		return false;
	}
}

bool FUECPAgentLoopBase::TryScheduleTransientRetry(bool bWasSuccessful, int32 Code, const FString& Body,
	FHttpResponsePtr Response, const FString& Provider, bool bRoundHadOutput)
{
	if (bStopped.load(std::memory_order_relaxed) || State == EState::Done) return false;
	if (!IsTransientHttpFailure(bWasSuccessful, Code)) return false;
	if (TransientRetryCount >= MaxTransientRetries) return false;

	// A transport failure after deltas already reached the UI cannot be replayed transparently.
	if (bRoundHadOutput && (!bWasSuccessful || Code == 0)) return false;

	static const float BackoffSeconds[MaxTransientRetries] = { 1.0f, 3.0f, 8.0f };
	float DelaySec = BackoffSeconds[FMath::Clamp(TransientRetryCount, 0, MaxTransientRetries - 1)];

	// Provider hints (Retry-After / "try again in Ns") only ever lengthen the wait.
	float Hint = 0.0f;
	if (Code == 429 && ParseRetry429Delay(Code, Body, Response, Hint))
	{
		DelaySec = FMath::Max(DelaySec, Hint);
	}
	else if (Response.IsValid())
	{
		const FString RA = Response->GetHeader(TEXT("Retry-After"));
		if (!RA.IsEmpty())
		{
			const float RASec = FCString::Atof(*RA);
			if (RASec > 0.0f) DelaySec = FMath::Max(DelaySec, FMath::Min(RASec, 30.0f));
		}
	}
	DelaySec += FMath::FRandRange(0.0f, 0.5f);

	++TransientRetryCount;
	UE_LOG(LogTemp, Log, TEXT("[Architect] %s %s — auto-retry %d/%d in %.2fs"),
		*(Provider.IsEmpty() ? FString(TEXT("provider")) : Provider),
		(!bWasSuccessful || Code == 0) ? TEXT("connection failed") : *FString::Printf(TEXT("HTTP %d"), Code),
		TransientRetryCount, MaxTransientRetries, DelaySec);

	TWeakPtr<FUECPAgentLoopBase> Weak = AsWeakBase();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Weak](float) -> bool
		{
			if (TSharedPtr<FUECPAgentLoopBase> Self = Weak.Pin())
			{
				if (!Self->bStopped.load(std::memory_order_relaxed) && Self->State != EState::Done)
				{
					--Self->Round;
					Self->SendRound();
				}
			}
			return false;
		}), DelaySec);
	return true;
}

void FUECPAgentLoopBase::WindowHistoryForRequest()
{
	const int32 Num = History.Num();
	if (Num == 0) return;

	TArray<int32> Sizes;
	Sizes.SetNumZeroed(Num);
	int64 Total = 0;
	for (int32 i = 0; i < Num; ++i)
	{
		Sizes[i] = EstimateEntryChars(History[i]);
		Total += Sizes[i];
	}
	if (Total <= MaxHistoryChars) return;

	const int64 Before = Total;

	// The most recent entries carry the tool results the model is about to act on — never elide them.
	constexpr int32 ProtectTail = 4;
	const int32 LastCandidate = FMath::Max(0, Num - ProtectTail);

	int32 ElidedTools = 0, ElidedTexts = 0;
	for (int32 i = 0; i < LastCandidate && Total > MaxHistoryChars; ++i)
	{
		if (!ElideToolResultsInEntry(History[i])) continue;
		const int32 NewSize = EstimateEntryChars(History[i]);
		Total += NewSize - Sizes[i];
		Sizes[i] = NewSize;
		++ElidedTools;
	}
	for (int32 i = 0; i < LastCandidate && Total > MaxHistoryChars; ++i)
	{
		if (!ElideAssistantTextInEntry(History[i])) continue;
		const int32 NewSize = EstimateEntryChars(History[i]);
		Total += NewSize - Sizes[i];
		Sizes[i] = NewSize;
		++ElidedTexts;
	}

	UE_LOG(LogTemp, Log, TEXT("[Architect] History window: %lld -> %lld chars (limit %d) — elided %d tool-result entries, %d assistant-text entries"),
		Before, Total, MaxHistoryChars, ElidedTools, ElidedTexts);
}

void FUECPAgentLoopBase::Stop()
{
	bStopped.store(true, std::memory_order_relaxed);
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	ResetSseState();
	Finish(false, FString());
}

void FUECPAgentLoopBase::ResetSseState()
{
	{
		FScopeLock L(&SseMutex);
		SseBuffer.Empty();
		RawResponseBody.Empty();
	}
	SseRemnant.Empty();
}

bool FUECPAgentLoopBase::IsRunning() const
{
	return State == EState::Streaming || State == EState::Dispatching;
}

void FUECPAgentLoopBase::Finish(bool bSuccess, const FString& Error)
{
	if (State == EState::Done) return;
	State = EState::Done;
	ActiveRequest = nullptr;
	if (OnDone) OnDone(bSuccess, Error);
}

void FUECPAgentLoopBase::OnStreamChunk(void* Ptr, int64& Length)
{
	FUTF8ToTCHAR Conv(reinterpret_cast<const UTF8CHAR*>(Ptr), static_cast<int32>(Length));
	FString Chunk(Conv.Length(), Conv.Get());
	Chunk.ReplaceInline(TEXT("\r"), TEXT(""));
	FScopeLock L(&SseMutex);
	SseBuffer       += Chunk;
	RawResponseBody += Chunk;
}

void FUECPAgentLoopBase::OnRequestProgress(FHttpRequestPtr, uint64, uint64)
{
	if (bStopped.load()) return;
	FString Drained;
	{
		FScopeLock L(&SseMutex);
		Drained = MoveTemp(SseBuffer);
	}
	if (!Drained.IsEmpty()) DrainSseBuffer(Drained);
}

FString FUECPAgentLoopBase::ExtractProviderErrorDetail(const FString& Body)
{
	auto TryStringField = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out) -> bool
	{
		if (O.IsValid() && O->TryGetStringField(Key, Out) && !Out.IsEmpty()) return true;
		Out.Reset();
		return false;
	};

	FString Detail;
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Body);
	if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
	{
		const TSharedPtr<FJsonObject>* ErrObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("error"), ErrObj) && ErrObj && ErrObj->IsValid())
		{
			if (!TryStringField(*ErrObj, TEXT("message"), Detail))
			{
				const TSharedPtr<FJsonObject>* Inner = nullptr;
				if ((*ErrObj)->TryGetObjectField(TEXT("error"), Inner) && Inner && Inner->IsValid())
				{
					if (!TryStringField(*Inner, TEXT("message"), Detail))
						TryStringField(*Inner, TEXT("type"), Detail);
				}
			}
			FString ErrType;
			if (TryStringField(*ErrObj, TEXT("type"), ErrType) && ErrType.Len() <= 64
				&& !Detail.Contains(ErrType))
			{
				Detail = Detail.IsEmpty() ? ErrType : (ErrType + TEXT(": ") + Detail);
			}
			FString ErrCodeStr;
			if (Detail.IsEmpty()) TryStringField(*ErrObj, TEXT("code"), ErrCodeStr);
			if (!ErrCodeStr.IsEmpty()) Detail = ErrCodeStr;
		}
		if (Detail.IsEmpty()) TryStringField(Obj, TEXT("error"), Detail);
		if (Detail.IsEmpty()) TryStringField(Obj, TEXT("message"), Detail);
		if (Detail.IsEmpty()) TryStringField(Obj, TEXT("detail"), Detail);
		if (Detail.IsEmpty()) TryStringField(Obj, TEXT("status"), Detail);
	}
	if (Detail.IsEmpty() && !Body.IsEmpty())
	{
		FString Trimmed = Body.TrimStartAndEnd();
		Trimmed.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Trimmed.ReplaceInline(TEXT("\n"), TEXT(" "));
		Detail = Trimmed.Len() > 400 ? Trimmed.Left(400) + TEXT(" …") : Trimmed;
	}
	return Detail;
}

FString FUECPAgentLoopBase::RecoverResponseBody(FHttpResponsePtr Response, const FString& StreamedBody)
{
	if (Response.IsValid())
	{
		FString Cached = Response->GetContentAsString();
		if (!Cached.IsEmpty()) return Cached;

		const TArray<uint8>& Raw = Response->GetContent();
		if (Raw.Num() > 0)
		{
			FUTF8ToTCHAR Conv(reinterpret_cast<const UTF8CHAR*>(Raw.GetData()), Raw.Num());
			FString FromBytes(Conv.Length(), Conv.Get());
			if (!FromBytes.IsEmpty()) return FromBytes;
		}
	}

	return StreamedBody;
}

bool FUECPAgentLoopBase::ParseRetry429Delay(int32 Code, const FString& Body,
	FHttpResponsePtr Resp, float& OutDelaySec)
{
	OutDelaySec = 0.0f;
	if (Code != 429) return false;

	{
		const FRegexPattern Pat(TEXT("try again in ([0-9]+(?:\\.[0-9]+)?)\\s*(s|ms)"));
		FRegexMatcher Matcher(Pat, Body);
		if (Matcher.FindNext())
		{
			const float N = FCString::Atof(*Matcher.GetCaptureGroup(1));
			const FString Unit = Matcher.GetCaptureGroup(2);
			OutDelaySec = (Unit == TEXT("ms")) ? (N / 1000.0f) : N;
		}
	}

	if (OutDelaySec <= 0.0f && Resp.IsValid())
	{
		const FString RA = Resp->GetHeader(TEXT("Retry-After"));
		if (!RA.IsEmpty())
		{
			OutDelaySec = FCString::Atof(*RA);
		}
	}

	if (OutDelaySec <= 0.0f) OutDelaySec = 2.0f;
	OutDelaySec = FMath::Clamp(OutDelaySec, 0.5f, 8.0f);
	return true;
}

FString FUECPAgentLoopBase::FormatHttpError(bool bWasSuccessful, int32 Code,
	const FString& Body, const FString& Provider)
{
	const FString P = Provider.IsEmpty() ? TEXT("the AI provider") : Provider;

	if (!bWasSuccessful || Code == 0)
		return FString::Printf(
			TEXT("**Connection failed**\n\n")
			TEXT("The request did not reach %s. This is usually a timeout or a temporary network issue.\n\n")
			TEXT("**Try:** Send the message again — %s may be temporarily down."), *P, *P);

	UE_LOG(LogTemp, Log, TEXT("[Architect] HTTP %d from %s, body=%s"),
		Code, *P, Body.IsEmpty() ? TEXT("<empty>") : *Body.Left(800));

	FString Detail = ExtractProviderErrorDetail(Body);
	if (Detail.IsEmpty())
	{
		Detail = FString::Printf(
			TEXT("(no error body received from %s — check the editor's Output Log "
			     "for the response. UE's streaming HTTP path can drop bodies on "
			     "non-2xx responses.)"), *P);
	}
	const FString DetailLine = TEXT("\n\n> ") + Detail;

	const FString DetailLower = Detail.ToLower();
	const bool bQuotaIssue =
		DetailLower.Contains(TEXT("quota"))     || DetailLower.Contains(TEXT("credit"))   ||
		DetailLower.Contains(TEXT("balance"))   || DetailLower.Contains(TEXT("insufficient")) ||
		DetailLower.Contains(TEXT("billing"))   || DetailLower.Contains(TEXT("usage limit")) ||
		DetailLower.Contains(TEXT("free tier")) || DetailLower.Contains(TEXT("payment"));

	{
		const bool bJinjaTemplateError =
			DetailLower.Contains(TEXT("jinja"))
			|| DetailLower.Contains(TEXT("rendering prompt"))
			|| DetailLower.Contains(TEXT("no user query found"))
			|| DetailLower.Contains(TEXT("chat template"))
			|| DetailLower.Contains(TEXT("template error"));
		if (bJinjaTemplateError)
		{
			return FString::Printf(
				TEXT("**Local model chat-template mismatch**\n\n")
				TEXT("The model's built-in Jinja chat template doesn't accept the message shape this plugin sends ")
				TEXT("(usually `role:\"tool\"` continuations or assistant `tool_calls` blocks). The template is baked ")
				TEXT("into the GGUF — there's nothing this plugin can change to satisfy it.\n\n")
				TEXT("**Workarounds (in order of effort):**\n")
				TEXT("- LM Studio: open **My Models → ⚙ next to the model → Prompt Template** and paste a template that supports OpenAI-style tool calls, OR\n")
				TEXT("- Search **lmstudio-community** for a re-quantised version of the same model — those often ship with patched templates, OR\n")
				TEXT("- Switch to a model known to work with tool-calling (Qwen3-Coder, DeepSeek-R1, Llama-3.1-Instruct, Mistral-Nemo, …).\n\n")
				TEXT("> %s"), *Detail);
		}
	}

	{
		const bool bNotChatModel =
			DetailLower.Contains(TEXT("not a chat model"))
			|| DetailLower.Contains(TEXT("v1/completions"));
		if (bNotChatModel)
			return FString::Printf(
				TEXT("**Model doesn't support chat (400)**\n\n")
				TEXT("This model is a base/completion model and cannot be used with the Chat Completions endpoint this plugin uses.\n\n")
				TEXT("**Fix:** Switch to the instruct or chat variant of the model (e.g. add `-turbo` or `-instruct` suffix in **Settings → API Keys → Model**). "
				     "Base models without an instruct fine-tune don't understand the system/user/assistant message format.\n\n")
				TEXT("> %s"), *Detail);
	}

	switch (Code)
	{
	case 401:
		return FString::Printf(
			TEXT("**Invalid API Key (401)**\n\n")
			TEXT("Your API key was rejected by %s. Open **Settings → API Keys** and check the key for the active slot is correct and hasn't expired.%s"),
			*P, *DetailLine);
	case 402:
		return FString::Printf(
			TEXT("**Billing required (402)**\n\n")
			TEXT("%s says your account is out of credit or needs payment to continue. Top up the account or switch to a different API key slot in **Settings → API Keys**.%s"),
			*P, *DetailLine);
	case 403:
		if (bQuotaIssue)
			return FString::Printf(
				TEXT("**Quota exhausted (403)**\n\n")
				TEXT("%s says this key is out of usage. Top up the account, wait for the quota reset, or switch API key slots in **Settings → API Keys**.%s"),
				*P, *DetailLine);
		return FString::Printf(
			TEXT("**Access Denied (403)**\n\n")
			TEXT("Your API key doesn't have permission to use this model or endpoint. ")
			TEXT("Check your API plan, or try switching to a different model in Settings.%s"), *DetailLine);
	case 429:
		if (bQuotaIssue)
			return FString::Printf(
				TEXT("**Usage limit reached (429)**\n\n")
				TEXT("%s says this key is out of credit / over its quota. Top up the account or switch API key slots in **Settings → API Keys**.%s"),
				*P, *DetailLine);
		return FString::Printf(
			TEXT("**Rate limit hit (429)**\n\n")
			TEXT("%s is throttling this account, and the auto-retry didn't clear it. ")
			TEXT("This is a per-minute token / request cap on **your** %s account, not the plugin. ")
			TEXT("Options: wait a minute and try again, raise your usage tier on the %s dashboard, ")
			TEXT("or switch to a smaller / faster model variant in **Settings → API Keys** for this round.%s"),
			*P, *P, *P, *DetailLine);
	case 400:
	{
		const bool bWrongTokenParam =
			DetailLower.Contains(TEXT("max_tokens"))
			&& (DetailLower.Contains(TEXT("unknown")) || DetailLower.Contains(TEXT("unsupported"))
				|| DetailLower.Contains(TEXT("not supported")) || DetailLower.Contains(TEXT("did you mean")));
		if (bWrongTokenParam)
			return FString::Printf(
				TEXT("**Unsupported parameter (400)**\n\n")
				TEXT("This model does not accept the `max_tokens` parameter — it uses `max_completion_tokens` instead. ")
				TEXT("This is a plugin bug; please report it.\n\n")
				TEXT("> %s"), *Detail);

		const bool bCtxOverflow =
			(DetailLower.Contains(TEXT("context")) || DetailLower.Contains(TEXT("length"))
			 || DetailLower.Contains(TEXT("exceed")) || DetailLower.Contains(TEXT("too long")))
			|| (DetailLower.Contains(TEXT("token")) &&
				(DetailLower.Contains(TEXT("limit")) || DetailLower.Contains(TEXT("maximum"))
				 || DetailLower.Contains(TEXT("window")) || DetailLower.Contains(TEXT("exceed"))));
		if (bCtxOverflow)
			return TEXT("**Context window exceeded (400)**\n\n")
				   TEXT("The system prompt and tool catalog this plugin sends (~8–12K tokens) already exceeds this model's context window, even on a fresh chat.\n\n")
				   TEXT("**Fix:** Switch to a model with at least **32K context** in **Settings → API Keys**. "
				        "If you added a large `max_tokens` in **Custom Request Params**, reduce it — the input + output must fit within the model's total context limit.");
		if (bQuotaIssue)
			return FString::Printf(
				TEXT("**Quota / billing issue (400)**\n\n")
				TEXT("%s rejected the request with a quota or billing message. Check the account in your provider dashboard or switch keys in **Settings → API Keys**.%s"),
				*P, *DetailLine);
		return FString::Printf(
			TEXT("**Bad request (400)**\n\n")
			TEXT("The request was rejected, possibly due to an unsupported parameter or malformed content.%s"), *DetailLine);
	}
	case 500: case 502: case 503: case 504: case 529:
		return FString::Printf(
			TEXT("**Server error (%d)**\n\n")
			TEXT("%s is experiencing issues and the automatic retries did not clear it. Wait a moment and try again.%s"), Code, *P, *DetailLine);
	default:
		return FString::Printf(TEXT("**API error (%d)**\n\n%s%s"), Code, *P, *DetailLine);
	}
}

TArray<FUECPToolCatalogEntry> FUECPAgentLoopBase::GetStandardToolEntries()
{
	static const FUECPToolCatalogEntry Entries[] =
	{
		{ TEXT("asset_management"), TEXT("Find, move, duplicate, rename, delete, save, open, validate, fix-up, batch-rename, import, and inspect project assets.") },
		{ TEXT("physics"),          TEXT("Physics Assets, Physical Materials, rigid body settings.") },
		{ TEXT("curve"),            TEXT("Curve assets, keys, interpolation, Curve Tables.") },
		{ TEXT("mesh"),             TEXT("Static + Skeletal Meshes: sockets, LODs, morph targets, clothing.") },
		{ TEXT("string_table"),     TEXT("String Tables and localisation entries.") },
		{ TEXT("editor_utility"),   TEXT("Editor Utility Blueprints + Widgets, plus editor-side automation: console, log, screenshot, filesystem, plugin discovery, project analysis.") },
		{ TEXT("play_test"),        TEXT("PIE profiling, screenshots, performance reports.") },
		{ TEXT("project_viz"),      TEXT("Project architecture visualisation dashboards.") },
		{ TEXT("render"),           TEXT("Rendering settings, ray tracing.") },
		{ TEXT("groom"),            TEXT("Groom assets: hair strands, cards, physics simulation.") },
		{ TEXT("project_plan"),     TEXT("Per-conversation plan BRIEF (goal, assets + their paths, conventions/guidelines, key decisions) — the durable 'what + why'. create_plan(title, context) | get_plan | clear_plan | list_plans | import_plan. The step-by-step checklist is the separate `task` tool. Use a plan for genuine multi-step work, not single calls or lookups.") },
		{ TEXT("task"),             TEXT("Lightweight task checklist (TodoWrite-style) — independent of any plan, usable any time / any mode. set_tasks(items=[{content, status}]) to (re)write the whole list; update_task(index, status) as you go (pending|in_progress|done|failed); also add_task/edit_task/remove_task/reorder_task/clear_tasks/get_tasks. Maintain it for any non-trivial multi-step work so the user can watch progress.") },
		{ TEXT("memory"),           TEXT("Working notes and AI memory: store + retrieve context for multi-step tasks.") },
		{ TEXT("git_tools"),        TEXT("Git operations: status, log, diff, branch management.") },
		{ TEXT("config"),           TEXT("Project settings (typed get/set) + raw .ini inspection: resolve, explain, diff, search.") },
		{ TEXT("meshy"),            TEXT("Meshy.ai 3D ecosystem: text→3D, image→3D, remesh, retexture, rig (humanoid), animate from action catalog, text→image, credit balance. Long-running actions return {status:'queued', job_id} immediately; the result lands later as an [TOOL_RESULT:meshy.*:...] message with the imported asset path.") },
		{ TEXT("search_tools"),         TEXT("Find a tool ACTION by intent: pass English keywords (e.g. \"set material two sided\", \"add IK to control rig\") → matching actions + their umbrella. Use FIRST when unsure which umbrella owns a capability, rather than guessing. Query in English even in a non-English chat. Or pass umbrella='<category>' with no query to list all of an umbrella's actions."), false },
		{ TEXT("find_tool"),            TEXT("Alias of search_tools — find a tool action by an English intent query."), false },
		{ TEXT("discover_tools"),       TEXT("Alias of search_tools — find a tool action by an English intent query."), false },
		{ TEXT("get_tool_docs"),        TEXT("Parameter docs for a tool category — or pass action='<action>' for just one action's params (token-cheap). search_tools usually suffices first."), false },
		{ TEXT("get_handle_reference"), TEXT("Fallback handle reference for build_blueprint_graph — auto-repair covers most mistakes; only fetch when a build fails AND repairs_made[] didn't cover it."), false },
		{ TEXT("get_selected_assets"),  TEXT("Assets currently selected in the Content Browser."), false },
		{ TEXT("get_selected_nodes"),   TEXT("Nodes currently selected in the focused asset-editor graph (Blueprint, AnimBP, Material, Behavior Tree). Use whenever the user references \"these nodes\" / \"selected nodes\" / \"this graph\"."), false },
		{ TEXT("get_current_folder"),   TEXT("Current Content Browser folder path."), false },
		{ TEXT("ask_user"),             TEXT("Ask the user structured questions in an inline card and BLOCK for their answer. questions=[{header, question, multiSelect?, options:[{label, description?}]}] — an 'Other' free-text + a feedback box are added automatically. Use for genuine decisions you can't infer (architecture, naming, scope trade-offs); not for things you can decide yourself."), false },
		{ TEXT("proceed_with_plan"),    TEXT("Leave Plan Mode and start building — switches the chat into the user's execution mode (asks once via a gate the first time, then remembers). Call ONLY after you've recorded a plan and the user confirms 'proceed'. No arguments."), false },
	};

	TArray<FUECPToolCatalogEntry> Result;
	Result.Reserve(UE_ARRAY_COUNT(Entries) + 16);

	IUECPExtensionService* ExtSvc = IUECPCoreModule::IsAvailable()
		? &IUECPCoreModule::Get().GetExtensionService() : nullptr;

	TSet<FName> HardCodedNames;
	HardCodedNames.Reserve(UE_ARRAY_COUNT(Entries));
	for (const FUECPToolCatalogEntry& E : Entries)
	{
		HardCodedNames.Add(FName(E.Name));
		if (!ExtSvc || ExtSvc->ShouldShowUmbrella(FName(E.Name)))
		{
			Result.Add(E);
		}
	}

	if (ExtSvc)
	{
		Result.Append(ExtSvc->GetExtensionCatalogEntries(HardCodedNames));
	}

	// Umbrella entries without a hand-written schema get the one derived from registered tool
	// metadata (action enum + typed, per-action-annotated parameters). Entries whose umbrella has
	// no metadata keep a null schema and fall back to the permissive {action} shape in each loop.
	if (IUECPCoreModule::IsAvailable())
	{
		IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
		for (FUECPToolCatalogEntry& E : Result)
		{
			if (E.bIsUmbrella && !E.InputSchema.IsValid())
			{
				E.InputSchema = Dispatcher.GetUmbrellaSchema(FName(*E.Name));
			}
		}
	}

	return Result;
}

bool FUECPAgentLoopBase::IsUmbrellaName(const FString& Name)
{
	if (UECPToolDispatch::IsUmbrellaName(FName(*Name))) return true;

	if (!IUECPCoreModule::IsAvailable()) return false;
	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
	if (TOptional<FUECPExtensionDescriptor> Owner = Ext.FindExtensionByUmbrella(FName(*Name)); Owner.IsSet())
	{
		return Ext.GetExtensionState(Owner->ExtensionId) == EUECPExtensionState::Loaded;
	}
	return false;
}

bool FUECPAgentLoopBase::TextEndsWithIntent(const FString& Text)
{
	if (Text.IsEmpty()) return false;

	FString Trimmed = Text.TrimStartAndEnd();
	while (Trimmed.Len() > 0)
	{
		const TCHAR Last = Trimmed[Trimmed.Len() - 1];
		if (Last == TEXT('.') || Last == TEXT('!') || Last == TEXT('?') ||
			Last == TEXT(':') || Last == TEXT(';') || Last == TEXT('*') ||
			Last == TEXT('"') || Last == TEXT(')') || FChar::IsWhitespace(Last))
		{
			Trimmed.LeftChopInline(1, EAllowShrinking::No);
		}
		else break;
	}
	if (Trimmed.IsEmpty()) return false;

	int32 LastBreak = INDEX_NONE;
	for (int32 i = Trimmed.Len() - 1; i >= 0; --i)
	{
		const TCHAR C = Trimmed[i];
		if (C == TEXT('.') || C == TEXT('!') || C == TEXT('?') || C == TEXT('\n'))
		{ LastBreak = i; break; }
	}
	FString LastSentence = (LastBreak >= 0)
		? Trimmed.RightChop(LastBreak + 1).TrimStartAndEnd()
		: Trimmed;

	if (LastSentence.Len() > 240) return false;
	const FString Lower = LastSentence.ToLower();

	static const TCHAR* const IntentPhrases[] = {
		TEXT("let me "),       TEXT("let's "),
		TEXT("i'll "),         TEXT("i will "),     TEXT("i'm going to "),
		TEXT("now let "),      TEXT("now i'll "),   TEXT("now i will "),
		TEXT("first, "),       TEXT("first let "),  TEXT("first i'll "),
		TEXT("starting with "),TEXT("looking at "), TEXT("checking "),
		TEXT("tracing "),      TEXT("investigating "),
	};
	for (const TCHAR* Phrase : IntentPhrases)
	{
		if (Lower.Contains(Phrase)) return true;
	}
	return false;
}

static void InjectPythonKwargs(const FString& KwargStr, const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid() || KwargStr.IsEmpty()) return;

	TArray<FString> Pairs;
	int32 Depth = 0;
	int32 Start = 0;
	for (int32 i = 0; i < KwargStr.Len(); i++)
	{
		TCHAR C = KwargStr[i];
		if (C == TEXT('[') || C == TEXT('(')) Depth++;
		else if (C == TEXT(']') || C == TEXT(')')) Depth--;
		else if (C == TEXT(',') && Depth == 0) { Pairs.Add(KwargStr.Mid(Start, i - Start).TrimStartAndEnd()); Start = i + 1; }
	}
	Pairs.Add(KwargStr.Mid(Start).TrimStartAndEnd());

	for (const FString& Pair : Pairs)
	{
		int32 EqIdx;
		if (!Pair.FindChar(TEXT('='), EqIdx)) continue;
		FString Key = Pair.Left(EqIdx).TrimStartAndEnd();
		FString Val = Pair.Mid(EqIdx + 1).TrimStartAndEnd();
		if (Key.IsEmpty() || Val.IsEmpty()) continue;

		if (Args->HasField(Key)) continue;

		if (Val.StartsWith(TEXT("[")))
		{
			TArray<TSharedPtr<FJsonValue>> JsonArr;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Val);
			if (FJsonSerializer::Deserialize(Reader, JsonArr))
			{
				Args->SetArrayField(Key, JsonArr);
			}
			else
			{
				int32 Close = Val.Find(TEXT("]"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				FString Inner = (Close != INDEX_NONE) ? Val.Mid(1, Close - 1) : Val.Mid(1);
				TArray<TSharedPtr<FJsonValue>> Arr;
				TArray<FString> Elems;
				Inner.ParseIntoArray(Elems, TEXT(","));
				for (FString& E : Elems)
				{
					E = E.TrimStartAndEnd();
					E.RemoveFromStart(TEXT("'")); E.RemoveFromEnd(TEXT("'"));
					E.RemoveFromStart(TEXT("\"")); E.RemoveFromEnd(TEXT("\""));
					if (!E.IsEmpty()) Arr.Add(MakeShared<FJsonValueString>(E));
				}
				if (Arr.Num() > 0) Args->SetArrayField(Key, Arr);
			}
		}
		else if (Val.StartsWith(TEXT("{")))
		{
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Val);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				Args->SetObjectField(Key, JsonObj);
		}
		else
		{
			Val.RemoveFromStart(TEXT("'")); Val.RemoveFromEnd(TEXT("'"));
			Val.RemoveFromStart(TEXT("\"")); Val.RemoveFromEnd(TEXT("\""));
			if (!Val.IsEmpty()) Args->SetStringField(Key, Val);
		}
	}
}

static FString ExtractXmlArgKeyPairs(const FString& RawName, const TSharedPtr<FJsonObject>& Args)
{
	int32 XmlStart;
	if (!RawName.FindChar(TEXT('<'), XmlStart))
		return RawName;

	FString Base = RawName.Left(XmlStart).TrimStartAndEnd();
	FString Remainder = RawName.Mid(XmlStart);

	while (!Remainder.IsEmpty())
	{
		const int32 KoIdx = Remainder.Find(TEXT("<arg_key>"));
		if (KoIdx == INDEX_NONE) break;
		const int32 KcIdx = Remainder.Find(TEXT("</arg_key>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, KoIdx + 9);
		if (KcIdx == INDEX_NONE) break;

		const FString Key = Remainder.Mid(KoIdx + 9, KcIdx - KoIdx - 9).TrimStartAndEnd();
		int32 NextSearch = KcIdx + 10;

		FString Val;
		const int32 VoIdx = Remainder.Find(TEXT("<arg_value>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, NextSearch);
		if (VoIdx != INDEX_NONE)
		{
			const int32 VcIdx = Remainder.Find(TEXT("</arg_value>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, VoIdx + 11);
			if (VcIdx != INDEX_NONE)
			{
				Val = Remainder.Mid(VoIdx + 11, VcIdx - VoIdx - 11).TrimStartAndEnd();
				NextSearch = VcIdx + 12;
			}
			else
			{
				Val = Remainder.Mid(VoIdx + 11).TrimStartAndEnd();
				NextSearch = Remainder.Len();
			}
		}

		if (!Key.IsEmpty() && !Val.IsEmpty() && Args.IsValid() && !Args->HasField(Key))
			Args->SetStringField(Key, Val);

		Remainder = Remainder.Mid(NextSearch);
	}

	return Base;
}

FName FUECPAgentLoopBase::ResolveDispatchName(const FString& ToolName,
	const TSharedPtr<FJsonObject>& Args)
{
	FString BaseName = ToolName;

	if (BaseName.Contains(TEXT("<arg_key>")))
		BaseName = ExtractXmlArgKeyPairs(BaseName, Args);

	int32 ParenIdx;
	if (BaseName.FindChar(TEXT('('), ParenIdx))
	{
		const FString Inner = BaseName.Mid(ParenIdx + 1);
		BaseName = BaseName.Left(ParenIdx).TrimStartAndEnd();
		int32 CloseIdx = Inner.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		const FString KwStr = (CloseIdx != INDEX_NONE) ? Inner.Left(CloseIdx) : Inner;
		InjectPythonKwargs(KwStr, Args);
	}

	if (!IsUmbrellaName(BaseName))
		return FName(*BaseName);

	FString Action;
	if (Args.IsValid() && Args->TryGetStringField(TEXT("action"), Action) && !Action.IsEmpty())
	{
		int32 XmlTagStart;
		if (Action.FindChar(TEXT('<'), XmlTagStart))
			Action.LeftInline(XmlTagStart, EAllowShrinking::No);
		Action.TrimStartAndEndInline();
		if (!Action.IsEmpty())
			return FName(*Action);
	}

	return FName(*BaseName);
}
