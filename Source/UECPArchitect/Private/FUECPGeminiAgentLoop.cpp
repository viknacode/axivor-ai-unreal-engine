// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPGeminiAgentLoop.h"
#include "Misc/EngineVersionComparison.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCrewService.h"
#include "Types/CallerContext.h"
#include "Containers/Ticker.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ToJsonStr(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Str)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Str);
		FJsonSerializer::Deserialize(R, Obj);
		return Obj;
	}

	// Gemini's function `parameters` is an OpenAPI subset: only type / description / properties /
	// required / items / enum / format / nullable are accepted (additionalProperties in particular is
	// rejected), and an OBJECT must declare at least one sub-property. Object-typed params with no
	// known sub-fields are therefore dropped here and reported through OutDropped so the caller can
	// mention them in a description instead (Gemini still forwards undeclared arguments).
	TSharedPtr<FJsonObject> SanitizeSchemaForGemini(const TSharedPtr<FJsonObject>& In,
		TArray<FString>* OutDropped, int32 Depth = 0)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!In.IsValid())
		{
			Out->SetStringField(TEXT("type"), TEXT("object"));
			Out->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
			return Out;
		}

		static const TCHAR* const CopiedFields[] = {
			TEXT("type"), TEXT("description"), TEXT("enum"), TEXT("format"), TEXT("nullable") };
		for (const TCHAR* Field : CopiedFields)
		{
			if (In->HasField(Field)) Out->SetField(Field, In->TryGetField(Field));
		}

		FString Type;
		In->TryGetStringField(TEXT("type"), Type);
		if (Type.IsEmpty())
		{
			Type = TEXT("string");
			Out->SetStringField(TEXT("type"), Type);
		}

		if (Type == TEXT("array"))
		{
			const TSharedPtr<FJsonObject>* ItemsIn = nullptr;
			TSharedPtr<FJsonObject> Items = (In->TryGetObjectField(TEXT("items"), ItemsIn) && ItemsIn && ItemsIn->IsValid())
				? SanitizeSchemaForGemini(*ItemsIn, nullptr, Depth + 1)
				: nullptr;
			if (!Items.IsValid())
			{
				Items = MakeShared<FJsonObject>();
				Items->SetStringField(TEXT("type"), TEXT("string"));
			}
			Out->SetObjectField(TEXT("items"), Items);
		}

		if (Type == TEXT("object"))
		{
			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			TSet<FString> Kept;
			const TSharedPtr<FJsonObject>* PropsIn = nullptr;
			if (In->TryGetObjectField(TEXT("properties"), PropsIn) && PropsIn && PropsIn->IsValid())
			{
				for (const auto& KV : (*PropsIn)->Values)
				{
					const FString PropName(*KV.Key);
					const TSharedPtr<FJsonObject> Child = KV.Value.IsValid() ? KV.Value->AsObject() : nullptr;
					if (!Child.IsValid()) continue;

					TSharedPtr<FJsonObject> Clean = SanitizeSchemaForGemini(Child, nullptr, Depth + 1);

					// Untyped nested objects (or arrays of them) cannot be expressed for Gemini.
					auto IsEmptyObject = [](const TSharedPtr<FJsonObject>& S) -> bool
					{
						FString T; S->TryGetStringField(TEXT("type"), T);
						if (T != TEXT("object")) return false;
						const TSharedPtr<FJsonObject>* P = nullptr;
						return !S->TryGetObjectField(TEXT("properties"), P) || !P || !P->IsValid() || (*P)->Values.Num() == 0;
					};
					bool bDrop = IsEmptyObject(Clean);
					if (!bDrop)
					{
						const TSharedPtr<FJsonObject>* CleanItems = nullptr;
						if (Clean->TryGetObjectField(TEXT("items"), CleanItems) && CleanItems && CleanItems->IsValid())
							bDrop = IsEmptyObject(*CleanItems);
					}
					if (bDrop)
					{
						if (OutDropped && Depth == 0) OutDropped->Add(PropName);
						continue;
					}
					Props->SetObjectField(PropName, Clean);
					Kept.Add(PropName);
				}
			}
			Out->SetObjectField(TEXT("properties"), Props);

			const TArray<TSharedPtr<FJsonValue>>* ReqIn = nullptr;
			if (In->TryGetArrayField(TEXT("required"), ReqIn) && ReqIn)
			{
				TArray<TSharedPtr<FJsonValue>> Req;
				for (const TSharedPtr<FJsonValue>& V : *ReqIn)
				{
					FString Name;
					if (V.IsValid() && V->TryGetString(Name) && Kept.Contains(Name))
						Req.Add(MakeShared<FJsonValueString>(Name));
				}
				if (Req.Num() > 0) Out->SetArrayField(TEXT("required"), Req);
			}
		}
		return Out;
	}
}

