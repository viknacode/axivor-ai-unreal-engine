// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

enum class EAssetRefType : uint8
{
	Blueprint,
	WidgetBlueprint,
	AnimBlueprint,
	BehaviorTree,
	Material,
	MaterialInstance,
	DataTable,
	Struct,
	Enum,
	NiagaraSystem,
	LevelSequence,
	StaticMesh,
	SkeletalMesh,
	Texture,
	Sound,
	Animation,
	PCGGraph,
	PhysicsAsset,
	StateTree,
	Curve,
	ParticleSystem,
	CppHeader,
	CppSource,
	Other
};

enum class EAIInteractionMode : uint8
{
	AutoEdit,
	AskBeforeEdit,
	JustChat,
	PlanMode
};

struct FTask
{
	int32 Index = 0;
	FString Content;
	FString Status = TEXT("pending");
	FString Verify;        // Axivor: read-back that proves the task (optional)
	FString Verification;  // Axivor: what was actually checked when marked done
};

static constexpr int32 MAX_API_KEY_SLOTS = 9;

struct FApiKeySlot
{
	FString Name;
	FString Provider;
	FString ApiKey;
	FString CustomBaseURL;
	FString CustomModelName;
	int32 SlotIndex;

	FString GeminiModel;

	FString OpenAIModel;

	FString ClaudeModel;

	FString DeepSeekModel;

	FString AgentModel;
	FString AgentEffort;

	FString CustomParams;

	FString TextureGenApiKey;
	FString TextureGenEndpoint;
	FString TextureGenModel;
	bool bTextureMode = true;

	FString MeshGenApiKey;
	FString MeshyAiModel;

	FString SoundGenApiKey;
	FString SoundGenEndpoint = TEXT("https://api.openai.com/v1/audio/speech");
	FString SoundGenModel    = TEXT("tts-1");
	FString SoundGenVoice    = TEXT("alloy");

	FApiKeySlot() : Provider(TEXT("Free")), ApiKey(TEXT("")), SlotIndex(0), GeminiModel(TEXT("gemini-2.5-flash")), OpenAIModel(TEXT("gpt-5-mini-2025-08-07")), ClaudeModel(TEXT("claude-sonnet-4-6")), DeepSeekModel(TEXT("deepseek-v4-flash")), TextureGenEndpoint(TEXT("https://generativelanguage.googleapis.com/v1beta/models/imagen-4.0-generate-001:predict")), TextureGenModel(TEXT("imagen-4.0-generate-001")), bTextureMode(true) {}
	FApiKeySlot(int32 InSlotIndex) : Provider(TEXT("Free")), ApiKey(TEXT("")), SlotIndex(InSlotIndex), GeminiModel(TEXT("gemini-2.5-flash")), OpenAIModel(TEXT("gpt-5-mini-2025-08-07")), ClaudeModel(TEXT("claude-sonnet-4-6")), TextureGenEndpoint(TEXT("https://generativelanguage.googleapis.com/v1beta/models/imagen-4.0-generate-001:predict")), TextureGenModel(TEXT("imagen-4.0-generate-001")), bTextureMode(true) {}
	bool IsValid() const { return !ApiKey.IsEmpty(); }
	bool IsCustomProvider() const { return Provider.Equals(TEXT("Custom"), ESearchCase::IgnoreCase); }
	void Clear()
	{
		Name = TEXT("");
		Provider = TEXT("Free");
		ApiKey = TEXT("");
		CustomBaseURL = TEXT("");
		CustomModelName = TEXT("");
		GeminiModel = TEXT("gemini-2.5-flash");
		OpenAIModel = TEXT("gpt-5-mini-2025-08-07");
		ClaudeModel = TEXT("claude-sonnet-4-6");
		AgentModel = TEXT("");
		AgentEffort = TEXT("");
		CustomParams = TEXT("");
		TextureGenApiKey = TEXT("");
		TextureGenEndpoint = TEXT("https://generativelanguage.googleapis.com/v1beta/models/imagen-4.0-generate-001:predict");
		TextureGenModel = TEXT("imagen-4.0-generate-001");
		bTextureMode = true;
		MeshGenApiKey = TEXT("");
		MeshyAiModel = TEXT("");
	}
	FString GetDisplayName() const
	{
		if (!Name.IsEmpty())
		{
			return Name;
		}
		if (IsCustomProvider() && !CustomBaseURL.IsEmpty())
		{
			return FString::Printf(TEXT("Slot %d: %s"), SlotIndex + 1, *CustomBaseURL);
		}
		return FString::Printf(TEXT("Slot %d: %s"), SlotIndex + 1, *Provider);
	}
};

