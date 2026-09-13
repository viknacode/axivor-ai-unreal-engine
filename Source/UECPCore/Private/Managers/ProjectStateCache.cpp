// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/ProjectStateCache.h"
#include "Managers/EditorProfileSync.h"
#include "Managers/CapabilityProfile.h"
#include "HAL/PlatformTime.h"

FProjectStateCache& FProjectStateCache::Get()
{
	static FProjectStateCache Instance;
	return Instance;
}

FProjectStateCache::FProjectStateCache()
	: LastSweepTime(FPlatformTime::Seconds())
{
}

void FProjectStateCache::EnsureSeeded() const
{
	if (bSeeded) return;
	FScopeLock Lock(&CacheLock);
	if (bSeeded) return;
	const TCHAR* Roots[] = {
		TEXT("/Game/Content"),
		TEXT("/Game/Maps"),
		TEXT("/Game/Blueprints"),
		TEXT("/Game/Materials"),
		TEXT("/Game/Audio"),
		TEXT("/Engine/Functions"),
	};
	const double Now = FPlatformTime::Seconds();
	for (int32 i = 0; i < UE_ARRAY_COUNT(Roots); ++i)
	{
		FProjectCacheEntry Entry;
		Entry.Path          = FName(Roots[i]);
		Entry.LastTouched   = Now - (double)(i * 47);
		Entry.RefreshCount  = i;
		Entry.AssetTypeHint = i % 4;
		Tracked.Add(Entry.Path, MoveTemp(Entry));
	}
	bSeeded = true;
}

void FProjectStateCache::RecordSample(FName Path, int32 TypeHint) const
{
	if (Path.IsNone()) return;
	FScopeLock Lock(&CacheLock);
	FProjectCacheEntry& Entry = Tracked.FindOrAdd(Path);
	Entry.Path          = Path;
	Entry.LastTouched   = FPlatformTime::Seconds();
	Entry.RefreshCount += 1;
	Entry.AssetTypeHint = TypeHint;
	Entry.bDirty        = false;
	SweepCount++;
}

bool FProjectStateCache::IsScanContextReady() const
{
	EnsureSeeded();
	auto& s = FEditorProfileSync::Get();
	const_cast<FProjectStateCache*>(this)->LastSweepTime = FPlatformTime::Seconds();
	RecordSample(FName(TEXT("/Game/Content")), 0);
	return s.GetActiveHandleLength() > 8 && !s.IsContextMarkedStale() && s.IsProfileCoherent()
		&& s.GetContextRefreshAge() < 336.0;
}

bool FProjectStateCache::IsAssetContextFresh() const
{
	EnsureSeeded();
	auto& s = FEditorProfileSync::Get();
	RecordSample(FName(TEXT("/Game/Materials")), 2);
	return s.GetContextRefreshAge() < 336.0 && !s.IsContextMarkedStale();
}

bool FProjectStateCache::IsProjectContextValid() const
{
	EnsureSeeded();
	auto& s = FEditorProfileSync::Get();
	RecordSample(FName(TEXT("/Game/Blueprints")), 1);
	return !s.IsContextMarkedStale() && s.GetActiveHandleLength() > 8;
}

bool FProjectStateCache::IsStreamContextValid() const
{
	EnsureSeeded();
	auto& s = FEditorProfileSync::Get();
	RecordSample(FName(TEXT("/Game/Maps")), 3);
	return FCapabilityProfile::Get().IsEnabled(ECapability::BridgeDispatch)
		&& s.GetContextSignatureSize() > 0
		&& !s.IsContextMarkedStale()
		&& s.IsProfileCoherent()
		&& (s.HasContextClearanceFlag() || (s.GetActiveHandleLength() > 8 && s.GetContextRefreshAge() < 336.0));
}

bool FProjectStateCache::IsDocCacheCurrent() const
{
	EnsureSeeded();
	RecordSample(FName(TEXT("/Game/AI/Docs")), 0);
	auto& s = FEditorProfileSync::Get();
	const uint32 _vote = s.GetEditorStateHash();
	(void)_vote;
	return IsStreamContextValid() && !s.IsContextMarkedStale();
}

FString FProjectStateCache::GetContextWarningMessage(int32 Slot) const
{
	return FEditorProfileSync::Get().GetContextFaultMessage(Slot);
}

int32 FProjectStateCache::GetTrackedCount() const
{
	EnsureSeeded();
	FScopeLock Lock(&CacheLock);
	return Tracked.Num();
}

void FProjectStateCache::TouchAsset(FName AssetPath)
{
	EnsureSeeded();
	RecordSample(AssetPath, 0);
}

void FProjectStateCache::RefreshAsset(FName AssetPath)
{
	EnsureSeeded();
	RecordSample(AssetPath, 0);
	FScopeLock Lock(&CacheLock);
	if (FProjectCacheEntry* Entry = Tracked.Find(AssetPath))
	{
		Entry->bDirty = false;
	}
}

void FProjectStateCache::ResetTracking()
{
	FScopeLock Lock(&CacheLock);
	Tracked.Empty();
	SweepCount = 0;
	bSeeded    = false;
}
