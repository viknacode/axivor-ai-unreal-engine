#include "Managers/EditorProfileSync.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformMemory.h"
#include "Misc/DateTime.h"
#include "Runtime/Launch/Resources/Version.h"

#define LOCTEXT_NAMESPACE "EditorSyncManager"

// ============================================================
// SISTEMA DE LICENÇA ANTIGO DESATIVADO
// ============================================================
//
// Este arquivo mantém todas as funções antigas existentes
// para que o restante do plugin continue compilando.
//
// Porém:
//
// - Não acessa backend
// - Não valida licença
// - Não valida FAB
// - Não cria heartbeat
// - Não cria polling
// - Não salva licença
// - Não lê licença
// - Não bloqueia máquina
// - Não espera verificação
// - Não usa grace period
//
// O plugin é considerado ACTIVE diretamente.
//
// ============================================================

const FString FEditorProfileSync::BackendBaseUrl = TEXT("");


// ============================================================
// SINGLETON
// ============================================================

FEditorProfileSync& FEditorProfileSync::Get()
{
	static FEditorProfileSync Instance;
	return Instance;
}


// ============================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================

FEditorProfileSync::FEditorProfileSync()
{
}

FEditorProfileSync::~FEditorProfileSync()
{
	ShutdownSync();
}


// ============================================================
// INITIALIZE
// ============================================================

void FEditorProfileSync::InitializeSync()
{
	if (bSyncInitialized)
	{
		return;
	}

	bSyncInitialized = true;

	// --------------------------------------------------------
	// LIBERAÇÃO DIRETA
	// --------------------------------------------------------

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.WorkstationId = TEXT("LOCAL-DIRECT");
	ProfileState.PresetTier = TEXT("pro");
	ProfileState.Origin = TEXT("local");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;

	// IMPORTANTE:
	// nunca deixar o plugin em estado de espera.
	ProfileState.bBundlePending = false;

	ProfileState.BundleCode.Empty();
	ProfileState.BundleEmail.Empty();

	ProfileState.LinkedAt = FDateTime::UtcNow();
	ProfileState.LastSyncAt = FDateTime::UtcNow();

	// Garante que não exista nenhum ticker antigo.
	StopSyncPulse();
	StopBundlePoll();

	// Avisa qualquer interface que esteja observando
	// o estado da licença.
	OnSyncStateChanged.Broadcast(true);

	// Avisa a UI antiga do FAB que o processo terminou.
	OnBundleClaimed.Broadcast();
}


// ============================================================
// SHUTDOWN
// ============================================================

void FEditorProfileSync::ShutdownSync()
{
	StopSyncPulse();
	StopBundlePoll();

	bSyncInitialized = false;
}


// ============================================================
// VALIDAÇÃO
// ============================================================

bool FEditorProfileSync::IsSyncValid() const
{
	return true;
}

bool FEditorProfileSync::IsEditorHostActive() const
{
	return true;
}

bool FEditorProfileSync::HasEngineContext() const
{
	return true;
}

bool FEditorProfileSync::IsClearanceSatisfied() const
{
	return true;
}

bool FEditorProfileSync::IsRefreshFresh() const
{
	return true;
}


// ============================================================
// ESTADO DO EDITOR
// ============================================================

uint32 FEditorProfileSync::GetEditorStateHash() const
{
	return 0xFFFFFFFF;
}

FString FEditorProfileSync::GetEditorMessage(int32 _idx) const
{
	return FString();
}

int32 FEditorProfileSync::GetActiveHandleLength() const
{
	return 32;
}

double FEditorProfileSync::GetContextRefreshAge() const
{
	return 0.0;
}

bool FEditorProfileSync::IsContextMarkedStale() const
{
	return false;
}

bool FEditorProfileSync::HasContextClearanceFlag() const
{
	return true;
}

int32 FEditorProfileSync::GetContextSignatureSize() const
{
	return 32;
}

bool FEditorProfileSync::IsProfileCoherent() const
{
	return true;
}


// ============================================================
// SUSPEND
// ============================================================

void FEditorProfileSync::SuspendProfile()
{
	// Sistema antigo desativado.
	//
	// Não permitimos que essa função marque o plugin
	// como bloqueado.

	ProfileState.bProfileSuspended = false;
	ProfileState.bProfileLinked = true;
	ProfileState.bBundlePending = false;

	OnSyncStateChanged.Broadcast(true);
}


