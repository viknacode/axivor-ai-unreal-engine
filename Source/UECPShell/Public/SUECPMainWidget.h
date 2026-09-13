// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPShell, Log, All);
#include "Widgets/SCompoundWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Viewports/SBodyZoneSelector.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/STextComboBox.h"
#include "Dom/JsonObject.h"
#include "AssetReferenceTypes.h"
#include "AgentRunnerTypes.h"
#include "Services/ACPTypes.h"
#include "ApiKeyManager.h"
#include "TextureGenManager.h"
#include "UIConfigManager.h"
#include "Managers/ThemeManager.h"
#include "Managers/SettingsManager.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/HttpCommunicationManager.h"
#include "Managers/EditorProfileSync.h"

class SUECPMainWidget;
class SMultiLineEditableTextBox;
class STextBlock;
class SWebBrowser;
class SWidgetSwitcher;
class UUECPSettingsBridge;
class UUECPAppBridge;
class UUECPMeshyBridge;
class UUECPImageGenBridge;
class UUECPGddBridge;
class UUECPMemoryBridge;
class UUECPBugReportBridge;
class UUECPScannerBridge;
class SEditableTextBox;
class SSplitter;
class SUECPAssetMentionPopup;
class SButton;
class SImage;
class SCheckBox;
class FSocket;

enum class EAnalystView : uint8
{
	BlueprintArchitect,
	AssetAnalyst,
	ProjectScanner
};

struct FToolCallExtractionResult
{
	bool bHasToolCall = false;
	bool bExplicitContinue = false;
	bool bExplicitDone = false;
	bool bDoneFieldPresent = false;
	bool bEnvelopeMatched = false;
	FString ToolName;
	TSharedPtr<FJsonObject> Arguments;
	FString ConversationalText;
};

struct FToolExecutionResult
{
	bool bSuccess = false;
	bool bIsPending = false;
	FString ResultJson;
	FString SummaryJson;
	FString ErrorMessage;
};

struct FAttachedImage
{
	FString Name;
	FString MimeType;
	FString Base64Data;
	int32 Width = 0;
	int32 Height = 0;
};

struct FAttachedFileContext
{
	FString FileName;
	FString Content;
	int32 CharCount;
	bool bExpanded;
};

struct FQueuedArchitectMessage
{
	FString Text;
	TArray<FAttachedFileContext> Files;
	TArray<FAttachedImage>       Images;
};

struct FFreeTierRateLimit
{
	int64   Limit     = 0;
	int64   Used      = 0;
	int64   Remaining = 0;
	FString ResetAt;
	bool    bValid    = false;
	bool    bEnforced = true;
};

struct FProviderRateLimitWindow
{
	int64   Limit     = 0;
	int64   Remaining = 0;
	int64   Used      = 0;
	FString ResetAt;
	bool    bValid    = false;
};

struct FProviderApiRateLimits
{
	FProviderRateLimitWindow Tokens;
	FProviderRateLimitWindow Requests;
	FDateTime                CapturedAt;
	bool                     bValid = false;
};

class SConversationListRow : public STableRow<TSharedPtr<FConversationInfo>>
{
public:
	SLATE_BEGIN_ARGS(SConversationListRow) {}
		SLATE_ARGUMENT(TSharedPtr<FConversationInfo>, ConversationInfo)
		SLATE_ARGUMENT(TWeakPtr<SUECPMainWidget>, ScribeWidget)
		SLATE_ARGUMENT(EAnalystView, ViewType)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	void EnterEditMode();

private:
	void OnRenameTextCommitted(const FText& InText, ETextCommit::Type InCommitType);
	TSharedPtr<FConversationInfo> ConversationInfo;
	TWeakPtr<SUECPMainWidget> ScribeWidget;
	TSharedPtr<SWidgetSwitcher> WidgetSwitcher;
	TSharedPtr<SEditableTextBox> RenameTextBox;
	EAnalystView ViewType;
};

class FBrowserToolServerWorker : public FRunnable
{
public:
	FBrowserToolServerWorker(FSocket* InListenSocket, SUECPMainWidget* InOwner);
	virtual ~FBrowserToolServerWorker();
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

private:

	bool ParseHttpRequest(const FString& RawRequest, FString& OutMethod, FString& OutBody);

	FString BuildHttpResponse(int32 StatusCode, const FString& Body);

	// Dispatches a tool onto the game thread with a bounded, use-after-free-safe wait.
	// Static member (not a free function) so it can reach SUECPMainWidget::DispatchToolCall
	// through this class's friendship.
	static FToolExecutionResult ExecuteBrowserToolCall(SUECPMainWidget* Owner, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments);

	FSocket* ListenSocket;
	FThreadSafeBool bStopping;
	SUECPMainWidget* OwnerWidget;
};

