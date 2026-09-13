// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPCore, Log, All);

class IUECPLicenseService;
class IUECPModuleRegistry;
class IUECPExtensionService;
class IUECPUserMcpService;
class IUECPSettingsService;
class IUECPNotificationService;
class IUECPToolDispatcher;
class IUECPCreateAssetRegistry;
class IUECPLearningService;
class IUECPAiMemoryService;
class IUECPAssetGenService;
class IUECPArchitectService;
class IUECPAnalystService;
class IUECPScannerService;
class IUECPBugReportService;
class IUECPAgentRunnerService;
class IUECPACPRegistryService;
class IUECPGddService;
class IUECPVoiceService;
class IUECPMcpInfoService;
class IUECPCrewService;

class UECPCORE_API IUECPCoreModule : public IModuleInterface
{
public:
	static IUECPCoreModule& Get();
	static bool IsAvailable();

	virtual IUECPLicenseService&     GetLicenseService() = 0;
	virtual IUECPModuleRegistry&     GetModuleRegistry() = 0;

	virtual IUECPExtensionService&   GetExtensionService() = 0;

	virtual IUECPUserMcpService&     GetUserMcpService() = 0;

	virtual IUECPSettingsService&    GetSettingsService() = 0;

	virtual IUECPNotificationService& GetNotificationService() = 0;
	virtual void                      SetNotificationService(TSharedPtr<IUECPNotificationService> Service) = 0;

	virtual IUECPToolDispatcher&     GetToolDispatcher() = 0;

	virtual IUECPCreateAssetRegistry& GetCreateAssetRegistry() = 0;

	virtual IUECPLearningService&    GetLearningService() = 0;
	virtual void                     SetLearningService(TSharedPtr<IUECPLearningService> Service) = 0;

	virtual IUECPAiMemoryService&    GetAiMemoryService() = 0;
	virtual void                     SetAiMemoryService(TSharedPtr<IUECPAiMemoryService> Service) = 0;

	virtual IUECPAssetGenService&    GetAssetGenService() = 0;
	virtual void                     SetAssetGenService(TSharedPtr<IUECPAssetGenService> Service) = 0;

	virtual IUECPArchitectService&   GetArchitectService() = 0;
	virtual void                     SetArchitectService(TSharedPtr<IUECPArchitectService> Service) = 0;

	virtual IUECPAnalystService&     GetAnalystService() = 0;
	virtual void                     SetAnalystService(TSharedPtr<IUECPAnalystService> Service) = 0;

	virtual IUECPScannerService&     GetScannerService() = 0;
	virtual void                     SetScannerService(TSharedPtr<IUECPScannerService> Service) = 0;

	virtual IUECPBugReportService&   GetBugReportService() = 0;
	virtual void                     SetBugReportService(TSharedPtr<IUECPBugReportService> Service) = 0;

	virtual IUECPAgentRunnerService& GetAgentRunnerService() = 0;
	virtual void                     SetAgentRunnerService(TSharedPtr<IUECPAgentRunnerService> Service) = 0;

	virtual IUECPACPRegistryService& GetACPRegistryService() = 0;
	virtual void                     SetACPRegistryService(TSharedPtr<IUECPACPRegistryService> Service) = 0;

	virtual IUECPGddService&         GetGddService() = 0;
	virtual void                     SetGddService(TSharedPtr<IUECPGddService> Service) = 0;

	virtual IUECPVoiceService&       GetVoiceService() = 0;
	virtual void                     SetVoiceService(TSharedPtr<IUECPVoiceService> Service) = 0;

	virtual IUECPMcpInfoService&     GetMcpInfoService() = 0;
	virtual void                     SetMcpInfoService(TSharedPtr<IUECPMcpInfoService> Service) = 0;

	virtual IUECPCrewService&        GetCrewService() = 0;
	virtual void                     SetCrewService(TSharedPtr<IUECPCrewService> Service) = 0;
};
