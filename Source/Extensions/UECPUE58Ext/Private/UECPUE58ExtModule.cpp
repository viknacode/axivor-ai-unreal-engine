// Axivor AI — Unreal Engine 5.8 feature pack (umbrella `ue58`).
//
// Everything here either drives an engine API directly (physics asset generation,
// renderer settings, plugin enabling, PVE asset creation, GASP child character) or
// composes existing Axivor tools through the dispatcher (Control Rig physics, DMC)
// or runs editor Python (MetaHuman Character editor subsystem). No link-time
// dependency on experimental plugins: their classes are resolved by reflection so
// the module loads even when a plugin is disabled, and reports it cleanly.

#include "UECPUE58ExtModule.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/UECPToolSafety.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Engine/Engine.h"
#include "Engine/RendererSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsAssetUtils.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Factories/Factory.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "IPythonScriptPlugin.h"

DEFINE_LOG_CATEGORY(LogUECPUE58Ext);

namespace
{
	// ── JSON helpers ────────────────────────────────────────────────────────
	static FString ToJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		W->Close();
		return Out;
	}
	static TSharedPtr<FJsonObject> ParseJson(const FString& In)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(In);
		if (!FJsonSerializer::Deserialize(R, Obj)) return nullptr;
		return Obj;
	}
	static FString ArgStr(const TSharedPtr<FJsonObject>& A, const TCHAR* K, const FString& D = FString())
	{
		FString V; if (A.IsValid() && A->TryGetStringField(K, V)) return V; return D;
	}
	static bool ArgBool(const TSharedPtr<FJsonObject>& A, const TCHAR* K, bool D)
	{
		bool V = D; if (A.IsValid() && A->HasTypedField<EJson::Boolean>(K)) V = A->GetBoolField(K); return V;
	}
	static double ArgNum(const TSharedPtr<FJsonObject>& A, const TCHAR* K, double D)
	{
		double V = D; if (A.IsValid()) A->TryGetNumberField(K, V); return V;
	}
	static FUECPToolResult Fail(const FString& Msg) { FUECPToolResult R; R.bSuccess = false; R.ErrorMessage = Msg; return R; }
	static FUECPToolResult Ok(const TSharedRef<FJsonObject>& O) { FUECPToolResult R; R.bSuccess = true; R.ResultJson = ToJson(O); return R; }

	static IUECPToolDispatcher* Dispatcher()
	{
		return IUECPCoreModule::IsAvailable() ? &IUECPCoreModule::Get().GetToolDispatcher() : nullptr;
	}
	// Run another Axivor tool and hand back its parsed JSON (or an error string).
	static bool CallTool(const TCHAR* Tool, const TSharedRef<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutJson, FString& OutErr)
	{
		IUECPToolDispatcher* D = Dispatcher();
		if (!D) { OutErr = TEXT("Tool dispatcher unavailable."); return false; }
		if (!D->IsRegistered(FName(Tool))) { OutErr = FString::Printf(TEXT("Tool '%s' is not registered (is its extension enabled?)."), Tool); return false; }
		const FUECPToolResult R = D->ExecuteFromArgs(FName(Tool), Args);
		if (!R.bSuccess) { OutErr = FString::Printf(TEXT("%s: %s"), Tool, *R.ErrorMessage); return false; }
		OutJson = ParseJson(R.ResultJson);
		if (!OutJson.IsValid()) OutJson = MakeShared<FJsonObject>();
		OutJson->SetStringField(TEXT("_raw"), R.ResultJson.Left(2000));
		return true;
	}

	static bool IsPluginEnabled(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(Name);
		return P.IsValid() && P->IsEnabled();
	}
	static UClass* FindClassByPath(const TCHAR* Path) { return FindObject<UClass>(nullptr, Path); }

	// ── Python helper ────────────────────────────────────────────────────────
	static bool RunPython(const FString& Code, TArray<FString>& OutLog, FString& OutErr)
	{
		IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
		if (!Py || !Py->IsPythonAvailable()) { OutErr = TEXT("Python Editor Script Plugin is not available."); return false; }
		FPythonCommandEx Cmd;
		Cmd.Command = Code;
		Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Cmd.FileExecutionScope = EPythonFileExecutionScope::Private;
		const bool bOk = Py->ExecPythonCommandEx(Cmd);
		for (const FPythonLogOutputEntry& E : Cmd.LogOutput)
		{
			OutLog.Add(E.Output);
			if (E.Type == EPythonLogOutputType::Error) OutErr += E.Output + TEXT("\n");
		}
		if (!bOk && OutErr.IsEmpty()) OutErr = TEXT("Python command failed (see log).");
		return bOk;
	}

	// ── Renderer settings via reflection (ConsoleVariable meta) ─────────────
	static bool SetRendererProjectSetting(const FString& CVarName, const FString& Value, FString& OutInfo)
	{
		URendererSettings* RS = GetMutableDefault<URendererSettings>();
		if (!RS) return false;
		for (TFieldIterator<FProperty> It(URendererSettings::StaticClass()); It; ++It)
		{
			FProperty* P = *It;
			if (!P->HasMetaData(TEXT("ConsoleVariable"))) continue;
			if (!P->GetMetaData(TEXT("ConsoleVariable")).Equals(CVarName, ESearchCase::IgnoreCase)) continue;
			void* Ptr = P->ContainerPtrToValuePtr<void>(RS);
			bool bSet = false;
			if (FBoolProperty* BP = CastField<FBoolProperty>(P)) { BP->SetPropertyValue(Ptr, Value.ToBool() || Value == TEXT("1")); bSet = true; }
			else if (FIntProperty* IP = CastField<FIntProperty>(P)) { IP->SetPropertyValue(Ptr, FCString::Atoi(*Value)); bSet = true; }
			else if (FFloatProperty* FP = CastField<FFloatProperty>(P)) { FP->SetPropertyValue(Ptr, FCString::Atof(*Value)); bSet = true; }
			else if (FByteProperty* BYP = CastField<FByteProperty>(P))
			{
				if (BYP->Enum) { const int64 EV = BYP->Enum->GetValueByNameString(Value); BYP->SetPropertyValue(Ptr, (uint8)(EV != INDEX_NONE ? EV : FCString::Atoi(*Value))); }
				else BYP->SetPropertyValue(Ptr, (uint8)FCString::Atoi(*Value));
				bSet = true;
			}
			else if (FEnumProperty* EP = CastField<FEnumProperty>(P))
			{
				const int64 EV = EP->GetEnum()->GetValueByNameString(Value);
				EP->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, EV != INDEX_NONE ? EV : (int64)FCString::Atoi(*Value));
				bSet = true;
			}
			if (bSet)
			{
				RS->PostEditChange();
				RS->TryUpdateDefaultConfigFile();
				OutInfo = FString::Printf(TEXT("%s (project setting '%s')"), *CVarName, *P->GetName());
			}
			return bSet;
		}
		return false;
	}
	static bool SetLiveCVar(const FString& Name, const FString& Value)
	{
		IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!CV) return false;
		CV->Set(*Value, ECVF_SetByConsole);
		return true;
	}
	static FString GetCVar(const TCHAR* Name)
	{
		IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(Name);
		return CV ? CV->GetString() : TEXT("<missing>");
	}

	// ═════════════════════════════════════════════════════════════════════════
	// ue58_feature_status
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleFeatureStatus(const TSharedPtr<FJsonObject>&)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Plugins = MakeShared<FJsonObject>();
		static const TCHAR* Names[] = {
			TEXT("ProceduralVegetationEditor"), TEXT("ControlRigPhysics"), TEXT("ControlRigDynamics"), TEXT("DirectMeshControl"),
			TEXT("PhysicsControl"), TEXT("MetaHumanCharacter"), TEXT("MetaHumanCrowd"), TEXT("MetaHuman"), TEXT("PoseSearch"), TEXT("Chooser"),
			TEXT("Mover"), TEXT("MovieSceneAnimMixer"), TEXT("RigMapper"), TEXT("SkeletalMeshModelingTools"),
			TEXT("ModelContextProtocol"), TEXT("ToolsetRegistry"), TEXT("AllToolsets"), TEXT("FileSandbox"), TEXT("PythonScriptPlugin"), TEXT("PCG") };
		for (const TCHAR* N : Names) Plugins->SetBoolField(N, IsPluginEnabled(N));
		Out->SetObjectField(TEXT("plugins_enabled"), Plugins);

		TSharedRef<FJsonObject> Render = MakeShared<FJsonObject>();
		static const TCHAR* CVars[] = {
			TEXT("r.MegaLights.EnableForProject"), TEXT("r.MegaLights.Allowed"), TEXT("r.Substrate"), TEXT("r.Nanite.ProjectEnabled"), TEXT("r.Nanite.Foliage"),
			TEXT("r.Shadow.Virtual.Enable"), TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Lumen.HardwareRayTracing"),
			TEXT("r.Lumen.IrradianceFieldGather"), TEXT("r.Fog.ScreenSpaceScattering"), TEXT("r.Substrate.EnableLayerSupport"), TEXT("r.Substrate.OpaqueMaterialRoughRefraction") };
		for (const TCHAR* C : CVars) Render->SetStringField(C, GetCVar(C));
		Out->SetObjectField(TEXT("rendering"), Render);
		Out->SetStringField(TEXT("hint"), TEXT("Use ue58_enable_plugins to turn on missing plugins (restart required), render_apply_preset for 5.8 rendering features, get_tool_docs(category='ue58') for workflows."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// ue58_enable_plugins(plugins[])
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleEnablePlugins(const TSharedPtr<FJsonObject>& Args)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("plugins"), Arr) || !Arr || Arr->Num() == 0)
			return Fail(TEXT("Provide plugins[] — e.g. [\"ProceduralVegetationEditor\",\"ControlRigPhysics\",\"MetaHumanCharacter\"]."));
		const bool bEnable = ArgBool(Args, TEXT("enable"), true);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Results;
		bool bAnyChanged = false;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString Name; if (!V.IsValid() || !V->TryGetString(Name)) continue;
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("plugin"), Name);
			const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(Name);
			if (!P.IsValid()) { R->SetBoolField(TEXT("ok"), false); R->SetStringField(TEXT("error"), TEXT("plugin not found in engine/project")); Results.Add(MakeShared<FJsonValueObject>(R)); continue; }
			if (P->IsEnabled() == bEnable) { R->SetBoolField(TEXT("ok"), true); R->SetStringField(TEXT("state"), bEnable ? TEXT("already enabled") : TEXT("already disabled")); Results.Add(MakeShared<FJsonValueObject>(R)); continue; }
			FText FailReason;
			const bool bOk = IProjectManager::Get().SetPluginEnabled(Name, bEnable, FailReason);
			R->SetBoolField(TEXT("ok"), bOk);
			if (!bOk) R->SetStringField(TEXT("error"), FailReason.ToString()); else bAnyChanged = true;
			Results.Add(MakeShared<FJsonValueObject>(R));
		}
		if (bAnyChanged)
		{
			FText SaveFail;
			const bool bSaved = IProjectManager::Get().SaveCurrentProjectToDisk(SaveFail);
			Out->SetBoolField(TEXT("project_saved"), bSaved);
			if (!bSaved) Out->SetStringField(TEXT("save_error"), SaveFail.ToString());
			Out->SetBoolField(TEXT("restart_required"), true);
		}
		Out->SetArrayField(TEXT("results"), Results);
		Out->SetStringField(TEXT("message"), bAnyChanged ? TEXT("Plugin list updated in the .uproject. Restart the editor to load them.") : TEXT("No change."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// PVE — Procedural Vegetation Editor (graph == PCG graph)
	// ═════════════════════════════════════════════════════════════════════════
	static UFactory* FindFactoryForClass(UClass* Cls)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UFactory::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) continue;
			UFactory* CDO = It->GetDefaultObject<UFactory>();
			if (CDO && CDO->GetSupportedClass() == Cls && CDO->CanCreateNew())
				return NewObject<UFactory>(GetTransientPackage(), *It);
		}
		return nullptr;
	}

	static FUECPToolResult HandlePveCreateVegetation(const TSharedPtr<FJsonObject>& Args)
	{
		if (!IsPluginEnabled(TEXT("ProceduralVegetationEditor")))
			return Fail(TEXT("Plugin 'ProceduralVegetationEditor' is disabled. Call ue58_enable_plugins(plugins=['ProceduralVegetationEditor']) and restart."));
		UClass* Cls = FindClassByPath(TEXT("/Script/ProceduralVegetation.ProceduralVegetation"));
		if (!Cls) return Fail(TEXT("UProceduralVegetation class not loaded (plugin enabled but module missing?)."));
		const FString Name = ArgStr(Args, TEXT("name"), TEXT("PV_NewTree"));
		const FString Path = ArgStr(Args, TEXT("save_path"), TEXT("/Game/Vegetation"));
		if (!FPackageName::IsValidLongPackageName(Path / Name)) return Fail(FString::Printf(TEXT("Invalid content path '%s/%s'."), *Path, *Name));

		UFactory* Factory = FindFactoryForClass(Cls);
		IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UObject* Asset = AT.CreateAsset(Name, Path, Cls, Factory);
		if (!Asset) return Fail(TEXT("CreateAsset failed for ProceduralVegetation (factory returned null)."));

		FString GraphPath;
		if (FObjectProperty* GP = CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("Graph"))))
		{
			if (UObject* Graph = GP->GetObjectPropertyValue_InContainer(Asset)) GraphPath = Graph->GetPathName();
		}
		UEditorAssetLibrary::SaveAsset(Asset->GetPathName(), false);

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Out->SetStringField(TEXT("graph_path"), GraphPath);
		Out->SetStringField(TEXT("hint"), GraphPath.IsEmpty()
			? TEXT("Asset created; open it with pve_open_editor. The inner graph is created by the editor on first open.")
			: TEXT("Author the tree with the pcg umbrella on graph_path (add_pcg_node with PV* classes, e.g. PVSeedGeneratorSettings → PVGrowerSettings → PVMeshBuilderSettings → PVExportSettings). See get_tool_docs(category='ue58')."));
		return Ok(Out);
	}

	static FUECPToolResult HandlePveListNodes(const TSharedPtr<FJsonObject>& Args)
	{
		UClass* PCGSettings = FindClassByPath(TEXT("/Script/PCG.PCGSettings"));
		if (!PCGSettings) return Fail(TEXT("PCG plugin not loaded."));
		const FString Filter = ArgStr(Args, TEXT("filter")).ToLower();
		const bool bProps = ArgBool(Args, TEXT("include_properties"), true);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(PCGSettings) || It->HasAnyClassFlags(CLASS_Abstract)) continue;
			const FString Pkg = It->GetOutermost()->GetName();
			if (!Pkg.Contains(TEXT("ProceduralVegetation"))) continue;
			const FString CName = It->GetName();
			const FString Disp = It->GetDisplayNameText().ToString();
			if (!Filter.IsEmpty() && !CName.ToLower().Contains(Filter) && !Disp.ToLower().Contains(Filter)) continue;
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("class"), CName);
			O->SetStringField(TEXT("display_name"), Disp);
			O->SetStringField(TEXT("parent"), It->GetSuperClass() ? It->GetSuperClass()->GetName() : TEXT(""));
			if (bProps)
			{
				TArray<TSharedPtr<FJsonValue>> Props;
				for (TFieldIterator<FProperty> PIt(*It, EFieldIteratorFlags::IncludeSuper); PIt; ++PIt)
				{
					if (!PIt->HasAnyPropertyFlags(CPF_Edit)) continue;
					if (PIt->GetOwnerClass() == PCGSettings || (PIt->GetOwnerClass() && PIt->GetOwnerClass()->IsChildOf(PCGSettings) && !PIt->GetOwnerClass()->GetOutermost()->GetName().Contains(TEXT("ProceduralVegetation")))) continue;
					TSharedRef<FJsonObject> PO = MakeShared<FJsonObject>();
					PO->SetStringField(TEXT("name"), PIt->GetName());
					PO->SetStringField(TEXT("type"), PIt->GetCPPType());
					const FString Tip = PIt->GetToolTipText().ToString();
					if (!Tip.IsEmpty()) PO->SetStringField(TEXT("tooltip"), Tip.Left(160));
					Props.Add(MakeShared<FJsonValueObject>(PO));
				}
				O->SetArrayField(TEXT("properties"), Props);
			}
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("count"), Arr.Num());
		Out->SetArrayField(TEXT("nodes"), Arr);
		if (Arr.Num() == 0) Out->SetStringField(TEXT("hint"), TEXT("No PV* node classes loaded — enable the 'ProceduralVegetationEditor' plugin and restart."));
		else Out->SetStringField(TEXT("hint"), TEXT("Add with pcg.add_pcg_node(graph_path, settings_class=<class>), configure with set_pcg_node_property, wire with connect_pcg_nodes."));
		return Ok(Out);
	}

	static FUECPToolResult HandlePveOpenEditor(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Path = ArgStr(Args, TEXT("asset_path"));
		UObject* Asset = Path.IsEmpty() ? nullptr : UEditorAssetLibrary::LoadAsset(Path);
		if (!Asset) return Fail(FString::Printf(TEXT("Could not load asset '%s'."), *Path));
		if (!GEditor) return Fail(TEXT("Editor unavailable."));
		const bool bOk = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("opened"), bOk);
		Out->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Out->SetStringField(TEXT("hint"), TEXT("Generation/Export of Procedural Vegetation runs in its editor: ask the user to press Generate then Export (or use the Export node settings you configured)."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Control Rig Physics — instantiate a physics rig from a Physics Asset
	// ═════════════════════════════════════════════════════════════════════════
	static FString PickNodeName(const TSharedPtr<FJsonObject>& J)
	{
		if (!J.IsValid()) return FString();
		static const TCHAR* Keys[] = { TEXT("node_name"), TEXT("name"), TEXT("node"), TEXT("node_path") };
		for (const TCHAR* K : Keys) { FString V; if (J->TryGetStringField(K, V) && !V.IsEmpty()) return V; }
		return FString();
	}

	static FUECPToolResult HandleControlRigPhysicsFromPhysicsAsset(const TSharedPtr<FJsonObject>& Args)
	{
		if (!IsPluginEnabled(TEXT("ControlRigPhysics")))
			return Fail(TEXT("Plugin 'ControlRigPhysics' is disabled. Call ue58_enable_plugins(plugins=['ControlRigPhysics','PhysicsControl']) and restart."));
		const FString Rig = ArgStr(Args, TEXT("control_rig_path"));
		const FString PA  = ArgStr(Args, TEXT("physics_asset_path"));
		const FString Owner = ArgStr(Args, TEXT("owner_bone"), TEXT("root"));
		const FString Event = ArgStr(Args, TEXT("event"), TEXT("Construction"));
		if (Rig.IsEmpty() || PA.IsEmpty()) return Fail(TEXT("control_rig_path and physics_asset_path are required."));

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Steps;
		auto Step = [&Steps](const FString& What, bool bOk, const FString& Info)
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("step"), What); S->SetBoolField(TEXT("ok"), bOk); S->SetStringField(TEXT("info"), Info.Left(600));
			Steps.Add(MakeShared<FJsonValueObject>(S));
		};

		// 1. Solver node
		TSharedRef<FJsonObject> A1 = MakeShared<FJsonObject>();
		A1->SetStringField(TEXT("asset_path"), Rig);
		A1->SetStringField(TEXT("unit_struct_path"), TEXT("/Script/ControlRigPhysics.RigUnit_AddPhysicsSolver"));
		A1->SetStringField(TEXT("event"), Event);
		A1->SetNumberField(TEXT("position_x"), 200); A1->SetNumberField(TEXT("position_y"), 0);
		TSharedPtr<FJsonObject> R1; FString E1;
		const bool bSolver = CallTool(TEXT("add_rig_vm_node"), A1, R1, E1);
		const FString SolverNode = PickNodeName(R1);
		Step(TEXT("add RigUnit_AddPhysicsSolver"), bSolver, bSolver ? SolverNode : E1);

		// 2. Instantiate-from-physics-asset node
		TSharedRef<FJsonObject> A2 = MakeShared<FJsonObject>();
		A2->SetStringField(TEXT("asset_path"), Rig);
		A2->SetStringField(TEXT("unit_struct_path"), TEXT("/Script/ControlRigPhysics.RigUnit_HierarchyInstantiateFromPhysicsAsset"));
		A2->SetStringField(TEXT("event"), Event);
		A2->SetNumberField(TEXT("position_x"), 600); A2->SetNumberField(TEXT("position_y"), 0);
		TSharedPtr<FJsonObject> R2; FString E2;
		const bool bInst = CallTool(TEXT("add_rig_vm_node"), A2, R2, E2);
		const FString InstNode = PickNodeName(R2);
		Step(TEXT("add RigUnit_HierarchyInstantiateFromPhysicsAsset"), bInst, bInst ? InstNode : E2);

		// 3. Pin defaults + wiring (best effort — reported per step)
		if (bSolver && !SolverNode.IsEmpty())
		{
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("asset_path"), Rig); P->SetStringField(TEXT("event"), Event);
			P->SetStringField(TEXT("pin_path"), SolverNode + TEXT(".Owner"));
			P->SetStringField(TEXT("pin_value"), FString::Printf(TEXT("(Type=Bone,Name=\"%s\")"), *Owner));
			TSharedPtr<FJsonObject> RP; FString EP;
			Step(TEXT("set Solver.Owner"), CallTool(TEXT("set_rig_pin_value"), P, RP, EP), EP);
		}
		if (bInst && !InstNode.IsEmpty())
		{
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("asset_path"), Rig); P->SetStringField(TEXT("event"), Event);
			P->SetStringField(TEXT("pin_path"), InstNode + TEXT(".PhysicsAsset"));
			P->SetStringField(TEXT("pin_value"), PA);
			TSharedPtr<FJsonObject> RP; FString EP;
			Step(TEXT("set Instantiate.PhysicsAsset"), CallTool(TEXT("set_rig_pin_value"), P, RP, EP), EP);
		}
		if (bSolver && bInst && !SolverNode.IsEmpty() && !InstNode.IsEmpty())
		{
			TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("asset_path"), Rig); C->SetStringField(TEXT("event"), Event);
			C->SetStringField(TEXT("source_pin_path"), SolverNode + TEXT(".PhysicsSolverComponentKey"));
			C->SetStringField(TEXT("target_pin_path"), InstNode + TEXT(".PhysicsSolverComponentKey"));
			TSharedPtr<FJsonObject> RC; FString EC;
			Step(TEXT("connect solver key → instantiate"), CallTool(TEXT("connect_rig_pins"), C, RC, EC), EC);
			TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
			X->SetStringField(TEXT("asset_path"), Rig); X->SetStringField(TEXT("event"), Event);
			X->SetStringField(TEXT("source_pin_path"), SolverNode + TEXT(".ExecuteContext"));
			X->SetStringField(TEXT("target_pin_path"), InstNode + TEXT(".ExecuteContext"));
			TSharedPtr<FJsonObject> RX; FString EX;
			Step(TEXT("connect execute chain"), CallTool(TEXT("connect_rig_pins"), X, RX, EX), EX);
		}
		Out->SetArrayField(TEXT("steps"), Steps);
		Out->SetStringField(TEXT("next"), TEXT("Wire the event's ExecuteContext into the solver node if not already, compile with compile_control_rig, then add RigUnit_AddPhysicsControl / RigUnit_HierarchyAddPhysicsBodyForce nodes for control and forces (see get_tool_docs(category='ue58'))."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// DMC — Direct Mesh Control component on a Blueprint
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleDmcAddComponent(const TSharedPtr<FJsonObject>& Args)
	{
		if (!IsPluginEnabled(TEXT("DirectMeshControl")))
			return Fail(TEXT("Plugin 'DirectMeshControl' is disabled. Call ue58_enable_plugins(plugins=['DirectMeshControl']) and restart."));
		const FString BP = ArgStr(Args, TEXT("blueprint_path"));
		const FString CompName = ArgStr(Args, TEXT("component_name"), TEXT("DirectMeshControl"));
		const FString Mesh = ArgStr(Args, TEXT("skeletal_mesh_path"));
		if (BP.IsEmpty()) return Fail(TEXT("blueprint_path is required."));
		TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BP);
		A->SetStringField(TEXT("component_class"), TEXT("DirectMeshControlComponent"));
		A->SetStringField(TEXT("component_name"), CompName);
		const FString Attach = ArgStr(Args, TEXT("attach_to"));
		if (!Attach.IsEmpty()) A->SetStringField(TEXT("attach_to"), Attach);
		TSharedPtr<FJsonObject> R; FString E;
		if (!CallTool(TEXT("add_component"), A, R, E)) return Fail(E);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("component_added"), true);
		if (!Mesh.IsEmpty())
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("blueprint_path"), BP); S->SetStringField(TEXT("component_name"), CompName);
			S->SetStringField(TEXT("property_name"), TEXT("SkeletalMeshAsset")); S->SetStringField(TEXT("value"), Mesh);
			TSharedPtr<FJsonObject> R2; FString E2;
			Out->SetBoolField(TEXT("mesh_assigned"), CallTool(TEXT("set_component_property"), S, R2, E2));
			if (!E2.IsEmpty()) Out->SetStringField(TEXT("mesh_error"), E2);
		}
		Out->SetStringField(TEXT("hint"), TEXT("DMC lets the user click-and-drag mesh regions in the viewport/Sequencer; controls come from the Control Rig assigned to this component (set its Anim Class or Control Rig via set_component_property)."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// MetaHuman — editor subsystem through Python (BlueprintCallable API)
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleMetaHumanApi(const TSharedPtr<FJsonObject>& Args)
	{
		if (!IsPluginEnabled(TEXT("MetaHumanCharacter")))
			return Fail(TEXT("Plugin 'MetaHumanCharacter' is disabled. Call ue58_enable_plugins(plugins=['MetaHumanCharacter']) and restart."));
		const FString Action = ArgStr(Args, TEXT("action"), TEXT("probe")).ToLower();
		const FString Ch = ArgStr(Args, TEXT("character_path"));
		FString Code;
		if (Action == TEXT("probe"))
		{
			Code = TEXT(
				"import unreal, json\n"
				"sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)\n"
				"names = [n for n in dir(sub) if not n.startswith('_')]\n"
				"structs = {}\n"
				"for sn in ['ConformTargetParams','MetaHumanCharacterTargetMeshKey','MetaHumanCharacterEditorBuildParameters']:\n"
				"    st = getattr(unreal, sn, None)\n"
				"    if st is not None:\n"
				"        try:\n"
				"            structs[sn] = [p for p in dir(st()) if not p.startswith('_') and not callable(getattr(st(), p))]\n"
				"        except Exception as e:\n"
				"            structs[sn] = str(e)\n"
				"print('AXJSON' + json.dumps({'subsystem_methods': names, 'structs': structs}))\n");
		}
		else if (Action == TEXT("can_build") || Action == TEXT("build"))
		{
			if (Ch.IsEmpty()) return Fail(TEXT("character_path is required."));
			const FString Quality = ArgStr(Args, TEXT("quality"), TEXT("Cinematic"));
			const FString Pipeline = ArgStr(Args, TEXT("pipeline"), TEXT("Cinematic"));
			Code = FString::Printf(TEXT(
				"import unreal, json\n"
				"sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)\n"
				"ch = unreal.load_asset('%s')\n"
				"res = {'character': '%s'}\n"
				"ok, msg = True, ''\n"
				"try:\n"
				"    r = sub.can_build_meta_human(ch)\n"
				"    if isinstance(r, tuple): ok, msg = bool(r[0]), str(r[1])\n"
				"    else: ok = bool(r)\n"
				"except Exception as e:\n"
				"    ok, msg = False, str(e)\n"
				"res['can_build'] = ok; res['message'] = msg\n"
				"if ok and '%s' == 'build':\n"
				"    p = unreal.MetaHumanCharacterEditorBuildParameters()\n"
				"    try: p.pipeline_quality = getattr(unreal.MetaHumanQualityLevel, '%s'.upper(), p.pipeline_quality)\n"
				"    except Exception as e: res['quality_warning'] = str(e)\n"
				"    try: p.pipeline_type = getattr(unreal.MetaHumanDefaultPipelineType, '%s'.upper(), p.pipeline_type)\n"
				"    except Exception as e: res['pipeline_warning'] = str(e)\n"
				"    sub.build_meta_human(ch, p); res['build_requested'] = True\n"
				"print('AXJSON' + json.dumps(res))\n"), *Ch, *Ch, *Action, *Quality, *Pipeline);
		}
		else if (Action == TEXT("export_dna"))
		{
			if (Ch.IsEmpty()) return Fail(TEXT("character_path is required."));
			const FString OutPath = ArgStr(Args, TEXT("output_path"));
			if (OutPath.IsEmpty()) return Fail(TEXT("output_path (absolute .dna file path) is required."));
			Code = FString::Printf(TEXT(
				"import unreal, json\n"
				"sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)\n"
				"ch = unreal.load_asset('%s')\n"
				"r = sub.export_dna(ch, r'%s')\n"
				"print('AXJSON' + json.dumps({'exported': bool(r), 'path': r'%s'}))\n"), *Ch, *OutPath, *OutPath);
		}
		else if (Action == TEXT("conform_body_from_mesh"))
		{
			if (Ch.IsEmpty()) return Fail(TEXT("character_path is required."));
			const FString Mesh = ArgStr(Args, TEXT("mesh_path"));
			if (Mesh.IsEmpty()) return Fail(TEXT("mesh_path (static or skeletal mesh) is required."));
			const bool bAPose = ArgBool(Args, TEXT("target_is_a_pose"), false);
			Code = FString::Printf(TEXT(
				"import unreal, json\n"
				"sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)\n"
				"ch = unreal.load_asset('%s'); mesh = unreal.load_asset('%s')\n"
				"res = {}\n"
				"try:\n"
				"    verts, tris = unreal.MetaHumanCharacterEditorSubsystem.get_mesh_data_for_conforming(mesh)\n"
				"    res['vertices'] = len(verts)\n"
				"    ok = sub.conform_body_to_target(ch, verts, [], %s, True)\n"
				"    res['conformed'] = bool(ok)\n"
				"except Exception as e:\n"
				"    res['error'] = str(e)\n"
				"print('AXJSON' + json.dumps(res))\n"), *Ch, *Mesh, bAPose ? TEXT("True") : TEXT("False"));
		}
		else if (Action == TEXT("python"))
		{
			Code = ArgStr(Args, TEXT("code"));
			if (Code.IsEmpty()) return Fail(TEXT("code is required for action='python'."));
		}
		else return Fail(TEXT("Unknown action. Use: probe | can_build | build | export_dna | conform_body_from_mesh | python."));

		TArray<FString> Log; FString Err;
		const bool bOk = RunPython(Code, Log, Err);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("python_ok"), bOk);
		for (const FString& L : Log)
		{
			const int32 I = L.Find(TEXT("AXJSON"));
			if (I != INDEX_NONE)
			{
				if (TSharedPtr<FJsonObject> J = ParseJson(L.Mid(I + 6))) { Out->SetObjectField(TEXT("result"), J); }
			}
		}
		TArray<TSharedPtr<FJsonValue>> LogArr;
		for (const FString& L : Log) if (L.Find(TEXT("AXJSON")) == INDEX_NONE) LogArr.Add(MakeShared<FJsonValueString>(L.Left(400)));
		Out->SetArrayField(TEXT("log"), LogArr);
		if (!bOk) return Fail(Err.IsEmpty() ? TEXT("MetaHuman python call failed.") : Err);
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Rendering — 5.8 presets, cvars, state
	// ═════════════════════════════════════════════════════════════════════════
	static void ApplyOne(const FString& CVar, const FString& Value, bool bProject, TArray<TSharedPtr<FJsonValue>>& Report)
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("cvar"), CVar); R->SetStringField(TEXT("value"), Value);
		FString Info;
		const bool bLive = SetLiveCVar(CVar, Value);
		const bool bProj = bProject ? SetRendererProjectSetting(CVar, Value, Info) : false;
		R->SetBoolField(TEXT("live"), bLive); R->SetBoolField(TEXT("project_setting"), bProj);
		if (!Info.IsEmpty()) R->SetStringField(TEXT("info"), Info);
		if (!bLive && !bProj) R->SetStringField(TEXT("error"), TEXT("cvar not found and no matching project setting"));
		Report.Add(MakeShared<FJsonValueObject>(R));
	}

	static FUECPToolResult HandleRenderApplyPreset(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Preset = ArgStr(Args, TEXT("preset")).ToLower();
		const bool bProject = ArgBool(Args, TEXT("project_wide"), true);
		TArray<TPair<FString, FString>> Set;
		FString Notes;
		if (Preset == TEXT("megalights") || Preset == TEXT("megalights_60fps"))
		{
			Set = { {TEXT("r.MegaLights.EnableForProject"), TEXT("1")}, {TEXT("r.MegaLights.Allowed"), TEXT("1")}, {TEXT("r.Shadow.Virtual.Enable"), TEXT("1")}, {TEXT("r.Nanite.ProjectEnabled"), TEXT("1")} };
			Notes = TEXT("MegaLights (production-ready in 5.8) needs Virtual Shadow Maps + Nanite; lights become unlimited-shadowed at 60fps targets. Use r.MegaLights.Debug 1 / the Light Finder for diagnostics.");
		}
		else if (Preset == TEXT("megalights_translucency"))
		{
			Set = { {TEXT("r.MegaLights.FrontLayerTranslucency.EnableForProject"), TEXT("1")} };
			Notes = TEXT("High-quality translucency lighting under MegaLights (front layer).");
		}
		else if (Preset == TEXT("lumen_lite"))
		{
			Set = { {TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1")}, {TEXT("r.Lumen.IrradianceFieldGather"), TEXT("1")}, {TEXT("r.Lumen.IrradianceFieldGather.AdaptivePlacement"), TEXT("1")} };
			Notes = TEXT("Lumen Lite (beta): Irradiance-Field gather with probe occlusion, ~2x faster than Lumen high, default on handhelds. Toggle back with r.Lumen.IrradianceFieldGather 0.");
		}
		else if (Preset == TEXT("substrate"))
		{
			Set = { {TEXT("r.Substrate"), TEXT("1")} };
			Notes = TEXT("Substrate materials (restart + shader recompile). Enable layer support with r.Substrate.EnableLayerSupport 1 if you need material layers.");
		}
		else if (Preset == TEXT("substrate_npr") || Preset == TEXT("toon"))
		{
			Set = { {TEXT("r.Substrate"), TEXT("1")}, {TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse"), TEXT("1")}, {TEXT("r.Substrate.Experimental.ToonReflectionQuantizationEnabled"), TEXT("1")} };
			Notes = TEXT("Substrate NPR (experimental): Toon BSDF + Toon Profile assets (ramp-based diffuse/specular). Create a ToonProfile asset with create_asset and reference it from Substrate Toon materials.");
		}
		else if (Preset == TEXT("fsss") || Preset == TEXT("fog_scattering"))
		{
			Set = { {TEXT("r.Fog.ScreenSpaceScattering"), TEXT("1")}, {TEXT("r.Fog.ScreenSpaceScattering.TAA"), TEXT("1")} };
			Notes = TEXT("Fog Screen Space Scattering (experimental): multiple scattering approximation for volumetric fog / local fog volumes.");
		}
		else if (Preset == TEXT("nanite_foliage"))
		{
			Set = { {TEXT("r.Nanite.ProjectEnabled"), TEXT("1")}, {TEXT("r.Nanite.Foliage"), TEXT("1")} };
			Notes = TEXT("Nanite Foliage (experimental): required for PVE-exported trees with 'Create Nanite Foliage'. Meshes need Nanite enabled + preserve-area/voxel settings.");
		}
		else if (Preset == TEXT("hwrt_lumen"))
		{
			Set = { {TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1")}, {TEXT("r.ReflectionMethod"), TEXT("1")}, {TEXT("r.Lumen.HardwareRayTracing"), TEXT("1")} };
			Notes = TEXT("Lumen GI + reflections with hardware ray tracing when available.");
		}
		else return Fail(TEXT("Unknown preset. Use: megalights | megalights_translucency | lumen_lite | substrate | substrate_npr | fsss | nanite_foliage | hwrt_lumen."));

		TArray<TSharedPtr<FJsonValue>> Report;
		for (const TPair<FString, FString>& KV : Set) ApplyOne(KV.Key, KV.Value, bProject, Report);
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("preset"), Preset);
		Out->SetArrayField(TEXT("applied"), Report);
		Out->SetStringField(TEXT("notes"), Notes);
		Out->SetBoolField(TEXT("restart_recommended"), bProject);
		return Ok(Out);
	}

	static FUECPToolResult HandleRenderSetCVars(const TSharedPtr<FJsonObject>& Args)
	{
		const TSharedPtr<FJsonObject>* Map = nullptr;
		if (!Args.IsValid() || !Args->TryGetObjectField(TEXT("cvars"), Map) || !Map || !Map->IsValid())
			return Fail(TEXT("Provide cvars{} — e.g. {\"r.MegaLights.Allowed\":1,\"r.Fog.ScreenSpaceScattering\":1}."));
		const bool bProject = ArgBool(Args, TEXT("project_wide"), false);
		TArray<TSharedPtr<FJsonValue>> Report;
		for (const auto& KV : (*Map)->Values)
		{
			FString V;
			if (!KV.Value.IsValid()) continue;
			if (KV.Value->Type == EJson::Boolean) V = KV.Value->AsBool() ? TEXT("1") : TEXT("0");
			else if (KV.Value->Type == EJson::Number) V = FString::SanitizeFloat(KV.Value->AsNumber());
			else V = KV.Value->AsString();
			ApplyOne(FString(KV.Key), V, bProject, Report);
		}
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetArrayField(TEXT("applied"), Report);
		return Ok(Out);
	}

	static FUECPToolResult HandleRenderGetState(const TSharedPtr<FJsonObject>& Args)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		TArray<FString> List;
		if (Args.IsValid() && Args->TryGetArrayField(TEXT("cvars"), Names) && Names)
			for (const TSharedPtr<FJsonValue>& V : *Names) { FString S; if (V.IsValid() && V->TryGetString(S)) List.Add(S); }
		if (List.Num() == 0)
			List = { TEXT("r.MegaLights.EnableForProject"), TEXT("r.MegaLights.Allowed"), TEXT("r.Substrate"), TEXT("r.Nanite.ProjectEnabled"), TEXT("r.Nanite.Foliage"), TEXT("r.Shadow.Virtual.Enable"),
			         TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Lumen.HardwareRayTracing"), TEXT("r.Lumen.IrradianceFieldGather"), TEXT("r.Fog.ScreenSpaceScattering"),
			         TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse"), TEXT("r.AntiAliasingMethod"), TEXT("r.ScreenPercentage") };
		for (const FString& N : List) Out->SetStringField(N, GetCVar(*N));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Physics Asset — one-call generation for fast iteration
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandlePhysicsAssetGenerate(const TSharedPtr<FJsonObject>& Args)
	{
		const FString MeshPath = ArgStr(Args, TEXT("skeletal_mesh_path"));
		if (MeshPath.IsEmpty()) return Fail(TEXT("skeletal_mesh_path is required."));
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
		if (!Mesh) return Fail(FString::Printf(TEXT("Could not load SkeletalMesh '%s'."), *MeshPath));

		FString PAPath = ArgStr(Args, TEXT("physics_asset_path"));
		UPhysicsAsset* PA = nullptr;
		bool bCreated = false;
		if (!PAPath.IsEmpty() && UEditorAssetLibrary::DoesAssetExist(PAPath))
		{
			PA = Cast<UPhysicsAsset>(UEditorAssetLibrary::LoadAsset(PAPath));
			if (!PA) return Fail(FString::Printf(TEXT("'%s' exists but is not a PhysicsAsset."), *PAPath));
		}
		else
		{
			FString Name, Folder;
			if (PAPath.IsEmpty())
			{
				Folder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
				Name = TEXT("PHYS_") + Mesh->GetName();
			}
			else
			{
				Folder = FPackageName::GetLongPackagePath(PAPath);
				Name = FPackageName::GetShortName(PAPath);
				int32 Dot = INDEX_NONE; if (Name.FindChar(TEXT('.'), Dot)) Name = Name.Left(Dot);
			}
			const FString PkgName = Folder / Name;
			if (!FPackageName::IsValidLongPackageName(PkgName)) return Fail(FString::Printf(TEXT("Invalid asset path '%s'."), *PkgName));
			UPackage* Pkg = CreatePackage(*PkgName);
			Pkg->FullyLoad();
			PA = NewObject<UPhysicsAsset>(Pkg, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(PA);
			bCreated = true;
			PAPath = PA->GetPathName();
		}

		FPhysAssetCreateParams Params;
		Params.MinBoneSize = (float)ArgNum(Args, TEXT("min_bone_size"), 20.0);
		const FString Geom = ArgStr(Args, TEXT("geometry"), TEXT("capsule")).ToLower();
		if      (Geom == TEXT("sphere"))          Params.GeomType = EFG_Sphere;
		else if (Geom == TEXT("box"))             Params.GeomType = EFG_Box;
		else if (Geom == TEXT("convex"))          Params.GeomType = EFG_SingleConvexHull;
		else if (Geom == TEXT("multi_convex"))    Params.GeomType = EFG_MultiConvexHull;
		else if (Geom == TEXT("tapered_capsule")) Params.GeomType = EFG_TaperedCapsule;
		else                                      Params.GeomType = EFG_Sphyl;
		Params.VertWeight = ArgStr(Args, TEXT("vert_weight"), TEXT("dominant")).ToLower() == TEXT("any") ? EVW_AnyWeight : EVW_DominantWeight;
		Params.bAutoOrientToBone = ArgBool(Args, TEXT("orient_to_bone"), true);
		Params.bCreateConstraints = ArgBool(Args, TEXT("create_constraints"), true);
		Params.bWalkPastSmall = ArgBool(Args, TEXT("walk_past_small"), true);
		Params.bBodyForAll = ArgBool(Args, TEXT("body_for_all"), false);
		Params.bDisableCollisionsByDefault = ArgBool(Args, TEXT("disable_collisions_by_default"), true);
		Params.HullCount = (int32)ArgNum(Args, TEXT("hull_count"), 4);
		Params.MaxHullVerts = (int32)ArgNum(Args, TEXT("max_hull_verts"), 16);
		Params.LodIndex = (int32)ArgNum(Args, TEXT("lod_index"), 0);
		const FString Angular = ArgStr(Args, TEXT("angular_constraint"), TEXT("limited")).ToLower();
		Params.AngularConstraintMode = Angular == TEXT("free") ? ACM_Free : (Angular == TEXT("locked") ? ACM_Locked : ACM_Limited);

		PA->Modify();
		FText Err;
		const bool bOk = FPhysicsAssetUtils::CreateFromSkeletalMesh(PA, Mesh, Params, Err, ArgBool(Args, TEXT("assign_to_mesh"), true), /*bShowProgress*/ false);
		if (!bOk) return Fail(FString::Printf(TEXT("CreateFromSkeletalMesh failed: %s"), *Err.ToString()));

		PA->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(PAPath, false);
		if (ArgBool(Args, TEXT("assign_to_mesh"), true)) { Mesh->MarkPackageDirty(); UEditorAssetLibrary::SaveAsset(MeshPath, false); }

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("physics_asset_path"), PAPath);
		Out->SetBoolField(TEXT("created"), bCreated);
		Out->SetNumberField(TEXT("bodies"), PA->SkeletalBodySetups.Num());
		Out->SetNumberField(TEXT("constraints"), PA->ConstraintSetup.Num());
		Out->SetStringField(TEXT("geometry"), Geom);
		Out->SetStringField(TEXT("next"), TEXT("Iterate with physics_asset_set_capsule/box/sphere, physics_asset_set_constraint_limits, set_physics_body_properties; re-run this tool with different min_bone_size/geometry to regenerate from scratch (bodies are rebuilt)."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Game Animation Sample (5.8)
	// ═════════════════════════════════════════════════════════════════════════
	static FUECPToolResult HandleGaspGuide(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Topic = ArgStr(Args, TEXT("topic"), TEXT("overview")).ToLower();
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("topic"), Topic);
		TArray<TSharedPtr<FJsonValue>> Steps;
		auto S = [&Steps](const TCHAR* T) { Steps.Add(MakeShared<FJsonValueString>(T)); };
		if (Topic == TEXT("retarget") || Topic == TEXT("add_character"))
		{
			S(TEXT("1. Import your SkeletalMesh; create an IK Rig for it (asset_management create_asset IKRig + ik_retarget.add_retarget_chain for spine/neck/head/arms/legs; auto-map works for humanoids)."));
			S(TEXT("2. Create an IK Retargeter: source = UEFN_Mannequin IK Rig (Content/Characters/UEFN_Mannequin), target = your IK Rig. Name it RTG_UEFN_to_<YourMesh>. 5.8: define Foot Definition (foot plane + toes) and use Retarget Override Sets when several characters share one retargeter."));
			S(TEXT("3. In ABP_GenericRetarget add your retargeter to the IKRetargeter_Map array (gasp_add_character does this by reflection when retargeter_path is given)."));
			S(TEXT("4. Create a child of CBP_SandboxCharacter named CBP_Sandbox_<YourName>; hide the original mesh; add a SkeletalMeshComponent child with your mesh, AnimClass=ABP_SandboxCharacter... (gasp_add_character automates 3-5)."));
			S(TEXT("5. Add Component Tag RTG_UEFN_to_<YourMesh> on the new mesh component (this is how ABP_GenericRetarget picks the retargeter)."));
			S(TEXT("6. Optional: add a character icon button in Content/Widgets/Game Animation Widget pointing to the new blueprint."));
		}
		else if (Topic == TEXT("motion_matching"))
		{
			S(TEXT("ABP_SandboxCharacter AnimGraph: Pose History (trajectory) → Motion Matching node fed by CHT_PoseSearchDatabases (Chooser with a Pose Match column selecting among several Pose Search Databases) → Simple Additive Lean blendspace → Aim Offset → Default Slot (traversal montages) → Offset Root Bone → Leg IK."));
			S(TEXT("Databases live in Content/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases; schema channels: trajectory (positions/facing at sample times), pose (feet/pelvis bones), plus 5.8 Pose Search Interaction Assets for multi-character interactions (Motion Match Multi)."));
			S(TEXT("Key AnimNode functions: Update_MotionMatching / Update_MotionMatching_PostSelection; BP functions GenerateTrajectory, UpdateStates, IsMoving/IsStarting/IsPivoting, Get_MMBlendTime, Get_OffsetRoot*Mode."));
			S(TEXT("Axivor tools: pose_search (create_pose_search_schema, add_pose_search_channel, create_pose_search_database, add_pose_search_database_entry, build_pose_search_database), chooser (create_chooser_table, add_chooser_column/row), animation.add_motion_matching_node."));
		}
		else if (Topic == TEXT("mover") || Topic == TEXT("ragdoll"))
		{
			S(TEXT("5.8 adds SandboxCharacter_Mover_Ragdoll: powered ragdoll via Physics Control Component with animation states (fall protection, injury, flail, roll) and motion-matched get-up that blends back to animated. Mover pawn's blend stack is driven by a Chooser with a Pose Match column and internal Pose Search Databases; blend spaces (slopes) can be used inside the Chooser/Blend Stack."));
			S(TEXT("Use mover umbrella tools for Mover setups, gas/physics tools + PhysicsControl component for powered ragdoll, chooser tools for the state chooser."));
		}
		else if (Topic == TEXT("traversal"))
		{
			S(TEXT("Traversal (vault/mantle/hurdle) = TraversableObstacle interface on level actors + TryTraversalAction in the character (trace front/back ledges) + chooser CHT_TraversalAnims selecting montages by obstacle height/depth + Motion Warping windows in the montages. Play through the Default Slot; Offset Root Bone releases during montages."));
			S(TEXT("5.8 also ships Smart Object bench sitting via State Tree tasks — see state_tree tools."));
		}
		else
		{
			S(TEXT("Game Animation Sample (GASP, UE 5.8): motion-matching locomotion (500+ animations), traversal, Mover and CharacterMovement variants, MetaHuman bodies (CBP_Sandbox_MetaHuman_Bodies), powered ragdoll (Mover), Smart Object interactions, multi-character Pose Search Interaction Assets."));
			S(TEXT("Key assets: CBP_SandboxCharacter (CharacterMovement), SandboxCharacter_Mover / SandboxCharacter_Mover_Ragdoll, ABP_SandboxCharacter, ABP_GenericRetarget (IKRetargeter_Map), CHT_PoseSearchDatabases, Content/Characters/UEFN_Mannequin/Animations/MotionMatchingData/."));
			S(TEXT("Topics: retarget | motion_matching | mover | traversal. Download: Fab 'Game Animation Sample' (updated for 5.8)."));
		}
		Out->SetArrayField(TEXT("steps"), Steps);
		return Ok(Out);
	}

	static UBlueprint* FindBlueprintByName(const FString& Name)
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter F;
		F.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		F.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
		F.bRecursiveClasses = true;
		TArray<FAssetData> Found;
		AR.GetAssets(F, Found);
		for (const FAssetData& D : Found)
			if (D.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase)) return Cast<UBlueprint>(D.GetAsset());
		return nullptr;
	}

	static FUECPToolResult HandleGaspAddCharacter(const TSharedPtr<FJsonObject>& Args)
	{
		const FString MeshPath = ArgStr(Args, TEXT("skeletal_mesh_path"));
		if (MeshPath.IsEmpty()) return Fail(TEXT("skeletal_mesh_path is required."));
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
		if (!Mesh) return Fail(FString::Printf(TEXT("Could not load SkeletalMesh '%s'."), *MeshPath));

		const FString ParentPath = ArgStr(Args, TEXT("parent_blueprint_path"));
		UBlueprint* Parent = ParentPath.IsEmpty() ? nullptr : Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(ParentPath));
		if (!Parent) Parent = FindBlueprintByName(TEXT("CBP_SandboxCharacter"));
		if (!Parent) Parent = FindBlueprintByName(TEXT("CBP_Sandbox_Character"));
		if (!Parent || !Parent->GeneratedClass) return Fail(TEXT("Could not find the GASP parent character (CBP_SandboxCharacter). Pass parent_blueprint_path or migrate the Game Animation Sample into this project first."));

		const FString AnimBPPath = ArgStr(Args, TEXT("anim_blueprint_path"));
		UBlueprint* AnimBP = AnimBPPath.IsEmpty() ? nullptr : Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
		if (!AnimBP) AnimBP = FindBlueprintByName(TEXT("ABP_SandboxCharacter"));

		const FString CharName = ArgStr(Args, TEXT("name"), TEXT("CBP_Sandbox_") + Mesh->GetName());
		const FString Folder = ArgStr(Args, TEXT("save_path"), FPackageName::GetLongPackagePath(Parent->GetOutermost()->GetName()));
		const FString Tag = ArgStr(Args, TEXT("retarget_tag"), TEXT("RTG_UEFN_to_") + Mesh->GetName());
		const FString RetargeterPath = ArgStr(Args, TEXT("retargeter_path"));

		const FString PkgName = Folder / CharName;
		if (!FPackageName::IsValidLongPackageName(PkgName)) return Fail(FString::Printf(TEXT("Invalid path '%s'."), *PkgName));
		if (UEditorAssetLibrary::DoesAssetExist(PkgName)) return Fail(FString::Printf(TEXT("Asset '%s' already exists."), *PkgName));

		UPackage* Pkg = CreatePackage(*PkgName);
		Pkg->FullyLoad();
		UBlueprint* Child = FKismetEditorUtilities::CreateBlueprint(Parent->GeneratedClass, Pkg, *CharName, BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("AxivorGASP")));
		if (!Child) return Fail(TEXT("CreateBlueprint failed."));
		FAssetRegistryModule::AssetCreated(Child);

		// Retarget mesh component under the character's native Mesh.
		USimpleConstructionScript* SCS = Child->SimpleConstructionScript;
		if (!SCS) return Fail(TEXT("Child blueprint has no SimpleConstructionScript."));
		USCS_Node* Node = SCS->CreateNode(USkeletalMeshComponent::StaticClass(), TEXT("RetargetMesh"));
		USkeletalMeshComponent* Tmpl = Cast<USkeletalMeshComponent>(Node->ComponentTemplate);
		if (Tmpl)
		{
			Tmpl->SetSkeletalMeshAsset(Mesh);
			if (AnimBP && AnimBP->GeneratedClass) Tmpl->SetAnimInstanceClass(AnimBP->GeneratedClass);
			Tmpl->ComponentTags.AddUnique(FName(*Tag));
			Tmpl->SetRelativeLocation(FVector(0, 0, -90.f));
			Tmpl->SetRelativeRotation(FRotator(0, -90.f, 0));
		}
		SCS->AddNode(Node);
		if (ACharacter* ParentCDO = Cast<ACharacter>(Parent->GeneratedClass->GetDefaultObject()))
		{
			if (USkeletalMeshComponent* ParentMesh = ParentCDO->GetMesh()) Node->SetParent(ParentMesh);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Child);
		FCompilerResultsLog Results; Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(Child, EBlueprintCompileOptions::None, &Results);

		// Hide the inherited mannequin mesh on the child's CDO.
		bool bHidOriginal = false;
		if (Child->GeneratedClass)
		{
			if (ACharacter* CDO = Cast<ACharacter>(Child->GeneratedClass->GetDefaultObject()))
			{
				if (USkeletalMeshComponent* M = CDO->GetMesh()) { M->SetVisibility(false); M->SetHiddenInGame(true); bHidOriginal = true; }
			}
		}
		Child->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(Child->GetPathName(), false);

		// Register the retargeter in ABP_GenericRetarget.IKRetargeter_Map (reflection: array of object refs).
		bool bMapUpdated = false; FString MapInfo;
		if (!RetargeterPath.IsEmpty())
		{
			UObject* Retargeter = UEditorAssetLibrary::LoadAsset(RetargeterPath);
			UBlueprint* GenericBP = FindBlueprintByName(TEXT("ABP_GenericRetarget"));
			if (!Retargeter) MapInfo = TEXT("retargeter asset not found");
			else if (!GenericBP || !GenericBP->GeneratedClass) MapInfo = TEXT("ABP_GenericRetarget not found in project");
			else
			{
				UObject* GCDO = GenericBP->GeneratedClass->GetDefaultObject();
				if (FArrayProperty* AP = CastField<FArrayProperty>(GenericBP->GeneratedClass->FindPropertyByName(TEXT("IKRetargeter_Map"))))
				{
					if (FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(AP->Inner))
					{
						FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(GCDO));
						bool bExists = false;
						for (int32 i = 0; i < H.Num(); ++i) if (Inner->GetObjectPropertyValue(H.GetRawPtr(i)) == Retargeter) bExists = true;
						if (!bExists) { const int32 Idx = H.AddValue(); Inner->SetObjectPropertyValue(H.GetRawPtr(Idx), Retargeter); }
						GenericBP->MarkPackageDirty();
						UEditorAssetLibrary::SaveAsset(GenericBP->GetPathName(), false);
						bMapUpdated = true; MapInfo = bExists ? TEXT("already present") : TEXT("added");
					}
					else MapInfo = TEXT("IKRetargeter_Map inner type is not an object reference");
				}
				else MapInfo = TEXT("IKRetargeter_Map property not found on ABP_GenericRetarget");
			}
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("blueprint_path"), Child->GetPathName());
		Out->SetStringField(TEXT("parent"), Parent->GetPathName());
		Out->SetStringField(TEXT("component"), TEXT("RetargetMesh"));
		Out->SetStringField(TEXT("retarget_tag"), Tag);
		Out->SetBoolField(TEXT("anim_class_assigned"), AnimBP != nullptr);
		Out->SetBoolField(TEXT("original_mesh_hidden"), bHidOriginal);
		Out->SetNumberField(TEXT("compile_errors"), Results.NumErrors);
		Out->SetBoolField(TEXT("retargeter_map_updated"), bMapUpdated);
		if (!MapInfo.IsEmpty()) Out->SetStringField(TEXT("retargeter_map_info"), MapInfo);
		Out->SetStringField(TEXT("next"), TEXT("Create/assign the IK Retargeter named like the tag if not done (ik_retarget umbrella), then add a button in the Game Animation Widget to select the character."));
		return Ok(Out);
	}
}

void FUECPUE58ExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("ue58_feature_status"),                  HandleFeatureStatus);
	D.RegisterHandler(TEXT("ue58_enable_plugins"),                  HandleEnablePlugins);
	D.RegisterHandler(TEXT("pve_create_vegetation"),                HandlePveCreateVegetation);
	D.RegisterHandler(TEXT("pve_list_nodes"),                       HandlePveListNodes);
	D.RegisterHandler(TEXT("pve_open_editor"),                      HandlePveOpenEditor);
	D.RegisterHandler(TEXT("controlrig_physics_from_physics_asset"), HandleControlRigPhysicsFromPhysicsAsset);
	D.RegisterHandler(TEXT("dmc_add_component"),                    HandleDmcAddComponent);
	D.RegisterHandler(TEXT("metahuman_api"),                        HandleMetaHumanApi);
	D.RegisterHandler(TEXT("render_apply_preset"),                  HandleRenderApplyPreset);
	D.RegisterHandler(TEXT("render_set_cvars"),                     HandleRenderSetCVars);
	D.RegisterHandler(TEXT("render_get_state"),                     HandleRenderGetState);
	D.RegisterHandler(TEXT("physics_asset_generate"),               HandlePhysicsAssetGenerate);
	D.RegisterHandler(TEXT("gasp_guide"),                           HandleGaspGuide);
	D.RegisterHandler(TEXT("gasp_add_character"),                   HandleGaspAddCharacter);

	{
		const FName U(TEXT("ue58"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };
		Meta(TEXT("ue58_feature_status"), TEXT("Which UE 5.8 feature plugins are enabled (PVE, Control Rig Physics/Dynamics, DMC, MetaHuman, PoseSearch, Chooser, Mover, MCP...) and the current rendering flags (MegaLights, Substrate, Nanite, VSM, Lumen)."), TEXT(""));
		Meta(TEXT("ue58_enable_plugins"), TEXT("Enable/disable engine plugins in the .uproject (restart required)."), TEXT("plugins[], enable?=true"));
		Meta(TEXT("pve_create_vegetation"), TEXT("Create a Procedural Vegetation asset (5.8 PVE: node-graph trees with Nanite foliage). Returns the inner PCG graph path to author with pcg tools."), TEXT("name, save_path"));
		Meta(TEXT("pve_list_nodes"), TEXT("List Procedural Vegetation node classes (growers, distributions, meshers, seed, carve, export...) with their editable properties."), TEXT("filter?, include_properties?=true"));
		Meta(TEXT("pve_open_editor"), TEXT("Open the Procedural Vegetation editor for an asset (Generate/Export run there)."), TEXT("asset_path"));
		Meta(TEXT("controlrig_physics_from_physics_asset"), TEXT("Control Rig Physics (5.8 beta): add a physics solver + 'Instantiate From Physics Asset' node chain into a Control Rig event."), TEXT("control_rig_path, physics_asset_path, owner_bone?=root, event?=Construction"));
		Meta(TEXT("dmc_add_component"), TEXT("Direct Mesh Controls (5.8 experimental): add a DirectMeshControlComponent to a Blueprint and assign a skeletal mesh."), TEXT("blueprint_path, component_name?, skeletal_mesh_path?, attach_to?"));
		Meta(TEXT("metahuman_api"), TEXT("MetaHuman Character editor API (Python): probe available methods/structs, can_build/build (assemble), export_dna, conform_body_from_mesh (Mesh to MetaHuman body), or raw python."), TEXT("action=probe|can_build|build|export_dna|conform_body_from_mesh|python, character_path?, quality?, pipeline?, output_path?, mesh_path?, target_is_a_pose?, code?"));
		Meta(TEXT("render_apply_preset"), TEXT("Apply a UE 5.8 rendering preset: megalights | megalights_translucency | lumen_lite | substrate | substrate_npr | fsss | nanite_foliage | hwrt_lumen (live cvars + project settings)."), TEXT("preset, project_wide?=true"));
		Meta(TEXT("render_set_cvars"), TEXT("Set arbitrary console variables live (and optionally as project settings)."), TEXT("cvars{}, project_wide?=false"));
		Meta(TEXT("render_get_state"), TEXT("Read rendering cvars (defaults to the 5.8 feature set)."), TEXT("cvars?[]"));
		Meta(TEXT("physics_asset_generate"), TEXT("Generate (or regenerate) a Physics Asset from a skeletal mesh in one call — bodies + constraints — for fast iteration."), TEXT("skeletal_mesh_path, physics_asset_path?, min_bone_size?=20, geometry?=capsule|sphere|box|convex|multi_convex|tapered_capsule, vert_weight?=dominant|any, create_constraints?=true, body_for_all?=false, orient_to_bone?=true, walk_past_small?=true, disable_collisions_by_default?=true, angular_constraint?=limited|free|locked, assign_to_mesh?=true, hull_count?, max_hull_verts?, lod_index?"));
		Meta(TEXT("gasp_guide"), TEXT("Game Animation Sample (5.8) guide: overview | retarget | motion_matching | mover | traversal — asset names and step lists."), TEXT("topic?"));
		Meta(TEXT("gasp_add_character"), TEXT("Add your character to the Game Animation Sample: child of CBP_SandboxCharacter with a retarget SkeletalMeshComponent (ABP_SandboxCharacter, RTG tag), hides the mannequin, registers the retargeter in ABP_GenericRetarget."), TEXT("skeletal_mesh_path, retargeter_path?, name?, save_path?, parent_blueprint_path?, anim_blueprint_path?, retarget_tag?"));
	}

	using UECPToolSafety::RegisterToolSafety;
	RegisterToolSafety(TEXT("ue58_feature_status"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("pve_list_nodes"),      EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("render_get_state"),    EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("gasp_guide"),          EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("ue58_enable_plugins"), EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("render_apply_preset"), EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("render_set_cvars"),    EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("metahuman_api"),       EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("physics_asset_generate"), EUECPToolSafety::Write);

	UE_LOG(LogUECPUE58Ext, Log, TEXT("Axivor UE 5.8 feature pack registered (umbrella 'ue58')."));
}

void FUECPUE58ExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const TCHAR* N : { TEXT("ue58_feature_status"), TEXT("ue58_enable_plugins"), TEXT("pve_create_vegetation"), TEXT("pve_list_nodes"), TEXT("pve_open_editor"),
	                        TEXT("controlrig_physics_from_physics_asset"), TEXT("dmc_add_component"), TEXT("metahuman_api"), TEXT("render_apply_preset"), TEXT("render_set_cvars"),
	                        TEXT("render_get_state"), TEXT("physics_asset_generate"), TEXT("gasp_guide"), TEXT("gasp_add_character") })
	{
		D.UnregisterHandler(FName(N));
	}
}

IMPLEMENT_MODULE(FUECPUE58ExtModule, UECPUE58Ext)
