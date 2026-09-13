// Copyright 2026, BlueprintsLab, All rights reserved

#include "ACP/FUECPACPEventMapper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace
{
	FString ExtractChunkText(const TSharedPtr<FJsonObject>& Update)
	{
		const TSharedPtr<FJsonObject>* ContentPtr = nullptr;
		if (!Update->TryGetObjectField(TEXT("content"), ContentPtr) || !ContentPtr || !ContentPtr->IsValid())
			return FString();
		FString Text;
		(*ContentPtr)->TryGetStringField(TEXT("text"), Text);
		return Text;
	}

	FString FlattenContentArray(const TArray<TSharedPtr<FJsonValue>>* Content)
	{
		if (!Content) return FString();
		FString Out;
		for (const TSharedPtr<FJsonValue>& V : *Content)
		{
			const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
			if (!Obj.IsValid()) continue;

			FString Type;
			Obj->TryGetStringField(TEXT("type"), Type);
			if (Type == TEXT("content"))
			{
				const TSharedPtr<FJsonObject> Inner = Obj->GetObjectField(TEXT("content"));
				if (Inner.IsValid())
				{
					FString Text; Inner->TryGetStringField(TEXT("text"), Text);
					Out += Text;
				}
			}
			else if (Type == TEXT("diff"))
			{
				FString Path; Obj->TryGetStringField(TEXT("path"), Path);
				Out += FString::Printf(TEXT("[diff: %s]"), *Path);
			}
			else if (Type == TEXT("terminal"))
			{
				Out += TEXT("[terminal output]");
			}
		}
		return Out;
	}

	void PopulateToolFields(FUECPACPStreamEvent& Ev, const TSharedPtr<FJsonObject>& Update)
	{
		Update->TryGetStringField(TEXT("toolCallId"), Ev.ToolCallId);
		Update->TryGetStringField(TEXT("title"),      Ev.ToolTitle);
		Update->TryGetStringField(TEXT("kind"),       Ev.ToolKind);
		Update->TryGetStringField(TEXT("status"),     Ev.ToolStatus);

		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (Update->TryGetArrayField(TEXT("content"), Content))
		{
			Ev.ToolContentText = FlattenContentArray(Content);
		}

		if (Ev.ToolContentText.IsEmpty())
		{
			const TSharedPtr<FJsonObject>* RawOutputPtr = nullptr;
			if (Update->TryGetObjectField(TEXT("rawOutput"), RawOutputPtr) &&
				RawOutputPtr && RawOutputPtr->IsValid() && (*RawOutputPtr)->Values.Num() > 0)
			{
				FString Pretty;
				const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
					TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Pretty);
				FJsonSerializer::Serialize((*RawOutputPtr).ToSharedRef(), W);
				W->Close();
				Ev.ToolContentText = MoveTemp(Pretty);
			}
		}

		const TSharedPtr<FJsonObject>* RawInputObj = nullptr;
		if (!Update->TryGetObjectField(TEXT("rawInput"), RawInputObj) || !RawInputObj || !RawInputObj->IsValid())
			Update->TryGetObjectField(TEXT("input"), RawInputObj);
		if (RawInputObj && RawInputObj->IsValid() && (*RawInputObj)->Values.Num() > 0)
		{
			(*RawInputObj)->TryGetStringField(TEXT("action"), Ev.ToolAction);

			FString Pretty;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Pretty);
			FJsonSerializer::Serialize((*RawInputObj).ToSharedRef(), W);
			W->Close();
			Ev.ToolRawInputJson = MoveTemp(Pretty);
		}
		else if (!Ev.ToolCallId.IsEmpty() && Ev.ToolStatus == TEXT("completed"))
		{
			FString AvailableKeys;
			for (const auto& KV : Update->Values)
			{
				if (!AvailableKeys.IsEmpty()) AvailableKeys += TEXT(", ");
				AvailableKeys += KV.Key;
			}
			UE_LOG(LogTemp, Verbose, TEXT("[ACP] tool_call_update completed — no rawInput/input found. Keys: %s"), *AvailableKeys);
		}

		Ev.bIsError = (Ev.ToolStatus == TEXT("failed"));
	}

	void PopulateUsageFields(FUECPACPStreamEvent& Ev, const TSharedPtr<FJsonObject>& Update)
	{
		double Used = 0;
		Update->TryGetNumberField(TEXT("used"), Used);
		Ev.TotalTokens = static_cast<int32>(Used);

		const TSharedPtr<FJsonObject>* CostPtr = nullptr;
		if (Update->TryGetObjectField(TEXT("cost"), CostPtr) && CostPtr && CostPtr->IsValid())
		{
			(*CostPtr)->TryGetNumberField(TEXT("amount"), Ev.CostAmount);
			(*CostPtr)->TryGetStringField(TEXT("currency"), Ev.CostCurrency);
		}
	}
}

namespace UECPACPEventMapper
{
	void Map(const TSharedPtr<FJsonObject>& UpdateObj, TArray<FUECPACPStreamEvent>& OutEvents)
	{
		if (!UpdateObj.IsValid()) return;

		FString Disc;
		if (!UpdateObj->TryGetStringField(TEXT("sessionUpdate"), Disc)) return;

		if (Disc == TEXT("agent_message_chunk"))
		{
			FUECPACPStreamEvent Ev;
			Ev.Kind = EUECPACPStreamEventKind::Text;
			Ev.Text = ExtractChunkText(UpdateObj);
			if (!Ev.Text.IsEmpty()) OutEvents.Add(MoveTemp(Ev));
		}
		else if (Disc == TEXT("agent_thought_chunk"))
		{
			FUECPACPStreamEvent Ev;
			Ev.Kind = EUECPACPStreamEventKind::Thinking;
			Ev.Text = ExtractChunkText(UpdateObj);
			if (!Ev.Text.IsEmpty()) OutEvents.Add(MoveTemp(Ev));
		}
		else if (Disc == TEXT("tool_call"))
		{
			FUECPACPStreamEvent Ev;
			Ev.Kind = EUECPACPStreamEventKind::ToolStart;
			PopulateToolFields(Ev, UpdateObj);
			OutEvents.Add(MoveTemp(Ev));
		}
		else if (Disc == TEXT("tool_call_update"))
		{
			FUECPACPStreamEvent Ev;
			PopulateToolFields(Ev, UpdateObj);
			Ev.Kind = (Ev.ToolStatus == TEXT("completed") || Ev.ToolStatus == TEXT("failed"))
				? EUECPACPStreamEventKind::ToolResult
				: EUECPACPStreamEventKind::ToolUpdate;
			OutEvents.Add(MoveTemp(Ev));
		}
		else if (Disc == TEXT("usage_update"))
		{
			FUECPACPStreamEvent Ev;
			Ev.Kind = EUECPACPStreamEventKind::UsageUpdate;
			PopulateUsageFields(Ev, UpdateObj);
			OutEvents.Add(MoveTemp(Ev));
		}
		else if (Disc == TEXT("available_commands_update"))
		{
			FUECPACPStreamEvent Ev;
			Ev.Kind = EUECPACPStreamEventKind::SlashCommands;
			Ev.RawUpdate = UpdateObj;
			OutEvents.Add(MoveTemp(Ev));
		}
		else
		{
			FUECPACPStreamEvent Ev;
			Ev.Kind = EUECPACPStreamEventKind::Unknown;
			Ev.UnknownDiscriminator = Disc;
			Ev.RawUpdate = UpdateObj;
			OutEvents.Add(MoveTemp(Ev));
		}
	}
}
