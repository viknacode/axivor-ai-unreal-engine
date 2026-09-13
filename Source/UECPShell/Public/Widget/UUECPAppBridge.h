// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UUECPAppBridge.generated.h"

class SUECPMainWidget;
class SWebBrowser;

UCLASS()
class UECPSHELL_API UUECPAppBridge : public UObject
{
	GENERATED_BODY()

public:
	TWeakPtr<SUECPMainWidget> OwnerWidget;
	TWeakPtr<SWebBrowser> BrowserRef;

	FGuid SelectedCrewRunId;

	void SubscribeToCrewEvents();

	void PushCrewState();

	void PushCrewActiveRunDetail();

	UFUNCTION() void SwitchView(const FString& ViewName);
	UFUNCTION() void OpenSettings();
	UFUNCTION() void ShowOverlay(const FString& OverlayName);
	UFUNCTION() void HideOverlay();
	UFUNCTION() void OpenExternalUrl(const FString& Url);

	UFUNCTION() void SetAnnouncementsHidden(bool bHidden);

	UFUNCTION() void SendMessage(const FString& View, const FString& Text);
	UFUNCTION() void SendMessageNow(const FString& View, const FString& Text);
	UFUNCTION() void StopGeneration(const FString& View);
	UFUNCTION() void RemoveQueuedArchitectMessage(const FString& IndexStr);
	UFUNCTION() void SendQueuedNow(const FString& IndexStr);
	UFUNCTION() void NewChat(const FString& View);
	UFUNCTION() void SwitchChat(const FString& View, const FString& ChatId);
	UFUNCTION() void DeleteChat(const FString& View, const FString& ChatId);
	UFUNCTION() void DeleteAllChats(const FString& View, bool bIncludeCrew);
	UFUNCTION() void RenameChat(const FString& View, const FString& ChatId, const FString& NewName);

	UFUNCTION() void SetInteractionMode(const FString& Mode);

	UFUNCTION() void SetChatSlot(int32 SlotIndex);
	// Axivor AI — in-chat model picker (provider/agent/model/options chosen from the composer).
	UFUNCTION() void RequestModelPicker();
	UFUNCTION() void PickerSelectSlot(int32 SlotIndex, bool bPinToChat);
	UFUNCTION() void PickerSetAgentOption(const FString& AgentId, const FString& Key, const FString& Value);
	UFUNCTION() void PickerDiscoverAgent(const FString& AgentId);
	void PushModelPicker();
	// Axivor Turbo — UE 5.8 File Sandbox controls (routed to the 'engine_sandbox' tool).
	UFUNCTION() void SandboxCommand(const FString& Action);
	void PushSandboxState();
	UFUNCTION() void ConfirmTool(const FString& Action);

	UFUNCTION() void AnswerUserQuestions(const FString& AnswerJson);
	UFUNCTION() void AnalyzePlugin();

	UFUNCTION() void AttachImage(const FString& View);
	UFUNCTION() void RemoveImage(const FString& View, int32 Index);
	UFUNCTION() void ImportFileContext(const FString& View);
	UFUNCTION() void RemoveFileContext(const FString& View, int32 Index);

	UFUNCTION() void AddAssetContext();
	UFUNCTION() void AddNodeContext();

	UFUNCTION() void CopyToClipboard(const FString& Text);

	UFUNCTION() void ExportConversation(const FString& View, const FString& Format);
	UFUNCTION() void SaveDiagramPng(const FString& Base64Png, const FString& Kind);
	UFUNCTION() void OpenDiagramWindow(const FString& SvgHtml);

	UFUNCTION() void SearchAssets(const FString& View, const FString& Query);
	UFUNCTION() void SelectAssetRef(const FString& View, const FString& AssetPath, const FString& DisplayName, const FString& ClassLabel);
	UFUNCTION() void DismissAssetPicker(const FString& View);

	UFUNCTION() void PasteImageFromClipboard(const FString& View);
	UFUNCTION() void AddImageFromData(const FString& View, const FString& Base64, const FString& Mime);