TArray<TSharedPtr<FJsonValue>> FUECPGeminiAgentLoop::BuildToolDefinitions()
{
	auto Decl = [](const FString& Name, const FString& Desc, bool bRequireAction)
		-> TSharedPtr<FJsonValue>
	{
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("type"), TEXT("object"));
		if (bRequireAction)
		{
			TSharedRef<FJsonObject> ActionProp = MakeShared<FJsonObject>();
			ActionProp->SetStringField(TEXT("type"), TEXT("string"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("action"), ActionProp);
			Params->SetObjectField(TEXT("properties"), Props);
			TArray<TSharedPtr<FJsonValue>> Req;
			Req.Add(MakeShared<FJsonValueString>(TEXT("action")));
			Params->SetArrayField(TEXT("required"), Req);
		}
		else
		{
			Params->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		}

		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("name"),        Name);
		D->SetStringField(TEXT("description"), Desc);
		D->SetObjectField(TEXT("parameters"),  Params);
		return MakeShared<FJsonValueObject>(D);
	};

	TArray<TSharedPtr<FJsonValue>> Decls;
	for (const FUECPToolCatalogEntry& E : GetStandardToolEntries())
	{
		if (FString(E.Name) == TEXT("get_tool_docs"))
		{
			TSharedRef<FJsonObject> CatProp = MakeShared<FJsonObject>();
			CatProp->SetStringField(TEXT("type"), TEXT("string"));
			CatProp->SetStringField(TEXT("description"), TEXT("Single category name (e.g. 'blueprint'). Omit when using categories[]."));
			TSharedRef<FJsonObject> CatsItems = MakeShared<FJsonObject>();
			CatsItems->SetStringField(TEXT("type"), TEXT("string"));
			TSharedRef<FJsonObject> CatsProp = MakeShared<FJsonObject>();
			CatsProp->SetStringField(TEXT("type"), TEXT("array"));
			CatsProp->SetObjectField(TEXT("items"), CatsItems);
			CatsProp->SetStringField(TEXT("description"), TEXT("Batch: list of category names."));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("category"), CatProp);
			Props->SetObjectField(TEXT("categories"), CatsProp);
			TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("type"), TEXT("object"));
			Params->SetObjectField(TEXT("properties"), Props);
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("name"),        TEXT("get_tool_docs"));
			D->SetStringField(TEXT("description"), FString(E.Description));
			D->SetObjectField(TEXT("parameters"),  Params);
			Decls.Add(MakeShared<FJsonValueObject>(D));
		}
		else if (FString(E.Name) == TEXT("project_plan"))
		{
			TSharedRef<FJsonObject> ActionProp = MakeShared<FJsonObject>();
			ActionProp->SetStringField(TEXT("type"), TEXT("string"));
			ActionProp->SetStringField(TEXT("description"), TEXT("create_plan | get_plan | clear_plan | list_plans | import_plan"));
			TSharedRef<FJsonObject> TitleProp = MakeShared<FJsonObject>();
			TitleProp->SetStringField(TEXT("type"), TEXT("string"));
			TSharedRef<FJsonObject> CtxProp = MakeShared<FJsonObject>();
			CtxProp->SetStringField(TEXT("type"), TEXT("string"));
			CtxProp->SetStringField(TEXT("description"), TEXT("The brief body: goal, assets + their paths, conventions/guidelines, key decisions."));
			TSharedRef<FJsonObject> SrcProp = MakeShared<FJsonObject>();
			SrcProp->SetStringField(TEXT("type"), TEXT("string"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetObjectField(TEXT("action"), ActionProp);
			Props->SetObjectField(TEXT("title"), TitleProp);
			Props->SetObjectField(TEXT("context"), CtxProp);
			Props->SetObjectField(TEXT("source_conv_id"), SrcProp);
			TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("type"), TEXT("object"));
			Params->SetObjectField(TEXT("properties"), Props);
			TArray<TSharedPtr<FJsonValue>> Req;
			Req.Add(MakeShared<FJsonValueString>(TEXT("action")));
			Params->SetArrayField(TEXT("required"), Req);
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("name"),        TEXT("project_plan"));
			D->SetStringField(TEXT("description"), FString(E.Description));
			D->SetObjectField(TEXT("parameters"),  Params);
			Decls.Add(MakeShared<FJsonValueObject>(D));
		}
		else if (FString(E.Name) == TEXT("memory"))
		{
			TSharedRef<FJsonObject> MemActionProp = MakeShared<FJsonObject>();
			MemActionProp->SetStringField(TEXT("type"), TEXT("string"));
			MemActionProp->SetStringField(TEXT("description"), TEXT("add_memory | suggest_memory | get_memories | delete_memory"));
			TSharedRef<FJsonObject> ContentProp = MakeShared<FJsonObject>();
			ContentProp->SetStringField(TEXT("type"), TEXT("string"));
			ContentProp->SetStringField(TEXT("description"), TEXT("Memory text (add_memory / suggest_memory)."));
			TSharedRef<FJsonObject> CatProp = MakeShared<FJsonObject>();
			CatProp->SetStringField(TEXT("type"), TEXT("string"));
			CatProp->SetStringField(TEXT("description"), TEXT("project_info | recent_work | preferences | patterns | asset_relations | decisions"));
			TSharedRef<FJsonObject> MemIdProp = MakeShared<FJsonObject>();
			MemIdProp->SetStringField(TEXT("type"), TEXT("integer"));
			MemIdProp->SetStringField(TEXT("description"), TEXT("Memory id to delete (delete_memory)."));
			TSharedRef<FJsonObject> MemProps = MakeShared<FJsonObject>();
			MemProps->SetObjectField(TEXT("action"), MemActionProp);
			MemProps->SetObjectField(TEXT("content"), ContentProp);
			MemProps->SetObjectField(TEXT("category"), CatProp);
			MemProps->SetObjectField(TEXT("memory_id"), MemIdProp);
			TSharedRef<FJsonObject> MemParams = MakeShared<FJsonObject>();
			MemParams->SetStringField(TEXT("type"), TEXT("object"));
			MemParams->SetObjectField(TEXT("properties"), MemProps);
			TArray<TSharedPtr<FJsonValue>> MemReq;
			MemReq.Add(MakeShared<FJsonValueString>(TEXT("action")));
			MemParams->SetArrayField(TEXT("required"), MemReq);
			TSharedRef<FJsonObject> MemD = MakeShared<FJsonObject>();
			MemD->SetStringField(TEXT("name"),        TEXT("memory"));
			MemD->SetStringField(TEXT("description"), FString(E.Description));
			MemD->SetObjectField(TEXT("parameters"),  MemParams);
			Decls.Add(MakeShared<FJsonValueObject>(MemD));
		}
		else if (E.InputSchema.IsValid())
		{
			// Both extension MCP proxy schemas and the metadata-derived umbrella schemas go through
			// the Gemini sanitizer (no additionalProperties, no empty OBJECT properties).
			TArray<FString> Dropped;
			TSharedPtr<FJsonObject> Params = SanitizeSchemaForGemini(E.InputSchema, &Dropped);

			FString Description = E.Description;
			if (Dropped.Num() > 0)
			{
				const FString Note = FString::Printf(
					TEXT(" Also accepted (JSON object/array values, not declared in the schema): %s."),
					*FString::Join(Dropped, TEXT(", ")));
				const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
				const TSharedPtr<FJsonObject>* ActionPtr = nullptr;
				if (Params->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr && PropsPtr->IsValid()
					&& (*PropsPtr)->TryGetObjectField(TEXT("action"), ActionPtr) && ActionPtr && ActionPtr->IsValid())
				{
					FString ActionDesc;
					(*ActionPtr)->TryGetStringField(TEXT("description"), ActionDesc);
					(*ActionPtr)->SetStringField(TEXT("description"), ActionDesc + Note);
				}
				else
				{
					Description += Note;
				}
			}

			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("name"),        E.Name);
			D->SetStringField(TEXT("description"), Description);
			D->SetObjectField(TEXT("parameters"),  Params);
			Decls.Add(MakeShared<FJsonValueObject>(D));
		}
		else
		{
			Decls.Add(Decl(E.Name, E.Description, E.bIsUmbrella));
		}
	}

	TSharedRef<FJsonObject> ToolEntry = MakeShared<FJsonObject>();
	ToolEntry->SetArrayField(TEXT("function_declarations"), Decls);

	TArray<TSharedPtr<FJsonValue>> Tools;
	Tools.Add(MakeShared<FJsonValueObject>(ToolEntry));
	return Tools;
}

