// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPExtensionRegistry.h"
#include "UECPCoreModule.h"
#include "Services/IUECPLicenseService.h"
#include "Services/IUECPSettingsService.h"
#include "Services/IUECPToolDispatcher.h"

#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/ScopeLock.h"
#include "ProjectDescriptor.h"
#include "Services/UECPExtensionManifest.h"

#if WITH_EDITOR
#include "UnrealEdMisc.h"
#endif

const TCHAR* LexToString(EUECPExtensionState State)
{
	switch (State)
	{
		case EUECPExtensionState::Disabled:              return TEXT("Disabled");
		case EUECPExtensionState::PreconditionsFailed:   return TEXT("PreconditionsFailed");
		case EUECPExtensionState::LicenseLocked:         return TEXT("LicenseLocked");
		case EUECPExtensionState::Loaded:                return TEXT("Loaded");
		case EUECPExtensionState::Connecting:            return TEXT("Connecting");
		case EUECPExtensionState::Failed:                return TEXT("Failed");
		case EUECPExtensionState::EngineVersionMismatch: return TEXT("EngineVersionMismatch");
	}
	return TEXT("Unknown");
}

namespace
{
	struct FRegistrantContextScope
	{
		IUECPToolDispatcher& Dispatcher;
		explicit FRegistrantContextScope(IUECPToolDispatcher& InDispatcher, FName ExtensionId)
			: Dispatcher(InDispatcher)
		{
			Dispatcher.SetRegistrantContext(ExtensionId);
		}
		~FRegistrantContextScope()
		{
			Dispatcher.SetRegistrantContext(NAME_None);
		}
	};

	bool DetectShadow(const FUECPExtensionDescriptor& Desc, FString& OutToolName, FString& OutOwnerLabel)
	{
		if (!IUECPCoreModule::IsAvailable()) return false;
		IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
		IUECPExtensionService& ExtService = IUECPCoreModule::Get().GetExtensionService();

		for (const FName& ToolName : Desc.OwnedTools)
		{
			if (ToolName.IsNone()) continue;
			if (!Dispatcher.IsRegistered(ToolName)) continue;

			const TOptional<FUECPExtensionDescriptor> OwnerExt = ExtService.FindExtensionByTool(ToolName);
			const FName OwnerId = OwnerExt.IsSet() ? OwnerExt->ExtensionId : NAME_None;

			if (OwnerId == Desc.ExtensionId) continue;

			OutToolName = ToolName.ToString();
			OutOwnerLabel = OwnerId.IsNone() ? FString(TEXT("core")) : OwnerId.ToString();
			return true;
		}
		return false;
	}
}

FName FUECPExtensionRegistryImpl::SettingsNamespace()
{
	static const FName NS(TEXT("Extensions"));
	return NS;
}

FString FUECPExtensionRegistryImpl::MakeEnabledKey(FName ExtensionId)
{
	return FString::Printf(TEXT("%s.Enabled"), *ExtensionId.ToString());
}

FName FUECPExtensionRegistryImpl::MakeProxyToolName(FName ExtensionId, const FString& McpToolName)
{
	return FName(*FString::Printf(TEXT("%s__%s"), *ExtensionId.ToString(), *McpToolName));
}

bool FUECPExtensionRegistryImpl::StartMcpAndRegisterProxies(const FUECPExtensionDescriptor& Desc, FString& OutError)
{
	if (!Desc.McpServer.IsSet())
	{
		return true;
	}

	FMcpBinding& Binding = McpBindings.FindOrAdd(Desc.ExtensionId);

	if (Binding.Client.IsValid() && Binding.Client->IsConnected() && Binding.RegisteredProxyNames.Num() > 0)
	{
		Binding.LastError.Reset();
		return true;
	}

	Binding.LastError.Reset();
	Binding.RegisteredProxyNames.Reset();

	if (!Binding.Client.IsValid())
	{
		Binding.Client = MakeShared<FUECPMcpClient>();
	}

	const FUECPMcpServerSpec& Spec = Desc.McpServer.GetValue();
	if (!Binding.Client->Start(Spec, OutError))
	{
		Binding.LastError = OutError;
		Binding.Client.Reset();
		return false;
	}

	const FName ExtId = Desc.ExtensionId;
	int32 RegisteredCount = 0;
	RegisterProxiesForBinding(ExtId, RegisteredCount);

	UE_LOG(LogUECPCore, Log,
		TEXT("Extension '%s' MCP-backed: registered %d proxy handlers"),
		*ExtId.ToString(), RegisteredCount);
	return true;
}