class UECPSHELL_API SUECPMainWidget : public SCompoundWidget
{
	friend class SConversationListRow;
	friend class FBGAInputProcessor;
	friend class FBrowserToolServerWorker;
	friend class UUECPSettingsBridge;
	friend class UUECPAppBridge;
	friend class UUECPMeshyBridge;
	friend class UUECPScannerBridge;
	friend class FUECPScannerCoordinator;
	friend class FUECPArchitectCoordinator;
	friend class FUECPAgentRunnerCoordinator;
	friend class FUECPBugReportCoordinator;

public:
	SLATE_BEGIN_ARGS(SUECPMainWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SUECPMainWidget();
	TSharedPtr<SWidget> OnGetContextMenuForChatList(EAnalystView ForView, TSharedPtr<FConversationInfo> TargetItem = nullptr);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	void ArrangeActiveGraph();

	void PushAppearanceToAppShell(const FString& Json);

	void RouteToast(const FString& Message, const FString& Severity);

private:
	TSharedRef<SWidget> CreateSettingsWidget();
	TSharedRef<SWidget> CreateSlotConfigSection();
	void OnProviderChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void UpdateCustomFieldsVisibility();
	FReply OnShowSettingsClicked();
	FReply OnSaveSettingsClicked();
	void LoadSettings();
	void SaveSettings();

	void OnSlotProviderChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, int32 SlotIndex);
	FReply OnSaveSlotClicked(int32 SlotIndex);
	FReply OnClearSlotClicked(int32 SlotIndex);
	void RefreshSlotUI();
	void RebuildSettingsWidget();
	FReply OnSwitchToSlotClicked(int32 SlotIndex);
	bool HandleGlobalKeyPress(const FKeyEvent& InKeyEvent);