TSharedPtr<FJsonValue> FUECPGeminiAgentLoop::FilterHistoryEntry(const TSharedPtr<FJsonValue>& Entry)
{
	if (!Entry.IsValid() || Entry->Type != EJson::Object) return nullptr;

	TSharedPtr<FJsonObject> Obj = Entry->AsObject();
	FString Role;
	if (!Obj->TryGetStringField(TEXT("role"), Role)) return nullptr;

	if (Role == TEXT("context") || Role == TEXT("tool_bubble") ||
		Role == TEXT("agent_thinking") || Role == TEXT("spinner")) return nullptr;

	static const TSet<FString> AllowedFields = {
		TEXT("role"), TEXT("parts"), TEXT("functionCall"), TEXT("functionResponse")
	};
	TSharedRef<FJsonObject> Clean = MakeShared<FJsonObject>();
	for (const auto& KV : Obj->Values)
	{
		const FString FieldName(*KV.Key);
		if (AllowedFields.Contains(FieldName))
			Clean->SetField(FieldName, KV.Value);
	}
	if (Clean->Values.Num() == 0) return nullptr;
	return MakeShared<FJsonValueObject>(Clean);
}

void FUECPGeminiAgentLoop::Start()
{
	check(IsInGameThread());
	State = EState::Streaming;
	SendRound();
}