void FUECPExtensionRegistryImpl::RegisterProxiesForBinding(FName ExtensionId, int32& OutCount)
{
	OutCount = 0;
	FMcpBinding* Binding = McpBindings.Find(ExtensionId);
	if (!Binding || !Binding->Client.IsValid()) return;

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
	TWeakPtr<FUECPMcpClient> WeakClient = Binding->Client;

	for (const FUECPMcpToolDescriptor& Tool : Binding->Client->GetTools())
	{
		if (Tool.Name.IsEmpty()) continue;

		const FName ProxyName = MakeProxyToolName(ExtensionId, Tool.Name);
		const FString ToolNameCopy = Tool.Name;

		Dispatcher.RegisterHandler(ProxyName,
			[WeakClient, ToolNameCopy](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
			{
				TSharedPtr<FUECPMcpClient> Pinned = WeakClient.Pin();
				if (!Pinned.IsValid())
				{
					FUECPToolResult R;
					R.bSuccess = false;
					R.ErrorMessage = TEXT("MCP client torn down");
					return R;
				}
				return Pinned->CallTool(ToolNameCopy, Args);
			},
			EUECPToolThreadAffinity::AnyThread);

		Binding->RegisteredProxyNames.Add(ProxyName);
	}
	OutCount = Binding->RegisteredProxyNames.Num();
}

void FUECPExtensionRegistryImpl::StartMcpAndRegisterProxiesAsync(
	const FUECPExtensionDescriptor& Desc,
	TFunction<void(bool, FString)> OnComplete)
{
	if (!Desc.McpServer.IsSet())
	{
		if (OnComplete) OnComplete(true, FString());
		return;
	}

	FMcpBinding& Binding = McpBindings.FindOrAdd(Desc.ExtensionId);

	if (Binding.Client.IsValid() && Binding.Client->IsConnected() && Binding.RegisteredProxyNames.Num() > 0)
	{
		Binding.LastError.Reset();
		if (OnComplete) OnComplete(true, FString());
		return;
	}

	Binding.LastError.Reset();
	Binding.RegisteredProxyNames.Reset();
	if (!Binding.Client.IsValid())
	{
		Binding.Client = MakeShared<FUECPMcpClient>();
	}

	const FName ExtId = Desc.ExtensionId;
	TSharedPtr<FUECPMcpClient> Client = Binding.Client;
	const FUECPMcpServerSpec Spec = Desc.McpServer.GetValue();

	Client->StartAsync(Spec,
		[this, ExtId, OnComplete = MoveTemp(OnComplete)](bool bOk, FString Err) mutable
		{
			FMcpBinding* B = McpBindings.Find(ExtId);
			if (!B)
			{
				if (OnComplete) OnComplete(false, TEXT("Binding torn down before handshake completed"));
				return;
			}

			if (!bOk)
			{
				B->LastError = Err;
				B->Client.Reset();
				if (OnComplete) OnComplete(false, MoveTemp(Err));
				return;
			}

			int32 RegisteredCount = 0;
			RegisterProxiesForBinding(ExtId, RegisteredCount);
			UE_LOG(LogUECPCore, Log,
				TEXT("Extension '%s' MCP-backed (async): registered %d proxy handlers"),
				*ExtId.ToString(), RegisteredCount);

			if (OnComplete) OnComplete(true, FString());
		});
}

void FUECPExtensionRegistryImpl::StopMcpAndUnregisterProxies(FName ExtensionId)
{
	FMcpBinding* Binding = McpBindings.Find(ExtensionId);
	if (!Binding) return;

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
		for (FName ProxyName : Binding->RegisteredProxyNames)
		{
			Dispatcher.UnregisterHandler(ProxyName);
		}
	}
	Binding->RegisteredProxyNames.Reset();

	if (Binding->Client.IsValid())
	{
		Binding->Client->Stop();
		Binding->Client.Reset();
	}
	Binding->LastError.Reset();

}

void FUECPExtensionRegistryImpl::RegisterExtension(FUECPExtensionDescriptor Descriptor)
{
	if (Descriptor.ExtensionId.IsNone())
	{
		UE_LOG(LogUECPCore, Warning, TEXT("RegisterExtension called with empty ExtensionId — ignored."));
		return;
	}
	if (Descriptor.RequiredModuleName.IsNone() && !Descriptor.McpServer.IsSet())
	{
		UE_LOG(LogUECPCore, Warning,
			TEXT("RegisterExtension '%s' has neither RequiredModuleName nor McpServer — ignored."),
			*Descriptor.ExtensionId.ToString());
		return;
	}

	if (Descriptor.LicenseFeatureId.IsNone())
	{
		Descriptor.LicenseFeatureId = Descriptor.ExtensionId;
	}

	const FName Id = Descriptor.ExtensionId;

	{
		FScopeLock Lock(&RegistryLock);
		const int32 ExistingIndex = Extensions.IndexOfByPredicate(
			[Id](const FUECPExtensionDescriptor& D) { return D.ExtensionId == Id; });
		if (ExistingIndex != INDEX_NONE)
		{
			Extensions[ExistingIndex] = MoveTemp(Descriptor);
		}
		else
		{
			Extensions.Add(MoveTemp(Descriptor));
			States.FindOrAdd(Id) = EUECPExtensionState::Disabled;
		}
	}
	StateChangedDelegate.Broadcast(Id);
}

