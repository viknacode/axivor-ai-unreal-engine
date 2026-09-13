// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"

struct FLearningCourse
{
	FString Id;
	FString Slug;
	FString Title;
	FString Description;
	FString IconUrl;
	int32 SortOrder = 0;
};

struct FLearningNode
{
	FString Id;
	FString Slug;
	FString Title;
	FString Description;
	FString NodeType;
	FString IconEmoji;
	FString CourseId;
	FString ContentHtml;
	FString VideoPath;
	FString AutoDetectTool;
	int32 GridX = 0;
	int32 GridY = 0;
	int32 XpReward = 100;
	int32 VideoDurationSeconds = 0;
	bool bRequiresExplicitMark = false;
	TSharedPtr<FJsonObject> AutoDetectArgsPattern;
	TArray<FString> PrerequisiteSlugs;
};

struct FLearningProgressEntry
{
	FString NodeSlug;
	FString Status;
	FDateTime CompletedAt;
	int32 XpEarned = 0;
};

struct FLearningUserStats
{
	int32 TotalXP = 0;
	int32 CurrentStreak = 0;
	int32 LongestStreak = 0;
	int32 Level = 1;
	int32 CompletedNodes = 0;
	int32 TotalPrompts = 0;
	FString LastLoginDate;
};

struct FLeaderboardEntry
{
	FString DisplayName;
	int32 Rank = 0;
	int32 XP = 0;
	int32 Level = 1;
	int32 Streak = 0;
	int32 CompletedNodes = 0;
	bool bIsCurrentUser = false;
};

DECLARE_MULTICAST_DELEGATE(FOnLearningProgressUpdated);
DECLARE_MULTICAST_DELEGATE(FOnLearningCourseDataLoaded);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLearningDailyLoginResult, bool );

class UECPLEARNING_API FLearningManager
{
public:
	static FLearningManager& Get()
	{
		static FLearningManager Instance;
		return Instance;
	}

	void Initialize();
	void Shutdown();

	void FetchCourseStructure();
	void FetchUserProgress();
	void RecordDailyLogin();

	void MarkNodeComplete(const FString& NodeSlug);
	void MarkNodeInProgress(const FString& NodeSlug);
	bool IsNodeUnlocked(const FString& NodeSlug) const;
	bool IsNodeComplete(const FString& NodeSlug) const;
	FString GetNodeStatus(const FString& NodeSlug) const;

	void RequestSignedVideoUrl(const FString& VideoPath, TFunction<void(bool, const FString&)> Callback);

	void OnToolExecuted(const FString& ToolName, const TSharedPtr<FJsonObject>& Args);

	void TrackToolCompletion(const FString& ToolName, const TSharedPtr<FJsonObject>& Args,
		bool bSuccess, const FString& ResultJson, const FString& ErrorMessage, const FString& Source = TEXT("widget"));

	void WebsitePost(const FString& Endpoint, const TSharedPtr<FJsonObject>& ExtraFields, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback);

	void FlushEventBuffer();

	int32 GetTotalXP() const;
	int32 GetCurrentStreak() const;
	int32 GetLevel() const;
	const FLearningUserStats& GetUserStats() const { return UserStats; }

	void FetchLeaderboard(const FString& Period, TFunction<void(bool, const TArray<FLeaderboardEntry>&)> Callback);
	void SetDisplayName(const FString& Name);
	void IncrementPromptCount();

	void TrackEvent(const FString& EventType, const TSharedPtr<FJsonObject>& Data = nullptr);

	void StartSyncPulse();

	const TArray<TSharedPtr<FLearningCourse>>& GetCourses() const { return Courses; }
	const TMap<FString, TSharedPtr<FLearningNode>>& GetNodesBySlug() const { return NodesBySlug; }
	TArray<TSharedPtr<FLearningNode>> GetNodesForCourse(const FString& CourseSlug) const;

	FString BuildSkillTreeJson() const;

	bool IsInitialized() const { return bInitialized; }
	bool IsDataLoaded() const { return bDataLoaded; }

	FOnLearningProgressUpdated OnProgressUpdated;
	FOnLearningCourseDataLoaded OnCourseDataLoaded;
	FOnLearningDailyLoginResult OnDailyLoginResult;

private:
	FLearningManager() = default;
	FLearningManager(const FLearningManager&) = delete;
	FLearningManager& operator=(const FLearningManager&) = delete;

	TArray<TSharedPtr<FLearningCourse>> Courses;
	TMap<FString, TSharedPtr<FLearningNode>> NodesBySlug;
	TMap<FString, FLearningProgressEntry> UserProgress;
	FLearningUserStats UserStats;
	FString DisplayName;

	bool bInitialized = false;
	bool bDataLoaded = false;
	bool bDailyLoginDone = false;
	mutable FCriticalSection DataCriticalSection;

	TArray<TSharedPtr<FJsonObject>> EventBuffer;
	mutable FCriticalSection EventBufferLock;
	FTSTicker::FDelegateHandle HeartbeatTickerHandle;
	double LastHeartbeatTime = 0.0;
	static constexpr int32 MaxEventsPerFlush = 50;
	static constexpr int32 FlushThreshold = 5;
	static constexpr double SyncPulseIntervalSeconds = 900.0;

	void SendHeartbeatEvent();

	FString GetRegistryBase() const;
	FString GetServiceRegistrationKey() const;
	FString GetHardwareId() const;
	FString GetLicenseKey() const;

	void IssueGet(const FString& PathAndQuery, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback);

	void CallEdgeFunction(const FString& FunctionName, const TSharedPtr<FJsonObject>& Body, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback);

	void WebsiteGet(const FString& UrlWithParams, TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback);

	void ParseCourseData(const FString& CoursesJson, const FString& NodesJson);
	void ParseProgressData(const FString& ProgressJson, const FString& StatsJson);
	void RecalculateNodeAvailability();
	void SaveCacheToFile();
	void LoadCacheFromFile();

	static FString ComposeDispatchString(const TArray<uint8>& Encoded, const FString& Key);
};
