#include "SEditorSyncPanel.h"
#include "Managers/EditorProfileSync.h"
#include "UIConfigManager.h"

#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "SEditorSyncPanel"

/*
 * Sistema antigo de ativação removido desta tela.
 *
 * A classe continua existindo para manter compatibilidade com o restante
 * do plugin, mas não exibe FAB Marketplace, Website Purchase, verificação,
 * polling ou formulário de licença.
 *
 * Ao ser construída, inicializa o gerenciador em modo direto e dispara
 * OnActivationSuccess no próximo tick do Slate.
 */

void SEditorSyncPanel::Construct(const FArguments& InArgs)
{
	OnActivationSuccessDelegate = InArgs._OnActivationSuccess;

	// Mantém compatibilidade com o restante do projeto.
	FEditorProfileSync::Get().InitializeSync();

	// Nunca entra em estados antigos de espera/verificação.
	bIsSubmitting = false;
	bIsBundlePending = false;
	ActiveTab = 0;

	// A tela de licença antiga não é mais exibida.
	ChildSlot
	[
		SNullWidget::NullWidget
	];

	// Dispara no próximo tick para evitar trocar widgets durante
	// a própria construção do Slate.
	RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateLambda(
			[this](double CurrentTime, float DeltaTime)
			{
				OnActivationSuccessDelegate.ExecuteIfBound();
				return EActiveTimerReturnType::Stop;
			}
		)
	);
}

FReply SEditorSyncPanel::OnBundleTabClicked()
{
	ActiveTab = 0;

	if (TabSwitcher.IsValid())
	{
		TabSwitcher->SetActiveWidgetIndex(0);
	}

	return FReply::Handled();
}

FReply SEditorSyncPanel::OnWebsiteTabClicked()
{
	ActiveTab = 1;

	if (TabSwitcher.IsValid())
	{
		TabSwitcher->SetActiveWidgetIndex(1);
	}

	return FReply::Handled();
}

FReply SEditorSyncPanel::OnSubmitClicked()
{
	// Compatibilidade caso alguma parte antiga ainda consiga chamar
	// este handler: libera imediatamente.
	bIsSubmitting = false;
	bIsBundlePending = false;

	OnActivationSuccessDelegate.ExecuteIfBound();

	return FReply::Handled();
}

void SEditorSyncPanel::UpdateStatus(const FString& Message, bool bIsError)
{
	// A tela antiga não é mais exibida, mas mantemos a função
	// para compatibilidade com o header.
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Message));

		StatusText->SetColorAndOpacity(
			bIsError
				? FSlateColor(FLinearColor(1.0f, 0.30f, 0.20f))
				: FSlateColor(FLinearColor(1.0f, 0.84f, 0.0f, 0.7f))
		);
	}
}

void SEditorSyncPanel::SetSubmitting(bool bSubmitting)
{
	// Nunca mantemos a tela presa em "Waiting".
	bIsSubmitting = false;
}

bool SEditorSyncPanel::ValidateProfileCodeFormat(const FString& Key) const
{
	// Sistema antigo desativado.
	return true;
}

bool SEditorSyncPanel::ValidateEmailFormat(const FString& Email) const
{
	// Sistema antigo desativado.
	return true;
}

bool SEditorSyncPanel::ValidateBundleCodeFormat(const FString& OrderId) const
{
	// Sistema antigo desativado.
	return true;
}

void SEditorSyncPanel::OnBundleClaimResumed()
{
	bIsSubmitting = false;
	bIsBundlePending = false;

	OnActivationSuccessDelegate.ExecuteIfBound();
}

void SEditorSyncPanel::OnLanguageChanged(
	TSharedPtr<FString> NewSelection,
	ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid())
	{
		return;
	}

	const FString LangCode = LangNameToCode(*NewSelection);
	FUIConfigManager::Get().SetLanguage(LangCode);
}

FString SEditorSyncPanel::LangNameToCode(const FString& Name)
{
	if (Name == TEXT("Español")) return TEXT("es");
	if (Name == TEXT("Français")) return TEXT("fr");
	if (Name == TEXT("Deutsch")) return TEXT("de");
	if (Name == TEXT("中文")) return TEXT("zh");
	if (Name == TEXT("日本語")) return TEXT("ja");
	if (Name == TEXT("Русский")) return TEXT("ru");
	if (Name == TEXT("Português")) return TEXT("pt");
	if (Name == TEXT("한국어")) return TEXT("ko");
	if (Name == TEXT("Italiano")) return TEXT("it");
	if (Name == TEXT("العربية")) return TEXT("ar");
	if (Name == TEXT("Nederlands")) return TEXT("nl");
	if (Name == TEXT("Türkçe")) return TEXT("tr");
	if (Name == TEXT("Polski")) return TEXT("pl");
	if (Name == TEXT("Tiếng Việt")) return TEXT("vi");
	if (Name == TEXT("Bahasa Indonesia")) return TEXT("id");
	if (Name == TEXT("हिन्दी")) return TEXT("hi");
	if (Name == TEXT("Română")) return TEXT("ro");
	if (Name == TEXT("ไทย")) return TEXT("th");
	if (Name == TEXT("Українська")) return TEXT("uk");

	return TEXT("en");
}

#undef LOCTEXT_NAMESPACE
