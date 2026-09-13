// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPPythonExtModule.h"

#include "Tools/PythonTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPPythonExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("execute_python"),
			TEXT("get_python_recipe"),
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

void FUECPPythonExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("execute_python"),    MakeHandler(PythonTools::HandleExecutePythonFromArgs));

	D.RegisterHandler(TEXT("get_python_recipe"), MakeHandler(PythonTools::HandleGetPythonRecipeFromArgs),
		EUECPToolThreadAffinity::AnyThread);

	{
		const FName U(TEXT("python_tools"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("get_python_recipe"), TEXT("Look up a vetted UE-Python recipe (CALL FIRST)."), TEXT("query"));
		Meta(TEXT("execute_python"),    TEXT("Run UE in-process Python (LAST resort — prefer dedicated umbrellas)."), TEXT("code, description"));
	}

	UE_LOG(LogUECPPythonExt, Log, TEXT("Registered %d Python tools (python_tools umbrella)"),
		OwnedToolNames().Num());
}

void FUECPPythonExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPPythonExtModule, UECPPythonExt)
