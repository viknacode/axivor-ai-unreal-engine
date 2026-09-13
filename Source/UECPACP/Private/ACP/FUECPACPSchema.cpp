// Copyright 2026, BlueprintsLab, All rights reserved

#include "ACP/FUECPACPSchema.h"

namespace UECPACPSchema
{
	TSharedRef<FJsonObject> BuildInitializeParams(const FString& ClientName,
		const FString& ClientTitle, const FString& ClientVersion)
	{
		const TSharedRef<FJsonObject> Caps = MakeShared<FJsonObject>();
		{
			const TSharedRef<FJsonObject> Fs = MakeShared<FJsonObject>();
			Fs->SetBoolField(TEXT("readTextFile"),  true);
			Fs->SetBoolField(TEXT("writeTextFile"), true);
			Caps->SetObjectField(TEXT("fs"), Fs);
			Caps->SetBoolField(TEXT("terminal"), false);
		}

		const TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("name"),    ClientName);
		Info->SetStringField(TEXT("title"),   ClientTitle);
		Info->SetStringField(TEXT("version"), ClientVersion);

		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("protocolVersion"),   1);
		P->SetObjectField(TEXT("clientCapabilities"), Caps);
		P->SetObjectField(TEXT("clientInfo"),         Info);
		return P;
	}

	namespace
	{
		TArray<TSharedPtr<FJsonValue>> RenderKeyValuePairs(const TArray<TPair<FString, FString>>& Pairs)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			Out.Reserve(Pairs.Num());
			for (const TPair<FString, FString>& KV : Pairs)
			{
				const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"),  KV.Key);
				Obj->SetStringField(TEXT("value"), KV.Value);
				Out.Add(MakeShared<FJsonValueObject>(Obj));
			}
			return Out;
		}
	}

	TSharedRef<FJsonObject> BuildSessionNewParams(const FString& Cwd, const TArray<FMcpServerSpec>& McpSpecs)
	{
		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("cwd"), Cwd);

		TArray<TSharedPtr<FJsonValue>> McpServers;
		McpServers.Reserve(McpSpecs.Num());
		for (const FMcpServerSpec& S : McpSpecs)
		{
			const TSharedRef<FJsonObject> Srv = MakeShared<FJsonObject>();
			Srv->SetStringField(TEXT("name"), S.Name.IsEmpty() ? TEXT("uecp") : S.Name);

			if (!S.HttpUrl.IsEmpty())
			{
				Srv->SetStringField(TEXT("type"), TEXT("http"));
				Srv->SetStringField(TEXT("url"),  S.HttpUrl);
				Srv->SetArrayField(TEXT("headers"), RenderKeyValuePairs(S.HttpHeaders));
			}
			else if (!S.StdioCommand.IsEmpty())
			{
				Srv->SetStringField(TEXT("command"), S.StdioCommand);
				TArray<TSharedPtr<FJsonValue>> ArgVals;
				ArgVals.Reserve(S.StdioArgs.Num());
				for (const FString& A : S.StdioArgs) ArgVals.Add(MakeShared<FJsonValueString>(A));
				Srv->SetArrayField(TEXT("args"), ArgVals);
				Srv->SetArrayField(TEXT("env"),  RenderKeyValuePairs(S.StdioEnv));
			}
			else
			{
				continue;
			}
			McpServers.Add(MakeShared<FJsonValueObject>(Srv));
		}

		P->SetArrayField(TEXT("mcpServers"), McpServers);
		return P;
	}

	TSharedRef<FJsonObject> BuildSessionPromptParams(const FString& SessionId, const FString& UserText)
	{
		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("sessionId"), SessionId);

		const TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), UserText);

		TArray<TSharedPtr<FJsonValue>> Prompt;
		Prompt.Add(MakeShared<FJsonValueObject>(TextBlock));
		P->SetArrayField(TEXT("prompt"), Prompt);
		return P;
	}

	TSharedRef<FJsonObject> BuildSessionPromptParamsRich(const FString& SessionId,
		const TArray<FPromptContentBlock>& Blocks)
	{
		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("sessionId"), SessionId);

		TArray<TSharedPtr<FJsonValue>> Prompt;
		Prompt.Reserve(Blocks.Num());
		for (const FPromptContentBlock& B : Blocks)
		{
			if (B.Type.IsEmpty()) continue;
			const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("type"), B.Type);
			if (B.Type == TEXT("text"))
			{
				Obj->SetStringField(TEXT("text"), B.Text);
			}
			else
			{
				Obj->SetStringField(TEXT("data"),     B.Base64Data);
				Obj->SetStringField(TEXT("mimeType"), B.MimeType);
			}
			Prompt.Add(MakeShared<FJsonValueObject>(Obj));
		}
		P->SetArrayField(TEXT("prompt"), Prompt);
		return P;
	}

	TSharedRef<FJsonObject> BuildSessionCancelParams(const FString& SessionId)
	{
		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("sessionId"), SessionId);
		return P;
	}

	TSharedRef<FJsonObject> BuildSessionSetModeParams(const FString& SessionId, const FString& ModeId)
	{
		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("sessionId"), SessionId);
		P->SetStringField(TEXT("modeId"),    ModeId);
		return P;
	}

	TSharedRef<FJsonObject> BuildSessionSetModelParams(const FString& SessionId, const FString& ModelId)
	{
		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("sessionId"), SessionId);
		P->SetStringField(TEXT("modelId"),   ModelId);
		return P;
	}

	TSharedRef<FJsonObject> BuildSessionSetOptionParams(const FString& SessionId,
		const FString& OptionId, const FString& Value)
	{
		const TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("sessionId"), SessionId);
		P->SetStringField(*OptionId,         Value);
		return P;
	}

	bool ReadInitializeResult(const TSharedPtr<FJsonObject>& Result,
		int32& OutNegotiatedVersion, FString& OutAgentName, FString& OutAgentVersion)
	{
		if (!Result.IsValid()) return false;

		double Ver = 0;
		if (!Result->TryGetNumberField(TEXT("protocolVersion"), Ver))
		{
			FString VerStr;
			if (Result->TryGetStringField(TEXT("protocolVersion"), VerStr))
				Ver = FCString::Atof(*VerStr);
		}
		OutNegotiatedVersion = static_cast<int32>(Ver);
		if (OutNegotiatedVersion < 1) return false;

		if (const TSharedPtr<FJsonObject> Info = Result->GetObjectField(TEXT("agentInfo")))
		{
			Info->TryGetStringField(TEXT("name"),    OutAgentName);
			Info->TryGetStringField(TEXT("version"), OutAgentVersion);
		}
		return true;
	}

	bool ReadSessionNewResult(const TSharedPtr<FJsonObject>& Result, FString& OutSessionId)
	{
		if (!Result.IsValid()) return false;
		return Result->TryGetStringField(TEXT("sessionId"), OutSessionId);
	}

	bool ReadSessionNewConfig(const TSharedPtr<FJsonObject>& Result, FConfigSnapshot& OutConfig)
	{
		if (!Result.IsValid()) return false;
		bool bFound = false;

		const TArray<TSharedPtr<FJsonValue>>* CfgArr = nullptr;
		if (Result->TryGetArrayField(TEXT("configOptions"), CfgArr) && CfgArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *CfgArr)
			{
				const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
				if (!Obj.IsValid()) continue;

				FString Category;
				Obj->TryGetStringField(TEXT("category"), Category);
				Category = Category.ToLower();

				const TArray<TSharedPtr<FJsonValue>>* OptsArr = nullptr;
				if (!Obj->TryGetArrayField(TEXT("options"), OptsArr) || !OptsArr) continue;

				TArray<FConfigOption> Parsed;
				Parsed.Reserve(OptsArr->Num());
				for (const TSharedPtr<FJsonValue>& OV : *OptsArr)
				{
					const TSharedPtr<FJsonObject> O = OV.IsValid() ? OV->AsObject() : nullptr;
					if (!O.IsValid()) continue;
					FConfigOption Entry;
					O->TryGetStringField(TEXT("value"),       Entry.Id);
					O->TryGetStringField(TEXT("name"),        Entry.Name);
					O->TryGetStringField(TEXT("description"), Entry.Description);
					if (Entry.Name.IsEmpty()) Entry.Name = Entry.Id;
					if (!Entry.Id.IsEmpty()) Parsed.Add(MoveTemp(Entry));
				}
				if (Parsed.Num() == 0) continue;

				if (Category == TEXT("model") || Category == TEXT("mode"))
				{
					TArray<FConfigOption>& TargetList = (Category == TEXT("model")) ? OutConfig.Models : OutConfig.Modes;
					FString& TargetCurrent          = (Category == TEXT("model")) ? OutConfig.CurrentModel : OutConfig.CurrentMode;
					Obj->TryGetStringField(TEXT("currentValue"), TargetCurrent);
					TargetList.Append(MoveTemp(Parsed));
					bFound = true;
				}
				else
				{
					FConfigSection Sec;
					Obj->TryGetStringField(TEXT("id"),   Sec.Id);
					Obj->TryGetStringField(TEXT("name"), Sec.Name);
					Obj->TryGetStringField(TEXT("currentValue"), Sec.CurrentValue);
					if (Sec.Id.IsEmpty()) Sec.Id = Category;
					if (Sec.Name.IsEmpty()) Sec.Name = Sec.Id;
					Sec.Options = MoveTemp(Parsed);
					OutConfig.OtherSections.Add(MoveTemp(Sec));
					bFound = true;
				}
			}
		}

		auto ReadNamedBlock = [&](const TCHAR* BlockName, const TCHAR* ArrayName, const TCHAR* CurrentName,
			const TCHAR* IdKey, TArray<FConfigOption>& OutList, FString& OutCurrent)
		{
			const TSharedPtr<FJsonObject>* BlockPtr = nullptr;
			if (!Result->TryGetObjectField(BlockName, BlockPtr) || !BlockPtr || !BlockPtr->IsValid()) return;
			(*BlockPtr)->TryGetStringField(CurrentName, OutCurrent);
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (!(*BlockPtr)->TryGetArrayField(ArrayName, Arr) || !Arr) return;
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				FConfigOption Entry;
				O->TryGetStringField(IdKey,               Entry.Id);
				if (Entry.Id.IsEmpty()) O->TryGetStringField(TEXT("id"), Entry.Id);
				O->TryGetStringField(TEXT("name"),        Entry.Name);
				O->TryGetStringField(TEXT("description"), Entry.Description);
				if (Entry.Name.IsEmpty()) Entry.Name = Entry.Id;
				if (!Entry.Id.IsEmpty()) OutList.Add(MoveTemp(Entry));
			}
		};

		if (OutConfig.Models.Num() == 0)
		{
			ReadNamedBlock(TEXT("models"), TEXT("availableModels"), TEXT("currentModelId"),
				TEXT("modelId"), OutConfig.Models, OutConfig.CurrentModel);
			if (OutConfig.Models.Num() > 0) bFound = true;
		}
		if (OutConfig.Modes.Num() == 0)
		{
			ReadNamedBlock(TEXT("modes"), TEXT("availableModes"), TEXT("currentModeId"),
				TEXT("id"), OutConfig.Modes, OutConfig.CurrentMode);
			if (OutConfig.Modes.Num() > 0) bFound = true;
		}

		return bFound;
	}

	bool ReadSessionUpdateDiscriminator(const TSharedPtr<FJsonObject>& NotifParams,
		FString& OutDiscriminator, TSharedPtr<FJsonObject>& OutUpdateObj)
	{
		if (!NotifParams.IsValid()) return false;
		const TSharedPtr<FJsonObject> Update = NotifParams->GetObjectField(TEXT("update"));
		if (!Update.IsValid()) return false;
		if (!Update->TryGetStringField(TEXT("sessionUpdate"), OutDiscriminator)) return false;
		OutUpdateObj = Update;
		return true;
	}

	bool ReadPermissionRequestParams(const TSharedPtr<FJsonObject>& Params,
		FString& OutToolCallId, FString& OutToolTitle, FString& OutToolKind,
		TArray<TPair<FString, FString>>& OutOptions)
	{
		if (!Params.IsValid()) return false;

		if (const TSharedPtr<FJsonObject> Tool = Params->GetObjectField(TEXT("toolCall")))
		{
			Tool->TryGetStringField(TEXT("toolCallId"), OutToolCallId);
			Tool->TryGetStringField(TEXT("title"),      OutToolTitle);
			Tool->TryGetStringField(TEXT("kind"),       OutToolKind);
		}
		if (OutToolCallId.IsEmpty()) return false;

		const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
		if (!Params->TryGetArrayField(TEXT("options"), Options) || !Options) return false;

		for (const TSharedPtr<FJsonValue>& V : *Options)
		{
			const TSharedPtr<FJsonObject> Opt = V.IsValid() ? V->AsObject() : nullptr;
			if (!Opt.IsValid()) continue;
			FString OptionId; Opt->TryGetStringField(TEXT("optionId"), OptionId);
			FString Kind;     Opt->TryGetStringField(TEXT("kind"),     Kind);
			if (!OptionId.IsEmpty()) OutOptions.Emplace(MoveTemp(OptionId), MoveTemp(Kind));
		}
		return OutOptions.Num() > 0;
	}

	TSharedRef<FJsonObject> BuildPermissionResultSelected(const FString& OptionId)
	{
		const TSharedRef<FJsonObject> Outcome = MakeShared<FJsonObject>();
		Outcome->SetStringField(TEXT("outcome"), TEXT("selected"));
		Outcome->SetStringField(TEXT("optionId"), OptionId);

		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("outcome"), Outcome);
		return Result;
	}

	TSharedRef<FJsonObject> BuildPermissionResultCancelled()
	{
		const TSharedRef<FJsonObject> Outcome = MakeShared<FJsonObject>();
		Outcome->SetStringField(TEXT("outcome"), TEXT("cancelled"));

		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("outcome"), Outcome);
		return Result;
	}
}