void FUECPGeminiAgentLoop::SendRound()
{
	check(IsInGameThread());

	if (bStopped || Round >= MaxRounds)
	{
		Finish(false, bStopped ? TEXT("") : FString::Printf(
			TEXT("Reached the %d-round limit. Type a follow-up message to continue."), MaxRounds));
		return;
	}
	++Round;

	PendingToolCalls.Reset();
	PendingRawNonTextParts.Reset();
	TextAccum.Empty();
	FinishReason.Empty();
	FinalInputTokens  = 0;
	FinalOutputTokens = 0;
	ResetSseState();

	WindowHistoryForRequest();

	TSharedRef<FJsonObject> SysText = MakeShared<FJsonObject>();
	SysText->SetStringField(TEXT("text"), SystemPrompt);
	TArray<TSharedPtr<FJsonValue>> SysParts;
	SysParts.Add(MakeShared<FJsonValueObject>(SysText));
	TSharedRef<FJsonObject> SysInstr = MakeShared<FJsonObject>();
	SysInstr->SetArrayField(TEXT("parts"), SysParts);

	TSharedRef<FJsonObject> FuncCfg = MakeShared<FJsonObject>();
	FuncCfg->SetStringField(TEXT("mode"), TEXT("AUTO"));
	TSharedRef<FJsonObject> ToolCfg = MakeShared<FJsonObject>();
	ToolCfg->SetObjectField(TEXT("function_calling_config"), FuncCfg);

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetObjectField(TEXT("system_instruction"), SysInstr);
	Payload->SetArrayField(TEXT("contents"), History);
	Payload->SetArrayField(TEXT("tools"), BuildToolDefinitions());
	Payload->SetObjectField(TEXT("tool_config"), ToolCfg);

	FString Body;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Payload, Writer);
	Writer->Close();

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetURL(EndpointURL);
	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ActiveRequest->SetContentAsString(Body);
	ConfigureRequestTimeout();

	TWeakPtr<FUECPGeminiAgentLoop> WeakSelf = AsShared();

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	ActiveRequest->SetResponseBodyReceiveStreamDelegateV2(
		FHttpRequestStreamDelegateV2::CreateLambda(
			[WeakSelf](void* Ptr, int64& Length)
			{
				TSharedPtr<FUECPGeminiAgentLoop> Self = WeakSelf.Pin();
				if (!Self || !Ptr || Length <= 0) return;
				Self->OnStreamChunk(Ptr, Length);
			}));