// ============================================================
// ERROS
// ============================================================

FString FEditorProfileSync::GetContextFaultMessage(int32 Slot) const
{
	return FString();
}


// ============================================================
// PROFILE
// ============================================================

FString FEditorProfileSync::GetPresetTier() const
{
	return TEXT("pro");
}

FString FEditorProfileSync::GetSyncKey() const
{
	return TEXT("LOCAL-DIRECT-ACTIVE");
}

FString FEditorProfileSync::GetMaskedSyncKey() const
{
	return TEXT("LOCAL-****-ACTIVE");
}

FString FEditorProfileSync::GetProfileOrigin() const
{
	return TEXT("local");
}


// ============================================================
// FAB / BUNDLE
// ============================================================

bool FEditorProfileSync::IsBundlePending() const
{
	return false;
}

FString FEditorProfileSync::GetPendingBundleCode() const
{
	return FString();
}

FString FEditorProfileSync::GetPendingBundleEmail() const
{
	return FString();
}


// ============================================================
// LAST SYNC
// ============================================================

FDateTime FEditorProfileSync::GetLastSyncTime() const
{
	return FDateTime::UtcNow();
}


// ============================================================
// MACHINE INFO
// ============================================================

FString FEditorProfileSync::GetWorkstationId()
{
	// Como o sistema de licença foi removido,
	// não precisamos gerar fingerprint.

	return TEXT("LOCAL-DIRECT");
}

FString FEditorProfileSync::GetProcessUser()
{
	return FPlatformProcess::UserName();
}

FString FEditorProfileSync::GetMachineName()
{
	return FPlatformProcess::ComputerName();
}


// ============================================================
// PLATFORM SPECS
// ============================================================

TSharedPtr<FJsonObject> FEditorProfileSync::GetPlatformSpecs()
{
	TSharedPtr<FJsonObject> Specs = MakeShared<FJsonObject>();

	Specs->SetStringField(
		TEXT("cpu"),
		FPlatformMisc::GetCPUBrand()
	);

	Specs->SetStringField(
		TEXT("gpu"),
		FPlatformMisc::GetPrimaryGPUBrand()
	);

	Specs->SetNumberField(
		TEXT("ram"),
		FPlatformMemory::GetPhysicalGBRam()
	);

	Specs->SetStringField(
		TEXT("os"),
		FPlatformMisc::GetOSVersion()
	);

	Specs->SetStringField(
		TEXT("ueVersion"),
		ENGINE_VERSION_STRING
	);

	return Specs;
}


// ============================================================
// LOCAL STATE
// ============================================================

FString FEditorProfileSync::GetStatePath() const
{
	// Não existe mais arquivo local de licença.
	return FString();
}

TArray<uint8> FEditorProfileSync::GetBlobBytes() const
{
	return TArray<uint8>();
}

FString FEditorProfileSync::ToBlob(
	const FString& PlainText
) const
{
	// Apenas mantém compatibilidade.
	return PlainText;
}

FString FEditorProfileSync::FromBlob(
	const FString& CipherText
) const
{
	// Apenas mantém compatibilidade.
	return CipherText;
}


// ============================================================
// LOAD STATE
// ============================================================

bool FEditorProfileSync::LoadLocalState()
{
	// Sempre retorna um estado ativo.

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.WorkstationId = TEXT("LOCAL-DIRECT");
	ProfileState.PresetTier = TEXT("pro");
	ProfileState.Origin = TEXT("local");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;
	ProfileState.bBundlePending = false;

	ProfileState.BundleCode.Empty();
	ProfileState.BundleEmail.Empty();

	ProfileState.LinkedAt = FDateTime::UtcNow();
	ProfileState.LastSyncAt = FDateTime::UtcNow();

	return true;
}


// ============================================================
// SAVE STATE
// ============================================================

void FEditorProfileSync::SaveLocalState()
{
	// Não salvamos mais nenhuma licença.
}


// ============================================================
// CLEAR STATE
// ============================================================

