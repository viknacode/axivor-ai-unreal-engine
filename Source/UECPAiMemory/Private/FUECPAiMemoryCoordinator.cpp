// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPAiMemoryCoordinator.h"
#include "AiMemoryManager.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "ApiKeyManager.h"
#include "Managers/UECPChatProvider.h"
#include "Managers/SettingsManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

namespace
{
	EAiMemoryCategory CategoryFromDisplayName(const FString& Display)
	{
		if (Display == TEXT("Project Info"))    return EAiMemoryCategory::ProjectInfo;
		if (Display == TEXT("Recent Work"))     return EAiMemoryCategory::RecentWork;
		if (Display == TEXT("Preferences"))     return EAiMemoryCategory::Preferences;
		if (Display == TEXT("Patterns"))        return EAiMemoryCategory::Patterns;
		if (Display == TEXT("Asset Relations")) return EAiMemoryCategory::AssetRelations;
		if (Display == TEXT("Decisions"))       return EAiMemoryCategory::Decisions;
		return EAiMemoryCategory::ProjectInfo;
	}

	EAiMemoryCategory CategoryFromApiName(const FString& Api)
	{
		if (Api == TEXT("project_info"))    return EAiMemoryCategory::ProjectInfo;
		if (Api == TEXT("recent_work"))     return EAiMemoryCategory::RecentWork;
		if (Api == TEXT("preferences"))     return EAiMemoryCategory::Preferences;
		if (Api == TEXT("patterns"))        return EAiMemoryCategory::Patterns;
		if (Api == TEXT("asset_relations")) return EAiMemoryCategory::AssetRelations;
		if (Api == TEXT("decisions"))       return EAiMemoryCategory::Decisions;
		return EAiMemoryCategory::RecentWork;
	}

	FString EscapeForJsString(const FString& In)
	{
		return FString(In)
			.Replace(TEXT("\\"), TEXT("\\\\"))
			.Replace(TEXT("\""), TEXT("\\\""))
			.Replace(TEXT("\r\n"), TEXT("\\n"))
			.Replace(TEXT("\n"), TEXT("\\n"))
			.Replace(TEXT("\r"), TEXT("\\n"));
	}
}

FUECPAiMemoryCoordinator::FUECPAiMemoryCoordinator()
{
	LoadThresholdFromConfig();
}

void FUECPAiMemoryCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;
}

void FUECPAiMemoryCoordinator::NotifyPendingMemoriesOnStartup()
{
	const int32 PendingCount = FAiMemoryManager::Get().GetPendingMemories().Num();

	UUECPAppBridge* B = Bridge.Get();
	if (!B) return;

	B->PushPendingMemoryCount(PendingCount);

	if (PendingCount > 0)
	{
		B->PushToast(FString::Printf(
			TEXT("%d pending memory %s from a previous session — open AI Memory to review."),
			PendingCount, PendingCount == 1 ? TEXT("suggestion") : TEXT("suggestions")),
			TEXT("info"));
	}
}

void FUECPAiMemoryCoordinator::LoadThresholdFromConfig()
{
	int32 Value = 7;
	if (GConfig && GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MemoryExtractionThreshold"),
		Value, FSettingsManager::GetGlobalConfigPath()))
	{
		if (Value > 0) ExtractionThreshold = Value;
	}
}

TArray<TSharedPtr<FAiMemoryEntry>>& FUECPAiMemoryCoordinator::GetAiMemories()
{
	return FAiMemoryManager::Get().GetAiMemories();
}

TArray<TSharedPtr<FAiMemoryEntry>>& FUECPAiMemoryCoordinator::GetPendingMemories()
{
	return FAiMemoryManager::Get().GetPendingMemories();
}

void FUECPAiMemoryCoordinator::SaveManifest()
{
	FAiMemoryManager::Get().SaveManifest();
}

FString FUECPAiMemoryCoordinator::GetContentForAI() const
{
	return FAiMemoryManager::Get().GetContentForAI();
}

int32 FUECPAiMemoryCoordinator::GetTokenCount() const
{
	return FAiMemoryManager::Get().GetTokenCount();
}