#endif

	ActiveRequest->OnRequestProgress64().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, uint64 Sent, uint64 Recv)
		{
			TSharedPtr<FUECPGeminiAgentLoop> Self = WeakSelf.Pin();
			if (Self) Self->OnRequestProgress(Req, Sent, Recv);
		});

	ActiveRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
		{
			TSharedPtr<FUECPGeminiAgentLoop> Self = WeakSelf.Pin();
			if (Self) Self->OnRequestComplete(Req, Resp, bOk);
		});

	ActiveRequest->ProcessRequest();
}

void FUECPGeminiAgentLoop::DrainSseBuffer(const FString& NewData)
{
	FString Combined = SseRemnant + NewData;
	SseRemnant.Empty();

	int32 Start = 0;
	while (Start < Combined.Len())
	{
		int32 NL = Combined.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (NL == INDEX_NONE)
		{
			SseRemnant = Combined.Mid(Start);
			break;
		}

		FString Line = Combined.Mid(Start, NL - Start).TrimStartAndEnd();
		Start = NL + 1;

		if (!Line.StartsWith(TEXT("data: "))) continue;

		FString Data = Line.Mid(6);
		if (Data == TEXT("[DONE]")) continue;

		ProcessSseData(Data);
	}
}

void FUECPGeminiAgentLoop::ProcessSseData(const FString& DataJson)
{
	if (bStopped) return;

	TSharedPtr<FJsonObject> Root = ParseJson(DataJson);
	if (!Root.IsValid()) return;

	const TSharedPtr<FJsonObject>* UsageMeta = nullptr;
	if (Root->TryGetObjectField(TEXT("usageMetadata"), UsageMeta) && UsageMeta)
	{
		int32 Prompt = 0, Candidates = 0;
		(*UsageMeta)->TryGetNumberField(TEXT("promptTokenCount"), Prompt);
		(*UsageMeta)->TryGetNumberField(TEXT("candidatesTokenCount"), Candidates);
		if (Prompt > 0)     FinalInputTokens  = Prompt;
		if (Candidates > 0) FinalOutputTokens = Candidates;
	}

	const TArray<TSharedPtr<FJsonValue>>* CandidatesArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("candidates"), CandidatesArr) ||
		!CandidatesArr || CandidatesArr->IsEmpty()) return;

	TSharedPtr<FJsonObject> Candidate = (*CandidatesArr)[0].IsValid()
		? (*CandidatesArr)[0]->AsObject() : nullptr;
	if (!Candidate.IsValid()) return;

	FString FR;
	if (Candidate->TryGetStringField(TEXT("finishReason"), FR) && !FR.IsEmpty() && FR != TEXT("null"))
		FinishReason = FR;

	const TSharedPtr<FJsonObject>* ContentPtr = nullptr;
	if (!Candidate->TryGetObjectField(TEXT("content"), ContentPtr) || !ContentPtr) return;

	const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
	if (!(*ContentPtr)->TryGetArrayField(TEXT("parts"), Parts) || !Parts) return;

	for (const TSharedPtr<FJsonValue>& PV : *Parts)
	{
		TSharedPtr<FJsonObject> Part = PV.IsValid() ? PV->AsObject() : nullptr;
		if (!Part.IsValid()) continue;

		FString Text;
		if (Part->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
		{
			TextAccum += Text;
			if (OnTextDelta) OnTextDelta(Text);
			continue;
		}

		PendingRawNonTextParts.Add(Part);

		const TSharedPtr<FJsonObject>* FuncCallPtr = nullptr;
		if (Part->TryGetObjectField(TEXT("functionCall"), FuncCallPtr) && FuncCallPtr)
		{
			FGeminiToolCall TC;
			TC.AutoId = FString::Printf(TEXT("g_%d"), ToolSeq++);
			(*FuncCallPtr)->TryGetStringField(TEXT("name"), TC.Name);

			const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
			if ((*FuncCallPtr)->TryGetObjectField(TEXT("args"), ArgsPtr) && ArgsPtr)
				TC.ArgsJson = ToJsonStr((*ArgsPtr).ToSharedRef());
			else
				TC.ArgsJson = TEXT("{}");

			PendingToolCalls.Add(MoveTemp(TC));
		}
	}
}