	TSharedRef<ITableRow> OnGenerateRowForChatList(TSharedPtr<FConversationInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnChatSelectionChanged(TSharedPtr<FConversationInfo> InItem, ESelectInfo::Type SelectInfo);
	FReply OnNewChatClicked();
	FReply OnSendClicked();
	FReply OnAddAssetContextClicked();
	FReply OnAddNodeContextClicked();

	FReply OnAddArchitectAssetContextClicked();
	FReply OnAddArchitectNodeContextClicked();
	FReply OnImportArchitectContextClicked();

	FReply OnAttachAnalystImageClicked();
	FReply OnAttachArchitectImageClicked();
	FReply OnAttachProjectImageClicked();
	void LoadAndAttachImage(const FString& ImagePath, TArray<FAttachedImage>& Attachments);
	void RefreshAnalystImagePreview();
	void RefreshArchitectImagePreview();
	void RefreshProjectImagePreview();
	bool TryPasteImageFromClipboard(TArray<FAttachedImage>& Attachments);

	void OnArchitectInputTextChanged(const FText& NewText);
	void ShowAssetPicker();
	void ShowAssetPickerForProject();
	void OnAssetRefSelected(const FAssetRefItem& SelectedItem);
	void OnAssetPickerDismissed();

	void RefreshChatHistoryView();
	void OnBrowserLoadError();

	bool OnBrowserLoadUrl(const FString& Method, const FString& Url, FString& Response);

	FString FindAssetPathByName(const FString& AssetName);

	FString LinkifyAssetNames(const FString& Text);

	void SaveManifest();
	void LoadManifest();
	void SaveChatHistory(const FString& ChatID);
	void LoadChatHistory(const FString& ChatID);

	TSharedPtr<SWidgetSwitcher> MainSwitcher;
	TSharedPtr<SBox> SettingsContentBox;
	TSharedPtr<SBox> EntireSettingsBox;
	TSharedPtr<STextBlock> ActiveSlotTextBlock;
	TArray<TSharedPtr<SWidget>> SlotButtonWidgets;
	TSharedPtr<STextComboBox> ProviderComboBox;
	TSharedPtr<SEditableTextBox> ApiKeyInput;
	TSharedPtr<SBox> CustomBaseURLWrapper;
	TSharedPtr<SEditableTextBox> CustomBaseURLInput;
	TSharedPtr<SBox> CustomModelNameWrapper;
	TSharedPtr<SEditableTextBox> CustomModelNameInput;
	TSharedPtr<SListView<TSharedPtr<FConversationInfo>>> ChatListView;
	TSharedPtr<SMultiLineEditableTextBox> InputTextBox;

	FApiSettings CurrentSettings;
	TArray<TSharedPtr<FString>> ProviderOptions;
	TArray<TSharedPtr<FString>> SlotProviderOptions;
	TArray<TSharedPtr<FString>> SlotAgentOptions;
	TArray<TSharedPtr<FString>> GeminiModelOptions;
	TArray<TSharedPtr<FString>> OpenAIModelOptions;
	TArray<TSharedPtr<FString>> ClaudeModelOptions;
	TArray<TSharedPtr<FString>> ThemePresetOptions;
	TArray<TSharedPtr<FString>> LanguageOptions;
	TArray<TSharedPtr<FConversationInfo>> ConversationList;
	TArray<TSharedPtr<FJsonValue>> AnalystConversationHistory;
	FString ActiveChatID;
	FString PendingContext;
	bool bIsThinking = false;

	TArray<FAttachedImage> AnalystAttachedImages;

	TArray<FAttachedFileContext> AnalystAttachedFiles;

	TSharedPtr<STextBlock> AnalystTokenCounterText;

	FString CustomInstructions;

	FReply OnAnalyzePluginClicked();
	void AnalyzePluginSource(const FString& PluginPath);

	bool bAppShellInitialized = false;
	FDelegateHandle UIConfigRefreshHandle;
	FDelegateHandle MeshProgressHandle;
	TMap<FString, TWeakPtr<SNotificationItem>> MeshProgressNotifications;
	TSharedPtr<STextBlock> ScanStatusTextBlock;
	TSharedPtr<SButton> ScanProjectButton;

	TArray<TSharedPtr<FJsonValue>> ProjectConversationHistory;
	bool bIsScanning = false;
	bool bIsProjectThinking = false;
	FString CurrentProjectStepInfo;
	TMap<FString, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> PendingProjectRequests;
	FString CachedProjectChatShellHtml;

	TArray<FAttachedImage> ProjectAttachedImages;

	void UpdateScanStatus(const FString& Message, bool bIsError = false);
	void CheckForExistingIndex();
	FReply OnProjectInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	void OnProjectInputTextChanged(const FText& NewText);
	FReply OnSendProjectQuestionClicked();
	FReply OnStopProjectClicked();
	void SendProjectChatRequest();
	void OnProjectApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void RefreshProjectChatView();

	TArray<TSharedPtr<FConversationInfo>> ProjectConversationList;

	TSharedPtr<SListView<TSharedPtr<FConversationInfo>>> ProjectChatListView;
	FString ActiveProjectChatID;

	void SaveProjectManifest();
	void LoadProjectManifest();
	void SaveProjectChatHistory(const FString& ChatID);
	void LoadProjectChatHistory(const FString& ChatID);
	TSharedRef<ITableRow> OnGenerateRowForProjectChatList(TSharedPtr<FConversationInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnProjectChatSelectionChanged(TSharedPtr<FConversationInfo> InItem, ESelectInfo::Type SelectInfo);
	FReply OnNewProjectChatClicked();

	TArray<TSharedPtr<FConversationInfo>> ArchitectConversationList;

	TSharedPtr<SListView<TSharedPtr<FConversationInfo>>> ArchitectChatListView;

	TArray<TSharedPtr<FJsonValue>> ArchitectConversationHistory;

	FString ActiveArchitectChatID;

	FString PendingArchitectContext;

	TArray<FAttachedImage> ArchitectAttachedImages;
	TArray<FAttachedFileContext> ArchitectAttachedFiles;

	TMap<FString, TArray<FQueuedArchitectMessage>> ArchitectMessageQueueByChat;

	TSharedPtr<SUECPAssetMentionPopup> ArchitectAssetPickerPopup;
	TSharedPtr<SUECPAssetMentionPopup> ProjectAssetPickerPopup;
	int32 AtSymbolPosition = INDEX_NONE;
	bool bAssetPickerOpen = false;
	int32 AssetPickerSourceView = 0;
	FString PreviousArchitectInputText;
	FString PreviousProjectInputText;

	bool bIsArchitectThinking = false;

	FString ArchitectUiActiveChatOverride;
	bool bIsExportingTemplate = false;

	bool bUserCancelledRequest = false;

	FString CompactPendingChatID;

	FString PreFastModeEffort;

	TArray<TSharedPtr<FJsonValue>> CachedTemplateMetadata;
	bool bTemplatesCacheFetched = false;
	void FetchTemplateCacheAsync();

	int32 LastArchitectUserMessageLength;

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveArchitectHttpRequest;

	FString PendingArchitectChatID;

	TMap<FString, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> PendingArchitectRequests;

	TSet<FString> ArchitectThinkingChats;

	FFreeTierRateLimit FreeTierRateLimit;

	void ParseAndPushFreeTierRateLimit(FHttpResponsePtr Response);

	TMap<FString, FProviderApiRateLimits> ProviderApiRateLimits;

	void ParseAndCacheProviderRateLimit(FHttpResponsePtr Response, const FString& ProviderSlug);

	TMap<FString, FString> ArchitectWorkingNotesByChat;

	TMap<FString, int32> PlanProposalHistIdxByChat;

	bool IsCurrentConversationThinking() const { return ArchitectThinkingChats.Contains(ActiveArchitectChatID) || PendingArchitectRequests.Contains(ActiveArchitectChatID) || AgentInstances.Contains(ActiveArchitectChatID); }

	void OnAssetUpdated(FString AssetName);

	void OnUpdateCheckCompleted();

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveTextureGenRequest;
	bool bIsTextureGenerating = false;
	bool bHasGeneratedTexture = false;

	TSharedPtr<SEditableTextBox> TexturePromptInput;
	TSharedPtr<STextComboBox> AspectRatioComboBox;
	TSharedPtr<SButton> GenerateTextureButton;
	TSharedPtr<SButton> CancelTextureGenButton;
	TSharedPtr<STextBlock> TextureGenStatusText;
	TSharedPtr<SImage> TexturePreviewImage;
	TArray<TSharedPtr<FString>> AspectRatioOptions;
	FTextureGenResult LastTextureGenResult;

	FAttachedImage SourceImageAttachment;

	struct FPBRMaterialGenState
	{
		FString MaterialName;
		FString Description;
		FString SavePath;
		bool bIsMetallic = false;
		bool bGenerateAO = true;
		int32 CurrentStep = 0;
		int32 TotalSteps = 5;
		FString BaseColorPath;
		FString NormalPath;
		FString RoughnessPath;
		FString MetallicPath;
		FString AOPath;
		bool bIsGenerating = false;
	};
	FPBRMaterialGenState ActivePBRMaterialGen;

	struct FSlotUIWidgets
	{
		FString CurrentProvider;
		TSharedPtr<SEditableTextBox> NameInput;
		TSharedPtr<STextComboBox> ProviderComboBox;
		TSharedPtr<STextComboBox> AgentSubComboBox;
		TSharedPtr<SEditableTextBox> ApiKeyInput;
		TSharedPtr<SBox> CustomFieldsWrapper;
		TSharedPtr<SEditableTextBox> CustomBaseURLInput;
		TSharedPtr<SEditableTextBox> CustomModelNameInput;
		TSharedPtr<STextBlock> StatusText;
		TSharedPtr<SBox> GeminiModelWrapper;
		TSharedPtr<STextComboBox> GeminiModelComboBox;
		TSharedPtr<SBox> OpenAIModelWrapper;
		TSharedPtr<STextComboBox> OpenAIModelComboBox;
		TSharedPtr<SBox> ClaudeModelWrapper;
		TSharedPtr<STextComboBox> ClaudeModelComboBox;

		TSharedPtr<SMultiLineEditableTextBox> CustomParamsInput;

		TSharedPtr<SEditableTextBox> TextureGenApiKeyInput;
		TSharedPtr<SEditableTextBox> TextureGenEndpointInput;
		TSharedPtr<SEditableTextBox> TextureGenModelInput;
		TSharedPtr<SCheckBox> TextureGenModeCheckBox;
	};
	FSlotUIWidgets SlotWidgets[MAX_API_KEY_SLOTS];
	TSharedPtr<SButton> SlotButtons[MAX_API_KEY_SLOTS];
	int32 CurrentlyEditingSlotIndex;

	EAIInteractionMode ArchitectInteractionMode = EAIInteractionMode::AutoEdit;

	TMap<FString, EAIInteractionMode> ArchitectInteractionModeByChat;

	TMap<FString, int32> ArchitectApiKeySlotByChat;

	EAIInteractionMode GetActiveArchitectInteractionMode() const;

	EAIInteractionMode GetArchitectInteractionModeForChat(const FString& ChatID) const;

	static FString InteractionModeToString(EAIInteractionMode Mode);
	static EAIInteractionMode StringToInteractionMode(const FString& Str, EAIInteractionMode Fallback = EAIInteractionMode::AutoEdit);

	bool bConfirmationPending = false;
	bool bBypassAskBeforeEditOnce = false;
	FString PendingConfirmToolName;
	TSharedPtr<FJsonObject> PendingConfirmArguments;
	FString PendingConfirmArgsPreview;

	FString PendingConfirmChatID;

	// Axivor: bumped on every confirm prompt so the auto-decide timer only ever
	// resolves the prompt it was armed for (never a later one).
	int32 ConfirmGateSerial = 0;

	bool bAgentConfirmPending = false;

	mutable FCriticalSection MCPConfirmSlotMutex;

	mutable FCriticalSection MCPConfirmTagLock;
	FString MCPPendingConfirmTool;
	TAtomic<int32> MCPPendingConfirmDecision { 0 };
	class FEvent* MCPPendingConfirmEvent = nullptr;

	mutable FCriticalSection MCPQuestionSlotMutex;

	mutable FCriticalSection MCPQuestionTagLock;
	FString MCPPendingQuestionTag;
	FString MCPPendingQuestionAnswer;
	class FEvent* MCPPendingQuestionEvent = nullptr;

public:

	enum class EMCPConfirmDecision : uint8 { Proceed, Skip, Stop, Timeout, Busy };
	EMCPConfirmDecision RequestMCPDestructiveConfirm(const FString& ToolName,
		const FString& ArgsPreview, double TimeoutSecs);

	FString RequestUserQuestions(const FString& QuestionsJson, double TimeoutSecs);

	void OnUserQuestionsAnswered(const FString& AnswerJson);
private:

	FReply OnArchitectViewSelected();

	FReply OnNewArchitectChatClicked();

	FReply OnArchitectInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);

	FReply OnSendArchitectMessageClicked();

	FReply OnStopClicked();

	void OnInteractionModeChanged(EAIInteractionMode NewMode);

	void OnJustChatModeClicked(ECheckBoxState NewState);
	void OnAskBeforeEditModeClicked(ECheckBoxState NewState);
	void OnAutoEditModeClicked(ECheckBoxState NewState);

	bool IsReadOnlyTool(const FString& ToolName) const;

	// Axivor: one-shot timer that resolves an unanswered confirm prompt on its own,
	// so a run never stalls waiting for a click that is not coming.
	void ArmConfirmAutoDecide(const FString& ToolName);
	FReply OnConfirmToolProceed();
	FReply OnConfirmToolSkip();
	FReply OnConfirmToolStop();

	void OnAgentConfirmAction(const FString& Decision);

	TSharedRef<ITableRow> OnGenerateRowForArchitectChatList(TSharedPtr<FConversationInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	void OnArchitectChatSelectionChanged(TSharedPtr<FConversationInfo> InItem, ESelectInfo::Type SelectInfo);

	void SaveArchitectManifest();

	void LoadArchitectManifest();

	void SaveArchitectChatHistory(const FString& ChatID);

	void LoadArchitectChatHistory(const FString& ChatID);

	void DeleteMessageFromHistory(const FString& View, int32 MsgIndex);

	bool MaybeFinalizeCompactReplacement(const FString& ChatID);

	void DrainQueuedArchitectMessage(const FString& ChatID);

	void PushArchitectQueueStateToJs();

	void SaveArchitectWorkingNotes(const FString& ChatID);

	void LoadArchitectWorkingNotes(const FString& ChatID);

	FString GetArchitectWorkingNotesForAI(const FString& ChatID) const;

	void CaptureArchitectDurableNotesFromToolResult(const FString& ToolName, const FToolExecutionResult& Result);
	void UpdateArchitectBlueprintVerificationState(const FString& ToolName, const FToolExecutionResult& Result);
	bool HasArchitectBlueprintVerificationIssues() const;
	FString BuildArchitectBlueprintVerificationFeedback() const;
	bool HasArchitectPendingStructuralInspection() const;
	FString BuildArchitectPendingStructuralInspectionFeedback() const;
	bool HasArchitectUnsavedLevelIssue() const;
	FString BuildArchitectUnsavedLevelFeedback() const;

	/**
	 * Post-turn verification gate for the native tool-calling loops. Checks, in order, the
	 * blueprint health issues, the pending structural inspections and the unsaved-level state;
	 * when one fails and the bounce budget (MaxArchitectVerificationBounces) is not exhausted,
	 * fills OutFeedback with the message to inject as a synthetic user turn, consumes one bounce
	 * and returns true (the model must continue). Returns false when the turn may end.
	 */
	bool BuildArchitectVerificationFeedback(const FString& ChatID, FString& OutFeedback);

	/**
	 * Feeds one native-loop tool result into the verification state (health issues, pending
	 * inspections, level-touch tracking). ToolLabel is the loop's display label
	 * ("umbrella · action" or a bare tool name).
	 */
	void NoteArchitectToolResultForVerification(const FString& ToolLabel, bool bSuccess, const FString& ResultJson);

	void RefreshAllChatViews();

	void RefreshArchitectChatView();

	void SendArchitectChatRequest();

	void InvalidatePromptCaches();

	int32 EstimateFullRequestTokens(const TArray<TSharedPtr<FJsonValue>>& Messages);

	static TArray<TSharedPtr<FJsonValue>> WindowConversationHistory(
		const TArray<TSharedPtr<FJsonValue>>& FullHistory,
		int32 MaxCharBudget,
		int32 MinRecentMessages = 8);

	void CompressOldToolResults(int32 KeepRecentCount = 12);

	void LoadArchitectChatShell();

	void UpdateArchitectThinkingState();

	FString RenderSingleArchitectMessageHtml(const TSharedPtr<FJsonObject>& MessageObject, int32 MessageIndex);

	static FString EscapeForJavascript(const FString& Input);

	static FString FormatHttpError(bool bWasSuccessful, FHttpResponsePtr Response, const FString& ProviderHint = TEXT(""));

	bool bArchitectChatShellLoaded = false;

	bool bSuppressChatViewRefresh = false;

	int32 LastRenderedArchitectHistoryCount = 0;
	FString LastRenderedArchitectChatId;

	bool LastRenderedArchitectFromAgent = false;

	struct FArchitectRangeKey
	{
		int32 Start = 0;
		int32 End   = 0;

		uint32 ContentHash = 0;
	};
	TArray<FArchitectRangeKey> LastRenderedArchitectRanges;

	struct FArchitectRangeCacheEntry
	{
		int32  End         = 0;
		uint32 ContentHash = 0;
		FString Html;
	};
	TMap<int32, FArchitectRangeCacheEntry> ArchitectRangeHtmlCache;

	double LastArchitectRefreshSeconds = 0.0;

	bool bArchitectRefreshPending = false;

	bool bArchitectForceFullRebuildOnce = false;

	int32 ShellLoadRetryCount = 0;

	FString CachedArchitectChatShellHtml;

	void ProcessPBRMaterialGenerationStep();

	void OnPBRTextureGenerated(const FTextureGenResult& Result);

	void CreatePBRMaterialFromGeneratedTextures();

	TArray<TSharedPtr<FString>> BoneList;
	TSet<FString> SelectedBones;
	FString LoadedMeshPath;
	TMap<EBodyZone, FString> ZoneToBoneMap;

	void OnMeshSplitterLoadBones();

	void UpdateConversationTokens(const FString& ChatID, int32 PromptTokens, int32 CompletionTokens = 0);

	int32 EstimateConversationTokens(const TArray<TSharedPtr<FJsonValue>>& ConversationHistory);

	int32 CurrentToolCallDepth = 0;

	int32 MaxToolCallDepth = 200;

	FString LastDispatchedToolName;
	FString LastDispatchedToolArgsHash;
	int32 ConsecutiveSameToolCount = 0;
	static constexpr int32 MaxConsecutiveSameTool = 25;

	FString CurrentToolStepInfo;

	int32 MaxBatchSize = 5;

	TSet<FString> SessionModifiedBlueprintPaths;

	TSet<FString> ArchitectModifiedBPsThisTurn;
	TSet<FString> SessionBlueprintsPendingStructuralInspection;
	TMap<FString, FString> SessionArchitectBlueprintVerificationIssues;
	int32 SessionArchitectVerificationBounceCount = 0;
	static constexpr int32 MaxArchitectVerificationBounces = 12;

	bool bSessionTouchedLevel = false;

	UPROPERTY()
	TMap<FString, TObjectPtr<UBlueprint>> DiffSnapshots;
	FString DiffSnapshotBpPath;
	FString PendingDiffHtml;

	void SaveDiffSnapshotsToDisk();
	void RestoreDiffSnapshotsFromDisk();
	void ClearDiffSnapshots();
	FString BuildDiffBarHtml();

	UBlueprint* CreateBlueprintDiffSnapshot(const FString& BlueprintPath);

	FDelegateHandle PreBeginPIEHandle;

	TSharedPtr<FJsonObject> CachedApiCheatsheet;

	FBpGeneratorTheme CurrentTheme;

	void ApplyTheme();

	void LoadThemeSettings();

	void SaveThemeSettings();

	TSharedPtr<SBorder> CreateThemedBorder(TSharedPtr<SWidget> Content, const FMargin& Padding = FMargin(8));

	TSharedPtr<SButton> CreateThemedButton(FText Text, FOnClicked OnClicked, const FLinearColor& OverrideColor = FLinearColor::Transparent);

	TSharedPtr<STextBlock> CreateThemedTextBlock(FText Text, bool bIsPrimary = true);

	void OnThemePresetChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnLanguageChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	static FString LanguageNameToCode(const FString& LanguageName);

	FToolCallExtractionResult ExtractToolCallFromResponse(const FString& AiResponse);

	int32 FindMatchingClosingBrace(const FString& Haystack, int32 StartIndex);

	bool TryParseToolCallJson(const FString& JsonString, FToolCallExtractionResult& OutResult);

	void ContinueConversationWithToolResult(const FString& ToolName, const FToolExecutionResult& Result, const FString& OriginalChatID = FString());

	void ContinueConversationWithBatchResult(const TArray<TPair<FString, FToolExecutionResult>>& Results, const FString& OriginalChatID = FString());

	bool SwapToArchitectChatContext(const FString& TargetChatID, FString& OutSavedChatID,
		TArray<TSharedPtr<FJsonValue>>& OutSavedHistory, FString& OutSavedStepInfo,
		TSet<FString>& OutSavedModifiedBPs, bool& OutSavedThinkingState);
	void RestoreArchitectChatContext(const FString& SavedChatID,
		TArray<TSharedPtr<FJsonValue>>& SavedHistory, const FString& SavedStepInfo,
		const TSet<FString>& SavedModifiedBPs, bool SavedThinkingState);

	FToolExecutionResult DispatchToolCall(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments);

	FToolExecutionResult ExecuteTool_GeneratePBRMaterial(const TSharedPtr<FJsonObject>& Args);
	FToolExecutionResult ExecuteTool_RetextureMesh(const TSharedPtr<FJsonObject>& Args);

	FToolExecutionResult ExecuteTool_GenerateTexture(const TSharedPtr<FJsonObject>& Args);

	FToolExecutionResult ExecuteTool_SelectFolder(const TSharedPtr<FJsonObject>& Args);

	FReply OnExportTemplateClicked();

	FToolExecutionResult ExecuteTool_ExportBlueprintTemplate(const TSharedPtr<FJsonObject>& Args);

	FToolExecutionResult ExecuteTool_SearchBlueprintTemplates(const TSharedPtr<FJsonObject>& Args);

	FToolExecutionResult ExecuteTool_ApplyBlueprintTemplate(const TSharedPtr<FJsonObject>& Args);

	FString AutoDetectCategory(const FString& Name, const FString& Description);

	bool TryDispatchMaterialTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	FToolExecutionResult ExecuteTool_GenerateSound(const TSharedPtr<FJsonObject>& Args);

	bool TryDispatchCollisionMobilityTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);
	bool TryDispatchAudioDepthTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	bool TryDispatchTemplatesTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	bool TryDispatchBlueprintTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);
	bool TryDispatchAssetCoreTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	FToolExecutionResult ExecuteTool_OpenProjectDashboard(const TSharedPtr<FJsonObject>& Args);
	bool TryDispatchProjectVizTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	bool bIsProfiling = false;
	int32 ProfileDurationSeconds = 10;
	FString LastProfileResultJson;
	TWeakPtr<SWindow> ActiveDashboardWindow;