FString FUECPAiMemoryCoordinator::GetCategoryDisplayName(EAiMemoryCategory Category) const
{
	return FAiMemoryManager::GetCategoryDisplayName(Category);
}

void FUECPAiMemoryCoordinator::AddMemory()
{
	TSharedPtr<FAiMemoryEntry> NewMemory = MakeShareable(new FAiMemoryEntry);
	NewMemory->Category = EAiMemoryCategory::ProjectInfo;
	NewMemory->Content  = TEXT("New memory - edit this");
	NewMemory->Source   = TEXT("user_added");
	NewMemory->bEnabled = true;

	FAiMemoryManager::Get().GetAiMemories().Add(NewMemory);
	FAiMemoryManager::Get().SaveManifest();

	RefreshOverlay();
}

void FUECPAiMemoryCoordinator::UpdateMemory(const FString& MemoryIdStr, const FString& Content,
	const FString& CategoryDisplayName)
{
	FGuid Guid;
	if (!FGuid::Parse(MemoryIdStr, Guid)) return;

	for (TSharedPtr<FAiMemoryEntry>& Mem : FAiMemoryManager::Get().GetAiMemories())
	{
		if (!Mem.IsValid() || Mem->MemoryId != Guid) continue;
		Mem->Content      = Content;
		Mem->LastAccessed = FDateTime::Now();
		Mem->Category     = CategoryFromDisplayName(CategoryDisplayName);

		FAiMemoryManager::Get().SaveManifest();

		if (UUECPAppBridge* B = Bridge.Get())
			B->PushToast(TEXT("Memory updated"), TEXT("info"));

		RefreshOverlay();
		return;
	}
}

void FUECPAiMemoryCoordinator::DeleteMemory(const FString& MemoryIdStr)
{
	FGuid Guid;
	if (!FGuid::Parse(MemoryIdStr, Guid)) return;

	TArray<TSharedPtr<FAiMemoryEntry>>& Memories = FAiMemoryManager::Get().GetAiMemories();
	FString DeletedCategory;

	for (int32 i = 0; i < Memories.Num(); i++)
	{
		if (Memories[i].IsValid() && Memories[i]->MemoryId == Guid)
		{
			DeletedCategory = FAiMemoryManager::GetCategoryDisplayName(Memories[i]->Category);
			Memories.RemoveAt(i);
			break;
		}
	}

	FAiMemoryManager::Get().SaveManifest();

	if (!DeletedCategory.IsEmpty())
	{
		if (UUECPAppBridge* B = Bridge.Get())
			B->PushToast(FString::Printf(TEXT("Memory deleted: %s"), *DeletedCategory), TEXT("info"));
	}

	RefreshOverlay();
}

void FUECPAiMemoryCoordinator::ApproveMemory(const FString& MemoryIdStr)
{
	FGuid Guid;
	if (!FGuid::Parse(MemoryIdStr, Guid)) return;

	TArray<TSharedPtr<FAiMemoryEntry>>& Pending = FAiMemoryManager::Get().GetPendingMemories();
	for (int32 i = 0; i < Pending.Num(); i++)
	{
		if (!Pending[i].IsValid() || Pending[i]->MemoryId != Guid) continue;
		TSharedPtr<FAiMemoryEntry> Approved = Pending[i];
		Approved->Source   = TEXT("ai_suggested");
		Approved->bEnabled = true;
		FAiMemoryManager::Get().GetAiMemories().Add(Approved);
		Pending.RemoveAt(i);

		FAiMemoryManager::Get().SaveManifest();

		if (UUECPAppBridge* B = Bridge.Get())
		{
			B->PushToast(FString::Printf(TEXT("Memory approved: %s"),
				*FAiMemoryManager::GetCategoryDisplayName(Approved->Category)), TEXT("success"));
		}
		break;
	}

	RefreshOverlay();
}

void FUECPAiMemoryCoordinator::RejectMemory(const FString& MemoryIdStr)
{
	FGuid Guid;
	if (!FGuid::Parse(MemoryIdStr, Guid)) return;

	TArray<TSharedPtr<FAiMemoryEntry>>& Pending = FAiMemoryManager::Get().GetPendingMemories();
	Pending.RemoveAll([&Guid](const TSharedPtr<FAiMemoryEntry>& E)
	{
		return E.IsValid() && E->MemoryId == Guid;
	});

	FAiMemoryManager::Get().SaveManifest();

	RefreshOverlay();
}

