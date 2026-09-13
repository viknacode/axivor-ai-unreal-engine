// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPCoreModule.h"
#include "Services/UECPLicenseService.h"
#include "Services/UECPModuleRegistry.h"
#include "Services/UECPExtensionRegistry.h"
#include "Services/IUECPExtensionService.h"
#include "Services/UECPExtensionManifest.h"
#include "Services/UECPUserMcpService.h"
#include "Misc/CoreDelegates.h"
#include "Services/UECPSettingsService.h"
#include "Services/UECPToolDispatcher.h"
#include "Services/UECPCreateAssetRegistry.h"
#include "Services/UECPNullNotificationService.h"
#include "Services/UECPNullAssetGenService.h"
#include "Services/UECPNullFeatureServices.h"
#include "Services/UECPToolSafety.h"
#include "Internal/MacIOSDetectionFix.h"

DEFINE_LOG_CATEGORY(LogUECPCore);

class FUECPCoreModule final : public IUECPCoreModule
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual IUECPLicenseService&     GetLicenseService()     override { return *LicenseService; }
	virtual IUECPModuleRegistry&     GetModuleRegistry()     override { return *ModuleRegistry; }
	virtual IUECPExtensionService&   GetExtensionService()   override { return *ExtensionService; }
	virtual IUECPUserMcpService&     GetUserMcpService()     override { return *UserMcpService; }
	virtual IUECPSettingsService&    GetSettingsService()    override { return *SettingsService; }
	virtual IUECPNotificationService& GetNotificationService() override { return NotificationService.IsValid() ? *NotificationService : *NullNotificationService; }
	virtual void                     SetNotificationService(TSharedPtr<IUECPNotificationService> Service) override { NotificationService = MoveTemp(Service); }
	virtual IUECPToolDispatcher&     GetToolDispatcher()     override { return *ToolDispatcher; }
	virtual IUECPCreateAssetRegistry& GetCreateAssetRegistry() override { return *CreateAssetRegistry; }
	virtual IUECPLearningService&    GetLearningService()    override { return LearningService.IsValid() ? *LearningService : *NullLearningService; }
	virtual void                     SetLearningService(TSharedPtr<IUECPLearningService> Service) override { LearningService = MoveTemp(Service); }
	virtual IUECPAiMemoryService&    GetAiMemoryService()    override { return AiMemoryService.IsValid() ? *AiMemoryService : *NullAiMemoryService; }
	virtual void                     SetAiMemoryService(TSharedPtr<IUECPAiMemoryService> Service) override { AiMemoryService = MoveTemp(Service); }
	virtual IUECPAssetGenService&    GetAssetGenService()    override { return AssetGenService.IsValid() ? *AssetGenService : *NullAssetGenService; }
	virtual void                     SetAssetGenService(TSharedPtr<IUECPAssetGenService> Service) override { AssetGenService = MoveTemp(Service); }
	virtual IUECPArchitectService&   GetArchitectService()   override { return ArchitectService.IsValid() ? *ArchitectService : *NullArchitectService; }
	virtual void                     SetArchitectService(TSharedPtr<IUECPArchitectService> Service) override { ArchitectService = MoveTemp(Service); }
	virtual IUECPAnalystService&     GetAnalystService()     override { return AnalystService.IsValid() ? *AnalystService : *NullAnalystService; }
	virtual void                     SetAnalystService(TSharedPtr<IUECPAnalystService> Service) override { AnalystService = MoveTemp(Service); }
	virtual IUECPScannerService&     GetScannerService()     override { return ScannerService.IsValid() ? *ScannerService : *NullScannerService; }
	virtual void                     SetScannerService(TSharedPtr<IUECPScannerService> Service) override { ScannerService = MoveTemp(Service); }
	virtual IUECPBugReportService&   GetBugReportService()   override { return BugReportService.IsValid() ? *BugReportService : *NullBugReportService; }
	virtual void                     SetBugReportService(TSharedPtr<IUECPBugReportService> Service) override { BugReportService = MoveTemp(Service); }
	virtual IUECPAgentRunnerService& GetAgentRunnerService() override { return AgentRunnerService.IsValid() ? *AgentRunnerService : *NullAgentRunnerService; }
	virtual void                     SetAgentRunnerService(TSharedPtr<IUECPAgentRunnerService> Service) override { AgentRunnerService = MoveTemp(Service); }
	virtual IUECPACPRegistryService& GetACPRegistryService() override { return ACPRegistryService.IsValid() ? *ACPRegistryService : *NullACPRegistryService; }
	virtual void                     SetACPRegistryService(TSharedPtr<IUECPACPRegistryService> Service) override { ACPRegistryService = MoveTemp(Service); }
	virtual IUECPGddService&         GetGddService()         override { return GddService.IsValid() ? *GddService : *NullGddService; }
	virtual void                     SetGddService(TSharedPtr<IUECPGddService> Service) override { GddService = MoveTemp(Service); }
	virtual IUECPVoiceService&       GetVoiceService()       override { return VoiceService.IsValid() ? *VoiceService : *NullVoiceService; }
	virtual void                     SetVoiceService(TSharedPtr<IUECPVoiceService> Service) override { VoiceService = MoveTemp(Service); }
	virtual IUECPMcpInfoService&     GetMcpInfoService()     override { return McpInfoService.IsValid() ? *McpInfoService : *NullMcpInfoService; }
	virtual void                     SetMcpInfoService(TSharedPtr<IUECPMcpInfoService> Service) override { McpInfoService = MoveTemp(Service); }
	virtual IUECPCrewService&        GetCrewService()        override { return CrewService.IsValid() ? *CrewService : *NullCrewService; }
	virtual void                     SetCrewService(TSharedPtr<IUECPCrewService> Service) override { CrewService = MoveTemp(Service); }