	FReply OnToggleSidebarClicked();

	void SetSidebarVisible(bool bVisible);

	EVisibility GetLoadingIndicatorVisibility() const;
	bool IsAnyViewThinking() const;

	TSharedRef<SWidget> CreateBurgerMenuButton();

	TSharedRef<SWidget> CreateLoadingIndicator();

	bool bSidebarVisible = true;

	TSharedPtr<SSplitter> ArchitectSplitter;
	TSharedPtr<SSplitter> AnalystSplitter;
	TSharedPtr<SSplitter> ProjectSplitter;

	TSharedPtr<STextBlock> LoadingIndicatorText;

	double LoadingAnimationTime = 0.0f;

	TSharedPtr<class IInputProcessor> ArrangeInputProcessor;

	struct FKeybindConfig
	{
		FKey ModifierKey = EKeys::LeftAlt;
		FKey ActionKey = EKeys::R;
		FString GetDisplayString() const
		{
			FString Mod = (ModifierKey == EKeys::Invalid) ? TEXT("") : ModifierKey.GetDisplayName().ToString();
			FString Act = ActionKey.GetDisplayName().ToString();
			return Mod.IsEmpty() ? Act : FString::Printf(TEXT("%s + %s"), *Mod, *Act);
		}
	};

	FKeybindConfig ArrangeNodesKeybind;

