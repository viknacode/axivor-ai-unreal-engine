// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPLevelSnapshotExtModule.h"
#include "Tools/LevelSnapshotTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPLevelSnapshotExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("take_level_snapshot"),
			TEXT("restore_level_snapshot"),
			TEXT("list_level_snapshots"),
			TEXT("delete_level_snapshot"),
			TEXT("compare_snapshot_to_world"),
			TEXT("restore_specific_actors"),
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

void FUECPLevelSnapshotExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("take_level_snapshot"),       MakeHandler(LevelSnapshotTools::HandleTakeLevelSnapshot));
	D.RegisterHandler(TEXT("restore_level_snapshot"),    MakeHandler(LevelSnapshotTools::HandleRestoreLevelSnapshot));
	D.RegisterHandler(TEXT("list_level_snapshots"),      MakeHandler(LevelSnapshotTools::HandleListLevelSnapshotsFromArgs));
	D.RegisterHandler(TEXT("delete_level_snapshot"),     MakeHandler(LevelSnapshotTools::HandleDeleteLevelSnapshotFromArgs));
	D.RegisterHandler(TEXT("compare_snapshot_to_world"), MakeHandler(LevelSnapshotTools::HandleCompareSnapshotToWorld));
	D.RegisterHandler(TEXT("restore_specific_actors"),   MakeHandler(LevelSnapshotTools::HandleRestoreSpecificActors));

	{
		const FName U(TEXT("level_snapshot"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("take_level_snapshot"),       TEXT("Capture a Level Snapshot asset."), TEXT("name, description?, save_path?"));
		Meta(TEXT("restore_level_snapshot"),    TEXT("Restore a snapshot (DESTRUCTIVE — compare first)."), TEXT("snapshot_path"));
		Meta(TEXT("list_level_snapshots"),      TEXT("List Level Snapshot assets."), TEXT(""));
		Meta(TEXT("delete_level_snapshot"),     TEXT("Delete a snapshot (batch)."), TEXT("snapshot_path"));
		Meta(TEXT("compare_snapshot_to_world"), TEXT("Diff a snapshot vs current world (added/removed/modified)."), TEXT("snapshot_path"));
		Meta(TEXT("restore_specific_actors"),   TEXT("Restore only the named actors from a snapshot (per-actor selective restore); refuses if none match."), TEXT("snapshot_path, actor_labels"));
	}

	UE_LOG(LogUECPLevelSnapshotExt, Log, TEXT("Registered %d Level Snapshot tools"), OwnedToolNames().Num());
}

void FUECPLevelSnapshotExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPLevelSnapshotExtModule, UECPLevelSnapshotExt)