void FEditorProfileSync::ClearLocalState()
{
	// Diferente do sistema antigo, limpar o estado
	// NÃO desativa o plugin.

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.WorkstationId = TEXT("LOCAL-DIRECT");
	ProfileState.PresetTier = TEXT("pro");
	ProfileState.Origin = TEXT("local");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;
	ProfileState.bBundlePending = false;

	ProfileState.BundleCode.Empty();
	ProfileState.BundleEmail.Empty();

	ProfileState.LinkedAt = FDateTime::UtcNow();
	ProfileState.LastSyncAt = FDateTime::UtcNow();

	OnSyncStateChanged.Broadcast(true);
}


// ============================================================
// HTTP
// ============================================================

void FEditorProfileSync::SendPostRequest(
	const FString& Endpoint,
	const TSharedPtr<FJsonObject>& Body,
	TFunction<void(
		bool bSuccess,
		TSharedPtr<FJsonObject> Response
	)> Callback
)
{
	// --------------------------------------------------------
	// NENHUMA REQUISIÇÃO HTTP É REALIZADA
	// --------------------------------------------------------

	TSharedPtr<FJsonObject> Response =
		MakeShared<FJsonObject>();

	Response->SetBoolField(
		TEXT("success"),
		true
	);

	Response->SetBoolField(
		TEXT("valid"),
		true
	);

	Response->SetBoolField(
		TEXT("blocked"),
		false
	);

	Response->SetStringField(
		TEXT("status"),
		TEXT("active")
	);

	Response->SetStringField(
		TEXT("tier"),
		TEXT("pro")
	);

	Response->SetStringField(
		TEXT("message"),
		TEXT("Plugin active.")
	);

	if (Callback)
	{
		Callback(
			true,
			Response
		);
	}
}


// ============================================================
// WEBSITE PURCHASE / PROFILE CODE
// ============================================================

void FEditorProfileSync::LinkProfile(
	const FString& ProfileCode,
	const FString& HardwareId,
	const FString& PcUser,
	const FString& MachineName,
	TFunction<void(
		bool bSuccess,
		const FString& Message
	)> Callback
)
{
	// --------------------------------------------------------
	// ATIVA DIRETAMENTE
	// --------------------------------------------------------

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.WorkstationId = TEXT("LOCAL-DIRECT");
	ProfileState.PresetTier = TEXT("pro");
	ProfileState.Origin = TEXT("local");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;
	ProfileState.bBundlePending = false;

	ProfileState.BundleCode.Empty();
	ProfileState.BundleEmail.Empty();

	ProfileState.LinkedAt = FDateTime::UtcNow();
	ProfileState.LastSyncAt = FDateTime::UtcNow();

	StopBundlePoll();
	StopSyncPulse();

	OnSyncStateChanged.Broadcast(true);
	OnBundleClaimed.Broadcast();

	if (Callback)
	{
		Callback(
			true,
			TEXT("Plugin activated successfully.")
		);
	}
}


// ============================================================
// FAB CLAIM
// ============================================================

void FEditorProfileSync::ClaimBundle(
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
)
{
	// --------------------------------------------------------
	// NÃO MANDA PEDIDO PARA O SERVIDOR.
	//
	// NÃO MARCA COMO PENDING.
	//
	// ATIVA DIRETAMENTE.
	// --------------------------------------------------------

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.WorkstationId = TEXT("LOCAL-DIRECT");
	ProfileState.PresetTier = TEXT("pro");
	ProfileState.Origin = TEXT("local");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;

	// CRÍTICO:
	ProfileState.bBundlePending = false;

	ProfileState.BundleCode.Empty();
	ProfileState.BundleEmail.Empty();

	ProfileState.LinkedAt = FDateTime::UtcNow();
	ProfileState.LastSyncAt = FDateTime::UtcNow();

	StopBundlePoll();
	StopSyncPulse();

	// Notifica sistema geral.
	OnSyncStateChanged.Broadcast(true);

	// Notifica especificamente a tela FAB
	// que a ativação terminou.
	OnBundleClaimed.Broadcast();

	if (Callback)
	{
		Callback(
			true,
			TEXT("Plugin activated successfully."),
			TEXT("active")
		);
	}
}


// ============================================================
// FAB POLL
// ============================================================

