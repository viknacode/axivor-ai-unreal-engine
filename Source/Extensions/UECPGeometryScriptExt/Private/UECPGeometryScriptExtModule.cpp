// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPGeometryScriptExtModule.h"
#include "Tools/GeometryScriptTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPGeometryScriptExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("spawn_dynamic_mesh_actor"),
			TEXT("append_box"),
			TEXT("append_sphere"),
			TEXT("append_cylinder"),
			TEXT("append_cone"),
			TEXT("copy_mesh_from_static_mesh"),
			TEXT("apply_boolean_operation"),
			TEXT("bake_to_static_mesh"),
			TEXT("clear_mesh"),
			TEXT("get_mesh_info"),
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

void FUECPGeometryScriptExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("spawn_dynamic_mesh_actor"),    MakeHandler(GeometryScriptTools::HandleSpawnDynamicMeshActorFromArgs));
	D.RegisterHandler(TEXT("append_box"),                  MakeHandler(GeometryScriptTools::HandleAppendBoxFromArgs));
	D.RegisterHandler(TEXT("append_sphere"),               MakeHandler(GeometryScriptTools::HandleAppendSphereFromArgs));
	D.RegisterHandler(TEXT("append_cylinder"),             MakeHandler(GeometryScriptTools::HandleAppendCylinderFromArgs));
	D.RegisterHandler(TEXT("append_cone"),                 MakeHandler(GeometryScriptTools::HandleAppendConeFromArgs));
	D.RegisterHandler(TEXT("copy_mesh_from_static_mesh"),  MakeHandler(GeometryScriptTools::HandleCopyMeshFromStaticMeshFromArgs));
	D.RegisterHandler(TEXT("apply_boolean_operation"),     MakeHandler(GeometryScriptTools::HandleApplyBooleanOperationFromArgs));
	D.RegisterHandler(TEXT("bake_to_static_mesh"),         MakeHandler(GeometryScriptTools::HandleBakeToStaticMeshFromArgs));
	D.RegisterHandler(TEXT("clear_mesh"),                  MakeHandler(GeometryScriptTools::HandleClearMeshFromArgs));
	D.RegisterHandler(TEXT("get_mesh_info"),               MakeHandler(GeometryScriptTools::HandleGetMeshInfoFromArgs));

	{
		const FName U(TEXT("geometry_script"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("spawn_dynamic_mesh_actor"),   TEXT("Spawn a DynamicMeshActor (call FIRST; appends accumulate onto it)."), TEXT("actor_label, location_x/y/z"));
		Meta(TEXT("append_box"),                 TEXT("Append a box to a dynamic mesh."), TEXT("actor_label, size_x/y/z=100, offset_x/y/z"));
		Meta(TEXT("append_sphere"),              TEXT("Append a sphere."), TEXT("actor_label, radius=50, latitude_steps, longitude_steps"));
		Meta(TEXT("append_cylinder"),            TEXT("Append a cylinder."), TEXT("actor_label, radius=50, height=100, radial_steps"));
		Meta(TEXT("append_cone"),                TEXT("Append a cone (apex_radius>0 = truncated)."), TEXT("actor_label, base_radius=50, apex_radius=0, height=100, radial_steps"));
		Meta(TEXT("copy_mesh_from_static_mesh"), TEXT("Copy a StaticMesh's geometry into the dynamic mesh."), TEXT("actor_label, static_mesh_path"));
		Meta(TEXT("apply_boolean_operation"),    TEXT("Boolean a tool actor's mesh into the target (Union|Intersect|Subtract)."), TEXT("actor_label, tool_actor_label, operation"));
		Meta(TEXT("bake_to_static_mesh"),        TEXT("Bake the dynamic mesh into a StaticMesh asset."), TEXT("actor_label, save_path, name"));
		Meta(TEXT("clear_mesh"),                 TEXT("Clear the dynamic mesh."), TEXT("actor_label"));
		Meta(TEXT("get_mesh_info"),              TEXT("Report the dynamic mesh's vertex/triangle counts."), TEXT("actor_label"));
	}

	UE_LOG(LogUECPGeometryScriptExt, Log, TEXT("Registered %d Geometry Script tools"), OwnedToolNames().Num());
}

void FUECPGeometryScriptExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPGeometryScriptExtModule, UECPGeometryScriptExt)