void FUECPGeminiAgentLoop::OnRequestComplete(FHttpRequestPtr, FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	check(IsInGameThread());
	if (bStopped) return;

	FString StreamedBody;
	{
		FString Remaining;
		{
			FScopeLock Lock(&SseMutex);
			Remaining     = MoveTemp(SseBuffer); SseBuffer.Empty();
			StreamedBody  = RawResponseBody;
		}
		if (Remaining.IsEmpty() && StreamedBody.IsEmpty() && Response.IsValid())
		{
			Remaining = RecoverResponseBody(Response, StreamedBody);
			Remaining.ReplaceInline(TEXT("\r"), TEXT(""));
			StreamedBody = Remaining;
		}
		if (!Remaining.IsEmpty()) DrainSseBuffer(Remaining);
	}

	if (OnRoundResponse && Response.IsValid()) OnRoundResponse(Response);

	const bool bRoundHadOutput = !TextAccum.IsEmpty() || PendingToolCalls.Num() > 0;

	if (!bWasSuccessful || !Response.IsValid())
	{
		if (TryScheduleTransientRetry(bWasSuccessful, 0, FString(), Response, TEXT("Gemini"), bRoundHadOutput))
			return;
		Finish(false, FormatHttpError(bWasSuccessful, 0, TEXT(""), TEXT("Gemini")));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		const FString Body = RecoverResponseBody(Response, StreamedBody);
		if (TryScheduleTransientRetry(bWasSuccessful, Code, Body, Response, TEXT("Gemini"), bRoundHadOutput))
			return;
		Finish(false, FormatHttpError(bWasSuccessful, Code, Body, TEXT("Gemini")));
		return;
	}

	TransientRetryCount = 0;

	if (PendingToolCalls.Num() > 0)
	{
		ToolCallsThisLoop += PendingToolCalls.Num();
		State = EState::Dispatching;
		DispatchToolsAndLoop();
	}
	else if (FinishReason == TEXT("MAX_TOKENS") && TextAccum.IsEmpty())
	{
		Finish(false, TEXT("Response cut off (output token limit reached). Send a follow-up to continue."));
	}
	else
	{
		const bool bLengthCutoff = (FinishReason == TEXT("MAX_TOKENS"));
		const bool bIntentStall  = false;
		if (bLengthCutoff || bIntentStall)
		{
			const FString Footer = bLengthCutoff
				? TEXT("\n\n---\n*Response cut off (token limit). Type \"continue\" to resume.*")
				: TEXT("\n\n---\n*Agent ended its turn without firing tools. Type \"continue\" if it should keep going.*");
			TextAccum += Footer;
			if (OnTextDelta) OnTextDelta(Footer);
			bStalledNeedsContinue = true;
		}
		AppendModelTurn();
		Finish(true);
	}
}

void FUECPGeminiAgentLoop::DispatchToolsAndLoop()
{
	check(IsInGameThread());

	for (const FGeminiToolCall& TC : PendingToolCalls)
	{
		TSharedPtr<FJsonObject> LA = ParseJson(TC.ArgsJson);
		FString LAction;
		if (LA.IsValid()) LA->TryGetStringField(TEXT("action"), LAction);
		const FString LLabel = LAction.IsEmpty() ? TC.Name
			: FString::Printf(TEXT("%s \xB7 %s"), *TC.Name, *LAction);
		if (OnToolStart) OnToolStart(TC.AutoId, LLabel, TC.ArgsJson.Left(200));
	}

	TArray<int32> Indices;
	for (int32 i = 0; i < PendingToolCalls.Num(); ++i) Indices.Add(i);

	DispatchOneTool(MoveTemp(Indices), 0, TArray<TTuple<FString, FString, bool>>{});
}