	FKeybindConfig VoicePTTKeybind;

	bool bCapturingKeybind = false;
	int32 CapturingKeybindTarget = 0;

	bool bUiCapturingKeybind = false;

	TSharedPtr<STextBlock> KeybindDisplayText;

	int32 SettingsTabIndex = 0;

	TSharedPtr<class SWidgetSwitcher> SettingsTabSwitcher;

	FReply OnSettingsApiKeysTabClicked();
	FReply OnSettingsPreferencesTabClicked();

	FReply OnRebindArrangeNodesClicked();

	void LoadKeybindConfig();
	void SaveKeybindConfig();

	bool bEditorSessionValid = false;
	void CheckEditorSyncState();
	void OnEditorSyncActivated();
	void OnEditorSyncStateChanged(bool bIsValid);

	void StartBrowserToolServer();

	void StopBrowserToolServer();

	FSocket* BrowserToolListenerSocket = nullptr;

	FRunnableThread* BrowserToolServerThread = nullptr;

	FBrowserToolServerWorker* BrowserToolServerWorker = nullptr;

	int32 BrowserToolServerPort = 0;

	TMap<FString, TSharedPtr<FAgentProviderConfig>> AgentProviders;

	TMap<FString, TSharedPtr<FAgentRunnerInstance>> AgentInstances;

