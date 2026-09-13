#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"

class UECPCORE_API FEditorProfileSync
{
public:

	// ============================================================
	// SINGLETON
	// ============================================================

	static FEditorProfileSync& Get();


	// ============================================================
	// INITIALIZE / SHUTDOWN
	// ============================================================

	void InitializeSync();
	void ShutdownSync();


	// ============================================================
	// STATUS
	// ============================================================

	bool IsSyncValid() const;

	bool IsEditorHostActive() const;
	bool HasEngineContext() const;
	bool IsClearanceSatisfied() const;
	bool IsRefreshFresh() const;
	bool IsProfileCoherent() const;

	bool IsContextMarkedStale() const;
	bool HasContextClearanceFlag() const;

	int32 GetActiveHandleLength() const;
	int32 GetContextSignatureSize() const;

	double GetContextRefreshAge() const;

	uint32 GetEditorStateHash() const;

	FString GetEditorMessage(int32 Index) const;
	FString GetContextFaultMessage(int32 Slot) const;


	// ============================================================
	// PROFILE
	// ============================================================

	FString GetPresetTier() const;

	FString GetSyncKey() const;

	FString GetMaskedSyncKey() const;

	FString GetProfileOrigin() const;

	FDateTime GetLastSyncTime() const;


	// ============================================================
	// FAB / BUNDLE
	// ============================================================

	bool IsBundlePending() const;

	FString GetPendingBundleCode() const;

	FString GetPendingBundleEmail() const;


	// ============================================================
	// PROFILE LINK
	// ============================================================

	void LinkProfile(
		const FString& ProfileCode,
		const FString& HardwareId,
		const FString& PcUser,
		const FString& MachineName,
		TFunction<void(
			bool bSuccess,
			const FString& Message
		)> Callback
	);


	// ============================================================
	// FAB CLAIM
	// ============================================================

	void ClaimBundle(
		const FString& Email,
		const FString& BundleCode,
		const FString& HardwareId,
		const FString& PcUser,
		const FString& MachineName,
		const TSharedPtr<FJsonObject>& PcSpecs,
		TFunction<void(
			bool bSuccess,
			const FString& Message,
			const FString& Status
		)> Callback
	);


	// ============================================================
	// FAB POLL
	// ============================================================

	void PollBundleClaim(
		const FString& BundleCode,
		const FString& HardwareId,
		const FString& PcUser,
		const FString& MachineName,
		TFunction<void(
			bool bSuccess,
			const FString& Message,
			const FString& ProfileCode
		)> Callback
	);


	// ============================================================
	// SYNC
	// ============================================================

	void PushSyncPulse();

	void RefreshProfile(
		TFunction<void(bool bValid)> Callback
	);

	void SuspendProfile();

	void StartSyncPulse();

	void StopSyncPulse();


	// ============================================================
	// BUNDLE POLLING
	// ============================================================

	void StartBundlePoll(
		const FString& BundleCode,
		const FString& Email
	);

	void StopBundlePoll();


	// ============================================================
	// MACHINE
	// ============================================================

	static FString GetWorkstationId();

	static FString GetProcessUser();

	static FString GetMachineName();

	static TSharedPtr<FJsonObject> GetPlatformSpecs();


	// ============================================================
	// EVENTS
	// ============================================================

	DECLARE_MULTICAST_DELEGATE_OneParam(
		FSyncStateChanged,
		bool
	);

	FSyncStateChanged OnSyncStateChanged;


	DECLARE_MULTICAST_DELEGATE_TwoParams(
		FVersionAvailable,
		const FString&,
		const FString&
	);

	FVersionAvailable OnVersionAvailable;


	DECLARE_MULTICAST_DELEGATE(
		FBundleClaimed
	);

	FBundleClaimed OnBundleClaimed;


private:

	// ============================================================
	// CONSTRUCTOR
	// ============================================================

	FEditorProfileSync();

	~FEditorProfileSync();


	// Não permite cópia.
	FEditorProfileSync(
		const FEditorProfileSync&
	) = delete;

	FEditorProfileSync& operator=(
		const FEditorProfileSync&
	) = delete;


	// ============================================================
	// PROFILE STATE
	// ============================================================

	struct FProfileRecord
	{
		FString SyncKey;

		FString WorkstationId;

		FString PresetTier;

		FString Origin;

		FString BundleCode;

		FString BundleEmail;

		FDateTime LinkedAt;

		FDateTime LastSyncAt;

		// No sistema direto esses valores permanecem:
		//
		// bProfileLinked    = true
		// bBundlePending    = false
		// bProfileSuspended = false

		bool bProfileLinked = true;

		bool bBundlePending = false;

		bool bProfileSuspended = false;
	};


	FProfileRecord ProfileState;


	// ============================================================
	// INITIALIZED
	// ============================================================

	bool bSyncInitialized = false;


	// Mantido somente para compatibilidade
	// com código legado.
	bool bProvisionalProfile = false;


	// ============================================================
	// TICKER HANDLES
	// ============================================================

	FTSTicker::FDelegateHandle PulseTickerHandle;

	FTSTicker::FDelegateHandle BundlePollTickerHandle;


	// ============================================================
	// LOCAL STATE
	// ============================================================

	bool LoadLocalState();

	void SaveLocalState();

	void ClearLocalState();

	FString GetStatePath() const;

	FString ToBlob(
		const FString& PlainText
	) const;

	FString FromBlob(
		const FString& CipherText
	) const;

	TArray<uint8> GetBlobBytes() const;


	// ============================================================
	// HTTP
	//
	// Mantido apenas porque outras partes antigas podem chamar.
	// No CPP direto NÃO realiza requisição externa.
	// ============================================================

	void SendPostRequest(
		const FString& Endpoint,
		const TSharedPtr<FJsonObject>& Body,
		TFunction<void(
			bool bSuccess,
			TSharedPtr<FJsonObject> Response
		)> Callback
	);


	// ============================================================
	// SYNC RESPONSE
	// ============================================================

	void OnSyncResponse(
		bool bSuccess,
		TSharedPtr<FJsonObject> Response
	);


	// ============================================================
	// TICK CALLBACKS
	// ============================================================

	bool OnSyncPulseTick(
		float DeltaTime
	);

	bool OnBundlePollTick(
		float DeltaTime
	);


	// ============================================================
	// GRACE
	// ============================================================

	bool IsWithinGracePeriod() const;


	// ============================================================
	// LEGACY CONSTANTS
	// ============================================================

	static constexpr double GraceWindowHours = 336.0;

	static constexpr float SyncPulseIntervalSeconds = 900.0f;

	static constexpr float BundlePollIntervalSeconds = 300.0f;


	// Mantido para compatibilidade.
	// No CPP direto fica:
	//
	// const FString FEditorProfileSync::BackendBaseUrl = TEXT("");
	//
	static const FString BackendBaseUrl;
};