void FUECPAiMemoryCoordinator::ApproveAllPendingMemories()
{
	TArray<TSharedPtr<FAiMemoryEntry>>& Pending = FAiMemoryManager::Get().GetPendingMemories();
	if (Pending.Num() == 0) return;

	const int32 Count = Pending.Num();
	for (TSharedPtr<FAiMemoryEntry>& Entry : Pending)
	{
		if (!Entry.IsValid()) continue;
		Entry->Source = TEXT("ai_suggested");
		Entry->bEnabled = true;
		FAiMemoryManager::Get().GetAiMemories().Add(Entry);
	}
	Pending.Reset();

	FAiMemoryManager::Get().SaveManifest();

	if (UUECPAppBridge* B = Bridge.Get())
	{
		B->PushToast(FString::Printf(TEXT("Approved %d pending %s."),
			Count, Count == 1 ? TEXT("memory") : TEXT("memories")), TEXT("success"));
	}
	RefreshOverlay();
}

void FUECPAiMemoryCoordinator::RejectAllPendingMemories()
{
	TArray<TSharedPtr<FAiMemoryEntry>>& Pending = FAiMemoryManager::Get().GetPendingMemories();
	if (Pending.Num() == 0) return;

	const int32 Count = Pending.Num();
	Pending.Reset();

	FAiMemoryManager::Get().SaveManifest();

	if (UUECPAppBridge* B = Bridge.Get())
	{
		B->PushToast(FString::Printf(TEXT("Cleared %d pending %s."),
			Count, Count == 1 ? TEXT("memory") : TEXT("memories")), TEXT("info"));
	}
	RefreshOverlay();
}

void FUECPAiMemoryCoordinator::ToggleMemory(const FString& MemoryIdStr, bool )
{
	FGuid Guid;
	if (!FGuid::Parse(MemoryIdStr, Guid)) return;

	for (TSharedPtr<FAiMemoryEntry>& Mem : FAiMemoryManager::Get().GetAiMemories())
	{
		if (Mem.IsValid() && Mem->MemoryId == Guid)
		{
			Mem->bEnabled = !Mem->bEnabled;
			FAiMemoryManager::Get().SaveManifest();
			break;
		}
	}

	RefreshOverlay();
}

void FUECPAiMemoryCoordinator::LoadMemoryContent(const FString& MemoryIdStr)
{
	FGuid Guid;
	if (!FGuid::Parse(MemoryIdStr, Guid)) return;

	for (const TSharedPtr<FAiMemoryEntry>& Mem : FAiMemoryManager::Get().GetAiMemories())
	{
		if (!Mem.IsValid() || Mem->MemoryId != Guid) continue;
		const FString Cat     = FAiMemoryManager::GetCategoryDisplayName(Mem->Category);
		const FString Content = EscapeForJsString(Mem->Content);
		const FString CatEsc  = EscapeForJsString(Cat);

		if (UUECPAppBridge* B = Bridge.Get())
		{
			B->ExecJs(FString::Printf(
				TEXT("if(typeof onMemoryContent==='function')onMemoryContent(\"%s\",\"%s\")"),
				*Content, *CatEsc));
		}
		return;
	}
}

void FUECPAiMemoryCoordinator::SetExtractionThreshold(int32 Threshold)
{
	if (Threshold <= 0) return;
	ExtractionThreshold = Threshold;
	if (GConfig)
	{
		GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("MemoryExtractionThreshold"),
			Threshold, FSettingsManager::GetGlobalConfigPath());
		GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
	}
}