struct FAssetRefItem
{
	FString Label;
	FString AssetPath;
	FString GroupName;
	EAssetRefType Type;

	FAssetRefItem()
		: Type(EAssetRefType::Other)
	{
	}

	FAssetRefItem(const FString& InLabel, const FString& InAssetPath, const FString& InGroupName, EAssetRefType InType)
		: Label(InLabel)
		, AssetPath(InAssetPath)
		, GroupName(InGroupName)
		, Type(InType)
	{
	}
};

struct FLinkedAsset
{
	FGuid LinkId;
	FString DisplayLabel;
	FString FullAssetPath;
	EAssetRefType Category;
	FString SummaryText;

	FLinkedAsset()
		: Category(EAssetRefType::Other)
	{
		LinkId = FGuid::NewGuid();
	}
};

struct FGddFileEntry
{
	FGuid FileId;
	FString FileName;
	FString FilePath;
	int32 CharCount;
	int32 EstimatedTokens;
	bool bEnabled;
	FDateTime LastModified;

	FGddFileEntry()
		: FileId(FGuid::NewGuid())
		, FileName()
		, FilePath()
		, CharCount(0)
		, EstimatedTokens(0)
		, bEnabled(true)
		, LastModified(FDateTime::Now())
	{
	}
};

struct FGddManifest
{
	TArray<FGddFileEntry> Files;
	int32 Version;

	FGddManifest() : Version(1) {}
};

UENUM(BlueprintType)
enum class EAiMemoryCategory : uint8
{
	ProjectInfo     UMETA(DisplayName = "Project Info"),
	RecentWork      UMETA(DisplayName = "Recent Work"),
	Preferences     UMETA(DisplayName = "Preferences"),
	Patterns        UMETA(DisplayName = "Patterns"),
	AssetRelations  UMETA(DisplayName = "Asset Relations"),
	Decisions       UMETA(DisplayName = "Decisions")
};

struct FAiMemoryEntry
{
	FGuid MemoryId;
	EAiMemoryCategory Category;
	FString Content;
	FString Source;
	FDateTime CreatedAt;
	FDateTime LastAccessed;
	int32 AccessCount;
	bool bEnabled;

	FAiMemoryEntry()
		: MemoryId(FGuid::NewGuid())
		, Category(EAiMemoryCategory::ProjectInfo)
		, Source(TEXT("user_added"))
		, AccessCount(0)
		, bEnabled(true)
	{
		CreatedAt = FDateTime::Now();
		LastAccessed = FDateTime::Now();
	}
};

struct FAiMemoryManifest
{
	TArray<FAiMemoryEntry> Memories;
	int32 Version;
	FString ProjectName;
	FDateTime LastUpdated;

	FAiMemoryManifest() : Version(1) {}
};

struct FSpawnRequest
{
	FString ActorClass;
	FVector Location;
	FRotator Rotation;
	FVector Scale;
	FString ActorLabel;

	FSpawnRequest()
		: Location(FVector::ZeroVector)
		, Rotation(FRotator::ZeroRotator)
		, Scale(FVector(1.0f))
	{}

	FSpawnRequest(const FString& InActorClass, const FVector& InLocation, const FRotator& InRotation, const FVector& InScale, const FString& InActorLabel)
		: ActorClass(InActorClass)
		, Location(InLocation)
		, Rotation(InRotation)
		, Scale(InScale)
		, ActorLabel(InActorLabel)
	{}
};