void FUECPExtensionRegistryImpl::UnregisterExtension(FName ExtensionId)
{
	StopMcpAndUnregisterProxies(ExtensionId);
	McpBindings.Remove(ExtensionId);

	bool bRemoved = false;
	{
		FScopeLock Lock(&RegistryLock);
		const int32 Removed = Extensions.RemoveAll(
			[ExtensionId](const FUECPExtensionDescriptor& D) { return D.ExtensionId == ExtensionId; });
		if (Removed > 0)
		{
			States.Remove(ExtensionId);
			bRemoved = true;
		}
	}
	if (bRemoved)
	{
		StateChangedDelegate.Broadcast(ExtensionId);
	}
}

TArray<FUECPExtensionDescriptor> FUECPExtensionRegistryImpl::GetExtensions() const
{
	FScopeLock Lock(&RegistryLock);
	return Extensions;
}

TOptional<FUECPExtensionDescriptor> FUECPExtensionRegistryImpl::FindExtension(FName ExtensionId) const
{
	FScopeLock Lock(&RegistryLock);
	if (const FUECPExtensionDescriptor* Found = Extensions.FindByPredicate(
		[ExtensionId](const FUECPExtensionDescriptor& D) { return D.ExtensionId == ExtensionId; }))
	{
		return *Found;
	}
	return {};
}

TOptional<FUECPExtensionDescriptor> FUECPExtensionRegistryImpl::FindExtensionByUmbrella(FName UmbrellaName) const
{
	if (UmbrellaName.IsNone()) return {};
	FScopeLock Lock(&RegistryLock);
	for (const FUECPExtensionDescriptor& D : Extensions)
	{
		if (D.OwnedUmbrellas.Contains(UmbrellaName)) return D;
	}
	return {};
}

TOptional<FUECPExtensionDescriptor> FUECPExtensionRegistryImpl::FindExtensionByTool(FName ToolName) const
{
	if (ToolName.IsNone()) return {};
	FScopeLock Lock(&RegistryLock);
	for (const FUECPExtensionDescriptor& D : Extensions)
	{
		if (D.OwnedTools.Contains(ToolName)) return D;
	}
	return {};
}

EUECPExtensionState FUECPExtensionRegistryImpl::GetExtensionState(FName ExtensionId) const
{
	FScopeLock Lock(&RegistryLock);
	if (const EUECPExtensionState* S = States.Find(ExtensionId))
	{
		return *S;
	}
	return EUECPExtensionState::Disabled;
}

bool FUECPExtensionRegistryImpl::IsUmbrellaProvidedByLoadedExtension(FName UmbrellaName) const
{
	if (UmbrellaName.IsNone()) return false;
	FScopeLock Lock(&RegistryLock);
	for (const FUECPExtensionDescriptor& D : Extensions)
	{
		if (D.OwnedUmbrellas.Contains(UmbrellaName))
		{
			const EUECPExtensionState* S = States.Find(D.ExtensionId);
			return S != nullptr && *S == EUECPExtensionState::Loaded;
		}
	}
	return false;
}

bool FUECPExtensionRegistryImpl::IsExtensionEnabled(FName ExtensionId) const
{
	if (!IUECPCoreModule::IsAvailable()) return false;
	const TOptional<FUECPExtensionDescriptor> Desc = FindExtension(ExtensionId);
	const bool bDefault = Desc.IsSet() ? Desc->bDefaultEnabled : false;
	return IUECPCoreModule::Get().GetSettingsService().GetBool(
		SettingsNamespace(), MakeEnabledKey(ExtensionId), bDefault);
}

