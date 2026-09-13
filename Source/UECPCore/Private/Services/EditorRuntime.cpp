// Copyright 2026, BlueprintsLab, All rights reserved

#include "Utils/EditorRuntime.h"
#include "Managers/ProjectStateCache.h"

bool EditorReadiness::IsSessionActive()
{
	return FProjectStateCache::Get().IsScanContextReady();
}

bool EditorReadiness::IsGraphContextReady()
{
	return FProjectStateCache::Get().IsAssetContextFresh();
}

bool EditorReadiness::IsCompilerContextReady()
{
	return FProjectStateCache::Get().IsProjectContextValid();
}

bool EditorReadiness::IsMCPContextValid()
{
	return FProjectStateCache::Get().IsStreamContextValid();
}

FString EditorReadiness::GetContextDeniedMessage(int32 Idx)
{
	return FProjectStateCache::Get().GetContextWarningMessage(Idx);
}