void FUECPGeminiAgentLoop::DispatchOneTool(TArray<int32> Indices, int32 NextIndex,
	TArray<TTuple<FString, FString, bool>> Results)
{
	if (bStopped.load()) return;

	while (NextIndex < Indices.Num())
	{
		const int32 Probe = Indices[NextIndex];
		if (Probe >= 0 && Probe < PendingToolCalls.Num()) break;
		++NextIndex;
	}

	if (NextIndex >= Indices.Num())
	{
		FinaliseDispatchAndContinue(Results);
		return;
	}

	IUECPToolDispatcher* Dispatcher = IUECPCoreModule::IsAvailable()
		? &IUECPCoreModule::Get().GetToolDispatcher() : nullptr;

	const int32 i = Indices[NextIndex];
	const FGeminiToolCall& TC = PendingToolCalls[i];
	const FString TCName  = TC.Name;
	const FString TCAutoId = TC.AutoId;
	const FString TCArgs  = TC.ArgsJson;

	if (!Dispatcher)
	{
		const FString ErrJson = TEXT("{\"error\":\"Tool dispatcher unavailable.\"}");
		if (OnToolResult) OnToolResult(TCAutoId, TCName, false, ErrJson);
		Results.Emplace(TCName, ErrJson, false);
		DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
		return;
	}

	TSharedPtr<FJsonObject> ArgsObj = ParseJson(TCArgs);
	if (!ArgsObj.IsValid()) ArgsObj = MakeShared<FJsonObject>();

	FString LAction2;
	if (ArgsObj.IsValid()) ArgsObj->TryGetStringField(TEXT("action"), LAction2);
	const FString Label = LAction2.IsEmpty() ? TCName
		: FString::Printf(TEXT("%s \xB7 %s"), *TCName, *LAction2);

	FName DispatchName = ResolveDispatchName(TCName, ArgsObj);

	{
		FString CrewDeny;
		if (!IUECPCoreModule::Get().GetCrewService().IsToolAllowedForChat(ChatID, DispatchName, CrewDeny))
		{
			const FString Msg = FString::Printf(TEXT("crew role denies tool: %s"), *CrewDeny);
			if (OnToolResult) OnToolResult(TCAutoId, Label, false, Msg);
			Results.Emplace(TCName, Msg, false);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
			return;
		}
	}

	UECPCallerContext::WriteCallerChatId(ArgsObj, ChatID);
	{
		FUECPToolResult TR;
		if (OnWidgetTool && OnWidgetTool(TCName, ArgsObj, TR))
		{
			FString ResultJson = !TR.ResultJson.IsEmpty() ? TR.ResultJson : TR.ErrorMessage;
			if (ResultJson.IsEmpty()) ResultJson = TEXT("Tool dispatch failed.");
			if (OnToolResult) OnToolResult(TCAutoId, Label, TR.bSuccess, ResultJson);
			Results.Emplace(TCName, ResultJson, TR.bSuccess);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
			return;
		}
	}

	FString DenyReason;
	if (OnBeforeDispatch && !OnBeforeDispatch(TCName, ArgsObj, DispatchName, DenyReason))
	{
		if (!DenyReason.IsEmpty())
		{
			if (OnToolResult) OnToolResult(TCAutoId, Label, false, DenyReason);
			Results.Emplace(TCName, DenyReason, false);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
			return;
		}

		PausedDispatchRemainingIndices.Reset();
		for (int32 jj = NextIndex; jj < Indices.Num(); ++jj)
			PausedDispatchRemainingIndices.Add(Indices[jj]);
		PausedDispatchPartialResults = MoveTemp(Results);
		bPendingSendRound = true;
		return;
	}

	Dispatcher->ExecuteFromArgsAsync(DispatchName, ArgsObj,
		[this, Indices = MoveTemp(Indices), NextIndex, Results = MoveTemp(Results),
		 TCName, TCAutoId, Label](FUECPToolResult TR) mutable
		{
			if (bStopped.load()) return;
			FString ResultJson = !TR.ResultJson.IsEmpty() ? TR.ResultJson : TR.ErrorMessage;
			if (ResultJson.IsEmpty()) ResultJson = TEXT("Tool dispatch failed.");
			if (OnToolResult) OnToolResult(TCAutoId, Label, TR.bSuccess, ResultJson);
			Results.Emplace(TCName, ResultJson, TR.bSuccess);
			DispatchOneTool(MoveTemp(Indices), NextIndex + 1, MoveTemp(Results));
		});
}