	UFUNCTION() void ToggleMic();

	UFUNCTION() void CrewRefresh();
	UFUNCTION() void CrewSelectRun(const FString& RunId);
	UFUNCTION() void CrewOpenRoleChat(const FString& RunId, const FString& RoleId);
	UFUNCTION() void CrewCreateRun(const FString& TemplateId, const FString& DisplayName, const FString& PerRoleSlotsJson);
	UFUNCTION() void CrewListSlots();
	UFUNCTION() void CrewSetRunRoleSlot(const FString& RunId, const FString& RoleId, const FString& SlotIndex);
	UFUNCTION() void CrewStartRun(const FString& RunId);
	UFUNCTION() void CrewPauseRun(const FString& RunId);
	UFUNCTION() void CrewResumeRun(const FString& RunId);
	UFUNCTION() void CrewAbortRun(const FString& RunId);
	UFUNCTION() void CrewDeleteRun(const FString& RunId, bool bDeleteChats);
	UFUNCTION() void CrewDeleteRunChats(const FString& RunId);
	UFUNCTION() void CrewCreateTemplate(const FString& DisplayName);
	UFUNCTION() void CrewUpdateTemplate(const FString& TemplateId, const FString& TemplateJson);
	UFUNCTION() void CrewDeleteTemplate(const FString& TemplateId);
	UFUNCTION() void CrewDuplicateTemplate(const FString& TemplateId, const FString& NewName);
	UFUNCTION() void CrewSavePlanAsTemplateDefault(const FString& RunId);
	UFUNCTION() void CrewExportTemplate(const FString& TemplateId);
	UFUNCTION() void CrewImportTemplate();
	UFUNCTION() void CrewDraftPlanFromGoal(const FString& RunId, const FString& GoalText,
		int32 MinCheckpoints, int32 MaxCheckpoints);
	UFUNCTION() void CrewCancelPlanDraft(const FString& RunId);
	UFUNCTION() void CrewAppendRunCheckpoints(const FString& RunId, const FString& CheckpointsJson);
	UFUNCTION() void CrewGetRunReport(const FString& RunId);
	void CrewDraftPlanFromGoal_ViaArchitect(const FString& RunId, const FString& GoalText,
		int32 MinCheckpoints, int32 MaxCheckpoints);
	UFUNCTION() void CrewOpenPlanEditor(const FString& RunId);
	UFUNCTION() void CrewSetRunPlan(const FString& RunId, const FString& PlanJson);

	void SendCrewSetPlanResult(const FString& RunId, bool bOk, const TArray<FString>& Errors);

	UFUNCTION() void CrewSetRunCaps(const FString& RunId, const FString& CapsJson);
	UFUNCTION() void CrewGetHandoff(const FString& RunId, const FString& HandoffId);
	UFUNCTION() void CrewListAllTools();
	UFUNCTION() void CrewSetRoleAllowlist(const FString& RunId, const FString& RoleId,
		const FString& Scope, const FString& AllowlistJson);
	UFUNCTION() void CrewRetryCheckpoint(const FString& RunId, const FString& CheckpointId);
	UFUNCTION() void CrewForkRun(const FString& SourceRunId, const FString& NewDisplayName);
	UFUNCTION() void CrewSetRoleSlot(const FString& RunId, const FString& RoleId, int32 SlotIndex);
	UFUNCTION() void CrewAnswerEscalation(const FString& RunId, const FString& AnswerText);

	UFUNCTION() void SaveCustomInstructions(const FString& Text);
	UFUNCTION() void LoadCustomInstructions();

	UFUNCTION() void BrowseSkeletalMesh();
	UFUNCTION() void LoadBones(const FString& MeshPath);
	UFUNCTION() void PreviewSplitMesh(const FString& Json);
	UFUNCTION() void ExecuteSplitMesh(const FString& Json);
	UFUNCTION() void RunProfiler(int32 DurationSeconds);
	UFUNCTION() void BrowseModelSourceImage();

	UFUNCTION() void RequestImageUpload();
	UFUNCTION() void RequestClipboardImage();