void FUECPAiMemoryCoordinator::ExtractMemoriesFromConversation()
{
	auto W = Shell.Pin();
	if (!W.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>& History = W->GetArchitectConversationHistoryForExtraction();

	TArray<TSharedPtr<FJsonValue>> RecentMessages;
	const int32 StartIndex = FMath::Max(0, History.Num() - 10);
	for (int32 i = StartIndex; i < History.Num(); i++) RecentMessages.Add(History[i]);

	int32 UserMessageCount = 0;
	for (const TSharedPtr<FJsonValue>& MV : RecentMessages)
	{
		TSharedPtr<FJsonObject> MO = MV.IsValid() ? MV->AsObject() : nullptr;
		if (!MO.IsValid()) continue;
		FString Role; MO->TryGetStringField(TEXT("role"), Role);
		if (Role == TEXT("user")) UserMessageCount++;
	}

	UE_LOG(LogTemp, Verbose, TEXT("AI Memory: ExtractMemoriesFromConversation — UserMessages=%d, Threshold=%d"),
		UserMessageCount, ExtractionThreshold);
	if (UserMessageCount < ExtractionThreshold)
	{
		UE_LOG(LogTemp, Verbose, TEXT("AI Memory: Skipped — not enough user messages yet (%d < %d)"),
			UserMessageCount, ExtractionThreshold);
		return;
	}

	FString ConversationText;
	for (const TSharedPtr<FJsonValue>& MV : RecentMessages)
	{
		TSharedPtr<FJsonObject> MO = MV.IsValid() ? MV->AsObject() : nullptr;
		if (!MO.IsValid()) continue;

		FString Role; MO->TryGetStringField(TEXT("role"), Role);
		if (Role == TEXT("context")) continue;

		FString Content;
		MO->TryGetStringField(TEXT("content"), Content);
		if (Content.IsEmpty())
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
			if (MO->TryGetArrayField(TEXT("parts"), Parts) && Parts && Parts->Num() > 0)
			{
				TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
				if (Part.IsValid()) Part->TryGetStringField(TEXT("text"), Content);
			}
		}

		if (Role == TEXT("user"))
		{
			ConversationText += TEXT("USER: ") + Content + TEXT("\n\n");
		}
		else if (Role == TEXT("assistant") || Role == TEXT("model"))
		{
			if (Content.Len() > 500) Content = Content.Left(500) + TEXT("...");
			ConversationText += TEXT("AI: ") + Content + TEXT("\n\n");
		}
	}

	if (ConversationText.IsEmpty()) return;

	const FString ExtractionPrompt = TEXT(
		"You are a memory extraction assistant. Analyze the conversation and identify information worth remembering for future interactions.\n\n"
		"Extract memories about:\n"
		"1. Project Info: Game name, genre, platform, art style\n"
		"2. Recent Work: What the user created, modified, or worked on\n"
		"3. Preferences: User's coding style, naming conventions, preferred approaches\n"
		"4. Patterns: Recurring patterns in user's requests or code\n"
		"5. Asset Relations: Connections between assets (blueprint A uses widget B)\n"
		"6. Decisions: Important technical or design decisions made\n\n"
		"Output ONLY a valid JSON array. If nothing worth remembering, output [].\n\n"
		"Format:\n"
		"[{\"category\": \"recent_work\", \"content\": \"Description of what was done\"}]\n\n"
		"Category must be one of: project_info, recent_work, preferences, patterns, asset_relations, decisions\n\n"
		"Conversation:\n"
	) + ConversationText;

	const FApiKeySlot Slot = FApiKeyManager::Get().GetActiveSlot();

	// Provider fan-out (endpoint, headers, body shape) is centralized in UECPChatProvider;
	// this returns false — and we skip, exactly as before — for an empty key or an
	// unsupported provider.
	UECPChatProvider::FCompletionRequest ProviderReq;
	FString BuildError;
	if (!UECPChatProvider::BuildCompletionRequest(Slot, ExtractionPrompt, 1024, ProviderReq, BuildError))
	{
		UE_LOG(LogTemp, Verbose, TEXT("AI Memory: Skipped — %s"), *BuildError);
		return;
	}

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetTimeout(60.0f);
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(ProviderReq.Endpoint);
	for (const TPair<FString, FString>& Header : ProviderReq.Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}

	FString RequestString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestString);
	FJsonSerializer::Serialize(ProviderReq.Body.ToSharedRef(), Writer);
	Request->SetContentAsString(RequestString);

	TWeakObjectPtr<UUECPAppBridge> BridgeWeak = Bridge;
	Request->OnProcessRequestComplete().BindLambda(
		[BridgeWeak](FHttpRequestPtr , FHttpResponsePtr Resp, bool bSuccess)
	{
		if (!bSuccess || !Resp.IsValid())
		{
			UE_LOG(LogTemp, Verbose, TEXT("AI Memory: Extraction HTTP request failed"));
			return;
		}

		const FString ResponseStr = Resp->GetContentAsString();
		FString JsonContent;

		TSharedPtr<FJsonObject> RespObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
		if (FJsonSerializer::Deserialize(Reader, RespObj) && RespObj.IsValid())
		{
			// Provider-agnostic extraction (Gemini/Claude/OpenAI shapes) lives in the helper.
			UECPChatProvider::ExtractCompletionText(RespObj, JsonContent);
		}

		if (JsonContent.IsEmpty())
		{
			UE_LOG(LogTemp, Verbose, TEXT("AI Memory: Could not extract text from response"));
			return;
		}

		int32 JsonStart = JsonContent.Find(TEXT("["));
		int32 JsonEnd = INDEX_NONE;
		JsonContent.FindLastChar(']', JsonEnd);
		if (JsonStart != INDEX_NONE && JsonEnd != INDEX_NONE && JsonEnd > JsonStart)
			JsonContent = JsonContent.Mid(JsonStart, JsonEnd - JsonStart + 1);

		TArray<TSharedPtr<FJsonValue>> MemoriesArray;
		TSharedRef<TJsonReader<>> ArrayReader = TJsonReaderFactory<>::Create(JsonContent);
		if (!FJsonSerializer::Deserialize(ArrayReader, MemoriesArray))
		{
			UE_LOG(LogTemp, Verbose, TEXT("AI Memory: Failed to parse JSON array"));
			return;
		}

		const bool bAutoApprove = FSettingsManager::Get().LoadAutoApproveMemories();

		int32 NewCount = 0;
		for (const TSharedPtr<FJsonValue>& MV : MemoriesArray)
		{
			TSharedPtr<FJsonObject> MO = MV.IsValid() ? MV->AsObject() : nullptr;
			if (!MO.IsValid()) continue;

			FString CategoryStr, ContentStr;
			MO->TryGetStringField(TEXT("category"), CategoryStr);
			MO->TryGetStringField(TEXT("content"),  ContentStr);
			if (ContentStr.IsEmpty()) continue;

			const EAiMemoryCategory Category = CategoryFromApiName(CategoryStr);

			bool bAlreadyExists = false;
			for (const TSharedPtr<FAiMemoryEntry>& E : FAiMemoryManager::Get().GetAiMemories())
				if (E.IsValid() && E->Content == ContentStr) { bAlreadyExists = true; break; }
			if (!bAlreadyExists)
			{
				for (const TSharedPtr<FAiMemoryEntry>& E : FAiMemoryManager::Get().GetPendingMemories())
					if (E.IsValid() && E->Content == ContentStr) { bAlreadyExists = true; break; }
			}
			if (bAlreadyExists) continue;

			TSharedPtr<FAiMemoryEntry> NewMem = MakeShareable(new FAiMemoryEntry);
			NewMem->Category = Category;
			NewMem->Content  = ContentStr;
			NewMem->Source   = TEXT("ai_suggested");
			if (bAutoApprove)
				FAiMemoryManager::Get().GetAiMemories().Add(NewMem);
			else
				FAiMemoryManager::Get().GetPendingMemories().Add(NewMem);
			NewCount++;
		}

		if (NewCount > 0)
		{
			if (bAutoApprove)
				FAiMemoryManager::Get().SaveManifest();

			AsyncTask(ENamedThreads::GameThread, [BridgeWeak, NewCount, bAutoApprove]()
			{
				if (UUECPAppBridge* B = BridgeWeak.Get())
				{
					B->PushToast(
						bAutoApprove
							? FString::Printf(TEXT("%d new memory entry(ies) auto-approved"), NewCount)
							: FString::Printf(TEXT("%d new memory suggestion(s) — review in AI Memory"), NewCount),
						TEXT("info"));
				}
			});
		}
	});

	Request->ProcessRequest();
}