void FUECPExtensionRegistryImpl::SetState(FName ExtensionId, EUECPExtensionState NewState)
{
	bool bChanged = false;
	{
		FScopeLock Lock(&RegistryLock);
		EUECPExtensionState& Cur = States.FindOrAdd(ExtensionId);
		if (Cur != NewState)
		{
			Cur = NewState;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		StateChangedDelegate.Broadcast(ExtensionId);
	}
}

EUECPExtensionState FUECPExtensionRegistryImpl::EvaluateAndLoad(const FUECPExtensionDescriptor& Desc,
	FString& OutError, TArray<FString>* OutMissingPlugins)
{
	if (!UECPExtensionManifest::IsEngineVersionInRange(Desc.MinEngineVersion, Desc.MaxEngineVersion))
	{
		OutError = FString::Printf(
			TEXT("Engine version out of range (extension wants min='%s' max='%s')"),
			*Desc.MinEngineVersion, *Desc.MaxEngineVersion);
		return EUECPExtensionState::EngineVersionMismatch;
	}

	IPluginManager& PM = IPluginManager::Get();
	TArray<FString> Missing;
	for (const FString& PluginName : Desc.RequiredPlugins)
	{
		TSharedPtr<IPlugin> Plugin = PM.FindPlugin(PluginName);
		if (!Plugin.IsValid() || !Plugin->IsEnabled())
		{
			Missing.Add(PluginName);
		}
	}
	if (Missing.Num() > 0)
	{
		if (OutMissingPlugins) *OutMissingPlugins = Missing;
		OutError = FString::Printf(TEXT("Required plugins not enabled: %s"), *FString::Join(Missing, TEXT(", ")));
		return EUECPExtensionState::PreconditionsFailed;
	}

	if (Desc.bRequiresLicense)
	{
		IUECPLicenseService& Lic = IUECPCoreModule::Get().GetLicenseService();
		if (!Lic.IsFeatureAvailable(Desc.LicenseFeatureId))
		{
			OutError = Lic.GetUnavailableReason(Desc.LicenseFeatureId).ToString();
			return EUECPExtensionState::LicenseLocked;
		}
	}

	if (!EnsureRequiredExtensionsLoaded(Desc, OutError))
	{
		return EUECPExtensionState::PreconditionsFailed;
	}

	if (!Desc.RequiredModuleName.IsNone())
	{
		{
			FString ShadowTool, ShadowOwner;
			if (DetectShadow(Desc, ShadowTool, ShadowOwner))
			{
				OutError = FString::Printf(
					TEXT("declares tool '%s' which is already provided by %s"),
					*ShadowTool, *ShadowOwner);
				return EUECPExtensionState::Failed;
			}
		}

		FModuleManager& MM = FModuleManager::Get();
		if (!MM.IsModuleLoaded(Desc.RequiredModuleName))
		{
			EModuleLoadResult LoadResult = EModuleLoadResult::Success;
			IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
			FRegistrantContextScope RegistrantScope(Dispatcher, Desc.ExtensionId);

			IModuleInterface* Loaded = MM.LoadModuleWithFailureReason(Desc.RequiredModuleName, LoadResult);
			if (!Loaded || LoadResult != EModuleLoadResult::Success)
			{
				OutError = FString::Printf(TEXT("LoadModule '%s' failed (result=%d)"),
					*Desc.RequiredModuleName.ToString(), (int32)LoadResult);
				return EUECPExtensionState::Failed;
			}
		}
	}

	if (!StartMcpAndRegisterProxies(Desc, OutError))
	{
		return EUECPExtensionState::Failed;
	}

	// Validate declared vs actually-registered tools. A manifest whose owned_tools omits a
	// registered tool leaves that tool without a declared owner (shadow detection and
	// tool->extension attribution silently miss it); the reverse means a declared tool never
	// registered. Both are surfaced as warnings so drift can't hide.
	{
		IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
		const TArray<FName> Registered = Dispatcher.GetToolsForOwner(Desc.ExtensionId);
		const TSet<FName> DeclaredSet(Desc.OwnedTools);
		const TSet<FName> RegisteredSet(Registered);
		for (const FName& Reg : Registered)
		{
			if (!DeclaredSet.Contains(Reg))
			{
				UE_LOG(LogUECPCore, Warning,
					TEXT("[Extension '%s'] tool '%s' is registered but missing from the manifest owned_tools — it has no declared owner."),
					*Desc.ExtensionId.ToString(), *Reg.ToString());
			}
		}
		for (const FName& Decl : Desc.OwnedTools)
		{
			if (!Decl.IsNone() && !RegisteredSet.Contains(Decl))
			{
				UE_LOG(LogUECPCore, Warning,
					TEXT("[Extension '%s'] manifest owned_tools lists '%s' but no handler registered under this extension."),
					*Desc.ExtensionId.ToString(), *Decl.ToString());
			}
		}
	}

	OutError.Reset();
	return EUECPExtensionState::Loaded;
}

bool FUECPExtensionRegistryImpl::EnsureRequiredExtensionsLoaded(const FUECPExtensionDescriptor& Desc, FString& OutError)
{
	for (const FString& ReqId : Desc.RequiredExtensions)
	{
		const FName ReqName(*ReqId);

		if (GetExtensionState(ReqName) == EUECPExtensionState::Loaded) continue;
		if (ExtensionsLoading.Contains(ReqName)) continue;

		const TOptional<FUECPExtensionDescriptor> ReqDesc = FindExtension(ReqName);
		if (!ReqDesc.IsSet())
		{
			OutError = FString::Printf(TEXT("Required extension '%s' is not installed."), *ReqId);
			return false;
		}

		ExtensionsLoading.Add(ReqName);
		IUECPCoreModule::Get().GetSettingsService().SetBool(SettingsNamespace(), MakeEnabledKey(ReqName), true);
		if (ReqDesc->ApplyProjectSetup) ReqDesc->ApplyProjectSetup();

		FString SubErr;
		const EUECPExtensionState SubState = EvaluateAndLoad(*ReqDesc, SubErr, nullptr);
		SetState(ReqName, SubState);
		ExtensionsLoading.Remove(ReqName);

		if (SubState != EUECPExtensionState::Loaded)
		{
			OutError = FString::Printf(TEXT("Required extension '%s' could not be enabled: %s"), *ReqId, *SubErr);
			return false;
		}
	}
	return true;
}

TArray<FString> FUECPExtensionRegistryImpl::FindEnabledDependents(FName ExtensionId) const
{
	TArray<FString> Dependents;
	for (const FUECPExtensionDescriptor& Other : Extensions)
	{
		if (Other.ExtensionId == ExtensionId) continue;
		if (!IsExtensionEnabled(Other.ExtensionId)) continue;
		const bool bDependsOnIt = Other.RequiredExtensions.ContainsByPredicate(
			[ExtensionId](const FString& R){ return FName(*R) == ExtensionId; });
		if (bDependsOnIt) Dependents.Add(Other.ExtensionId.ToString());
	}
	return Dependents;
}

bool FUECPExtensionRegistryImpl::SetExtensionEnabled(FName ExtensionId, bool bEnabled, FString& OutError,
	TArray<FString>* OutMissingPlugins, bool bRunRemoveSetup)
{
	const TOptional<FUECPExtensionDescriptor> Desc = FindExtension(ExtensionId);
	if (!Desc.IsSet()) { OutError = TEXT("Unknown extension"); return false; }

	if (!IUECPCoreModule::IsAvailable())
	{
		OutError = TEXT("Core not initialized");
		return false;
	}

	if (bEnabled && !UECPExtensionManifest::IsEngineVersionInRange(Desc->MinEngineVersion, Desc->MaxEngineVersion))
	{
		OutError = FString::Printf(
			TEXT("This extension requires a different engine version (min='%s' max='%s')."),
			*Desc->MinEngineVersion, *Desc->MaxEngineVersion);
		SetState(ExtensionId, EUECPExtensionState::EngineVersionMismatch);
		return false;
	}

	if (!bEnabled)
	{
		const TArray<FString> Dependents = FindEnabledDependents(ExtensionId);
		if (Dependents.Num() > 0)
		{
			OutError = FString::Printf(
				TEXT("Can't disable '%s' — still required by: %s. Disable %s first."),
				*ExtensionId.ToString(), *FString::Join(Dependents, TEXT(", ")),
				Dependents.Num() > 1 ? TEXT("those") : TEXT("it"));
			return false;
		}
	}

	IUECPSettingsService& Settings = IUECPCoreModule::Get().GetSettingsService();
	Settings.SetBool(SettingsNamespace(), MakeEnabledKey(ExtensionId), bEnabled);

	if (bEnabled)
	{
		if (Desc->ApplyProjectSetup)
		{
			Desc->ApplyProjectSetup();
		}

		const EUECPExtensionState NewState = EvaluateAndLoad(*Desc, OutError, OutMissingPlugins);
		SetState(ExtensionId, NewState);
		return NewState == EUECPExtensionState::Loaded;
	}
	else
	{
		StopMcpAndUnregisterProxies(ExtensionId);

		if (!Desc->RequiredModuleName.IsNone())
		{
			FModuleManager& MM = FModuleManager::Get();
			if (MM.IsModuleLoaded(Desc->RequiredModuleName))
			{
				IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
				Dispatcher.RemoveHandlersForOwner(Desc->ExtensionId);
				FRegistrantContextScope RegistrantScope(Dispatcher, Desc->ExtensionId);
				MM.UnloadModule(Desc->RequiredModuleName, false);
			}
		}
		IUECPCoreModule::Get().GetToolDispatcher().RemoveToolMetadataForOwner(Desc->ExtensionId);

		if (bRunRemoveSetup && Desc->RemoveProjectSetup)
		{
			Desc->RemoveProjectSetup();
		}
		SetState(ExtensionId, EUECPExtensionState::Disabled);
		return true;
	}
}

void FUECPExtensionRegistryImpl::SetExtensionEnabledAsync(FName ExtensionId, bool bEnable,
	TFunction<void(bool, FString, TArray<FString>)> OnComplete, bool bRunRemoveSetup)
{
	auto Done = [&OnComplete](bool bOk, FString Err, TArray<FString> Missing = {})
	{
		if (OnComplete) OnComplete(bOk, MoveTemp(Err), MoveTemp(Missing));
	};

	const TOptional<FUECPExtensionDescriptor> Desc = FindExtension(ExtensionId);
	if (!Desc.IsSet())
	{
		Done(false, TEXT("Unknown extension"));
		return;
	}
	if (!IUECPCoreModule::IsAvailable())
	{
		Done(false, TEXT("Core not initialized"));
		return;
	}

	if (!bEnable)
	{
		FString Err; TArray<FString> Missing;
		const bool bOk = SetExtensionEnabled(ExtensionId, false, Err, &Missing, bRunRemoveSetup);
		Done(bOk, MoveTemp(Err), MoveTemp(Missing));
		return;
	}

	if (!UECPExtensionManifest::IsEngineVersionInRange(Desc->MinEngineVersion, Desc->MaxEngineVersion))
	{
		SetState(ExtensionId, EUECPExtensionState::EngineVersionMismatch);
		Done(false, FString::Printf(
			TEXT("This extension requires a different engine version (min='%s' max='%s')."),
			*Desc->MinEngineVersion, *Desc->MaxEngineVersion));
		return;
	}

	IUECPSettingsService& Settings = IUECPCoreModule::Get().GetSettingsService();
	Settings.SetBool(SettingsNamespace(), MakeEnabledKey(ExtensionId), true);
	if (Desc->ApplyProjectSetup) Desc->ApplyProjectSetup();

	IPluginManager& PM = IPluginManager::Get();
	TArray<FString> Missing;
	for (const FString& PluginName : Desc->RequiredPlugins)
	{
		TSharedPtr<IPlugin> Plugin = PM.FindPlugin(PluginName);
		if (!Plugin.IsValid() || !Plugin->IsEnabled()) Missing.Add(PluginName);
	}
	if (Missing.Num() > 0)
	{
		SetState(ExtensionId, EUECPExtensionState::PreconditionsFailed);
		Done(false, FString::Printf(TEXT("Required plugins not enabled: %s"),
			*FString::Join(Missing, TEXT(", "))), MoveTemp(Missing));
		return;
	}

	if (Desc->bRequiresLicense)
	{
		IUECPLicenseService& Lic = IUECPCoreModule::Get().GetLicenseService();
		if (!Lic.IsFeatureAvailable(Desc->LicenseFeatureId))
		{
			SetState(ExtensionId, EUECPExtensionState::LicenseLocked);
			Done(false, Lic.GetUnavailableReason(Desc->LicenseFeatureId).ToString());
			return;
		}
	}

	{
		FString ReqErr;
		if (!EnsureRequiredExtensionsLoaded(*Desc, ReqErr))
		{
			SetState(ExtensionId, EUECPExtensionState::PreconditionsFailed);
			Done(false, MoveTemp(ReqErr));
			return;
		}
	}

	if (!Desc->RequiredModuleName.IsNone())
	{
		{
			FString ShadowTool, ShadowOwner;
			if (DetectShadow(*Desc, ShadowTool, ShadowOwner))
			{
				SetState(ExtensionId, EUECPExtensionState::Failed);
				Done(false, FString::Printf(
					TEXT("declares tool '%s' which is already provided by %s"),
					*ShadowTool, *ShadowOwner));
				return;
			}
		}

		FModuleManager& MM = FModuleManager::Get();
		if (!MM.IsModuleLoaded(Desc->RequiredModuleName))
		{
			EModuleLoadResult LoadResult = EModuleLoadResult::Success;
			IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
			FRegistrantContextScope RegistrantScope(Dispatcher, ExtensionId);

			IModuleInterface* Loaded = MM.LoadModuleWithFailureReason(Desc->RequiredModuleName, LoadResult);
			if (!Loaded || LoadResult != EModuleLoadResult::Success)
			{
				SetState(ExtensionId, EUECPExtensionState::Failed);
				Done(false, FString::Printf(TEXT("LoadModule '%s' failed (result=%d)"),
					*Desc->RequiredModuleName.ToString(), (int32)LoadResult));
				return;
			}
		}
	}

	if (!Desc->McpServer.IsSet())
	{
		SetState(ExtensionId, EUECPExtensionState::Loaded);
		Done(true, FString());
		return;
	}

	SetState(ExtensionId, EUECPExtensionState::Connecting);

	StartMcpAndRegisterProxiesAsync(*Desc,
		[this, ExtensionId, OnComplete = MoveTemp(OnComplete)](bool bOk, FString Err) mutable
		{
			SetState(ExtensionId, bOk ? EUECPExtensionState::Loaded : EUECPExtensionState::Failed);
			if (OnComplete) OnComplete(bOk, MoveTemp(Err), TArray<FString>{});
		});
}

TSharedPtr<FUECPMcpClient> FUECPExtensionRegistryImpl::GetMcpClient(FName ExtensionId) const
{
	if (const FMcpBinding* Binding = McpBindings.Find(ExtensionId))
	{
		return Binding->Client;
	}
	return nullptr;
}

FString FUECPExtensionRegistryImpl::GetMcpLastError(FName ExtensionId) const
{
	if (const FMcpBinding* Binding = McpBindings.Find(ExtensionId))
	{
		return Binding->LastError;
	}
	return FString();
}

void FUECPExtensionRegistryImpl::RefreshAll()
{
	if (!IUECPCoreModule::IsAvailable()) return;

	for (const FUECPExtensionDescriptor& Desc : Extensions)
	{
		if (!UECPExtensionManifest::IsEngineVersionInRange(Desc.MinEngineVersion, Desc.MaxEngineVersion))
		{
			SetState(Desc.ExtensionId, EUECPExtensionState::EngineVersionMismatch);
			continue;
		}

		const bool bUserEnabled = IsExtensionEnabled(Desc.ExtensionId);
		if (!bUserEnabled)
		{
			SetState(Desc.ExtensionId, EUECPExtensionState::Disabled);
			continue;
		}

		if (GetExtensionState(Desc.ExtensionId) == EUECPExtensionState::Loaded)
		{
			continue;
		}

		FString DummyError;
		const EUECPExtensionState NewState = EvaluateAndLoad(Desc, DummyError, nullptr);
		SetState(Desc.ExtensionId, NewState);

		if (NewState != EUECPExtensionState::Loaded)
		{
			UE_LOG(LogUECPCore, Log,
				TEXT("Extension '%s' opted-in but not loaded: %s — %s"),
				*Desc.ExtensionId.ToString(), LexToString(NewState), *DummyError);
		}
	}
}

IUECPExtensionService::FOnExtensionStateChanged& FUECPExtensionRegistryImpl::OnExtensionStateChanged()
{
	return StateChangedDelegate;
}

bool FUECPExtensionRegistryImpl::ShouldShowUmbrella(FName UmbrellaName) const
{
	if (UmbrellaName.IsNone()) return true;
	FScopeLock Lock(&RegistryLock);
	for (const FUECPExtensionDescriptor& D : Extensions)
	{
		if (D.OwnedUmbrellas.Contains(UmbrellaName))
		{
			const EUECPExtensionState* S = States.Find(D.ExtensionId);
			return S != nullptr && *S == EUECPExtensionState::Loaded;
		}
	}
	return true;
}

namespace
{
	TArray<FString> ComputeMissingRequiredPlugins(const FUECPExtensionDescriptor& Desc)
	{
		TArray<FString> Missing;
		IPluginManager& PM = IPluginManager::Get();
		for (const FString& PluginName : Desc.RequiredPlugins)
		{
			const TSharedPtr<IPlugin> Plugin = PM.FindPlugin(PluginName);
			if (!Plugin.IsValid() || !Plugin->IsEnabled())
				Missing.Add(PluginName);
		}
		return Missing;
	}
}

void FUECPExtensionRegistryImpl::QueueExtensionChange(FName ExtensionId, bool bEnable)
{
	const TOptional<FUECPExtensionDescriptor> Desc = FindExtension(ExtensionId);
	if (!Desc.IsSet()) return;

	FPendingChange Change;
	Change.ExtensionId = ExtensionId;
	Change.bEnabling   = bEnable;
	if (bEnable)
	{
		Change.RequiredPluginsToEnable = ComputeMissingRequiredPlugins(*Desc);
	}

	if (PendingByExtension.Contains(ExtensionId))
	{
		PendingByExtension[ExtensionId] = MoveTemp(Change);
	}
	else
	{
		PendingOrder.Add(ExtensionId);
		PendingByExtension.Add(ExtensionId, MoveTemp(Change));
	}
}

void FUECPExtensionRegistryImpl::UnqueueExtensionChange(FName ExtensionId)
{
	if (PendingByExtension.Remove(ExtensionId) > 0)
	{
		PendingOrder.Remove(ExtensionId);
	}
}

void FUECPExtensionRegistryImpl::CancelPendingChanges()
{
	PendingOrder.Reset();
	PendingByExtension.Reset();
}

TArray<IUECPExtensionService::FPendingChange> FUECPExtensionRegistryImpl::GetPendingChanges() const
{
	TArray<FPendingChange> Out;
	Out.Reserve(PendingOrder.Num());
	for (const FName& Id : PendingOrder)
	{
		if (const FPendingChange* C = PendingByExtension.Find(Id))
			Out.Add(*C);
	}
	return Out;
}

bool FUECPExtensionRegistryImpl::PendingChangesRequireRestart() const
{
	for (const FName& Id : PendingOrder)
	{
		const FPendingChange* C = PendingByExtension.Find(Id);
		if (C && C->bEnabling && C->RequiredPluginsToEnable.Num() > 0)
			return true;
	}
	return false;
}

bool FUECPExtensionRegistryImpl::ApplyPendingChanges(FString& OutError)
{
	if (PendingOrder.Num() == 0) return true;
	if (!IUECPCoreModule::IsAvailable())
	{
		OutError = TEXT("Core not initialized");
		return false;
	}

	const bool bRestartNeeded = PendingChangesRequireRestart();
	IUECPSettingsService& Settings = IUECPCoreModule::Get().GetSettingsService();

	if (bRestartNeeded)
	{
		TSet<FString> PluginsToEnable;
		for (const FName& Id : PendingOrder)
		{
			const FPendingChange* C = PendingByExtension.Find(Id);
			if (!C || !C->bEnabling) continue;
			for (const FString& PluginName : C->RequiredPluginsToEnable)
				PluginsToEnable.Add(PluginName);
		}

		IProjectManager& PM = IProjectManager::Get();
		for (const FString& PluginName : PluginsToEnable)
		{
			FText Reason;
			if (!PM.SetPluginEnabled(PluginName, true, Reason))
			{
				OutError = FString::Printf(
					TEXT("Failed to enable UE plugin '%s' in .uproject: %s"),
					*PluginName, *Reason.ToString());
				return false;
			}
		}

		FText SaveReason;
		if (!PM.SaveCurrentProjectToDisk(SaveReason))
		{
			OutError = FString::Printf(TEXT("Failed to save .uproject: %s"), *SaveReason.ToString());
			return false;
		}

		for (const FName& Id : PendingOrder)
		{
			const FPendingChange* C = PendingByExtension.Find(Id);
			if (!C) continue;
			Settings.SetBool(SettingsNamespace(), MakeEnabledKey(Id), C->bEnabling);
		}

		CancelPendingChanges();

#if WITH_EDITOR
		FUnrealEdMisc::Get().RestartEditor(false);
#endif
		return true;
	}

	bool bAllOk = true;
	FString FirstError;
	for (const FName& Id : PendingOrder)
	{
		const FPendingChange* C = PendingByExtension.Find(Id);
		if (!C) continue;
		FString PerError;
		if (!SetExtensionEnabled(Id, C->bEnabling, PerError))
		{
			bAllOk = false;
			if (FirstError.IsEmpty()) FirstError = PerError;
		}
	}
	CancelPendingChanges();
	if (!bAllOk) OutError = FirstError;
	return bAllOk;
}

TArray<FUECPToolCatalogEntry> FUECPExtensionRegistryImpl::GetExtensionCatalogEntries(
	const TSet<FName>& ExcludeUmbrellaNames) const
{
	TArray<FUECPToolCatalogEntry> Result;

	struct FLoadedSnapshot
	{
		FName         ExtensionId;
		TArray<FName> OwnedUmbrellas;
		FString       CatalogBlurb;
		FString       Description;
		bool          bHasMcpServer = false;
	};
	TArray<FLoadedSnapshot> Loaded;
	{
		FScopeLock Lock(&RegistryLock);
		for (const FUECPExtensionDescriptor& D : Extensions)
		{
			const EUECPExtensionState* S = States.Find(D.ExtensionId);
			if (!S || *S != EUECPExtensionState::Loaded) continue;
			FLoadedSnapshot Snap;
			Snap.ExtensionId    = D.ExtensionId;
			Snap.OwnedUmbrellas = D.OwnedUmbrellas;
			Snap.CatalogBlurb   = D.CatalogBlurb;
			Snap.Description     = D.Description.ToString();
			Snap.bHasMcpServer  = D.McpServer.IsSet();
			Loaded.Add(MoveTemp(Snap));
		}
	}

	IUECPToolDispatcher* Dispatcher = IUECPCoreModule::IsAvailable()
		? &IUECPCoreModule::Get().GetToolDispatcher() : nullptr;

	for (const FLoadedSnapshot& D : Loaded)
	{
		for (const FName& Umbrella : D.OwnedUmbrellas)
		{
			if (ExcludeUmbrellaNames.Contains(Umbrella)) continue;

			FUECPToolCatalogEntry E;
			E.Name              = Umbrella.ToString();
			E.Description       = !D.CatalogBlurb.IsEmpty() ? D.CatalogBlurb : D.Description;
			E.bIsUmbrella       = true;
			E.OwningExtensionId = D.ExtensionId;
			// Schema derived from the extension's registered tool metadata (null when it has none;
			// consumers then fall back to the bare {action} shape).
			if (Dispatcher) E.InputSchema = Dispatcher->GetUmbrellaSchema(Umbrella);
			Result.Add(MoveTemp(E));
		}
	}

	for (const FLoadedSnapshot& D : Loaded)
	{
		if (!D.bHasMcpServer) continue;

		const TSharedPtr<FUECPMcpClient> Client = GetMcpClient(D.ExtensionId);
		if (!Client.IsValid() || !Client->IsConnected()) continue;

		const FString IdStr = D.ExtensionId.ToString();
		for (const FUECPMcpToolDescriptor& T : Client->GetTools())
		{
			if (T.Name.IsEmpty()) continue;

			FUECPToolCatalogEntry E;
			E.Name              = FString::Printf(TEXT("%s__%s"), *IdStr, *T.Name);
			E.Description       = T.Description.IsEmpty()
				? FString::Printf(TEXT("Tool '%s' from MCP server '%s'."), *T.Name, *IdStr)
				: T.Description;
			E.bIsUmbrella       = false;
			E.InputSchema       = T.InputSchema;
			E.OwningExtensionId = D.ExtensionId;
			Result.Add(MoveTemp(E));
		}
	}

	return Result;
}