	UFUNCTION() void ReviewChanges();
	UFUNCTION() void ReviewChangesBP(const FString& BpPath);
	void ReviewChangesForBP(const FString& BpPath);

	UFUNCTION() void OpenAssetLink(const FString& AssetRef);

	UFUNCTION() void RetryLastMessage();

	UFUNCTION() void DeleteMessage(const FString& View, int32 MsgIndex);
	UFUNCTION() void CompactConversation(const FString& View);
	UFUNCTION() void ToggleFastMode(const FString& View);
	UFUNCTION() void SwitchModel(const FString& View, const FString& ModelName);
	UFUNCTION() void SetThinkingEffort(const FString& View, const FString& Effort);
	UFUNCTION() void LoginAgent(const FString& AgentName);

	UFUNCTION() void ClearActivePlan();
	UFUNCTION() void LoadPlanPicker();
	UFUNCTION() void ImportActivePlan(const FString& SourceConvID);
	UFUNCTION() void DeletePlan(const FString& ConvID);

	UFUNCTION() void PreviewPlan(const FString& SourceConvID);

	UFUNCTION() void SetPlanBrief(const FString& Brief);
	UFUNCTION() void EditTask(const FString& IndexStr, const FString& NewContent);
	UFUNCTION() void AddTask(const FString& AtIndexStr, const FString& Content);
	UFUNCTION() void RemoveTask(const FString& IndexStr);
	UFUNCTION() void ReorderTask(const FString& FromStr, const FString& ToStr);
	UFUNCTION() void SetTaskStatus(const FString& IndexStr, const FString& Status);

	UFUNCTION() void ClearTasks();

	UFUNCTION() void GetClipboardText();

	UFUNCTION() void ShowNotification(const FString& Message);

	void PushSlashContext();

	void PushChatList(const FString& View, const FString& JsonArray);
	void PushChatHistory(const FString& View, const FString& JsonArray);
	void PushAppendMessage(const FString& View, const FString& Html);
	void PushUpdateLastMessage(const FString& View, const FString& Html);
	void PushTokenCount(const FString& View, int32 Count);
	void PushThinkingState(const FString& View, bool bShow, const FString& Label, const FString& Content);
	void PushLoadingState(const FString& View, bool bShow, const FString& Message);
	void PushToolConfirmation(const FString& ToolName, const FString& ArgsPreview);

	void PushUserQuestions(const FString& QuestionsJson);
	void PushAssetSearchResults(const FString& View, const FString& JsonArray);
	void PushImageAttachments(const FString& View, const FString& JsonArray);
	void PushFileContexts(const FString& View, const FString& JsonArray);

	void PushACPSlashCommands(const FString& AgentId, const FString& JsonArray);
	void PushScanStatus(const FString& StatusText);
	void PushScanState(bool bScanning);
	void PushToast(const FString& Message, const FString& Type);
	void PushMeshProgress(const FString& Id, const FString& Name, const FString& Phase, int32 Progress, const FString& ErrorMsg);
	void PushBanner(const FString& Message);
	void PushViewState(const FString& Json);

	static FString BuildAppHtmlDataUri();

	static FString BuildImagesJson(const TArray<struct FAttachedImage>& Images);
	static FString BuildFileContextsJson(const TArray<struct FAttachedFileContext>& Files);

	void ExecJs(const FString& Js);

	void PushOverlayHtml(const FString& Html);

	void PushPendingMemoryCount(int32 Count);

private:
	void PushBoneListFromWidget(SUECPMainWidget* W);

	struct FCrewDraftInFlight
	{
		FString RunId;
		bool    bIsAcp = false;
		TWeakPtr<class IHttpRequest, ESPMode::ThreadSafe> HttpReq;
		FString AcpDraftChatId;
		FString AcpOriginalActive;
		FDelegateHandle AcpHandle;
	};
	TMap<FString, TUniquePtr<FCrewDraftInFlight>> ActiveDrafts;
	void CancelInFlightDraft(const FString& RunId, bool bNotifyJs);
};