	TSet<FString> NativeLoopStreamingChats;

	TMap<FString, TPair<int32, int32>> AgentTokenTotals;

	TArray<FString> AgentInstancesToCleanup;

	TMap<FString, FUECPACPAgentConfigSnapshot> CachedAgentConfigs;

	struct FACPAgentAuthState
	{
		bool    bRequired = false;
		FString MethodId;
	};
	TMap<FString, FACPAgentAuthState> AgentAuthStates;

	TSet<FString> AgentDiscoveryAttempted;

	FAgentProviderConfig* GetActiveAgentProvider();

	void RefreshAgentProvidersFromRegistry();

	FAgentRunnerInstance* GetActiveAgentInstance();

	FAgentRunnerInstance* GetAgentInstanceForChat(const FString& ChatID);

	void SpawnAgentInstance(const FString& ChatID, FAgentProviderConfig& Config, const FString& Prompt, EAgentSourceView SourceView = EAgentSourceView::Architect);

	TArray<TSharedPtr<FJsonValue>>& GetAgentHistory(const FAgentRunnerInstance& Inst);
	void RefreshAgentView(const FAgentRunnerInstance& Inst);
	void SaveAgentChatHistory(const FAgentRunnerInstance& Inst);

	void StopAgentInstance(const FString& ChatID);