private:
	FDelegateHandle OnAllModuleLoadingPhasesCompleteHandle;
	FDelegateHandle OnFEngineLoopInitCompleteHandle;

	TUniquePtr<FUECPLicenseServiceImpl>     LicenseService;
	TUniquePtr<FUECPModuleRegistryImpl>     ModuleRegistry;
	TUniquePtr<FUECPExtensionRegistryImpl>  ExtensionService;
	TUniquePtr<FUECPUserMcpServiceImpl>     UserMcpService;
	TUniquePtr<FUECPSettingsServiceImpl>    SettingsService;
	TSharedPtr<IUECPNotificationService>    NotificationService;
	TUniquePtr<FUECPNullNotificationService> NullNotificationService;
	TUniquePtr<FUECPToolDispatcherImpl>     ToolDispatcher;
	TUniquePtr<FUECPCreateAssetRegistryImpl> CreateAssetRegistry;

	TSharedPtr<IUECPLearningService>        LearningService;

	TUniquePtr<FUECPNullLearningService>    NullLearningService;

	TSharedPtr<IUECPAiMemoryService>        AiMemoryService;
	TUniquePtr<FUECPNullAiMemoryService>    NullAiMemoryService;

	TSharedPtr<IUECPAssetGenService>        AssetGenService;
	TUniquePtr<FUECPNullAssetGenService>    NullAssetGenService;

	TSharedPtr<IUECPArchitectService>       ArchitectService;
	TUniquePtr<FUECPNullArchitectService>   NullArchitectService;

	TSharedPtr<IUECPAnalystService>         AnalystService;
	TUniquePtr<FUECPNullAnalystService>     NullAnalystService;

	TSharedPtr<IUECPScannerService>         ScannerService;
	TUniquePtr<FUECPNullScannerService>     NullScannerService;

	TSharedPtr<IUECPBugReportService>       BugReportService;
	TUniquePtr<FUECPNullBugReportService>   NullBugReportService;

	TSharedPtr<IUECPAgentRunnerService>     AgentRunnerService;
	TUniquePtr<FUECPNullAgentRunnerService> NullAgentRunnerService;

	TSharedPtr<IUECPACPRegistryService>     ACPRegistryService;
	TUniquePtr<FUECPNullACPRegistryService> NullACPRegistryService;

	TSharedPtr<IUECPGddService>             GddService;
	TUniquePtr<FUECPNullGddService>         NullGddService;

	TSharedPtr<IUECPVoiceService>           VoiceService;
	TUniquePtr<FUECPNullVoiceService>       NullVoiceService;

	TSharedPtr<IUECPMcpInfoService>         McpInfoService;
	TUniquePtr<FUECPNullMcpInfoService>     NullMcpInfoService;

	TSharedPtr<IUECPCrewService>            CrewService;
	TUniquePtr<FUECPNullCrewService>        NullCrewService;
};

