// Copyright 2026, BlueprintsLab, All rights reserved.

#include "UECPSearchExtModule.h"
#include "Tools/SearchTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPSearchExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("search"),
			TEXT("web_search"),
			TEXT("answer"),
		};
		return Names;
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPSearchExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("search"),     MakeHandler(SearchTools::HandleSearchFromArgs), EUECPToolThreadAffinity::AnyThread);
	D.RegisterHandler(TEXT("web_search"), MakeHandler(SearchTools::HandleSearchFromArgs), EUECPToolThreadAffinity::AnyThread);
	D.RegisterHandler(TEXT("answer"),     MakeHandler(SearchTools::HandleSearchFromArgs), EUECPToolThreadAffinity::AnyThread);

	{
		const FName U(TEXT("search"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("web_search"), TEXT("Web search via Brave (multiple results) — docs/tutorials/repos."), TEXT("query, count?, filter?"));
		Meta(TEXT("answer"),     TEXT("Direct Q&A via Brave — factual questions / API lookups."), TEXT("query"));
		Meta(TEXT("search"),     TEXT("Web search/Q&A entrypoint (see web_search / answer)."), TEXT("action, query"));
	}

	UE_LOG(LogUECPSearchExt, Log, TEXT("Registered %d Search tools"), OwnedToolNames().Num());
}

void FUECPSearchExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPSearchExtModule, UECPSearchExt)
