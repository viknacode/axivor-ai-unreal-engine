// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPCppExtModule.h"

#include "Tools/CppFileTools.h"
#include "Tools/CppCompileTools.h"
#include "Tools/CppEngineSearchTools.h"
#include "Tools/CppAuthoringTools.h"
#include "Tools/CppReflectionTools.h"
#include "Tools/CppModuleTools.h"
#include "Tools/CppInspectionTools.h"
#include "Tools/CppPluginTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPCppExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("read_cpp_file"),
			TEXT("write_cpp_file"),
			TEXT("edit_cpp_file"),

			TEXT("check_project_source"),
			TEXT("compile_project"),

			TEXT("search_engine_source"),
			TEXT("search_engine_api"),

			TEXT("create_uclass"),
			TEXT("create_actor"),
			TEXT("create_actor_component"),
			TEXT("create_ustruct"),
			TEXT("create_uenum"),
			TEXT("create_uinterface"),

			TEXT("add_uproperty"),
			TEXT("add_ufunction"),
			TEXT("add_member"),
			TEXT("add_include"),

			TEXT("list_project_modules"),
			TEXT("edit_module_deps"),

			TEXT("create_plugin"),
			TEXT("add_plugin_module"),
			TEXT("add_plugin_dependency"),
			TEXT("create_uecp_extension"),
			TEXT("register_extension_tool"),

			TEXT("get_class_summary"),
			TEXT("find_class_definition"),
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

void FUECPCppExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("read_cpp_file"),  MakeHandler(CppFileTools::HandleReadCppFileFromArgs));
	D.RegisterHandler(TEXT("write_cpp_file"), MakeHandler(CppFileTools::HandleWriteCppFileFromArgs));

	D.RegisterHandler(TEXT("edit_cpp_file"),
		MakeHandler(CppFileTools::HandleEditCppFileFromArgs),
		EUECPToolThreadAffinity::AnyThread);

	D.RegisterHandler(TEXT("check_project_source"),
		MakeHandler(CppCompileTools::HandleCheckProjectSourceFromArgs));

	D.RegisterHandler(TEXT("compile_project"),
		MakeHandler(CppCompileTools::HandleCompileProjectFromArgs));

	D.RegisterHandler(TEXT("search_engine_source"),
		MakeHandler(CppEngineSearchTools::HandleSearchEngineSourceFromArgs),
		EUECPToolThreadAffinity::AnyThread);
	D.RegisterHandler(TEXT("search_engine_api"),
		MakeHandler(CppEngineSearchTools::HandleSearchEngineApiFromArgs),
		EUECPToolThreadAffinity::AnyThread);

	D.RegisterHandler(TEXT("create_uclass"),          MakeHandler(CppAuthoringTools::HandleCreateUClassFromArgs));
	D.RegisterHandler(TEXT("create_actor"),           MakeHandler(CppAuthoringTools::HandleCreateActorFromArgs));
	D.RegisterHandler(TEXT("create_actor_component"), MakeHandler(CppAuthoringTools::HandleCreateActorComponentFromArgs));
	D.RegisterHandler(TEXT("create_ustruct"),         MakeHandler(CppAuthoringTools::HandleCreateUStructFromArgs));
	D.RegisterHandler(TEXT("create_uenum"),           MakeHandler(CppAuthoringTools::HandleCreateUEnumFromArgs));
	D.RegisterHandler(TEXT("create_uinterface"),      MakeHandler(CppAuthoringTools::HandleCreateUInterfaceFromArgs));

	D.RegisterHandler(TEXT("add_uproperty"), MakeHandler(CppReflectionTools::HandleAddUPropertyFromArgs));
	D.RegisterHandler(TEXT("add_ufunction"), MakeHandler(CppReflectionTools::HandleAddUFunctionFromArgs));
	D.RegisterHandler(TEXT("add_member"),    MakeHandler(CppReflectionTools::HandleAddMemberFromArgs));
	D.RegisterHandler(TEXT("add_include"),   MakeHandler(CppReflectionTools::HandleAddIncludeFromArgs));

	D.RegisterHandler(TEXT("list_project_modules"), MakeHandler(CppModuleTools::HandleListProjectModulesFromArgs));
	D.RegisterHandler(TEXT("edit_module_deps"),     MakeHandler(CppModuleTools::HandleEditModuleDepsFromArgs));

	D.RegisterHandler(TEXT("create_plugin"),         MakeHandler(CppPluginTools::HandleCreatePluginFromArgs));
	D.RegisterHandler(TEXT("add_plugin_module"),     MakeHandler(CppPluginTools::HandleAddPluginModuleFromArgs));
	D.RegisterHandler(TEXT("add_plugin_dependency"), MakeHandler(CppPluginTools::HandleAddPluginDependencyFromArgs));
	D.RegisterHandler(TEXT("create_uecp_extension"), MakeHandler(CppPluginTools::HandleCreateUECPExtensionFromArgs));
	D.RegisterHandler(TEXT("register_extension_tool"), MakeHandler(CppPluginTools::HandleRegisterExtensionToolFromArgs));

	D.RegisterHandler(TEXT("get_class_summary"),     MakeHandler(CppInspectionTools::HandleGetClassSummaryFromArgs));
	D.RegisterHandler(TEXT("find_class_definition"), MakeHandler(CppInspectionTools::HandleFindClassDefinitionFromArgs));

	{
		const FName U(TEXT("cpp_tools"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("create_uclass"),            TEXT("Generate a UCLASS (header+source) with MODULENAME_API/GENERATED_BODY and optional inline properties[]/functions[]."), TEXT("name, parent_class, properties?, functions?, module?, subfolder?, abstract?, blueprintable?"));
		Meta(TEXT("create_actor"),             TEXT("Generate an AActor subclass (BeginPlay+Tick stubs)."), TEXT("name, parent_class=AActor, properties?, functions?, module?"));
		Meta(TEXT("create_actor_component"),   TEXT("Generate a UActorComponent subclass (BlueprintSpawnableComponent, tick off by default)."), TEXT("name, parent_class=UActorComponent, properties?, functions?, module?"));
		Meta(TEXT("create_ustruct"),           TEXT("Generate a USTRUCT (header-only)."), TEXT("name, members=[{type,name,default_value,specifiers,comment}], blueprint_type?"));
		Meta(TEXT("create_uenum"),             TEXT("Generate a UENUM (header-only)."), TEXT("name, values=[...], underlying_type?, blueprint_type?"));
		Meta(TEXT("create_uinterface"),        TEXT("Generate a UInterface + IInterface pair (header+source)."), TEXT("name, methods=[{name,return_type,params,specifiers,comment}], minimal_api?"));
		Meta(TEXT("add_uproperty"),            TEXT("Add a UPROPERTY to a class header (batch via items/properties)."), TEXT("file_path, class_name?, name, type, specifiers?, default_value?, comment?"));
		Meta(TEXT("add_ufunction"),            TEXT("Add a UFUNCTION decl to the .h plus a stub to the matching .cpp (batch)."), TEXT("file_path, class_name, name, return_type?, params?, specifiers?, virtual?, static?, const?"));
		Meta(TEXT("add_member"),               TEXT("Add a non-reflected member (no UPROPERTY) to a class (batch)."), TEXT("file_path, class_name, name, type, default_value?, access?"));
		Meta(TEXT("add_include"),              TEXT("Add an #include to a header (idempotent, inserted before .generated.h; batch)."), TEXT("file_path, include"));
		Meta(TEXT("list_project_modules"),     TEXT("List every project + plugin module (deps, API macro, which is the primary game module)."), TEXT(""));
		Meta(TEXT("edit_module_deps"),         TEXT("Patch a module's .Build.cs public/private dependency arrays."), TEXT("module_name, add_public[], add_private[], remove_public[], remove_private[]"));
		Meta(TEXT("create_plugin"),            TEXT("Scaffold a full UE plugin (.uplugin + module + Build.cs + module boilerplate) and enable it in the uproject."), TEXT("name, module_type?, loading_phase?, public_dependencies?, private_dependencies?, plugin_dependencies?"));
		Meta(TEXT("add_plugin_module"),        TEXT("Add a second module to an existing plugin."), TEXT("plugin_name, module_name, module_type?, loading_phase?, public_dependencies?, private_dependencies?"));
		Meta(TEXT("add_plugin_dependency"),    TEXT("Add/update a UE plugin dependency in a plugin's .uplugin Plugins[] (idempotent)."), TEXT("plugin_name, dependency, enabled?"));
		Meta(TEXT("create_uecp_extension"),    TEXT("Scaffold a UECP extension (manifest + module + sample tool + docs) inside a host plugin — use this to add a new tool umbrella."), TEXT("extension_id, module_name, umbrella, host_plugin?, display_name?, ..."));
		Meta(TEXT("register_extension_tool"),  TEXT("Wire a new tool into an extension (manifest owned_tools + OwnedToolNames + RegisterHandler)."), TEXT("extension_root, tool_name, handler_namespace, handler_function, docs_section?"));
		Meta(TEXT("get_class_summary"),        TEXT("Parse a header into its classes/properties/functions/includes structure."), TEXT("file_path"));
		Meta(TEXT("find_class_definition"),    TEXT("Find which project/plugin header declares a class (engine classes: use search_engine_source)."), TEXT("class_name"));
		Meta(TEXT("read_cpp_file"),            TEXT("Read a C++ source file (.h/.cpp/.hpp/.c/.cc/.inl)."), TEXT("file_path"));
		Meta(TEXT("write_cpp_file"),           TEXT("Write a C++ file (validates UE conventions, auto-fixes lowercase reflection macros)."), TEXT("file_path, content"));
		Meta(TEXT("edit_cpp_file"),            TEXT("Pattern-replace text in a C++ file (read first to copy exact text)."), TEXT("file_path, old_text, new_text, replace_all?"));
		Meta(TEXT("check_project_source"),     TEXT("Probe whether the project has C++ source / project file set up."), TEXT(""));
		Meta(TEXT("compile_project"),          TEXT("Compile the project — auto (Live Coding hot-patch) / live / full (UBT relink, editor must be closed)."), TEXT("mode=auto|live|full"));
		Meta(TEXT("search_engine_source"),     TEXT("Regex scan engine/project .h files for class/struct/enum declarations matching a substring."), TEXT("pattern, scope?, max_results?"));
		Meta(TEXT("search_engine_api"),        TEXT("Find engine class methods by name — returns {class_name, method_name, signature, file, line}."), TEXT("pattern, class_filter?, scope?, max_results?, exact_match?"));
	}

	UE_LOG(LogUECPCppExt, Log, TEXT("Registered %d C++ tools (cpp_tools umbrella)"),
		OwnedToolNames().Num());
}

void FUECPCppExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPCppExtModule, UECPCppExt)