void FEditorProfileSync::PollBundleClaim(
	const FString& BundleCode,
	const FString& HardwareId,
	const FString& PcUser,
	const FString& MachineName,
	TFunction<void(
		bool bSuccess,
		const FString& Message,
		const FString& ProfileCode
	)> Callback
)
{
	// --------------------------------------------------------
	// O polling antigo não existe mais.
	//
	// Caso alguma tela ainda chame essa função,
	// retornamos ACTIVE imediatamente.
	// --------------------------------------------------------

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.WorkstationId = TEXT("LOCAL-DIRECT");
	ProfileState.PresetTier = TEXT("pro");
	ProfileState.Origin = TEXT("local");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;
	ProfileState.bBundlePending = false;

	ProfileState.BundleCode.Empty();
	ProfileState.BundleEmail.Empty();

	ProfileState.LinkedAt = FDateTime::UtcNow();
	ProfileState.LastSyncAt = FDateTime::UtcNow();

	StopBundlePoll();
	StopSyncPulse();

	OnSyncStateChanged.Broadcast(true);
	OnBundleClaimed.Broadcast();

	if (Callback)
	{
		Callback(
			true,
			TEXT("Plugin activated successfully."),
			TEXT("LOCAL-DIRECT-ACTIVE")
		);
	}
}


// ============================================================
// HEARTBEAT
// ============================================================

void FEditorProfileSync::PushSyncPulse()
{
	// Sistema antigo removido.

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;
	ProfileState.bBundlePending = false;

	ProfileState.LastSyncAt = FDateTime::UtcNow();
}


// ============================================================
// REFRESH
// ============================================================

void FEditorProfileSync::RefreshProfile(
	TFunction<void(bool bValid)> Callback
)
{
	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.PresetTier = TEXT("pro");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;
	ProfileState.bBundlePending = false;

	ProfileState.LastSyncAt = FDateTime::UtcNow();

	OnSyncStateChanged.Broadcast(true);

	if (Callback)
	{
		Callback(true);
	}
}


// ============================================================
// SYNC RESPONSE
// ============================================================

void FEditorProfileSync::OnSyncResponse(
	bool bSuccess,
	TSharedPtr<FJsonObject> Response
)
{
	// Não importa o resultado recebido por código legado.
	//
	// Mantemos o plugin ativo.

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.PresetTier = TEXT("pro");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;
	ProfileState.bBundlePending = false;

	ProfileState.LastSyncAt = FDateTime::UtcNow();

	OnSyncStateChanged.Broadcast(true);
}


// ============================================================
// SYNC TICKER
// ============================================================

void FEditorProfileSync::StartSyncPulse()
{
	// Não inicia ticker.
	StopSyncPulse();
}

void FEditorProfileSync::StopSyncPulse()
{
	if (PulseTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(
			PulseTickerHandle
		);

		PulseTickerHandle.Reset();
	}
}

bool FEditorProfileSync::OnSyncPulseTick(
	float DeltaTime
)
{
	// False impede o ticker de continuar.
	return false;
}


// ============================================================
// FAB POLLING
// ============================================================

void FEditorProfileSync::StartBundlePoll(
	const FString& BundleCode,
	const FString& Email
)
{
	// --------------------------------------------------------
	// NÃO INICIA POLLING.
	// --------------------------------------------------------

	StopBundlePoll();

	ProfileState.SyncKey = TEXT("LOCAL-DIRECT-ACTIVE");
	ProfileState.PresetTier = TEXT("pro");
	ProfileState.Origin = TEXT("local");

	ProfileState.bProfileLinked = true;
	ProfileState.bProfileSuspended = false;

	// JAMAIS pending.
	ProfileState.bBundlePending = false;

	ProfileState.BundleCode.Empty();
	ProfileState.BundleEmail.Empty();

	ProfileState.LastSyncAt = FDateTime::UtcNow();

	OnSyncStateChanged.Broadcast(true);
	OnBundleClaimed.Broadcast();
}

void FEditorProfileSync::StopBundlePoll()
{
	if (BundlePollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(
			BundlePollTickerHandle
		);

		BundlePollTickerHandle.Reset();
	}
}

bool FEditorProfileSync::OnBundlePollTick(
	float DeltaTime
)
{
	// Não continua nenhum polling.
	return false;
}


// ============================================================
// GRACE PERIOD
// ============================================================

bool FEditorProfileSync::IsWithinGracePeriod() const
{
	return true;
}


#undef LOCTEXT_NAMESPACE