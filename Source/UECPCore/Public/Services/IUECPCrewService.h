// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class SUECPMainWidget;
class UUECPAppBridge;

struct FCrewRole;
struct FCrewTemplate;
struct FCrewCheckpoint;
struct FCrewHandoff;
struct FCrewRunCaps;
struct FCrewRun;
enum class ECrewRoleKind : uint8;
enum class ECheckpointState : uint8;
enum class EHandoffType : uint8;
enum class ECrewRunState : uint8;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnCrewRunChanged, const FCrewRun&);

DECLARE_MULTICAST_DELEGATE(FOnCrewTemplatesChanged);

class UECPCORE_API IUECPCrewService
{
public:
	virtual ~IUECPCrewService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual TArray<FCrewTemplate> GetTemplates() const = 0;

	virtual bool GetTemplate(const FString& TemplateId, FCrewTemplate& OutTemplate) const = 0;

	virtual FString CloneTemplate(const FString& SourceTemplateId, const FString& NewDisplayName) = 0;

	virtual FString CreateTemplate(const FString& DisplayName) = 0;

	virtual bool UpdateTemplate(const FCrewTemplate& Template) = 0;

	virtual bool DeleteTemplate(const FString& TemplateId) = 0;

	virtual TArray<FCrewRun> GetRuns() const = 0;

	virtual bool GetRun(const FGuid& RunId, FCrewRun& OutRun) const = 0;

	virtual FGuid GetActiveRunId() const = 0;

	virtual FGuid CreateRun(const FString& TemplateId, const FString& DisplayName) = 0;

	virtual bool SetRunRoles(const FGuid& RunId, const TArray<FCrewRole>& Roles) = 0;

	virtual bool SetRunPlan(const FGuid& RunId, const TArray<FCrewCheckpoint>& Plan,
		TArray<FString>* OutErrors = nullptr) = 0;

	virtual bool RetryCheckpoint(const FGuid& RunId, const FString& CheckpointId) = 0;

	virtual bool SetRoleApiKeySlot(const FGuid& RunId, const FString& RoleId, int32 SlotIndex) = 0;

	virtual FGuid ForkRun(const FGuid& SourceRunId, const FString& NewDisplayName) = 0;

	static bool ValidatePlan(const TArray<FCrewCheckpoint>& Plan, TArray<FString>& OutErrors);

	virtual bool SetRunCaps(const FGuid& RunId, const FCrewRunCaps& Caps) = 0;

	virtual bool SetRunDisplayName(const FGuid& RunId, const FString& DisplayName) = 0;

	virtual bool MarkRunReady(const FGuid& RunId) = 0;

	virtual bool StartRun(const FGuid& RunId, FString& OutReason) = 0;

	virtual bool PauseRun(const FGuid& RunId, const FString& Reason = FString()) = 0;

	virtual bool ResumeRun(const FGuid& RunId) = 0;

	virtual bool AnswerEscalation(const FGuid& RunId, const FString& AnswerText) = 0;

	virtual bool AbortRun(const FGuid& RunId, const FString& Reason) = 0;

	virtual bool CompleteRun(const FGuid& RunId, const FString& Summary) = 0;

	virtual bool DeleteRun(const FGuid& RunId) = 0;

	virtual bool AppendHandoff(const FGuid& RunId, const FCrewHandoff& Handoff) = 0;

	virtual bool SetCheckpointState(const FGuid& RunId, const FString& CheckpointId,
		ECheckpointState NewState, const FString& ResultSummary) = 0;

	virtual bool IsCrewChat(const FString& ChatId, FGuid& OutRunId, FString& OutRoleId) const = 0;

	virtual bool RouteInstructionDispatch(const FGuid& RunId, const FString& FromRoleId,
		const FString& ToRoleId, const FString& Instruction, const FString& CheckpointId,
		FString& OutDenyReason) = 0;

	virtual bool RouteRoleReport(const FGuid& RunId, const FString& FromRoleId,
		const FString& CheckpointId, const FString& Status, const FString& Summary,
		FString& OutDenyReason) = 0;

	virtual bool RouteRoleQuestion(const FGuid& RunId, const FString& FromRoleId,
		const FString& Question, FString& OutDenyReason) = 0;

	virtual bool IsToolAllowedForChat(const FString& ChatId, FName ToolName, FString& OutDenyReason) const = 0;

	virtual bool SetRoleAllowlistOnRun(const FGuid& RunId, const FString& RoleId,
		const TArray<FString>& Allowlist) = 0;

	virtual bool ShouldAutoApproveDestructiveForChat(const FString& ) const { return false; }

	virtual bool ExportTemplate(const FString& TemplateId, FString& OutJson) const = 0;

	virtual FString ImportTemplate(const FString& Json) = 0;

	virtual FOnCrewRunChanged&        OnRunChanged() = 0;
	virtual FOnCrewTemplatesChanged&  OnTemplatesChanged() = 0;
};