void FUECPGeminiAgentLoop::FinaliseDispatchAndContinue(TArray<TTuple<FString, FString, bool>>& Results)
{
	AppendModelTurn();
	AppendFunctionResponses(Results);

	if (OnBeforeNextRound && !OnBeforeNextRound())
	{
		bPendingSendRound = true;
		return;
	}
	State = EState::Streaming;
	SendRound();
}

void FUECPGeminiAgentLoop::SkipPausedDispatch()
{
	if (PausedDispatchRemainingIndices.Num() == 0) return;

	TArray<TTuple<FString, FString, bool>> Results = MoveTemp(PausedDispatchPartialResults);
	const FString SkippedJson = TEXT("{\"skipped\":true,\"message\":\"User skipped this tool — continue without it.\"}");
	for (int32 i : PausedDispatchRemainingIndices)
	{
		if (i < 0 || i >= PendingToolCalls.Num()) continue;
		const FGeminiToolCall& TC = PendingToolCalls[i];
		TSharedPtr<FJsonObject> LA = ParseJson(TC.ArgsJson);
		FString LAction;
		if (LA.IsValid()) LA->TryGetStringField(TEXT("action"), LAction);
		const FString Label = LAction.IsEmpty() ? TC.Name
			: FString::Printf(TEXT("%s \xB7 %s"), *TC.Name, *LAction);
		if (OnToolResult) OnToolResult(TC.AutoId, Label, false, SkippedJson);
		Results.Emplace(TC.Name, SkippedJson, false);
	}
	PausedDispatchRemainingIndices.Reset();
	PausedDispatchPartialResults.Reset();
	bPendingSendRound = false;

	FinaliseDispatchAndContinue(Results);
}

void FUECPGeminiAgentLoop::TriggerPendingSendRound()
{
	if (!bPendingSendRound || bStopped.load()) return;
	bPendingSendRound = false;

	if (PausedDispatchRemainingIndices.Num() > 0)
	{
		TArray<int32> Resume = MoveTemp(PausedDispatchRemainingIndices);
		TArray<TTuple<FString, FString, bool>> Results = MoveTemp(PausedDispatchPartialResults);
		PausedDispatchRemainingIndices.Reset();
		PausedDispatchPartialResults.Reset();
		DispatchOneTool(MoveTemp(Resume), 0, MoveTemp(Results));
		return;
	}

	State = EState::Streaming;
	SendRound();
}

void FUECPGeminiAgentLoop::AppendModelTurn()
{
	TArray<TSharedPtr<FJsonValue>> Parts;

	if (!TextAccum.IsEmpty())
	{
		TSharedRef<FJsonObject> TextPart = MakeShared<FJsonObject>();
		TextPart->SetStringField(TEXT("text"), TextAccum);
		Parts.Add(MakeShared<FJsonValueObject>(TextPart));
	}

	for (const TSharedPtr<FJsonObject>& RawPart : PendingRawNonTextParts)
	{
		if (RawPart.IsValid())
			Parts.Add(MakeShared<FJsonValueObject>(RawPart.ToSharedRef()));
	}

	if (Parts.IsEmpty()) return;

	TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), TEXT("model"));
	Msg->SetArrayField(TEXT("parts"), Parts);
	History.Add(MakeShared<FJsonValueObject>(Msg));
}

void FUECPGeminiAgentLoop::AppendFunctionResponses(
	const TArray<TTuple<FString, FString, bool>>& Results)
{
	TArray<TSharedPtr<FJsonValue>> Parts;
	for (const TTuple<FString, FString, bool>& R : Results)
	{
		TSharedRef<FJsonObject> ResponseContent = MakeShared<FJsonObject>();
		ResponseContent->SetStringField(TEXT("content"), CapToolResultText(R.Get<1>()));

		TSharedRef<FJsonObject> FuncResp = MakeShared<FJsonObject>();
		FuncResp->SetStringField(TEXT("name"), R.Get<0>());
		FuncResp->SetObjectField(TEXT("response"), ResponseContent);

		TSharedRef<FJsonObject> Part = MakeShared<FJsonObject>();
		Part->SetObjectField(TEXT("functionResponse"), FuncResp);
		Parts.Add(MakeShared<FJsonValueObject>(Part));
	}

	if (Parts.IsEmpty()) return;

	TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), TEXT("user"));
	Msg->SetArrayField(TEXT("parts"), Parts);
	History.Add(MakeShared<FJsonValueObject>(Msg));
}