void FUECPAiMemoryCoordinator::RefreshOverlay()
{
	if (UUECPAppBridge* B = Bridge.Get())
	{
		B->PushOverlayHtml(BuildOverlayHtml());
		B->PushPendingMemoryCount(FAiMemoryManager::Get().GetPendingMemories().Num());
	}
}

FString FUECPAiMemoryCoordinator::BuildOverlayHtml() const
{
	const TArray<TSharedPtr<FAiMemoryEntry>>& Memories = FAiMemoryManager::Get().GetAiMemories();
	const TArray<TSharedPtr<FAiMemoryEntry>>& Pending  = FAiMemoryManager::Get().GetPendingMemories();

	int32 EnabledMems = 0, TotalTokens = 0;
	for (const TSharedPtr<FAiMemoryEntry>& M : Memories)
		if (M.IsValid() && M->bEnabled) { EnabledMems++; TotalTokens += M->Content.Len() / 4; }

	auto EscHtml = [](const FString& In)
	{
		return FString(In)
			.Replace(TEXT("&"), TEXT("&amp;"))
			.Replace(TEXT("<"), TEXT("&lt;"))
			.Replace(TEXT(">"), TEXT("&gt;"));
	};
	auto CatToColor = [](EAiMemoryCategory Cat)
	{
		const FLinearColor C = FAiMemoryManager::GetCategoryColor(Cat);
		return FString::Printf(TEXT("rgb(%d,%d,%d)"),
			FMath::RoundToInt(C.R * 255.f),
			FMath::RoundToInt(C.G * 255.f),
			FMath::RoundToInt(C.B * 255.f));
	};

	FString Html = TEXT("<div class='kb-root'>");

	Html += FString::Printf(TEXT(
		"<div class='kb-tabs'>"
		  "<button id='mem-tab-memories' class='kb-tab active' onclick=\"memShowTab('memories')\">Memories<span class='kb-tab-chip'>%d</span></button>"
		  "<button id='mem-tab-pending' class='kb-tab' onclick=\"memShowTab('pending')\">Pending<span class='kb-tab-chip%s'>%d</span></button>"
		"</div>"),
		Memories.Num(),
		Pending.Num() > 0 ? TEXT(" warn") : TEXT(""),
		Pending.Num());

	Html += TEXT("<div id='mem-panel-memories' class='kb-panel'>");
	Html += TEXT(
		"<div class='kb-toolbar'>"
		  "<div class='kb-search-wrap'>"
			"<svg class='kb-search-icon' width='14' height='14' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
			  "<circle cx='11' cy='11' r='8'/><line x1='21' y1='21' x2='16.65' y2='16.65'/>"
			"</svg>"
			"<input type='text' class='kb-search' placeholder='Filter memories...' oninput=\"memFilter(this.value)\">"
		  "</div>"
		  "<button id='mem-sort-toggle' class='attach-btn' onclick=\"memToggleSort(this)\" title='Sort by date added' data-sort='newest'>&#x2193; Newest</button>"
		  "<button class='attach-btn' onclick=\"app('savememory','{}')\">+ Add</button>"
		  "<button class='attach-btn' onclick=\"app('extractmemories')\">&#129504; Extract</button>"
		"</div>");

	if (Memories.Num() == 0)
	{
		Html += TEXT(
			"<div class='kb-empty'>No memories yet."
			"<span class='kb-empty-sub'>Suggested memories from your chats will land in the Pending tab for you to review.</span>"
			"</div>");
	}
	else
	{
		TMap<EAiMemoryCategory, TArray<TSharedPtr<FAiMemoryEntry>>> ByCategory;
		for (const TSharedPtr<FAiMemoryEntry>& M : Memories)
			if (M.IsValid()) ByCategory.FindOrAdd(M->Category).Add(M);

		const TArray<EAiMemoryCategory> Order = {
			EAiMemoryCategory::ProjectInfo,
			EAiMemoryCategory::RecentWork,
			EAiMemoryCategory::Preferences,
			EAiMemoryCategory::Patterns,
			EAiMemoryCategory::AssetRelations,
			EAiMemoryCategory::Decisions
		};

		for (EAiMemoryCategory Cat : Order)
		{
			TArray<TSharedPtr<FAiMemoryEntry>>* List = ByCategory.Find(Cat);
			if (!List || List->Num() == 0) continue;

			List->Sort([](const TSharedPtr<FAiMemoryEntry>& A, const TSharedPtr<FAiMemoryEntry>& B) {
				return A->CreatedAt > B->CreatedAt;
			});

			int32 EnabledInCat = 0;
			for (const TSharedPtr<FAiMemoryEntry>& M : *List) if (M->bEnabled) ++EnabledInCat;

			const FString CatName  = FAiMemoryManager::GetCategoryDisplayName(Cat);
			const FString CatColor = CatToColor(Cat);

			Html += FString::Printf(TEXT(
				"<div class='kb-group'>"
				  "<div class='kb-group-head' onclick=\"memCatToggle(this)\">"
					"<span class='kb-group-arrow'>&#x25BC;</span>"
					"<span class='kb-cat-dot' style='background:%s'></span>"
					"<span class='kb-group-name'>%s</span>"
					"<span class='kb-group-count'>%d / %d enabled</span>"
				  "</div>"
				  "<div class='kb-group-body'>"),
				*CatColor, *CatName, EnabledInCat, List->Num());

			for (const TSharedPtr<FAiMemoryEntry>& Mem : *List)
			{
				const FString MemId      = Mem->MemoryId.ToString();
				const FString EscContent = EscHtml(Mem->Content);
				const FString CatDisp    = FAiMemoryManager::GetCategoryDisplayName(Mem->Category);

				const TCHAR* Sel = Mem->Category == Cat ? TEXT(" selected") : TEXT("");
				(void)Sel;

				const int64 CreatedTicks = Mem->CreatedAt.GetTicks();

				Html += FString::Printf(TEXT(
					"<div class='kb-card%s' data-mem-id='%s' data-mem-cat='%s' data-mem-created='%lld'>"
					  "<div class='kb-card-row'>"
						"<input type='checkbox' class='kb-chk' %s onchange=\"event.stopPropagation();app('togglememory','%s',this.checked?'true':'false')\">"
						"<div class='kb-card-content' onclick=\"memCardEdit(this.parentNode.parentNode)\">%s</div>"
						"<button class='kb-card-del' onclick=\"event.stopPropagation();memDeleteCard('%s')\" title='Delete'>&times;</button>"
					  "</div>"
					  "<div class='kb-card-edit'>"
						"<textarea class='kb-card-textarea' rows='4'>%s</textarea>"
						"<div class='kb-card-edit-row'>"
						  "<select class='kb-card-cat'>"
							"<option value='Project Info'%s>Project Info</option>"
							"<option value='Recent Work'%s>Recent Work</option>"
							"<option value='Preferences'%s>Preferences</option>"
							"<option value='Patterns'%s>Patterns</option>"
							"<option value='Asset Relations'%s>Asset Relations</option>"
							"<option value='Decisions'%s>Decisions</option>"
						  "</select>"
						  "<button class='attach-btn' onclick=\"memCardCancel(this)\">Cancel</button>"
						  "<button class='send-btn' onclick=\"memCardSave(this,'%s')\">Save</button>"
						"</div>"
					  "</div>"
					"</div>"),
					Mem->bEnabled ? TEXT("") : TEXT(" disabled"),
					*MemId, *CatDisp, CreatedTicks,
					Mem->bEnabled ? TEXT("checked") : TEXT(""), *MemId,
					*EscContent,
					*MemId,
					*EscContent,
					Mem->Category == EAiMemoryCategory::ProjectInfo    ? TEXT(" selected") : TEXT(""),
					Mem->Category == EAiMemoryCategory::RecentWork     ? TEXT(" selected") : TEXT(""),
					Mem->Category == EAiMemoryCategory::Preferences    ? TEXT(" selected") : TEXT(""),
					Mem->Category == EAiMemoryCategory::Patterns       ? TEXT(" selected") : TEXT(""),
					Mem->Category == EAiMemoryCategory::AssetRelations ? TEXT(" selected") : TEXT(""),
					Mem->Category == EAiMemoryCategory::Decisions      ? TEXT(" selected") : TEXT(""),
					*MemId);
			}

			Html += TEXT("</div></div>");
		}

		Html += FString::Printf(TEXT(
			"<div class='kb-footer'>"
			  "<span class='kb-footer-stat'>%d enabled &middot; ~%d tokens</span>"
			  "<span class='kb-footer-spacer'></span>"
			  "<label class='kb-footer-label'>Auto-extract every"
			  " <select onchange=\"app('setextractionthreshold',this.value)\" class='kb-threshold'>"
				"<option value='3'%s>3</option>"
				"<option value='5'%s>5</option>"
				"<option value='10'%s>10</option>"
				"<option value='20'%s>20</option>"
			  "</select> prompts</label>"
			"</div>"),
			EnabledMems, TotalTokens,
			ExtractionThreshold == 3  ? TEXT(" selected") : TEXT(""),
			ExtractionThreshold == 5  ? TEXT(" selected") : TEXT(""),
			ExtractionThreshold == 10 ? TEXT(" selected") : TEXT(""),
			ExtractionThreshold == 20 ? TEXT(" selected") : TEXT(""));
	}
	Html += TEXT("</div>");

	Html += TEXT("<div id='mem-panel-pending' class='kb-panel' style='display:none'>");

	{
		const bool bAutoApprove = FSettingsManager::Get().LoadAutoApproveMemories();
		Html += FString::Printf(TEXT(
			"<div class='kb-toolbar' style='gap:8px'>"
			  "<label style='display:flex;align-items:center;gap:6px;font-size:12.5px;color:var(--text2);cursor:pointer'>"
				"<input type='checkbox' %s onchange=\"app('setautoapprovememories',this.checked?'true':'false')\">"
				"Auto-approve new memories"
			  "</label>"
			  "<span style='font-size:11.5px;color:var(--text3)'>Skips this review step for future extractions.</span>"
			"</div>"),
			bAutoApprove ? TEXT("checked") : TEXT(""));
	}

	if (Pending.Num() == 0)
	{
		Html += TEXT(
			"<div class='kb-empty'>No pending memories."
			"<span class='kb-empty-sub'>The AI suggests memories from your chats. They land here for you to review before joining the active context.</span>"
			"</div>");
	}
	else
	{
		Html += FString::Printf(TEXT(
			"<div class='kb-bulk-bar'>"
			  "<span>%d %s pending review</span>"
			  "<button class='send-btn' onclick=\"memApproveAll(%d)\">Approve all</button>"
			  "<button class='attach-btn kb-danger' onclick=\"memClearAll(%d)\">Clear all</button>"
			"</div>"),
			Pending.Num(), Pending.Num() == 1 ? TEXT("memory") : TEXT("memories"),
			Pending.Num(), Pending.Num());

		for (const TSharedPtr<FAiMemoryEntry>& Mem : Pending)
		{
			if (!Mem.IsValid()) continue;
			const FString MemId      = Mem->MemoryId.ToString();
			const FString EscContent = EscHtml(Mem->Content);
			const FString CatColor   = CatToColor(Mem->Category);
			const FString CatName    = FAiMemoryManager::GetCategoryDisplayName(Mem->Category);

			Html += FString::Printf(TEXT(
				"<div class='kb-pending-card'>"
				  "<div class='kb-pending-head'>"
					"<span class='kb-cat-dot' style='background:%s'></span>"
					"<span class='kb-pending-cat'>%s</span>"
				  "</div>"
				  "<div class='kb-pending-content'>%s</div>"
				  "<div class='kb-pending-actions'>"
					"<button class='send-btn' onclick=\"app('approvememory','%s')\">Approve</button>"
					"<button class='attach-btn' onclick=\"app('rejectmemory','%s')\">Reject</button>"
				  "</div>"
				"</div>"),
				*CatColor, *CatName, *EscContent, *MemId, *MemId);
		}
	}
	Html += TEXT("</div>");

	Html += TEXT("</div>");
	return Html;
}
