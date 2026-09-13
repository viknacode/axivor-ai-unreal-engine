// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/ChatHistoryManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

FChatHistoryManager& FChatHistoryManager::Get()
{
	static FChatHistoryManager Instance;
	return Instance;
}

FChatHistoryManager::FChatHistoryManager()
{
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FChatHistoryManager::FlushPendingChatSaves);
}

FString FChatHistoryManager::GetViewFolder(EConversationViewType ViewType)
{
	switch (ViewType)
	{
	case EConversationViewType::Project:
		return FPaths::ProjectSavedDir() / TEXT("CodeExplainer") / TEXT("project_chats");
	case EConversationViewType::Architect:
		return FPaths::ProjectSavedDir() / TEXT("CodeExplainer") / TEXT("architect_chats");
	case EConversationViewType::Analyst:
	default:
		return FPaths::ProjectSavedDir() / TEXT("CodeExplainer");
	}
}

TArray<TSharedPtr<FConversationInfo>> FChatHistoryManager::LoadManifest(EConversationViewType ViewType)
{
	TArray<TSharedPtr<FConversationInfo>> Conversations;

	FString FileName;
	switch (ViewType)
	{
	case EConversationViewType::Project:
		FileName = TEXT("project_conversations.json");
		break;
	case EConversationViewType::Architect:
		FileName = TEXT("architect_conversations.json");
		break;
	case EConversationViewType::Analyst:
	default:
		FileName = TEXT("conversations.json");
		break;
	}

	FString FilePath = FPaths::ProjectSavedDir() / TEXT("CodeExplainer") / FileName;

	if (FPaths::FileExists(FilePath))
	{
		FString FileContent;
		if (FFileHelper::LoadFileToString(FileContent, *FilePath))
		{
			TSharedPtr<FJsonObject> RootObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

			if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* ManifestArray;
				if (RootObject->TryGetArrayField(TEXT("manifest"), ManifestArray))
				{
					for (const TSharedPtr<FJsonValue>& Value : *ManifestArray)
					{
						const TSharedPtr<FJsonObject>& ConvObject = Value->AsObject();
						if (ConvObject.IsValid())
						{
							TSharedPtr<FConversationInfo> Info = MakeShared<FConversationInfo>();
							ConvObject->TryGetStringField(TEXT("id"), Info->ID);
							ConvObject->TryGetStringField(TEXT("title"), Info->Title);
							ConvObject->TryGetStringField(TEXT("last_updated"), Info->LastUpdated);
							ConvObject->TryGetNumberField(TEXT("prompt_tokens"), Info->TotalPromptTokens);
							ConvObject->TryGetNumberField(TEXT("completion_tokens"), Info->TotalCompletionTokens);
							ConvObject->TryGetNumberField(TEXT("total_tokens"), Info->TotalTokens);
							ConvObject->TryGetStringField(TEXT("mode"), Info->Mode);
							{
								int32 SlotIdx = -1;
								if (ConvObject->TryGetNumberField(TEXT("api_key_slot_index"), SlotIdx))
								{
									Info->ApiKeySlotIndex = SlotIdx;
								}
							}
							Conversations.Add(Info);
						}
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ChatHistoryManager: Failed to parse manifest JSON at: %s"), *FilePath);
			}
		}
	}

	return Conversations;
}

void FChatHistoryManager::SaveManifest(EConversationViewType ViewType, const TArray<TSharedPtr<FConversationInfo>>& Conversations)
{
	FString FileName;
	switch (ViewType)
	{
	case EConversationViewType::Project:
		FileName = TEXT("project_conversations.json");
		break;
	case EConversationViewType::Architect:
		FileName = TEXT("architect_conversations.json");
		break;
	case EConversationViewType::Analyst:
	default:
		FileName = TEXT("conversations.json");
		break;
	}

	FString FilePath = FPaths::ProjectSavedDir() / TEXT("CodeExplainer") / FileName;
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ManifestArray;

	for (const TSharedPtr<FConversationInfo>& Info : Conversations)
	{
		TSharedPtr<FJsonObject> ConvObject = MakeShareable(new FJsonObject);
		ConvObject->SetStringField(TEXT("id"), Info->ID);
		ConvObject->SetStringField(TEXT("title"), Info->Title);
		ConvObject->SetStringField(TEXT("last_updated"), Info->LastUpdated);
		ConvObject->SetNumberField(TEXT("prompt_tokens"), Info->TotalPromptTokens);
		ConvObject->SetNumberField(TEXT("completion_tokens"), Info->TotalCompletionTokens);
		ConvObject->SetNumberField(TEXT("total_tokens"), Info->TotalTokens);
		if (!Info->Mode.IsEmpty()) ConvObject->SetStringField(TEXT("mode"), Info->Mode);
		if (Info->ApiKeySlotIndex >= 0) ConvObject->SetNumberField(TEXT("api_key_slot_index"), Info->ApiKeySlotIndex);
		ManifestArray.Add(MakeShareable(new FJsonValueObject(ConvObject)));
	}

	RootObject->SetArrayField(TEXT("manifest"), ManifestArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(OutputString, *FilePath);
}

TArray<TSharedPtr<FJsonValue>> FChatHistoryManager::LoadChatHistory(EConversationViewType ViewType, const FString& ChatID)
{
	TArray<TSharedPtr<FJsonValue>> History;

	if (ChatID.IsEmpty())
	{
		return History;
	}

	FString FolderPath = GetViewFolder(ViewType);
	FString FilePath = FolderPath / ChatID + TEXT(".json");

	if (FPaths::FileExists(FilePath))
	{
		FString FileContent;
		if (FFileHelper::LoadFileToString(FileContent, *FilePath))
		{
			TSharedPtr<FJsonObject> RootObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

			if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* HistoryArray;
				if (RootObject->TryGetArrayField(TEXT("history"), HistoryArray))
				{
					History = *HistoryArray;
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ChatHistoryManager: Failed to parse history JSON at: %s"), *FilePath);
			}
		}
	}

	return History;
}

void FChatHistoryManager::SaveChatHistory(EConversationViewType ViewType, const FString& ChatID, const TArray<TSharedPtr<FJsonValue>>& History)
{
	if (ChatID.IsEmpty() || History.Num() == 0)
	{
		return;
	}

	FString FolderPath = GetViewFolder(ViewType);
	FString FilePath = FolderPath / ChatID + TEXT(".json");

	IFileManager::Get().MakeDirectory(*FolderPath, true);

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	RootObject->SetArrayField(TEXT("history"), History);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	EnqueueChatWrite(FilePath, MoveTemp(OutputString));
}

void FChatHistoryManager::EnqueueChatWrite(const FString& FilePath, FString&& Content)
{
	{
		FScopeLock Lock(&ChatWriteLock);
		if (ChatWritesInFlight.Contains(FilePath))
		{
			ChatPendingWrites.Add(FilePath, MoveTemp(Content));
			return;
		}
		ChatWritesInFlight.Add(FilePath);
	}

	Async(EAsyncExecution::ThreadPool, [this, FilePath, Content = MoveTemp(Content)]() mutable
	{
		FString Cur = MoveTemp(Content);
		for (;;)
		{
			FFileHelper::SaveStringToFile(Cur, *FilePath);

			FScopeLock Lock(&ChatWriteLock);
			if (FString* Pending = ChatPendingWrites.Find(FilePath))
			{
				Cur = MoveTemp(*Pending);
				ChatPendingWrites.Remove(FilePath);
				continue;
			}
			ChatWritesInFlight.Remove(FilePath);
			return;
		}
	});
}

void FChatHistoryManager::FlushPendingChatSaves()
{
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	for (;;)
	{
		{
			FScopeLock Lock(&ChatWriteLock);
			if (ChatWritesInFlight.Num() == 0 && ChatPendingWrites.Num() == 0)
			{
				return;
			}
		}
		if (FPlatformTime::Seconds() >= Deadline)
		{
			return;
		}
		FPlatformProcess::Sleep(0.005f);
	}
}

int32 FChatHistoryManager::EstimateConversationTokens(const TArray<TSharedPtr<FJsonValue>>& ConversationHistory)
{
	int32 TotalChars = 0;
	for (const TSharedPtr<FJsonValue>& Value : ConversationHistory)
	{
		const TSharedPtr<FJsonObject>* ObjectPtr;
		if (Value->TryGetObject(ObjectPtr))
		{
			FString Role, Content;
			(*ObjectPtr)->TryGetStringField(TEXT("role"), Role);
			(*ObjectPtr)->TryGetStringField(TEXT("content"), Content);
			TotalChars += Content.Len() + Role.Len();
		}
	}
	return TotalChars / 4;
}

FString FChatHistoryManager::ExportConversationToFile(
	const FString& ChatID, const FString& Title, const FString& FileFormat,
	const TArray<TSharedPtr<FJsonValue>>& ConversationHistory)
{
	if (ChatID.IsEmpty() || ConversationHistory.Num() == 0)
	{
		return FString();
	}

	TStringBuilder<16384> ContentBuilder;
	for (const TSharedPtr<FJsonValue>& MessageValue : ConversationHistory)
	{
		const TSharedPtr<FJsonObject>& MessageObject = MessageValue->AsObject();
		if (!MessageObject.IsValid()) continue;

		FString Role, Content;
		if (!MessageObject->TryGetStringField(TEXT("role"), Role)) continue;

		if (Role == TEXT("agent_thinking") || Role == TEXT("context") || Role == TEXT("tool_bubble"))
			continue;

		const TArray<TSharedPtr<FJsonValue>>* PartsArray = nullptr;
		if (!MessageObject->TryGetArrayField(TEXT("parts"), PartsArray) || PartsArray->Num() == 0)
			continue;
		if (!(*PartsArray)[0]->AsObject()->TryGetStringField(TEXT("text"), Content))
			continue;

		if (FileFormat == TEXT("txt"))
		{
			const TCHAR* Speaker = (Role == TEXT("user")) ? TEXT("You:") : TEXT("AI:");
			ContentBuilder.Appendf(TEXT("%s\n%s\n\n-----------------\n\n"), Speaker, *Content);
		}
		else
		{
			const TCHAR* Speaker = (Role == TEXT("user")) ? TEXT("**You:**") : TEXT("**AI:**");
			ContentBuilder.Appendf(TEXT("%s\n\n%s\n\n---\n\n"), Speaker, *Content);
		}
	}

	const FString SaveDirectory = FPaths::ProjectSavedDir() / TEXT("Exported Conversations");
	IFileManager::Get().MakeDirectory(*SaveDirectory, true);

	FString SafeFileName = FPaths::MakeValidFileName(Title);
	if (SafeFileName.IsEmpty()) SafeFileName = ChatID;

	const FString FullPath = SaveDirectory / FString::Printf(TEXT("%s.%s"), *SafeFileName, *FileFormat);

	if (!FFileHelper::SaveStringToFile(ContentBuilder.ToString(), *FullPath))
	{
		return FString();
	}
	return FullPath;
}