	void StopAllAgentInstances();

	bool OnAgentInstanceTick(TSharedPtr<FAgentRunnerInstance> Inst, float DeltaTime);

	void RefreshAgentLiveMessage(FAgentRunnerInstance& Inst, bool bProcessing);

	void SendQueryToInstance(FAgentRunnerInstance& Inst, FAgentProviderConfig& Config, const FString& Prompt);

	TMap<FString, TArray<TSharedPtr<FString>>> FetchedModelCache;

	void FetchModelsForProvider(const FString& Provider, const FString& ApiKey, int32 SlotIndex);

	void OnModelsFetched(const FString& Provider, int32 SlotIndex, const TArray<FString>& ModelIds);

	static bool CheckCliInPath(const FString& CliName);

	static FString FindCliFullPath(const FString& CliName);

	TSharedPtr<SWebBrowser> AppBrowser;

	UUECPAppBridge* AppBridgeObject = nullptr;

	UUECPMeshyBridge* MeshyBridgeObject = nullptr;

	UUECPImageGenBridge* ImageGenBridgeObject = nullptr;

	UUECPGddBridge* GddBridgeObject = nullptr;

	UUECPMemoryBridge* MemoryBridgeObject = nullptr;

	UUECPBugReportBridge* BugReportBridgeObject = nullptr;

	UUECPScannerBridge* ScannerBridgeObject = nullptr;

	FString ArchitectPendingBridgeText;
	FString AnalystPendingBridgeText;
	FString ScannerPendingBridgeText;

	void OnAppBrowserLoaded();

	void OnUIConfigRefreshed();

	void PushUITranslations();

	void OnMeshProgressUpdated(const FString& AssetName, const FString& Phase, int32 Progress, const FString& ErrorMsg);

	void SetArchitectInputText(const FString& Text);

	void SetAnalystInputText(const FString& Text);

	void SetScannerInputText(const FString& Text);

	void PushArchitectTokenCount(int32 N);

	void PushAnalystTokenCount(int32 N);

	void PushArchitectChatListToJs();

	void PushAnalystChatListToJs();

	void PushScannerChatListToJs();

	TSharedPtr<SWebBrowser> SettingsBrowser;

	UUECPSettingsBridge* SettingsBridgeObject = nullptr;

	void OnSettingsBrowserLoaded();

	bool bClaudeCliFound = false;
	bool bCodexCliFound = false;
	bool bCopilotCliFound = false;
	bool bGeminiCliFound = false;

	bool bGitHubAuthed = false;

	FString NodeVersion;
	FString NpmVersion;

	void RefreshCliStatusAsync();

	void InstallCliAsync(const FString& ProviderName, const FString& Command, const FString& Args,
		const FString& FollowUpCommand = FString(), const FString& FollowUpArgs = FString());

	bool bCliInstallInProgress = false;

	FToolExecutionResult ExecuteTool_ProjectPlan(const TSharedPtr<FJsonObject>& Args);

	FToolExecutionResult ExecuteTool_Memory(const TSharedPtr<FJsonObject>& Args);

	FToolExecutionResult ExecuteTool_WorkingNotes(const TSharedPtr<FJsonObject>& Args);

	FToolExecutionResult ExecuteTool_Task(const TSharedPtr<FJsonObject>& Args);

	bool TryDispatchPlanTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	bool TryDispatchTaskTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	bool TryDispatchWidgetOwnedTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult);

	void NotifyPlanUpdated(const FString& ConvID);

	void NotifyTasksUpdated(const FString& ConvID);

	void OnPlanModeClicked(ECheckBoxState NewState);

	TSharedPtr<SCheckBox> PlanModeButton;