void FUECPCoreModule::StartupModule()
{
#if PLATFORM_MAC
	UECPMacIOSDetectionFix::ApplyIfNeeded();
#endif

	LicenseService      = MakeUnique<FUECPLicenseServiceImpl>();
	ModuleRegistry      = MakeUnique<FUECPModuleRegistryImpl>();
	ExtensionService    = MakeUnique<FUECPExtensionRegistryImpl>();
	UserMcpService      = MakeUnique<FUECPUserMcpServiceImpl>();
	SettingsService     = MakeUnique<FUECPSettingsServiceImpl>();
	NullNotificationService = MakeUnique<FUECPNullNotificationService>();
	ToolDispatcher      = MakeUnique<FUECPToolDispatcherImpl>();
	CreateAssetRegistry = MakeUnique<FUECPCreateAssetRegistryImpl>();
	NullLearningService = MakeUnique<FUECPNullLearningService>();
	NullAiMemoryService = MakeUnique<FUECPNullAiMemoryService>();
	NullAssetGenService = MakeUnique<FUECPNullAssetGenService>();
	NullArchitectService   = MakeUnique<FUECPNullArchitectService>();
	NullAnalystService     = MakeUnique<FUECPNullAnalystService>();
	NullScannerService     = MakeUnique<FUECPNullScannerService>();
	NullBugReportService   = MakeUnique<FUECPNullBugReportService>();
	NullAgentRunnerService = MakeUnique<FUECPNullAgentRunnerService>();
	NullACPRegistryService = MakeUnique<FUECPNullACPRegistryService>();
	NullGddService         = MakeUnique<FUECPNullGddService>();
	NullVoiceService       = MakeUnique<FUECPNullVoiceService>();
	NullMcpInfoService     = MakeUnique<FUECPNullMcpInfoService>();
	NullCrewService        = MakeUnique<FUECPNullCrewService>();

	using UECPToolSafety::RegisterToolSafety;
	using ::EUECPToolSafety;
	RegisterToolSafety(TEXT("move_asset"),       EUECPToolSafety::Destructive);
	RegisterToolSafety(TEXT("rename_asset"),     EUECPToolSafety::Destructive);
	RegisterToolSafety(TEXT("overwrite_asset"),  EUECPToolSafety::Destructive);
	RegisterToolSafety(TEXT("ask_user"),         EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("proceed_with_plan"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("task"),             EUECPToolSafety::PlanWrite);
	RegisterToolSafety(TEXT("project_plan"),     EUECPToolSafety::PlanWrite);
	RegisterToolSafety(TEXT("memory"),           EUECPToolSafety::PlanWrite);
	RegisterToolSafety(TEXT("working_notes"),    EUECPToolSafety::PlanWrite);
	RegisterToolSafety(TEXT("crew"),                 EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.dispatch_to"),     EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.report_back"),     EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.ask_orchestrator"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.complete"),        EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.escalate"),        EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.checkpoint_pass"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.checkpoint_fail"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.checkpoint_retry"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.request_verify"), EUECPToolSafety::Read);
	RegisterToolSafety(TEXT("crew.system_message"), EUECPToolSafety::Read);

	UE_LOG(LogUECPCore, Log, TEXT("FUECPCoreModule: StartupModule — services ready"));

	OnAllModuleLoadingPhasesCompleteHandle = FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda([this]()
	{
		if (ExtensionService.IsValid())
		{
			UECPExtensionManifest::ScanAndRegisterAll();

			if (UserMcpService.IsValid())
			{
				UserMcpService->Initialize();
			}

			UE_LOG(LogUECPCore, Log, TEXT("Extensions: refreshing after all phases complete"));
			ExtensionService->RefreshAll();
		}
	});
	OnFEngineLoopInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]()
	{
		if (ExtensionService.IsValid())
		{
			ExtensionService->RefreshAll();
		}
	});
}

void FUECPCoreModule::ShutdownModule()
{
	// Remove the core delegates first: their lambdas capture `this`, so leaving them
	// bound past module unload would dangle if they fired during a hot reload.
	if (OnAllModuleLoadingPhasesCompleteHandle.IsValid())
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.Remove(OnAllModuleLoadingPhasesCompleteHandle);
		OnAllModuleLoadingPhasesCompleteHandle.Reset();
	}
	if (OnFEngineLoopInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(OnFEngineLoopInitCompleteHandle);
		OnFEngineLoopInitCompleteHandle.Reset();
	}

	CrewService.Reset();
	NullCrewService.Reset();
	McpInfoService.Reset();
	NullMcpInfoService.Reset();
	VoiceService.Reset();
	NullVoiceService.Reset();
	GddService.Reset();
	NullGddService.Reset();
	ACPRegistryService.Reset();
	NullACPRegistryService.Reset();
	AgentRunnerService.Reset();
	NullAgentRunnerService.Reset();
	BugReportService.Reset();
	NullBugReportService.Reset();
	ScannerService.Reset();
	NullScannerService.Reset();
	AnalystService.Reset();
	NullAnalystService.Reset();
	ArchitectService.Reset();
	NullArchitectService.Reset();
	AssetGenService.Reset();
	NullAssetGenService.Reset();
	AiMemoryService.Reset();
	NullAiMemoryService.Reset();
	LearningService.Reset();
	NullLearningService.Reset();
	CreateAssetRegistry.Reset();
	ToolDispatcher.Reset();
	NotificationService.Reset();
	NullNotificationService.Reset();
	SettingsService.Reset();
	UserMcpService.Reset();
	ExtensionService.Reset();
	ModuleRegistry.Reset();
	LicenseService.Reset();

	UE_LOG(LogUECPCore, Log, TEXT("FUECPCoreModule: ShutdownModule"));
}

IUECPCoreModule& IUECPCoreModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUECPCoreModule>(TEXT("UECPCore"));
}

bool IUECPCoreModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("UECPCore"));
}

IMPLEMENT_MODULE(FUECPCoreModule, UECPCore)