public:

	const TArray<TSharedPtr<FJsonValue>>& GetArchitectConversationHistoryForExtraction() const { return ArchitectConversationHistory; }
	const TArray<TSharedPtr<FJsonValue>>& GetProjectConversationHistoryForExtraction()   const { return ProjectConversationHistory; }
	bool PasteImageFromClipboardForExtraction(TArray<FAttachedImage>& Attachments)             { return TryPasteImageFromClipboard(Attachments); }
	bool IsArchitectThinkingForExtraction() const                                               { return bIsArchitectThinking; }

	const TArray<TSharedPtr<FJsonValue>>&          GetAnalystHistoryForExtraction() const     { return AnalystConversationHistory; }
	TArray<TSharedPtr<FJsonValue>>&                GetAnalystHistoryMutableForExtraction()    { return AnalystConversationHistory; }
	const TArray<TSharedPtr<FConversationInfo>>&   GetAnalystChatListForExtraction() const    { return ConversationList; }
	TArray<TSharedPtr<FConversationInfo>>&         GetAnalystChatListMutableForExtraction()   { return ConversationList; }
	const FString&                                 GetAnalystActiveChatIDForExtraction() const{ return ActiveChatID; }
	FString&                                       GetAnalystActiveChatIDMutableForExtraction(){ return ActiveChatID; }
	bool                                           GetAnalystThinkingForExtraction() const    { return bIsThinking; }
	void                                           ClearAnalystHistoryForExtraction()         { AnalystConversationHistory.Empty(); }

	void AnalystAttachImageForExtraction()             { OnAttachAnalystImageClicked(); }
	void AnalystRefreshChatHistoryViewForExtraction()  { RefreshChatHistoryView(); }
	void AnalystPushChatListToJsForExtraction()        { PushAnalystChatListToJs(); }
	void AnalystSelectChatForExtraction(TSharedPtr<FConversationInfo> Info)
	{
		OnChatSelectionChanged(Info, ESelectInfo::OnMouseClick);
	}
	void AnalystSetInputTextForExtraction(const FString& Text) { SetAnalystInputText(Text); }
	void AnalystPushTokenCountForExtraction(int32 N)           { PushAnalystTokenCount(N); }
	FString& GetAnalystPendingContextMutableForExtraction()    { return PendingContext; }
	TArray<FAttachedFileContext>& GetAnalystAttachedFilesMutableForExtraction() { return AnalystAttachedFiles; }
	const TArray<FAttachedFileContext>& GetAnalystAttachedFilesForExtraction() const { return AnalystAttachedFiles; }
	TArray<FAttachedImage>& GetAnalystAttachedImagesMutableForExtraction()     { return AnalystAttachedImages; }
	const FString& GetAnalystPendingBridgeTextForExtraction() const            { return AnalystPendingBridgeText; }
	void           ClearAnalystPendingBridgeTextForExtraction()                { AnalystPendingBridgeText.Empty(); }
	void           SetAnalystThinkingForExtraction(bool bNew)                  { bIsThinking = bNew; }
	void           RefreshAnalystImagePreviewForExtraction()                   { RefreshAnalystImagePreview(); }
	const FString& GetCustomInstructionsForExtraction() const                  { return CustomInstructions; }
	void           ParseAndPushFreeTierRateLimitForExtraction(FHttpResponsePtr R) { ParseAndPushFreeTierRateLimit(R); }
	void           UpdateConversationTokensForExtraction(const FString& ChatID, int32 P, int32 C) { UpdateConversationTokens(ChatID, P, C); }

	const TArray<TSharedPtr<FJsonValue>>&        GetScannerHistoryForExtraction() const      { return ProjectConversationHistory; }
	const TArray<TSharedPtr<FConversationInfo>>& GetScannerChatListForExtraction() const     { return ProjectConversationList; }
	const FString&                               GetScannerActiveChatIDForExtraction() const { return ActiveProjectChatID; }

	void ScannerSendForExtraction(const FString& Text)       { ScannerPendingBridgeText = Text; OnSendProjectQuestionClicked(); }
	void ScannerStopForExtraction()                          { OnStopProjectClicked(); }
	void ScannerNewChatForExtraction()                       { OnNewProjectChatClicked(); }
	void ScannerSwitchChatForExtraction(const FString& ChatID)
	{
		for (const TSharedPtr<FConversationInfo>& Info : ProjectConversationList)
		{
			if (Info.IsValid() && Info->ID == ChatID)
			{
				OnProjectChatSelectionChanged(Info, ESelectInfo::OnMouseClick);
				return;
			}
		}
	}
	void ScannerAttachImageForExtraction()                   { OnAttachProjectImageClicked(); }

	const TArray<TSharedPtr<FJsonValue>>&        GetArchitectChatHistoryForExtraction() const   { return ArchitectConversationHistory; }
	const TArray<TSharedPtr<FConversationInfo>>& GetArchitectChatListForExtraction() const      { return ArchitectConversationList; }
	const FString&                               GetArchitectActiveChatIDForExtraction() const  { return ActiveArchitectChatID; }

	bool ArchitectIsThinkingForExtraction() const { return bIsArchitectThinking; }

	void VoiceAutoSendArchitectForExtraction(const FString& Text)
	{
		ArchitectPendingBridgeText = Text;
		OnSendArchitectMessageClicked();
	}

	TSharedPtr<SWebBrowser> GetAppBrowserForExtraction() const { return AppBrowser; }

	static TArray<TSharedPtr<FJsonValue>> WindowConversationHistoryForExtraction(
		const TArray<TSharedPtr<FJsonValue>>& FullHistory,
		int32 MaxCharBudget,
		int32 MinRecentMessages = 8)
	{
		return WindowConversationHistory(FullHistory, MaxCharBudget, MinRecentMessages);
	}
	static FString FormatHttpErrorForExtraction(bool bWasSuccessful, FHttpResponsePtr Response, const FString& ProviderHint = TEXT(""))
	{
		return FormatHttpError(bWasSuccessful, Response, ProviderHint);
	}
};
