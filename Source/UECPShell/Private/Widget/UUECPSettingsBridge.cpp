// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPSettingsBridge.h"
#include "Managers/UpdateManager.h"
#include "Managers/ProviderConfigManager.h"
#include "Managers/FreeTierConfigManager.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "ApiKeyManager.h"
#include "AgentRunnerTypes.h"
#include "Managers/ThemeManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPVoiceService.h"
#include "Services/IUECPACPRegistryService.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPMcpInfoService.h"
#include "Services/IUECPUserMcpService.h"
#include "Mcp/UECPMcpClient.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Services/IUECPSettingsService.h"
#include "Interfaces/IProjectManager.h"
#include "UnrealEdMisc.h"
#include "UIConfigManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SWebBrowser.h"
#include "Tools/GitTools.h"
#include "Managers/EditorProfileSync.h"
#include "Tools/PerforceTools.h"
#include "ISourceControlModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "SourceControlOperations.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "SWebBrowser.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"

#define LOCTEXT_NAMESPACE "UUECPSettingsBridge"

static FString LinearColorToHex(const FLinearColor& C)
{
	FColor Srgb = C.ToFColor(true);
	return FString::Printf(TEXT("#%02X%02X%02X"), Srgb.R, Srgb.G, Srgb.B);
}

static FLinearColor HexToLinearColor(const FString& Hex)
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
}

static FString JsonStr(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Out;
}

static TSharedPtr<FJsonObject> ParseJson(const FString& Str)
{
	TSharedPtr<FJsonObject> Out;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Str);
	FJsonSerializer::Deserialize(R, Out);
	return Out;
}

static FString ThemePresetName(EThemePreset P)
{
	switch (P)
	{
		case EThemePreset::GoldenTurquoise:  return TEXT("GoldenTurquoise");
		case EThemePreset::SlateProfessional: return TEXT("SlateProfessional");
		case EThemePreset::RoseGold:          return TEXT("RoseGold");
		case EThemePreset::Emerald:           return TEXT("Emerald");
		case EThemePreset::Cyberpunk:         return TEXT("Cyberpunk");
		case EThemePreset::SolarFlare:        return TEXT("SolarFlare");
		case EThemePreset::DeepOcean:         return TEXT("DeepOcean");
		case EThemePreset::LavenderDream:     return TEXT("LavenderDream");
		default:                              return TEXT("Custom");
	}
}

static EThemePreset ThemePresetFromName(const FString& Name)
{
	if (Name == TEXT("SlateProfessional")) return EThemePreset::SlateProfessional;
	if (Name == TEXT("RoseGold"))          return EThemePreset::RoseGold;
	if (Name == TEXT("Emerald"))           return EThemePreset::Emerald;
	if (Name == TEXT("Cyberpunk"))         return EThemePreset::Cyberpunk;
	if (Name == TEXT("SolarFlare"))        return EThemePreset::SolarFlare;
	if (Name == TEXT("DeepOcean"))         return EThemePreset::DeepOcean;
	if (Name == TEXT("LavenderDream"))     return EThemePreset::LavenderDream;
	if (Name == TEXT("Custom"))            return EThemePreset::Custom;
	return EThemePreset::GoldenTurquoise;
}

static FString LangCodeToName(const FString& Code)
{
	if (Code == TEXT("es")) return TEXT("Español");
	if (Code == TEXT("fr")) return TEXT("Français");
	if (Code == TEXT("de")) return TEXT("Deutsch");
	if (Code == TEXT("zh")) return TEXT("中文");
	if (Code == TEXT("ja")) return TEXT("日本語");
	if (Code == TEXT("ru")) return TEXT("Русский");
	if (Code == TEXT("pt")) return TEXT("Português");
	if (Code == TEXT("ko")) return TEXT("한국어");
	if (Code == TEXT("it")) return TEXT("Italiano");
	if (Code == TEXT("ar")) return TEXT("العربية");
	if (Code == TEXT("nl")) return TEXT("Nederlands");
	if (Code == TEXT("tr")) return TEXT("Türkçe");
	if (Code == TEXT("pl")) return TEXT("Polski");
	if (Code == TEXT("vi")) return TEXT("Tiếng Việt");
	if (Code == TEXT("id")) return TEXT("Bahasa Indonesia");
	if (Code == TEXT("hi")) return TEXT("हिंदी");
	if (Code == TEXT("ro")) return TEXT("Română");
	if (Code == TEXT("th")) return TEXT("ภาษาไทย");
	if (Code == TEXT("uk")) return TEXT("Українська");
	return TEXT("English");
}

static FString LangNameToCode(const FString& Name)
{
	if (Name == TEXT("Español"))          return TEXT("es");
	if (Name == TEXT("Français"))         return TEXT("fr");
	if (Name == TEXT("Deutsch"))          return TEXT("de");
	if (Name == TEXT("中文"))              return TEXT("zh");
	if (Name == TEXT("日本語"))            return TEXT("ja");
	if (Name == TEXT("Русский"))          return TEXT("ru");
	if (Name == TEXT("Português"))        return TEXT("pt");
	if (Name == TEXT("한국어"))            return TEXT("ko");
	if (Name == TEXT("Italiano"))         return TEXT("it");
	if (Name == TEXT("العربية"))          return TEXT("ar");
	if (Name == TEXT("Nederlands"))       return TEXT("nl");
	if (Name == TEXT("Türkçe"))           return TEXT("tr");
	if (Name == TEXT("Polski"))           return TEXT("pl");
	if (Name == TEXT("Tiếng Việt"))       return TEXT("vi");
	if (Name == TEXT("Bahasa Indonesia")) return TEXT("id");
	if (Name == TEXT("हिंदी"))            return TEXT("hi");
	if (Name == TEXT("Română"))           return TEXT("ro");
	if (Name == TEXT("ภาษาไทย"))          return TEXT("th");
	if (Name == TEXT("Українська"))       return TEXT("uk");
	return TEXT("en");
}

static TSharedPtr<FJsonObject> SerializeSlot(const FApiKeySlot& S)
{
	auto O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("index"), S.SlotIndex);
	O->SetStringField(TEXT("name"), S.Name);
	O->SetStringField(TEXT("provider"), S.Provider);
	O->SetStringField(TEXT("apiKey"), S.ApiKey);
	O->SetStringField(TEXT("geminiModel"),   S.GeminiModel);
	O->SetStringField(TEXT("openAIModel"),   S.OpenAIModel);
	O->SetStringField(TEXT("claudeModel"),   S.ClaudeModel);
	O->SetStringField(TEXT("deepseekModel"), S.DeepSeekModel);
	O->SetStringField(TEXT("agentModel"), S.AgentModel);
	O->SetStringField(TEXT("agentEffort"), S.AgentEffort);
	O->SetStringField(TEXT("customBaseURL"), S.CustomBaseURL);
	O->SetStringField(TEXT("customModelName"), S.CustomModelName);
	O->SetStringField(TEXT("customParams"), S.CustomParams);

	FString LegacyMeshKey;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("MeshyApiKey"), LegacyMeshKey, FSettingsManager::GetGlobalConfigPath());

	auto Tex = MakeShared<FJsonObject>();
	Tex->SetStringField(TEXT("apiKey"), S.TextureGenApiKey);
	Tex->SetStringField(TEXT("endpoint"), S.TextureGenEndpoint);
	Tex->SetStringField(TEXT("model"), S.TextureGenModel);
	Tex->SetBoolField(TEXT("textureMode"), S.bTextureMode);
	O->SetObjectField(TEXT("textureGen"), Tex);

	auto Mesh = MakeShared<FJsonObject>();
	Mesh->SetStringField(TEXT("apiKey"),  S.MeshGenApiKey.IsEmpty() ? LegacyMeshKey : S.MeshGenApiKey);
	Mesh->SetStringField(TEXT("aiModel"), S.MeshyAiModel.IsEmpty() ? TEXT("latest") : S.MeshyAiModel);
	O->SetObjectField(TEXT("meshGen"), Mesh);

	auto SoundGen = MakeShared<FJsonObject>();
	SoundGen->SetStringField(TEXT("apiKey"),   S.SoundGenApiKey);
	SoundGen->SetStringField(TEXT("endpoint"), S.SoundGenEndpoint);
	SoundGen->SetStringField(TEXT("model"),    S.SoundGenModel);
	SoundGen->SetStringField(TEXT("voice"),    S.SoundGenVoice);
	O->SetObjectField(TEXT("soundGen"), SoundGen);

	return O;
}

static TSharedPtr<FJsonObject> SerializeTheme(const FBpGeneratorTheme& T)
{
	auto O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("preset"), ThemePresetName(T.Preset));
	O->SetStringField(TEXT("primaryColor"),    LinearColorToHex(T.PrimaryColor));
	O->SetStringField(TEXT("secondaryColor"),  LinearColorToHex(T.SecondaryColor));
	O->SetStringField(TEXT("tertiaryColor"),   LinearColorToHex(T.TertiaryColor));
	O->SetStringField(TEXT("mainBackground"),  LinearColorToHex(T.MainBackgroundColor));
	O->SetStringField(TEXT("panelBackground"), LinearColorToHex(T.PanelBackgroundColor));
	O->SetStringField(TEXT("borderBackground"),LinearColorToHex(T.BorderBackgroundColor));
	O->SetStringField(TEXT("primaryText"),     LinearColorToHex(T.PrimaryTextColor));
	O->SetStringField(TEXT("secondaryText"),   LinearColorToHex(T.SecondaryTextColor));
	O->SetStringField(TEXT("hintText"),        LinearColorToHex(T.HintTextColor));
	O->SetStringField(TEXT("analystColor"),    LinearColorToHex(T.AnalystColor));
	O->SetStringField(TEXT("scannerColor"),    LinearColorToHex(T.ScannerColor));
	O->SetStringField(TEXT("architectColor"),  LinearColorToHex(T.ArchitectColor));
	O->SetStringField(TEXT("gradientTop"),     LinearColorToHex(T.GradientTop));
	O->SetStringField(TEXT("gradientBottom"),  LinearColorToHex(T.GradientBottom));
	O->SetBoolField(TEXT("useGradients"), T.bUseGradients);
	O->SetNumberField(TEXT("borderOpacity"), T.BorderOpacity);
	O->SetNumberField(TEXT("cornerRadius"), T.CornerRadius);
	return O;
}

FString UUECPSettingsBridge::LoadAllSettings()
{
	auto Root = MakeShared<FJsonObject>();

	int32 ActiveIdx = FApiKeyManager::Get().GetActiveSlotIndex();
	Root->SetNumberField(TEXT("activeSlotIndex"), ActiveIdx);

	{
		FString SelSlot = TEXT("slot1");
		GConfig->GetString(TEXT("FreeTier"), TEXT("SelectedSlot"), SelSlot, FSettingsManager::GetGlobalConfigPath());
		if (SelSlot.IsEmpty()) SelSlot = TEXT("slot1");
		Root->SetStringField(TEXT("freeTierSlot"), SelSlot);

		FString SelSlot2Model;
		GConfig->GetString(TEXT("FreeTier"), TEXT("Slot2Model"), SelSlot2Model, FSettingsManager::GetGlobalConfigPath());

		TArray<TPair<FString,FString>> AvailableModels = FFreeTierConfigManager::Get().GetSlot2Models();

		if (!SelSlot2Model.IsEmpty() && !AvailableModels.IsEmpty())
		{
			bool bFound = false;
			for (const auto& M : AvailableModels)
				if (M.Key == SelSlot2Model) { bFound = true; break; }
			if (!bFound)
			{
				SelSlot2Model = AvailableModels[0].Key;
				GConfig->SetString(TEXT("FreeTier"), TEXT("Slot2Model"), *SelSlot2Model, FSettingsManager::GetGlobalConfigPath());
				GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
			}
		}

		Root->SetStringField(TEXT("freeTierSlot2Model"), SelSlot2Model);

		TArray<TSharedPtr<FJsonValue>> S2Arr;
		for (const auto& M : AvailableModels)
		{
			auto MO = MakeShared<FJsonObject>();
			MO->SetStringField(TEXT("id"), M.Key);
			MO->SetStringField(TEXT("display"), M.Value);
			S2Arr.Add(MakeShared<FJsonValueObject>(MO));
		}
		Root->SetArrayField(TEXT("freeTierSlot2Models"), S2Arr);
	}

	{
		auto ML = MakeShared<FJsonObject>();
		for (const FString& Prov : {TEXT("gemini"), TEXT("openai"), TEXT("claude"), TEXT("deepseek")})
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			TArray<FString> Models = FProviderConfigManager::Get().GetModelsForProvider(Prov);
			for (const FString& M : Models)
				Arr.Add(MakeShared<FJsonValueString>(M));
			ML->SetArrayField(Prov, Arr);
		}
		auto AL = MakeShared<FJsonObject>();
		for (const FString& Ag : {TEXT("claude"), TEXT("codex"), TEXT("copilot"), TEXT("gemini")})
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FString& M : FProviderConfigManager::Get().GetAgentModels(Ag))
				Arr.Add(MakeShared<FJsonValueString>(M));
			AL->SetArrayField(Ag, Arr);
		}
		ML->SetObjectField(TEXT("agents"), AL);
		Root->SetObjectField(TEXT("modelLists"), ML);

		int32 Ai = FApiKeyManager::Get().GetActiveSlotIndex();
		FApiKeySlot AS = FApiKeyManager::Get().GetSlot(Ai);
	}

	TArray<TSharedPtr<FJsonValue>> SlotsArr;
	TArray<FApiKeySlot> AllSlots = FApiKeyManager::Get().GetAllSlots();
	for (const FApiKeySlot& S : AllSlots)
		SlotsArr.Add(MakeShared<FJsonValueObject>(SerializeSlot(S)));
	Root->SetArrayField(TEXT("slots"), SlotsArr);

	{
		const FApiKeyManager::FImageGenConfig Ig = FApiKeyManager::Get().GetImageGenConfig();
		auto Img = MakeShared<FJsonObject>();
		Img->SetStringField(TEXT("apiKey"),      Ig.ApiKey);
		Img->SetStringField(TEXT("endpoint"),    Ig.Endpoint);
		Img->SetStringField(TEXT("model"),       Ig.Model);
		Img->SetBoolField  (TEXT("textureMode"), Ig.bTextureMode);
		Root->SetObjectField(TEXT("imageGen"), Img);
	}

	{
		auto V = MakeShared<FJsonObject>();
		const FUECPVoiceSettingsSnapshot VoiceCfg = IUECPCoreModule::Get().GetVoiceService().GetSettings();
		V->SetStringField(TEXT("apiKey"),   VoiceCfg.VoiceApiKey);
		V->SetBoolField(TEXT("enabled"),    VoiceCfg.bVoiceEnabled);
		V->SetBoolField(TEXT("autoPlay"),   VoiceCfg.bAutoPlayResponse);
		V->SetBoolField(TEXT("autoSend"),   VoiceCfg.bAutoSendAfterTranscription);
		V->SetStringField(TEXT("ttsVoice"), VoiceCfg.TTSVoice);
		{
			FApiKeySlot AS = FApiKeyManager::Get().GetActiveSlot();
			V->SetStringField(TEXT("soundGenApiKey"),    AS.SoundGenApiKey);
			V->SetStringField(TEXT("soundGenEndpoint"),  AS.SoundGenEndpoint);
			V->SetStringField(TEXT("soundGenModel"),     AS.SoundGenModel);
			V->SetStringField(TEXT("soundGenVoice"),     AS.SoundGenVoice);

			FString SgProv;
			GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("SoundGenProvider"), SgProv, FSettingsManager::GetGlobalConfigPath());
			V->SetStringField(TEXT("soundGenProvider"), SgProv.IsEmpty() ? TEXT("openai") : SgProv);

			TArray<TSharedPtr<FJsonValue>> SgProviders;
			FString SgJson = FProviderConfigManager::Get().GetSoundGenProvidersJson();
			if (!SgJson.IsEmpty())
			{
				TSharedRef<TJsonReader<>> SR = TJsonReaderFactory<>::Create(SgJson);
				FJsonSerializer::Deserialize(SR, SgProviders);
			}
			if (SgProviders.Num() == 0)
			{
				auto MkProv = [](const FString& Id, const FString& Name, const FString& Ep, const FString& Models, const FString& Voices) {
					auto P = MakeShared<FJsonObject>();
					P->SetStringField(TEXT("provider_id"), Id);
					P->SetStringField(TEXT("display_name"), Name);
					P->SetStringField(TEXT("api_endpoint"), Ep);
					TArray<TSharedPtr<FJsonValue>> MA, VA;
					TSharedRef<TJsonReader<>> MR = TJsonReaderFactory<>::Create(Models);
					FJsonSerializer::Deserialize(MR, MA);
					TSharedRef<TJsonReader<>> VR = TJsonReaderFactory<>::Create(Voices);
					FJsonSerializer::Deserialize(VR, VA);
					P->SetArrayField(TEXT("models"), MA);
					P->SetArrayField(TEXT("voices"), VA);
					return MakeShared<FJsonValueObject>(P);
				};
				SgProviders.Add(MkProv(TEXT("openai"), TEXT("OpenAI TTS"),
					TEXT("https://api.openai.com/v1/audio/speech"),
					TEXT("[{\"id\":\"tts-1\"},{\"id\":\"tts-1-hd\"}]"),
					TEXT("[{\"id\":\"alloy\"},{\"id\":\"echo\"},{\"id\":\"fable\"},{\"id\":\"onyx\"},{\"id\":\"nova\"},{\"id\":\"shimmer\"}]")));
				SgProviders.Add(MkProv(TEXT("elevenlabs"), TEXT("ElevenLabs"),
					TEXT("https://api.elevenlabs.io/v1/text-to-speech"),
					TEXT("[{\"id\":\"eleven_multilingual_v2\"},{\"id\":\"eleven_turbo_v2_5\"}]"),
					TEXT("[{\"id\":\"Rachel\"},{\"id\":\"Bella\"},{\"id\":\"Antoni\"}]")));
				SgProviders.Add(MkProv(TEXT("elevenlabs_sfx"), TEXT("ElevenLabs Sound Effects"),
					TEXT("https://api.elevenlabs.io/v1/sound-generation"),
					TEXT("[]"), TEXT("[]")));
				SgProviders.Add(MkProv(TEXT("google"), TEXT("Google Cloud TTS"),
					TEXT("https://texttospeech.googleapis.com/v1/text:synthesize"),
					TEXT("[{\"id\":\"en-US-Neural2-A\"},{\"id\":\"en-US-Neural2-C\"},{\"id\":\"en-US-Studio-O\"}]"),
					TEXT("[]")));
				SgProviders.Add(MkProv(TEXT("stability"), TEXT("Stability AI (SFX & Music)"),
					TEXT("https://api.stability.ai/v2beta/stable-audio/generate"),
					TEXT("[]"), TEXT("[]")));
				SgProviders.Add(MkProv(TEXT("huggingface"), TEXT("Hugging Face AudioGen (Free)"),
					TEXT("https://api-inference.huggingface.co/models/facebook/audiogen-medium"),
					TEXT("[]"), TEXT("[]")));
				SgProviders.Add(MkProv(TEXT("suno"), TEXT("Suno AI (Music)"),
					TEXT("https://apibox.erweima.ai/api/v1/generate"),
					TEXT("[]"), TEXT("[]")));
			}
			V->SetArrayField(TEXT("soundGenProviders"), SgProviders);

			FString SgKeyCache;
			GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("SoundGenKeyCache"), SgKeyCache, FSettingsManager::GetGlobalConfigPath());
			V->SetStringField(TEXT("soundGenKeyCache"), SgKeyCache);
		}
		Root->SetObjectField(TEXT("voice"), V);
	}

	{
		FBpGeneratorTheme T = FThemeManager::Get().LoadTheme();
		Root->SetObjectField(TEXT("theme"), SerializeTheme(T));

		auto Accents = MakeShared<FJsonObject>();
		auto AddAccent = [&](const FString& Name, const FBpGeneratorTheme& PT)
		{
			auto A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("primary"), LinearColorToHex(PT.PrimaryColor));
			A->SetStringField(TEXT("secondary"), LinearColorToHex(PT.SecondaryColor));
			A->SetStringField(TEXT("bg"), LinearColorToHex(PT.MainBackgroundColor));
			Accents->SetObjectField(Name, A);
		};
		AddAccent(TEXT("GoldenTurquoise"),  FThemeManager::GetGoldenTurquoiseTheme());
		AddAccent(TEXT("SlateProfessional"),FThemeManager::GetSlateProfessionalTheme());
		AddAccent(TEXT("RoseGold"),         FThemeManager::GetRoseGoldTheme());
		AddAccent(TEXT("Emerald"),          FThemeManager::GetEmeraldTheme());
		AddAccent(TEXT("Cyberpunk"),        FThemeManager::GetCyberpunkTheme());
		AddAccent(TEXT("SolarFlare"),       FThemeManager::GetSolarFlareTheme());
		AddAccent(TEXT("DeepOcean"),        FThemeManager::GetDeepOceanTheme());
		AddAccent(TEXT("LavenderDream"),    FThemeManager::GetLavenderDreamTheme());
		AddAccent(TEXT("Custom"), T);
		Root->SetObjectField(TEXT("themePresetAccents"), Accents);
	}

	{
		auto P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("language"), LangCodeToName(FUIConfigManager::Get().GetLanguage()));

		FString AMod, AKey, VMod, VKey;
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ArrangeNodesModifier"), AMod, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ArrangeNodesKey"), AKey, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VoicePTTModifier"), VMod, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VoicePTTKey"), VKey, FSettingsManager::GetGlobalConfigPath());
		if (AMod.IsEmpty()) AMod = TEXT("LeftControl");
		if (AKey.IsEmpty()) AKey = TEXT("R");
		if (VMod.IsEmpty()) VMod = TEXT("LeftAlt");
		if (VKey.IsEmpty()) VKey = TEXT("V");
		auto AKB = MakeShared<FJsonObject>();
		AKB->SetStringField(TEXT("modifier"), AMod);
		AKB->SetStringField(TEXT("key"), AKey);
		auto VKB = MakeShared<FJsonObject>();
		VKB->SetStringField(TEXT("modifier"), VMod);
		VKB->SetStringField(TEXT("key"), VKey);
		P->SetObjectField(TEXT("arrangeKeybind"), AKB);
		P->SetObjectField(TEXT("voicePTTKeybind"), VKB);
		Root->SetObjectField(TEXT("preferences"), P);
	}

	{
		auto Agents = MakeShared<FJsonObject>();
		auto W = OwnerWidget.Pin();

		struct FAgentEntry { FString Name; FString AgentKey; FString DefaultModel; FString DefaultEffort; };
		TArray<FAgentEntry> Entries = {
			{TEXT("Claude Agent"),  TEXT("claude"),  FProviderConfigManager::Get().GetAgentDefaultModel(TEXT("claude")),  TEXT("low")},
			{TEXT("Codex"),         TEXT("codex"),   FProviderConfigManager::Get().GetAgentDefaultModel(TEXT("codex")),   TEXT("medium")},
			{TEXT("Copilot"),       TEXT("copilot"), FProviderConfigManager::Get().GetAgentDefaultModel(TEXT("copilot")), TEXT("medium")},
			{TEXT("Gemini Agent"),  TEXT("gemini"),  FProviderConfigManager::Get().GetAgentDefaultModel(TEXT("gemini")),  TEXT("medium")},
		};
		const TArray<FString> AgentKeys = {TEXT("claude"), TEXT("codex"), TEXT("copilot"), TEXT("gemini")};
		bool bClaude = false, bCodex = false, bCopilot = false, bGemini = false, bGhAuthed = false;
		if (W.IsValid())
		{
			bClaude  = W->bClaudeCliFound;
			bCodex   = W->bCodexCliFound;
			bCopilot = W->bCopilotCliFound;
			bGemini  = W->bGeminiCliFound;
			bGhAuthed = W->bGitHubAuthed;
		}
		const bool CliStatus[4] = {bClaude, bCodex, bCopilot, bGemini};

		for (int32 i = 0; i < Entries.Num(); i++)
		{
			auto A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("model"),  Entries[i].DefaultModel);
			A->SetStringField(TEXT("effort"), Entries[i].DefaultEffort);
			A->SetBoolField(TEXT("cliFound"), CliStatus[i]);
			if (i == 0) A->SetBoolField(TEXT("authFound"), false);
			if (i == 2) A->SetBoolField(TEXT("githubAuthed"), bGhAuthed);
			Agents->SetObjectField(AgentKeys[i], A);
		}
		Root->SetObjectField(TEXT("agents"), Agents);
	}

	{
		auto A = MakeShared<FJsonObject>();
		FString FSStr, Density, AccentColor, ColorTheme, CodeFontFamily;
		FString UiFontSizeStr, UiFontFamily, BgColor, SurfaceColor, TextColor;
		FString UserBubbleBg, UserBubbleText, AiBubbleBg, AiBubbleText;
		bool bAutoContrast = true;
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ChatFontSize"),  FSStr,         FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ChatDensity"),   Density,       FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("AccentColor"),   AccentColor,   FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ColorTheme"),    ColorTheme,    FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("CodeFontFamily"), CodeFontFamily, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("UiFontSize"),    UiFontSizeStr, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("UiFontFamily"),  UiFontFamily,  FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("BgColor"),       BgColor,       FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("SurfaceColor"),  SurfaceColor,  FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("TextColor"),     TextColor,     FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("UserBubbleBg"),   UserBubbleBg,   FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("UserBubbleText"), UserBubbleText, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("AiBubbleBg"),     AiBubbleBg,     FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("AiBubbleText"),   AiBubbleText,   FSettingsManager::GetGlobalConfigPath());
		FString LayoutMode, InputStyle;
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("LayoutMode"), LayoutMode, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("InputStyle"), InputStyle, FSettingsManager::GetGlobalConfigPath());
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoContrast"), bAutoContrast, FSettingsManager::GetGlobalConfigPath()))
			bAutoContrast = true;
		A->SetNumberField(TEXT("chatFontSize"), FSStr.IsEmpty()       ? 13.5         : FCString::Atof(*FSStr));
		A->SetStringField(TEXT("chatDensity"),  Density.IsEmpty()     ? TEXT("normal") : Density);
		A->SetStringField(TEXT("accentColor"),  AccentColor.IsEmpty() ? TEXT("#8b7cf6") : AccentColor);
		A->SetStringField(TEXT("colorTheme"),   ColorTheme.IsEmpty()  ? TEXT("dark")   : ColorTheme);
		A->SetStringField(TEXT("codeFontFamily"), CodeFontFamily.IsEmpty() ? TEXT("System Default") : CodeFontFamily);
		A->SetNumberField(TEXT("uiFontSize"),    UiFontSizeStr.IsEmpty() ? 13.0 : FCString::Atof(*UiFontSizeStr));
		A->SetStringField(TEXT("uiFontFamily"),  UiFontFamily.IsEmpty()  ? TEXT("System Default") : UiFontFamily);
		A->SetStringField(TEXT("bgColor"),       BgColor);
		A->SetStringField(TEXT("surfaceColor"),  SurfaceColor);
		A->SetStringField(TEXT("textColor"),     TextColor);
		A->SetStringField(TEXT("userBubbleBg"),   UserBubbleBg);
		A->SetStringField(TEXT("userBubbleText"), UserBubbleText);
		A->SetStringField(TEXT("aiBubbleBg"),     AiBubbleBg);
		A->SetStringField(TEXT("aiBubbleText"),   AiBubbleText);
		A->SetBoolField(TEXT("autoContrast"),    bAutoContrast);
		A->SetStringField(TEXT("layoutMode"),    LayoutMode.IsEmpty() ? TEXT("top") : LayoutMode);
		A->SetStringField(TEXT("inputStyle"),    InputStyle.IsEmpty() ? TEXT("compact") : InputStyle);
		{
			// Axivor Studio (visual identity) keys
			FString VisualStyle; bool bGlow = true, bGlass = true, bBrand = true;
			GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VisualStyle"), VisualStyle, FSettingsManager::GetGlobalConfigPath());
			if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("GlowEffects"), bGlow, FSettingsManager::GetGlobalConfigPath())) bGlow = true;
			if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("GlassPanels"), bGlass, FSettingsManager::GetGlobalConfigPath())) bGlass = true;
			if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("BrandMark"), bBrand, FSettingsManager::GetGlobalConfigPath())) bBrand = true;
			A->SetStringField(TEXT("visualStyle"), VisualStyle.IsEmpty() ? TEXT("aurora") : VisualStyle);
			A->SetBoolField(TEXT("glowEffects"), bGlow);
			A->SetBoolField(TEXT("glassPanels"), bGlass);
			A->SetBoolField(TEXT("brandMark"), bBrand);
		}
		A->SetStringField(TEXT("language"),     FUIConfigManager::Get().GetLanguage());
		Root->SetObjectField(TEXT("appearance"), A);
	}

	{
		auto Acc = MakeShared<FJsonObject>();
		bool bReducedMotion = false, bHighContrast = false;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ReducedMotion"), bReducedMotion, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("HighContrast"), bHighContrast, FSettingsManager::GetGlobalConfigPath());
		Acc->SetBoolField(TEXT("reducedMotion"), bReducedMotion);
		Acc->SetBoolField(TEXT("highContrast"), bHighContrast);
		Root->SetObjectField(TEXT("accessibility"), Acc);
	}

	{
		bool bTimestamps = false, bSoundOnCompletion = false;
		int32 ToastDuration = 5;
		FString DefaultSavePath;
		bool bBatchedToolBlocks = true;
		bool bMergeToolCallAndResult = false;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ShowTimestamps"), bTimestamps, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("SoundOnCompletion"), bSoundOnCompletion, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("ToastDuration"), ToastDuration, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("DefaultSavePath"), DefaultSavePath, GEditorPerProjectIni);
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("BatchedToolBlocks"), bBatchedToolBlocks, FSettingsManager::GetGlobalConfigPath()))
			bBatchedToolBlocks = true;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("MergeToolCallAndResult"), bMergeToolCallAndResult, FSettingsManager::GetGlobalConfigPath());
		bool bAutoCompact = false;
		int32 AutoCompactPercent = 80;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoCompact"),        bAutoCompact,        FSettingsManager::GetGlobalConfigPath());
		GConfig->GetInt (TEXT("BpGeneratorUltimate"), TEXT("AutoCompactPercent"), AutoCompactPercent,  FSettingsManager::GetGlobalConfigPath());
		if (AutoCompactPercent < 50 || AutoCompactPercent > 95) AutoCompactPercent = 80;

		auto Prefs = MakeShared<FJsonObject>();
		Prefs->SetBoolField(TEXT("timestamps"), bTimestamps);
		Prefs->SetBoolField(TEXT("soundOnCompletion"), bSoundOnCompletion);
		Prefs->SetNumberField(TEXT("toastDuration"), ToastDuration > 0 ? ToastDuration : 5);
		Prefs->SetStringField(TEXT("defaultSavePath"), DefaultSavePath.IsEmpty() ? TEXT("/Game/Generated") : DefaultSavePath);
		Prefs->SetBoolField(TEXT("batchedToolBlocks"),     bBatchedToolBlocks);
		Prefs->SetBoolField(TEXT("mergeToolCallAndResult"), bMergeToolCallAndResult);
		Prefs->SetBoolField  (TEXT("autoCompact"),        bAutoCompact);
		Prefs->SetNumberField(TEXT("autoCompactPercent"), AutoCompactPercent);
		Root->SetObjectField(TEXT("chatPrefs"), Prefs);
	}

	{
		auto T = MakeShared<FJsonObject>();
		int32 DiscoverNodesLimit = 10, MaxToolCallDepth = 200;
		int32 ListAssetsLimit = 500;
		int32 AutoValidateMaxPasses = 2;
		bool bDestructiveConfirm = true, bAutoValidate = true, bEnablePreFlight = true;
		bool bHideAnnouncements = false;
		bool bSurgicalNoPIE = false;
		GConfig->GetInt (TEXT("BpGeneratorUltimate"), TEXT("DiscoverNodesLimit"),   DiscoverNodesLimit, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetInt (TEXT("BpGeneratorUltimate"), TEXT("MaxToolCallDepth"),     MaxToolCallDepth,   FSettingsManager::GetGlobalConfigPath());
		GConfig->GetInt (TEXT("BpGeneratorUltimate"), TEXT("ListAssetsLimit"),      ListAssetsLimit,    FSettingsManager::GetGlobalConfigPath());
		if (!GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("AutoValidateMaxPasses"), AutoValidateMaxPasses, FSettingsManager::GetGlobalConfigPath())) AutoValidateMaxPasses = 2;
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"),   bDestructiveConfirm, FSettingsManager::GetGlobalConfigPath())) bDestructiveConfirm = true;
		int32 ConfirmTimeoutSeconds = 25;
		bool bConfirmProceedWhenUnanswered = true;
		if (!GConfig->GetInt (TEXT("BpGeneratorUltimate"), TEXT("ConfirmTimeoutSeconds"), ConfirmTimeoutSeconds, FSettingsManager::GetGlobalConfigPath())) ConfirmTimeoutSeconds = 25;
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ConfirmProceedWhenUnanswered"), bConfirmProceedWhenUnanswered, FSettingsManager::GetGlobalConfigPath())) bConfirmProceedWhenUnanswered = true;
		ConfirmTimeoutSeconds = FMath::Clamp(ConfirmTimeoutSeconds, 5, 600);
		// Ceiling the PCG tools check before generating: past this the level is the kind of
		// instance soup that stalls the editor, so generation warns or refuses instead.
		int32 PCGInstanceBudget = 60000;
		if (!GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("PCGInstanceBudget"), PCGInstanceBudget, FSettingsManager::GetGlobalConfigPath())) PCGInstanceBudget = 60000;
		PCGInstanceBudget = FMath::Clamp(PCGInstanceBudget, 1000, 5000000);
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoValidateBlueprints"), bAutoValidate,       FSettingsManager::GetGlobalConfigPath())) bAutoValidate       = true;
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("EnablePreFlight"),         bEnablePreFlight,   FSettingsManager::GetGlobalConfigPath())) bEnablePreFlight     = true;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("HideAnnouncements"), bHideAnnouncements, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("SurgicalNoPIE"),     bSurgicalNoPIE,     FSettingsManager::GetGlobalConfigPath());
		if (DiscoverNodesLimit <= 0) DiscoverNodesLimit = 10;
		if (MaxToolCallDepth  < 10)  MaxToolCallDepth  = 200;
		if (ListAssetsLimit < 50)   ListAssetsLimit   = 500;
		AutoValidateMaxPasses = FMath::Clamp(AutoValidateMaxPasses, 0, 5);
		T->SetNumberField(TEXT("discoverNodesLimit"), DiscoverNodesLimit);
		T->SetNumberField(TEXT("maxToolCallDepth"),   MaxToolCallDepth);
		T->SetNumberField(TEXT("listAssetsLimit"),    ListAssetsLimit);
		T->SetNumberField(TEXT("autoValidateMaxPasses"), AutoValidateMaxPasses);
		T->SetBoolField  (TEXT("destructiveOpsConfirm"),   bDestructiveConfirm);
		T->SetNumberField(TEXT("confirmTimeoutSeconds"),  ConfirmTimeoutSeconds);
		T->SetBoolField  (TEXT("confirmProceedWhenUnanswered"), bConfirmProceedWhenUnanswered);
		T->SetNumberField(TEXT("pcgInstanceBudget"),      PCGInstanceBudget);
		T->SetBoolField  (TEXT("autoValidateBlueprints"),  bAutoValidate);
		T->SetBoolField  (TEXT("enablePreFlight"),          bEnablePreFlight);
		{
			bool bTurbo = false, bTurboSandbox = false;
			GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"),    bTurbo,        FSettingsManager::GetGlobalConfigPath());
			GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboSandbox"), bTurboSandbox, FSettingsManager::GetGlobalConfigPath());
			T->SetBoolField(TEXT("turboMode"),    bTurbo);
			T->SetBoolField(TEXT("turboSandbox"), bTurboSandbox);
		}
		T->SetBoolField  (TEXT("hideAnnouncements"),        bHideAnnouncements);
		T->SetBoolField  (TEXT("surgicalNoPIE"),            bSurgicalNoPIE);
		T->SetStringField(TEXT("defaultInteractionMode"),
			SUECPMainWidget::InteractionModeToString(FSettingsManager::Get().LoadDefaultInteractionMode()));
		FString PlanExecuteMode;
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("PlanExecuteMode"), PlanExecuteMode, FSettingsManager::GetGlobalConfigPath());
		T->SetStringField(TEXT("planExecuteMode"), PlanExecuteMode);
		Root->SetObjectField(TEXT("tools"), T);
	}

	return JsonStr(Root);
}

void UUECPSettingsBridge::SaveImageGenConfig(const FString& Json)
{
	TSharedPtr<FJsonObject> O = ParseJson(Json);
	if (!O.IsValid()) return;

	FApiKeyManager::FImageGenConfig Cfg = FApiKeyManager::Get().GetImageGenConfig();
	O->TryGetStringField(TEXT("apiKey"),      Cfg.ApiKey);
	O->TryGetStringField(TEXT("endpoint"),    Cfg.Endpoint);
	O->TryGetStringField(TEXT("model"),       Cfg.Model);
	O->TryGetBoolField  (TEXT("textureMode"), Cfg.bTextureMode);
	FApiKeyManager::Get().SetImageGenConfig(Cfg);
}

void UUECPSettingsBridge::SaveSlot(const FString& SlotJson)
{
	TSharedPtr<FJsonObject> O = ParseJson(SlotJson);
	if (!O.IsValid()) return;

	int32 Idx = (int32)O->GetNumberField(TEXT("index"));
	if (Idx < 0 || Idx >= MAX_API_KEY_SLOTS) return;

	FApiKeySlot Slot = FApiKeyManager::Get().GetSlot(Idx);
	Slot.SlotIndex = Idx;

	FString Tmp;
	if (O->TryGetStringField(TEXT("name"), Tmp))          Slot.Name = Tmp;
	if (O->TryGetStringField(TEXT("provider"), Tmp))      Slot.Provider = Tmp;
	if (O->TryGetStringField(TEXT("apiKey"), Tmp))        Slot.ApiKey = Tmp;
	if (O->TryGetStringField(TEXT("geminiModel"),   Tmp)) Slot.GeminiModel   = Tmp;
	if (O->TryGetStringField(TEXT("openAIModel"),   Tmp)) Slot.OpenAIModel   = Tmp;
	if (O->TryGetStringField(TEXT("claudeModel"),   Tmp)) Slot.ClaudeModel   = Tmp;
	if (O->TryGetStringField(TEXT("deepseekModel"), Tmp)) Slot.DeepSeekModel = Tmp;
	if (O->TryGetStringField(TEXT("agentModel"), Tmp))    Slot.AgentModel = Tmp;
	if (O->TryGetStringField(TEXT("agentEffort"), Tmp))   Slot.AgentEffort = Tmp;
	if (O->TryGetStringField(TEXT("customBaseURL"), Tmp)) Slot.CustomBaseURL = Tmp;
	if (O->TryGetStringField(TEXT("customModelName"), Tmp)) Slot.CustomModelName = Tmp;
	if (O->TryGetStringField(TEXT("customParams"), Tmp))  Slot.CustomParams = Tmp;

	const TSharedPtr<FJsonObject>* TexO = nullptr;
	if (O->TryGetObjectField(TEXT("textureGen"), TexO) && TexO)
	{
		if ((*TexO)->TryGetStringField(TEXT("apiKey"), Tmp))   Slot.TextureGenApiKey = Tmp;
		if ((*TexO)->TryGetStringField(TEXT("endpoint"), Tmp)) Slot.TextureGenEndpoint = Tmp;
		if ((*TexO)->TryGetStringField(TEXT("model"), Tmp))    Slot.TextureGenModel = Tmp;
		bool bMode = true;
		if ((*TexO)->TryGetBoolField(TEXT("textureMode"), bMode)) Slot.bTextureMode = bMode;
	}

	const TSharedPtr<FJsonObject>* MeshO = nullptr;
	if (O->TryGetObjectField(TEXT("meshGen"), MeshO) && MeshO)
	{
		if ((*MeshO)->TryGetStringField(TEXT("apiKey"), Tmp))  Slot.MeshGenApiKey = Tmp;
		if ((*MeshO)->TryGetStringField(TEXT("aiModel"), Tmp)) Slot.MeshyAiModel = Tmp;
	}

	const TSharedPtr<FJsonObject>* SndO = nullptr;
	if (O->TryGetObjectField(TEXT("soundGen"), SndO) && SndO)
	{
		if ((*SndO)->TryGetStringField(TEXT("apiKey"), Tmp))   Slot.SoundGenApiKey = Tmp;
		if ((*SndO)->TryGetStringField(TEXT("endpoint"), Tmp)) Slot.SoundGenEndpoint = Tmp;
		if ((*SndO)->TryGetStringField(TEXT("model"), Tmp))    Slot.SoundGenModel = Tmp;
		if ((*SndO)->TryGetStringField(TEXT("voice"), Tmp))    Slot.SoundGenVoice = Tmp;
	}

	FApiKeyManager::Get().SetSlot(Idx, Slot);
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("MeshyApiKey"), *Slot.MeshGenApiKey, FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());

	if (auto W = OwnerWidget.Pin())
	{
		if (W->AppBridgeObject) W->AppBridgeObject->CrewListSlots();
	}
}

void UUECPSettingsBridge::SaveFreeTierSlot(const FString& Slot)
{
	if (Slot.Contains(TEXT(":")))
	{
		FString SlotPart, ModelPart;
		Slot.Split(TEXT(":"), &SlotPart, &ModelPart);
		FString Validated = (SlotPart == TEXT("slot2")) ? TEXT("slot2") : TEXT("slot1");
		GConfig->SetString(TEXT("FreeTier"), TEXT("SelectedSlot"), *Validated, FSettingsManager::GetGlobalConfigPath());
		if (!ModelPart.IsEmpty())
			GConfig->SetString(TEXT("FreeTier"), TEXT("Slot2Model"), *ModelPart, FSettingsManager::GetGlobalConfigPath());
	}
	else
	{
		FString Validated = (Slot == TEXT("slot2")) ? TEXT("slot2") : TEXT("slot1");
		GConfig->SetString(TEXT("FreeTier"), TEXT("SelectedSlot"), *Validated, FSettingsManager::GetGlobalConfigPath());
	}
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

void UUECPSettingsBridge::SaveToolSettings(const FString& Json)
{
	TSharedPtr<FJsonObject> O = ParseJson(Json);
	if (!O.IsValid()) return;

	double D = 0; bool B = true;
	if (O->TryGetNumberField(TEXT("discoverNodesLimit"), D) && D >= 1)
		GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("DiscoverNodesLimit"), (int32)D, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetNumberField(TEXT("maxToolCallDepth"), D) && D >= 10)
		GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxToolCallDepth"), FMath::Clamp((int32)D, 10, 500), FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetNumberField(TEXT("listAssetsLimit"), D) && D >= 50)
		GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("ListAssetsLimit"), FMath::Clamp((int32)D, 50, 5000), FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetNumberField(TEXT("autoValidateMaxPasses"), D) && D >= 0)
		GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("AutoValidateMaxPasses"), FMath::Clamp((int32)D, 0, 5), FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("destructiveOpsConfirm"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"), B, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetNumberField(TEXT("confirmTimeoutSeconds"), D))
		GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("ConfirmTimeoutSeconds"), FMath::Clamp((int32)D, 5, 600), FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("confirmProceedWhenUnanswered"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("ConfirmProceedWhenUnanswered"), B, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetNumberField(TEXT("pcgInstanceBudget"), D))
		GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("PCGInstanceBudget"), FMath::Clamp((int32)D, 1000, 5000000), FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("autoValidateBlueprints"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoValidateBlueprints"), B, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("enablePreFlight"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("EnablePreFlight"), B, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("turboMode"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), B, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("turboSandbox"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboSandbox"), B, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("hideAnnouncements"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("HideAnnouncements"), B, FSettingsManager::GetGlobalConfigPath());
	if (O->TryGetBoolField(TEXT("surgicalNoPIE"), B))
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("SurgicalNoPIE"), B, FSettingsManager::GetGlobalConfigPath());
		{
			FString PlanExec;
			if (O->TryGetStringField(TEXT("planExecuteMode"), PlanExec))
			{
				PlanExec = PlanExec.TrimStartAndEnd().ToLower();
				if (PlanExec != TEXT("auto") && PlanExec != TEXT("ask")) PlanExec = TEXT("");
				GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("PlanExecuteMode"), *PlanExec, FSettingsManager::GetGlobalConfigPath());
			}
		}
	FString ModeStr;
	if (O->TryGetStringField(TEXT("defaultInteractionMode"), ModeStr) && !ModeStr.IsEmpty())
	{
		FSettingsManager::Get().SaveDefaultInteractionMode(
			SUECPMainWidget::StringToInteractionMode(ModeStr, EAIInteractionMode::AutoEdit));
	}
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

void UUECPSettingsBridge::RequestExtensionsCatalog()
{
	PushExtensionsCatalog();
	PushExtensionsPending();
}

void UUECPSettingsBridge::ToggleExtension(const FString& ExtensionId, bool bEnable)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
	const FName Id(*ExtensionId);

	FString Error;
	TArray<FString> MissingPlugins;
	const bool bOk = Ext.SetExtensionEnabled(Id, bEnable, Error, &MissingPlugins,  false);

	const bool bAutoQueued = (!bOk && bEnable && MissingPlugins.Num() > 0);
	if (bAutoQueued)
	{
		Ext.QueueExtensionChange(Id, true);
	}

	auto Browser = BrowserRef.Pin();
	if (Browser.IsValid())
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), bOk);
		O->SetBoolField(TEXT("queued"), bAutoQueued);
		O->SetStringField(TEXT("error"), bAutoQueued ? FString() : Error);
		if (MissingPlugins.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FString& P : MissingPlugins) Arr.Add(MakeShared<FJsonValueString>(P));
			O->SetArrayField(TEXT("missingPlugins"), Arr);
		}
		const FString Body = JsonStr(O);
		Browser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof onExtensionToggleResult==='function')onExtensionToggleResult('%s', %s)"),
			*ExtensionId.ReplaceCharWithEscapedChar(), *Body));
	}

	PushExtensionsCatalog();
	if (bAutoQueued) PushExtensionsPending();
}

void UUECPSettingsBridge::RestartExtensionMcpServer(const FString& ExtensionId)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
	const FName Id(*ExtensionId);
	TOptional<FUECPExtensionDescriptor> Desc = Ext.FindExtension(Id);
	if (!Desc.IsSet() || !Desc->McpServer.IsSet())
	{
		UE_LOG(LogTemp, Warning, TEXT("RestartExtensionMcpServer: '%s' is not MCP-backed"), *ExtensionId);
		return;
	}

	FString DummyError;
	Ext.SetExtensionEnabled(Id, false, DummyError, nullptr,  false);
	FString Error;
	Ext.SetExtensionEnabled(Id, true, Error, nullptr,  false);

	if (!Error.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("RestartExtensionMcpServer '%s' failed: %s"),
			*ExtensionId, *Error);
	}

	PushExtensionsCatalog();
}

void UUECPSettingsBridge::EnableExtensionPluginsAndRestart(const FString& ExtensionId)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
	TOptional<FUECPExtensionDescriptor> Desc = Ext.FindExtension(FName(*ExtensionId));
	if (!Desc.IsSet())
	{
		UE_LOG(LogTemp, Warning, TEXT("EnableExtensionPluginsAndRestart: unknown extension '%s'"), *ExtensionId);
		return;
	}

	IProjectManager& PM = IProjectManager::Get();

	TArray<FString> Failures;
	for (const FString& PluginName : Desc->RequiredPlugins)
	{
		FText FailReason;
		if (!PM.SetPluginEnabled(PluginName,  true, FailReason))
		{
			Failures.Add(FString::Printf(TEXT("%s: %s"), *PluginName, *FailReason.ToString()));
		}
	}

	if (Failures.Num() > 0)
	{
		auto Browser = BrowserRef.Pin();
		if (Browser.IsValid())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), false);
			O->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to enable plugins:\n  - %s"),
				*FString::Join(Failures, TEXT("\n  - "))));
			const FString Body = JsonStr(O);
			Browser->ExecuteJavascript(FString::Printf(
				TEXT("if(typeof onExtensionToggleResult==='function')onExtensionToggleResult('%s', %s)"),
				*ExtensionId.ReplaceCharWithEscapedChar(), *Body));
		}
		return;
	}

	FText SaveFailReason;
	if (!PM.SaveCurrentProjectToDisk(SaveFailReason))
	{
		UE_LOG(LogTemp, Warning, TEXT("EnableExtensionPluginsAndRestart: SaveCurrentProjectToDisk failed: %s"),
			*SaveFailReason.ToString());
		auto Browser = BrowserRef.Pin();
		if (Browser.IsValid())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), false);
			O->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to save project: %s"),
				*SaveFailReason.ToString()));
			const FString Body = JsonStr(O);
			Browser->ExecuteJavascript(FString::Printf(
				TEXT("if(typeof onExtensionToggleResult==='function')onExtensionToggleResult('%s', %s)"),
				*ExtensionId.ReplaceCharWithEscapedChar(), *Body));
		}
		return;
	}

	IUECPCoreModule::Get().GetSettingsService().SetBool(
		TEXT("Extensions"),
		FString::Printf(TEXT("%s.Enabled"), *ExtensionId),
		true);

	UE_LOG(LogTemp, Log, TEXT("EnableExtensionPluginsAndRestart: enabled %d plugins for '%s' — restarting editor"),
		Desc->RequiredPlugins.Num(), *ExtensionId);

	FUnrealEdMisc::Get().RestartEditor( false);
}

void UUECPSettingsBridge::QueueExtensionChange(const FString& ExtensionId, bool bEnable)
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPCoreModule::Get().GetExtensionService().QueueExtensionChange(FName(*ExtensionId), bEnable);
	PushExtensionsPending();
}

void UUECPSettingsBridge::UnqueueExtensionChange(const FString& ExtensionId)
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPCoreModule::Get().GetExtensionService().UnqueueExtensionChange(FName(*ExtensionId));
	PushExtensionsPending();
}

void UUECPSettingsBridge::CancelPendingExtensionChanges()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPCoreModule::Get().GetExtensionService().CancelPendingChanges();
	PushExtensionsPending();
}

void UUECPSettingsBridge::ApplyPendingExtensionChanges()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();

	FString Error;
	const bool bOk = Ext.ApplyPendingChanges(Error);

	auto Browser = BrowserRef.Pin();
	if (Browser.IsValid())
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), bOk);
		O->SetStringField(TEXT("error"), Error);
		const FString Body = JsonStr(O);
		Browser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof onExtensionsApplyResult==='function')onExtensionsApplyResult(%s)"), *Body));
	}

	PushExtensionsCatalog();
	PushExtensionsPending();
}

void UUECPSettingsBridge::PushExtensionsPending()
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid() || !IUECPCoreModule::IsAvailable()) return;

	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
	const TArray<IUECPExtensionService::FPendingChange> Changes = Ext.GetPendingChanges();

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const IUECPExtensionService::FPendingChange& C : Changes)
	{
		TOptional<FUECPExtensionDescriptor> Desc = Ext.FindExtension(C.ExtensionId);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), C.ExtensionId.ToString());
		O->SetStringField(TEXT("name"), Desc.IsSet() ? Desc->DisplayName.ToString() : C.ExtensionId.ToString());
		O->SetBoolField(TEXT("enabling"), C.bEnabling);
		TArray<TSharedPtr<FJsonValue>> Plugins;
		for (const FString& P : C.RequiredPluginsToEnable) Plugins.Add(MakeShared<FJsonValueString>(P));
		O->SetArrayField(TEXT("requiredPlugins"), Plugins);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("changes"), Arr);
	Root->SetBoolField(TEXT("requiresRestart"), Ext.PendingChangesRequireRestart());
	const FString Body = JsonStr(Root);

	Browser->ExecuteJavascript(FString::Printf(
		TEXT("if(typeof onExtensionsPending==='function')onExtensionsPending(%s)"), *Body));
}

void UUECPSettingsBridge::PushExtensionsCatalog()
{
	if (!IUECPCoreModule::IsAvailable()) return;

	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
	const TArray<FUECPExtensionDescriptor>& Descriptors = Ext.GetExtensions();

	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Reserve(Descriptors.Num());
	for (const FUECPExtensionDescriptor& D : Descriptors)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), D.ExtensionId.ToString());
		O->SetStringField(TEXT("name"), D.DisplayName.ToString());
		O->SetStringField(TEXT("description"), D.Description.ToString());
		O->SetStringField(TEXT("category"), D.Category.IsEmpty() ? TEXT("Other") : D.Category.ToString());
		O->SetStringField(TEXT("module"), D.RequiredModuleName.ToString());
		O->SetBoolField(TEXT("requiresLicense"), D.bRequiresLicense);
		O->SetBoolField(TEXT("enabled"), Ext.IsExtensionEnabled(D.ExtensionId));
		O->SetStringField(TEXT("state"), LexToString(Ext.GetExtensionState(D.ExtensionId)));

		O->SetBoolField(TEXT("isThirdParty"), D.bIsThirdParty);
		if (!D.AuthorName.IsEmpty())       O->SetStringField(TEXT("authorName"),       D.AuthorName);
		if (!D.WebsiteUrl.IsEmpty())       O->SetStringField(TEXT("websiteUrl"),       D.WebsiteUrl);
		if (!D.IconUrl.IsEmpty())          O->SetStringField(TEXT("iconUrl"),          D.IconUrl);
		if (!D.MinEngineVersion.IsEmpty()) O->SetStringField(TEXT("minEngineVersion"), D.MinEngineVersion);
		if (!D.MaxEngineVersion.IsEmpty()) O->SetStringField(TEXT("maxEngineVersion"), D.MaxEngineVersion);

		TArray<TSharedPtr<FJsonValue>> Plugins;
		for (const FString& P : D.RequiredPlugins) Plugins.Add(MakeShared<FJsonValueString>(P));
		O->SetArrayField(TEXT("requiredPlugins"), Plugins);

		if (D.McpServer.IsSet())
		{
			O->SetBoolField(TEXT("mcpBacked"), true);
			O->SetStringField(TEXT("mcpCommand"), D.McpServer->Command);

			TSharedPtr<FUECPMcpClient> Client = Ext.GetMcpClient(D.ExtensionId);
			const FString LastError = Ext.GetMcpLastError(D.ExtensionId);
			FString McpStatus = TEXT("idle");
			int32 ToolCount = 0;
			if (Client.IsValid())
			{
				if (Client->IsConnected()) { McpStatus = TEXT("running"); ToolCount = Client->GetTools().Num(); }
				else                       { McpStatus = TEXT("connecting"); }
			}
			else if (!LastError.IsEmpty())
			{
				McpStatus = TEXT("failed");
			}
			O->SetStringField(TEXT("mcpStatus"), McpStatus);
			O->SetNumberField(TEXT("mcpToolCount"), ToolCount);
			if (!LastError.IsEmpty()) O->SetStringField(TEXT("mcpLastError"), LastError);
		}

		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	FString Body;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Arr, W); W->Close();

	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;

	const FString EngineVer = FString::Printf(TEXT("%d.%d.%d"),
		ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION);

	Browser->ExecuteJavascript(FString::Printf(
		TEXT("window.runningEngineVersion='%s';if(typeof onExtensionsCatalog==='function')onExtensionsCatalog(%s)"),
		*EngineVer, *Body));
}

void UUECPSettingsBridge::SetActiveSlot(int32 Index)
{
	if (Index >= 0 && Index < MAX_API_KEY_SLOTS)
		FApiKeyManager::Get().SetActiveSlot(Index);

	auto W = OwnerWidget.Pin();
	if (W.IsValid() && W->AppBridgeObject)
		W->AppBridgeObject->PushSlashContext();

	PushACPCatalog();

	if (W.IsValid() && W->AppBridgeObject)
	{
		W->AppBridgeObject->CrewListSlots();
	}
}

void UUECPSettingsBridge::SaveAgentConfig(const FString& Json)
{
	TSharedPtr<FJsonObject> O = ParseJson(Json);
	if (!O.IsValid()) return;

	FString Provider, Model, Effort;
	O->TryGetStringField(TEXT("provider"), Provider);
	O->TryGetStringField(TEXT("model"), Model);
	O->TryGetStringField(TEXT("effort"), Effort);
	if (Provider.IsEmpty()) return;

	{
		int32 TargetIdx = -1;
		double SlotIdxDouble = 0.0;
		if (O->TryGetNumberField(TEXT("slotIndex"), SlotIdxDouble))
			TargetIdx = (int32)SlotIdxDouble;
		if (TargetIdx < 0 || TargetIdx >= MAX_API_KEY_SLOTS)
			TargetIdx = FApiKeyManager::Get().GetActiveSlotIndex();

		FApiKeySlot Slot = FApiKeyManager::Get().GetSlot(TargetIdx);
		if (!Model.IsEmpty())  Slot.AgentModel = Model;
		if (!Effort.IsEmpty()) Slot.AgentEffort = Effort;
		FApiKeyManager::Get().SetSlot(TargetIdx, Slot);
	}

	auto W = OwnerWidget.Pin();
	if (W.IsValid() && W->AppBridgeObject)
		W->AppBridgeObject->PushSlashContext();
}

void UUECPSettingsBridge::SaveVoiceSettings(const FString& Json)
{
	TSharedPtr<FJsonObject> O = ParseJson(Json);
	if (!O.IsValid()) return;

	FString ApiKey;
	bool bEnabled = false, bAutoPlay = false, bAutoSend = false;
	bool bHasVoiceFields = O->HasField(TEXT("apiKey"));
	if (bHasVoiceFields)
	{
		O->TryGetStringField(TEXT("apiKey"), ApiKey);
		O->TryGetBoolField(TEXT("enabled"), bEnabled);
		O->TryGetBoolField(TEXT("autoPlay"), bAutoPlay);
		O->TryGetBoolField(TEXT("autoSend"), bAutoSend);
	}

	if (bHasVoiceFields)
	{
		FUECPVoiceSettingsSnapshot VoiceSnap = IUECPCoreModule::Get().GetVoiceService().GetSettings();
		VoiceSnap.VoiceApiKey                 = ApiKey;
		VoiceSnap.bVoiceEnabled               = bEnabled;
		VoiceSnap.bAutoPlayResponse           = bAutoPlay;
		VoiceSnap.bAutoSendAfterTranscription = bAutoSend;

		FString TtsVoice;
		if (O->TryGetStringField(TEXT("ttsVoice"), TtsVoice) && !TtsVoice.IsEmpty())
		{
			VoiceSnap.TTSVoice = TtsVoice;
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("TTSVoice"), *TtsVoice, FSettingsManager::GetGlobalConfigPath());
		}

		IUECPCoreModule::Get().GetVoiceService().ApplySettings(VoiceSnap);

		GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("VoiceApiKey"), *ApiKey, FSettingsManager::GetGlobalConfigPath());
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("VoiceEnabled"), bEnabled, FSettingsManager::GetGlobalConfigPath());
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoPlayResponse"), bAutoPlay, FSettingsManager::GetGlobalConfigPath());
		GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoSendVoice"), bAutoSend, FSettingsManager::GetGlobalConfigPath());
	}

	if (O->HasField(TEXT("soundGenApiKey")))
	{
		FString SgApiKey, SgEndpoint, SgModel, SgVoice, SgProvider;
		O->TryGetStringField(TEXT("soundGenApiKey"),   SgApiKey);
		O->TryGetStringField(TEXT("soundGenEndpoint"), SgEndpoint);
		O->TryGetStringField(TEXT("soundGenModel"),    SgModel);
		O->TryGetStringField(TEXT("soundGenVoice"),    SgVoice);
		O->TryGetStringField(TEXT("soundGenProvider"), SgProvider);

		int32 Idx = FApiKeyManager::Get().GetActiveSlotIndex();
		FApiKeySlot Slot = FApiKeyManager::Get().GetSlot(Idx);
		Slot.SoundGenApiKey   = SgApiKey;
		Slot.SoundGenEndpoint = SgEndpoint.IsEmpty() ? TEXT("https://api.openai.com/v1/audio/speech") : SgEndpoint;
		Slot.SoundGenModel    = SgModel.IsEmpty()    ? TEXT("tts-1") : SgModel;
		Slot.SoundGenVoice    = SgVoice.IsEmpty()    ? TEXT("alloy") : SgVoice;
		FApiKeyManager::Get().SetSlot(Idx, Slot);
		if (!SgProvider.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("SoundGenProvider"), *SgProvider, FSettingsManager::GetGlobalConfigPath());
		FString SgKeyCache;
		if (O->TryGetStringField(TEXT("soundGenKeyCache"), SgKeyCache) && !SgKeyCache.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("SoundGenKeyCache"), *SgKeyCache, FSettingsManager::GetGlobalConfigPath());
	}

	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());

	if (bHasVoiceFields)
	{
		if (auto W = OwnerWidget.Pin())
			if (W->AppBridgeObject)
				W->AppBridgeObject->ExecJs(FString::Printf(
					TEXT("if(typeof onVoiceEnabled==='function')onVoiceEnabled(%s)"),
					bEnabled ? TEXT("true") : TEXT("false")));
	}
}

void UUECPSettingsBridge::SaveTheme(const FString& Json)
{
	TSharedPtr<FJsonObject> O = ParseJson(Json);
	if (!O.IsValid()) return;

	FString PresetName;
	O->TryGetStringField(TEXT("preset"), PresetName);
	EThemePreset Preset = ThemePresetFromName(PresetName);

	FBpGeneratorTheme Theme;
	if (Preset != EThemePreset::Custom)
	{
		Theme = FBpGeneratorTheme::GetPreset(Preset);
	}
	else
	{
		Theme = FThemeManager::Get().LoadTheme();
	}
	Theme.Preset = Preset;

	auto TryColor = [&](const FString& Field, FLinearColor& Out)
	{
		FString Hex;
		if (O->TryGetStringField(Field, Hex) && !Hex.IsEmpty())
			Out = HexToLinearColor(Hex);
	};
	TryColor(TEXT("primaryColor"),    Theme.PrimaryColor);
	TryColor(TEXT("secondaryColor"),  Theme.SecondaryColor);
	TryColor(TEXT("tertiaryColor"),   Theme.TertiaryColor);
	TryColor(TEXT("mainBackground"),  Theme.MainBackgroundColor);
	TryColor(TEXT("panelBackground"), Theme.PanelBackgroundColor);
	TryColor(TEXT("borderBackground"),Theme.BorderBackgroundColor);
	TryColor(TEXT("primaryText"),     Theme.PrimaryTextColor);
	TryColor(TEXT("secondaryText"),   Theme.SecondaryTextColor);
	TryColor(TEXT("hintText"),        Theme.HintTextColor);
	TryColor(TEXT("analystColor"),    Theme.AnalystColor);
	TryColor(TEXT("scannerColor"),    Theme.ScannerColor);
	TryColor(TEXT("architectColor"),  Theme.ArchitectColor);
	TryColor(TEXT("gradientTop"),     Theme.GradientTop);
	TryColor(TEXT("gradientBottom"),  Theme.GradientBottom);

	bool bUseGrad = Theme.bUseGradients;
	if (O->TryGetBoolField(TEXT("useGradients"), bUseGrad)) Theme.bUseGradients = bUseGrad;
	double Opacity = Theme.BorderOpacity, Radius = Theme.CornerRadius;
	if (O->TryGetNumberField(TEXT("borderOpacity"), Opacity)) Theme.BorderOpacity = (float)Opacity;
	if (O->TryGetNumberField(TEXT("cornerRadius"), Radius))   Theme.CornerRadius  = (float)Radius;

	FThemeManager::Get().SaveTheme(Theme);

	if (auto W = OwnerWidget.Pin())
		W->CurrentTheme = Theme;
}

void UUECPSettingsBridge::SetRebindCapturing(bool bCapturing)
{
	if (auto W = OwnerWidget.Pin())
	{
		W->bUiCapturingKeybind = bCapturing;
	}
}

void UUECPSettingsBridge::SavePreferences(const FString& Json)
{
	TSharedPtr<FJsonObject> O = ParseJson(Json);
	if (!O.IsValid()) return;

	FString LangName;
	if (O->TryGetStringField(TEXT("language"), LangName) && !LangName.IsEmpty())
	{
		FString LangCode = LangNameToCode(LangName);
		FUIConfigManager::Get().SetLanguage(LangCode);
		if (auto W = OwnerWidget.Pin())
		{
			W->InvalidatePromptCaches();
			W->PushAppearanceToAppShell(FString::Printf(TEXT("{\"language\":\"%s\"}"), *LangCode));
		}
	}

	auto SaveKeybind = [&](const FString& ModField, const FString& KeyField,
	                        const FString& ModCfgKey, const FString& KeyCfgKey,
	                        FString& OutModStr, FString& OutKeyStr)
	{
		const TSharedPtr<FJsonObject>* KB = nullptr;
		if (!O->TryGetObjectField(ModField, KB) || !KB) return;
		(*KB)->TryGetStringField(TEXT("modifier"), OutModStr);
		(*KB)->TryGetStringField(TEXT("key"), OutKeyStr);
		if (!OutModStr.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), *ModCfgKey, *OutModStr, FSettingsManager::GetGlobalConfigPath());
		if (!OutKeyStr.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), *KeyCfgKey, *OutKeyStr, FSettingsManager::GetGlobalConfigPath());
	};

	FString AMod, AKey, VMod, VKey;
	SaveKeybind(TEXT("arrangeKeybind"), TEXT("arrangeKeybind"),
	            TEXT("ArrangeNodesModifier"), TEXT("ArrangeNodesKey"), AMod, AKey);
	SaveKeybind(TEXT("voicePTTKeybind"), TEXT("voicePTTKeybind"),
	            TEXT("VoicePTTModifier"), TEXT("VoicePTTKey"), VMod, VKey);

	if (auto W = OwnerWidget.Pin())
	{
		if (!AMod.IsEmpty()) W->ArrangeNodesKeybind.ModifierKey = (AMod == TEXT("None")) ? EKeys::Invalid : FKey(FName(*AMod));
		if (!AKey.IsEmpty()) W->ArrangeNodesKeybind.ActionKey = FKey(FName(*AKey));
		if (!VMod.IsEmpty()) W->VoicePTTKeybind.ModifierKey = (VMod == TEXT("None")) ? EKeys::Invalid : FKey(FName(*VMod));
		if (!VKey.IsEmpty()) W->VoicePTTKeybind.ActionKey = FKey(FName(*VKey));
	}

	const TSharedPtr<FJsonObject>* AppearanceObj = nullptr;
	if (O->TryGetObjectField(TEXT("appearance"), AppearanceObj) && AppearanceObj)
	{
		double FontSize = 0.0, UiFontSize = 0.0;
		FString Density, AccentColor, ColorTheme, CodeFontFamily;
		FString UiFontFamily, BgColor, SurfaceColor;
		if ((*AppearanceObj)->TryGetNumberField(TEXT("chatFontSize"), FontSize) && FontSize > 0.0)
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("ChatFontSize"),
				*FString::Printf(TEXT("%.1f"), FontSize), FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("chatDensity"), Density) && !Density.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("ChatDensity"), *Density, FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("accentColor"), AccentColor) && !AccentColor.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("AccentColor"), *AccentColor, FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("colorTheme"), ColorTheme) && !ColorTheme.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("ColorTheme"), *ColorTheme, FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("codeFontFamily"), CodeFontFamily) && !CodeFontFamily.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("CodeFontFamily"), *CodeFontFamily, FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetNumberField(TEXT("uiFontSize"), UiFontSize) && UiFontSize > 0.0)
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("UiFontSize"),
				*FString::Printf(TEXT("%.1f"), UiFontSize), FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("uiFontFamily"), UiFontFamily) && !UiFontFamily.IsEmpty())
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("UiFontFamily"), *UiFontFamily, FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("bgColor"), BgColor))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("BgColor"), *BgColor, FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("surfaceColor"), SurfaceColor))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("SurfaceColor"), *SurfaceColor, FSettingsManager::GetGlobalConfigPath());
		FString TextColor;
		if ((*AppearanceObj)->TryGetStringField(TEXT("textColor"), TextColor))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("TextColor"), *TextColor, FSettingsManager::GetGlobalConfigPath());
		FString UserBubbleBg, UserBubbleText, AiBubbleBg, AiBubbleText;
		if ((*AppearanceObj)->TryGetStringField(TEXT("userBubbleBg"), UserBubbleBg))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("UserBubbleBg"),   *UserBubbleBg,   FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("userBubbleText"), UserBubbleText))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("UserBubbleText"), *UserBubbleText, FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("aiBubbleBg"), AiBubbleBg))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("AiBubbleBg"),     *AiBubbleBg,     FSettingsManager::GetGlobalConfigPath());
		if ((*AppearanceObj)->TryGetStringField(TEXT("aiBubbleText"), AiBubbleText))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("AiBubbleText"),   *AiBubbleText,   FSettingsManager::GetGlobalConfigPath());
		bool bAutoContrast = true;
		if ((*AppearanceObj)->TryGetBoolField(TEXT("autoContrast"), bAutoContrast))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoContrast"), bAutoContrast, FSettingsManager::GetGlobalConfigPath());
		FString LayoutMode;
		if ((*AppearanceObj)->TryGetStringField(TEXT("layoutMode"), LayoutMode))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("LayoutMode"), *LayoutMode, FSettingsManager::GetGlobalConfigPath());
		FString InputStyle;
		if ((*AppearanceObj)->TryGetStringField(TEXT("inputStyle"), InputStyle))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("InputStyle"), *InputStyle, FSettingsManager::GetGlobalConfigPath());
		{
			// Axivor Studio (visual identity) keys
			FString VisualStyle; bool bFlag = true;
			if ((*AppearanceObj)->TryGetStringField(TEXT("visualStyle"), VisualStyle) && !VisualStyle.IsEmpty())
				GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("VisualStyle"), *VisualStyle, FSettingsManager::GetGlobalConfigPath());
			if ((*AppearanceObj)->TryGetBoolField(TEXT("glowEffects"), bFlag))
				GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("GlowEffects"), bFlag, FSettingsManager::GetGlobalConfigPath());
			if ((*AppearanceObj)->TryGetBoolField(TEXT("glassPanels"), bFlag))
				GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("GlassPanels"), bFlag, FSettingsManager::GetGlobalConfigPath());
			if ((*AppearanceObj)->TryGetBoolField(TEXT("brandMark"), bFlag))
				GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("BrandMark"), bFlag, FSettingsManager::GetGlobalConfigPath());
		}

		if (auto W = OwnerWidget.Pin())
			W->PushAppearanceToAppShell(JsonStr(*AppearanceObj));
	}

	const TSharedPtr<FJsonObject>* A11yObj = nullptr;
	if (O->TryGetObjectField(TEXT("accessibility"), A11yObj) && A11yObj)
	{
		bool bVal = false;
		if ((*A11yObj)->TryGetBoolField(TEXT("reducedMotion"), bVal))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("ReducedMotion"), bVal, FSettingsManager::GetGlobalConfigPath());
		if ((*A11yObj)->TryGetBoolField(TEXT("highContrast"), bVal))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("HighContrast"), bVal, FSettingsManager::GetGlobalConfigPath());

		if (auto W = OwnerWidget.Pin())
			W->PushAppearanceToAppShell(JsonStr(*A11yObj));
	}

	const TSharedPtr<FJsonObject>* ChatPrefsObj = nullptr;
	if (O->TryGetObjectField(TEXT("chatPrefs"), ChatPrefsObj) && ChatPrefsObj)
	{
		bool bVal = false;
		if ((*ChatPrefsObj)->TryGetBoolField(TEXT("timestamps"), bVal))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("ShowTimestamps"), bVal, FSettingsManager::GetGlobalConfigPath());
		if ((*ChatPrefsObj)->TryGetBoolField(TEXT("soundOnCompletion"), bVal))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("SoundOnCompletion"), bVal, FSettingsManager::GetGlobalConfigPath());
		if ((*ChatPrefsObj)->TryGetBoolField(TEXT("batchedToolBlocks"), bVal))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("BatchedToolBlocks"), bVal, FSettingsManager::GetGlobalConfigPath());
		if ((*ChatPrefsObj)->TryGetBoolField(TEXT("mergeToolCallAndResult"), bVal))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("MergeToolCallAndResult"), bVal, FSettingsManager::GetGlobalConfigPath());
		if ((*ChatPrefsObj)->TryGetBoolField(TEXT("autoCompact"), bVal))
			GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoCompact"), bVal, FSettingsManager::GetGlobalConfigPath());
		double AcPctNum = 0;
		if ((*ChatPrefsObj)->TryGetNumberField(TEXT("autoCompactPercent"), AcPctNum))
		{
			int32 AcPct = FMath::Clamp((int32)AcPctNum, 50, 95);
			GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("AutoCompactPercent"), AcPct, FSettingsManager::GetGlobalConfigPath());
		}
		double ToastDur = 0;
		if ((*ChatPrefsObj)->TryGetNumberField(TEXT("toastDuration"), ToastDur) && ToastDur >= 2)
			GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("ToastDuration"), (int32)ToastDur, FSettingsManager::GetGlobalConfigPath());
		FString SavePath;
		if ((*ChatPrefsObj)->TryGetStringField(TEXT("defaultSavePath"), SavePath))
			GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("DefaultSavePath"), *SavePath, GEditorPerProjectIni);

		if (auto W = OwnerWidget.Pin())
			W->PushAppearanceToAppShell(JsonStr(*ChatPrefsObj));
	}

	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

void UUECPSettingsBridge::RequestCliStatus()
{
	if (auto W = OwnerWidget.Pin())
		W->RefreshCliStatusAsync();
}

void UUECPSettingsBridge::InstallCli(const FString& ProviderName)
{
	if (auto W = OwnerWidget.Pin())
	{
		FString Cmd, Args;
		FString FollowUpCmd, FollowUpArgs;

		if (ProviderName == TEXT("Claude Agent"))
		{
			Cmd = TEXT("npm"); Args = TEXT("install -g @anthropic-ai/claude-code");
#if PLATFORM_WINDOWS
			FollowUpCmd = TEXT("pip"); FollowUpArgs = TEXT("install --upgrade claude-agent-sdk");
#else
			FollowUpCmd = TEXT("pip3"); FollowUpArgs = TEXT("install --upgrade claude-agent-sdk");
#endif
		}
		else if (ProviderName == TEXT("Codex"))
		{
			Cmd = TEXT("npm"); Args = TEXT("install -g @openai/codex");
		}
		else if (ProviderName == TEXT("Copilot"))
		{
			Cmd = TEXT("npm"); Args = TEXT("install -g @github/copilot");
#if PLATFORM_WINDOWS
			FollowUpCmd = TEXT("pip"); FollowUpArgs = TEXT("install --upgrade github-copilot-sdk");
#else
			FollowUpCmd = TEXT("pip3"); FollowUpArgs = TEXT("install --upgrade github-copilot-sdk");
#endif
		}
		else if (ProviderName == TEXT("Gemini Agent"))
		{
			Cmd = TEXT("npm"); Args = TEXT("install -g @google/gemini-cli");
		}
		if (!Cmd.IsEmpty())
			W->InstallCliAsync(ProviderName, Cmd, Args, FollowUpCmd, FollowUpArgs);
	}
}

void UUECPSettingsBridge::CloseSettings()
{
	if (auto W = OwnerWidget.Pin())
	{
		if (W->MainSwitcher.IsValid())
			W->MainSwitcher->SetActiveWidgetIndex(0);
	}
}

void UUECPSettingsBridge::ShowNotification(const FString& Message)
{
	if (auto W = OwnerWidget.Pin())
	{
		if (W->AppBridgeObject)
		{
			W->AppBridgeObject->PushToast(Message, TEXT("info"));
			return;
		}
	}
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 2.5f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void UUECPSettingsBridge::MarkSettingsTourSeen()
{
	GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("SettingsTourSeen"), true,
		FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

void UUECPSettingsBridge::ResetTutorials()
{
	GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("SettingsTourSeen"), false,
		FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());

	if (auto W = OwnerWidget.Pin())
		if (W->AppBridgeObject)
			W->AppBridgeObject->ExecJs(TEXT("if(typeof tourResetAll==='function')tourResetAll()"));
}

void UUECPSettingsBridge::CopyToClipboard(const FString& Text)
{
	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

void UUECPSettingsBridge::GetClipboardText()
{
	FString ClipText;
	FPlatformApplicationMisc::ClipboardPaste(ClipText);
	ClipText.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	ClipText.ReplaceInline(TEXT("'"), TEXT("\\'"));
	ClipText.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
	ClipText.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	ClipText.ReplaceInline(TEXT("\r"), TEXT("\\n"));
	if (auto Browser = BrowserRef.Pin())
		Browser->ExecuteJavascript(FString::Printf(TEXT("if(typeof onClipboardText==='function')onClipboardText('%s')"), *ClipText));
}

static FCriticalSection GGitOpLock;
static bool GGitOpInFlight = false;
struct FGitOpInFlightToken { ~FGitOpInFlightToken() { FScopeLock L(&GGitOpLock); GGitOpInFlight = false; } };

void UUECPSettingsBridge::GitAction(const FString& Action, const FString& Param)
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;

	auto ExecJs = [&Browser](const FString& Js) {
		Browser->ExecuteJavascript(Js);
	};

	auto EscJs = [](const FString& In) -> FString {
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\r"), TEXT(""));
		return Out;
	};

	TWeakPtr<SWebBrowser> WeakBrowser = BrowserRef;
	auto ExecJsAsync = [WeakBrowser](FString Js)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakBrowser, Js = MoveTemp(Js)]()
		{
			if (auto B = WeakBrowser.Pin()) B->ExecuteJavascript(Js);
		});
	};

	const FString ProjectDir = FPaths::ProjectDir();

	TWeakObjectPtr<UUECPSettingsBridge> WeakSelf = this;

	TSharedPtr<FGitOpInFlightToken> GitOpToken;
	{
		static const TSet<FString> MutatingGitOps = {
			TEXT("push"), TEXT("pull"), TEXT("setRemote"), TEXT("initRepo"),
		};
		if (MutatingGitOps.Contains(Action))
		{
			FScopeLock L(&GGitOpLock);
			if (GGitOpInFlight)
			{
				ExecJs(TEXT("gitNotify('A git operation is already in progress — wait for it to finish.','warn')"));
				return;
			}
			GGitOpInFlight = true;
			GitOpToken = MakeShared<FGitOpInFlightToken>();
		}
	}

	if (Action == TEXT("checkAccess"))
	{
		auto& _sm = FEditorProfileSync::Get();
		bool bHasAccess = (_sm.GetEditorStateHash() & 0x5C0E) != 0 && _sm.GetEditorStateHash() != 0;
		ExecJs(FString::Printf(TEXT("if(typeof onScAccess==='function')onScAccess(%s)"),
			bHasAccess ? TEXT("true") : TEXT("false")));
		return;
	}
	else if (Action == TEXT("refreshStatus"))
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, ProjectDir]()
		{
			FString GitBin = GitTools::FindGitBinary();
			bool bGitFound = !GitBin.IsEmpty();
			bool bIsRepo = bGitFound && GitTools::IsGitRepository(ProjectDir);

			if (!bIsRepo)
			{
				ExecJsAsync(FString::Printf(
					TEXT("if(typeof onGitStatus==='function')onGitStatus({git_found:%s,is_repo:false})"),
					bGitFound ? TEXT("true") : TEXT("false")));
				return;
			}

			FString FetchOut, FetchErr; int32 FetchRet;
			GitTools::RunGitCommand(TEXT("fetch --prune"), ProjectDir, FetchOut, FetchErr, FetchRet);

			FString BranchOut, BranchErr, StatusOut, StatusErr, RemoteOut, RemoteErr;
			int32 BranchRet, StatusRet, RemoteRet;
			GitTools::RunGitCommand(TEXT("rev-parse --abbrev-ref HEAD"), ProjectDir, BranchOut, BranchErr, BranchRet);
			if (BranchRet != 0) BranchOut = TEXT("(no commits)");
			GitTools::RunGitCommand(TEXT("status --porcelain"), ProjectDir, StatusOut, StatusErr, StatusRet);
			GitTools::RunGitCommand(TEXT("remote get-url origin"), ProjectDir, RemoteOut, RemoteErr, RemoteRet);

			int32 Modified = 0, Untracked = 0;
			TArray<FString> Lines;
			StatusOut.ParseIntoArrayLines(Lines);
			for (const FString& L : Lines)
			{
				if (L.Contains(TEXT("?"))) Untracked++;
				else if (L.Len() >= 2) Modified++;
			}

			int32 Ahead = 0, Behind = 0;
			FString AheadOut, AheadErr, BehindOut, BehindErr; int32 AheadRet, BehindRet;
			GitTools::RunGitCommand(TEXT("rev-list @{u}..HEAD --count"), ProjectDir, AheadOut, AheadErr, AheadRet);
			if (AheadRet == 0) Ahead = FCString::Atoi(*AheadOut.TrimStartAndEnd());
			GitTools::RunGitCommand(TEXT("rev-list HEAD..@{u} --count"), ProjectDir, BehindOut, BehindErr, BehindRet);
			if (BehindRet == 0) Behind = FCString::Atoi(*BehindOut.TrimStartAndEnd());

			FString IncomingFilesJs = TEXT("[]");
			if (Behind > 0)
			{
				FString DiffOut, DiffErr; int32 DiffRet;
				GitTools::RunGitCommand(TEXT("diff --name-status HEAD..@{u}"), ProjectDir, DiffOut, DiffErr, DiffRet);
				if (DiffRet == 0 && !DiffOut.IsEmpty())
				{
					TArray<FString> DiffLines;
					DiffOut.ParseIntoArrayLines(DiffLines);
					IncomingFilesJs = TEXT("[");
					bool bFirst = true;
					for (const FString& DL : DiffLines)
					{
						if (DL.Len() < 2) continue;
						FString Code = DL.Left(1);
						FString FileName = DL.Mid(1).TrimStartAndEnd();
						int32 TabIdx;
						if (FileName.FindChar(TEXT('\t'), TabIdx))
							FileName = FileName.Mid(TabIdx + 1).TrimStartAndEnd();
						FString St = (Code == TEXT("M")) ? TEXT("modified") :
						             (Code == TEXT("A")) ? TEXT("added") :
						             (Code == TEXT("D")) ? TEXT("deleted") :
						             (Code == TEXT("R")) ? TEXT("renamed") : TEXT("changed");
						if (!bFirst) IncomingFilesJs += TEXT(",");
						bFirst = false;
						IncomingFilesJs += FString::Printf(TEXT("{file:'%s',status:'%s'}"),
							*FileName.Replace(TEXT("'"), TEXT("\\'")), *St);
					}
					IncomingFilesJs += TEXT("]");
				}
			}

			ExecJsAsync(FString::Printf(
				TEXT("if(typeof onGitStatus==='function')onGitStatus({git_found:true,is_repo:true,branch:'%s',remote:'%s',modified:%d,untracked:%d,ahead:%d,behind:%d,incoming:%s})"),
				*BranchOut.TrimStartAndEnd().Replace(TEXT("'"), TEXT("\\'")),
				*RemoteOut.TrimStartAndEnd().Replace(TEXT("'"), TEXT("\\'")),
				Modified, Untracked, Ahead, Behind, *IncomingFilesJs));
		});
	}
	else if (Action == TEXT("initRepo"))
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, EscJs, ProjectDir, WeakSelf, GitOpToken]()
		{
			FString Json, Err;
			GitTools::HandleGitInit(ProjectDir, Json, Err);
			if (Err.IsEmpty())
			{
				ExecJsAsync(TEXT("gitNotify('Git repository initialized!')"));
				AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
				{
					if (UUECPSettingsBridge* Self = WeakSelf.Get())
						Self->GitAction(TEXT("refreshStatus"), TEXT(""));
				});
			}
			else
			{
				ExecJsAsync(FString::Printf(TEXT("gitNotify('Git init failed: %s','err')"), *EscJs(Err)));
			}
		});
	}
	else if (Action == TEXT("setRemote"))
	{
		if (Param.IsEmpty()) return;
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, EscJs, ProjectDir, WeakSelf, Param, GitOpToken]()
		{
			FString StdOut, StdErr; int32 Ret;
			GitTools::RunGitCommand(TEXT("remote remove origin"), ProjectDir, StdOut, StdErr, Ret);
			FString Args = FString::Printf(TEXT("remote add origin \"%s\""), *Param);
			GitTools::RunGitCommand(Args, ProjectDir, StdOut, StdErr, Ret);
			FString Msg = Ret == 0 ? TEXT("Remote set!") : FString::Printf(TEXT("Failed: %s"), *StdErr.Left(200));
			ExecJsAsync(FString::Printf(TEXT("gitNotify(\"%s\")"), *EscJs(Msg)));
			AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
			{
				if (UUECPSettingsBridge* Self = WeakSelf.Get())
					Self->GitAction(TEXT("refreshStatus"), TEXT(""));
			});
		});
	}
	else if (Action == TEXT("push"))
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, EscJs, ProjectDir, WeakSelf, GitOpToken]()
		{
			FString StdOut, StdErr; int32 Ret;
			GitTools::RunGitCommand(TEXT("push -u origin HEAD"), ProjectDir, StdOut, StdErr, Ret);
			FString Msg = Ret == 0 ? TEXT("Pushed successfully!") : FString::Printf(TEXT("Push failed: %s"), *StdErr.Left(100).Replace(TEXT("\n"), TEXT(" ")));
			ExecJsAsync(FString::Printf(TEXT("gitNotify('%s')"), *EscJs(Msg)));
			AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
			{
				if (UUECPSettingsBridge* Self = WeakSelf.Get())
				{
					Self->GitAction(TEXT("refreshStatus"), TEXT(""));
					Self->GitAction(TEXT("refreshLog"), TEXT(""));
				}
			});
		});
	}
	else if (Action == TEXT("pull"))
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, EscJs, ProjectDir, WeakSelf, GitOpToken]()
		{
			FString StdOut, StdErr; int32 Ret;
			GitTools::RunGitCommand(TEXT("pull"), ProjectDir, StdOut, StdErr, Ret);
			FString Msg = Ret == 0 ? TEXT("Pulled successfully!") : FString::Printf(TEXT("Pull failed: %s"), *StdErr.Left(200));
			ExecJsAsync(FString::Printf(TEXT("gitNotify(\"%s\")"), *EscJs(Msg)));
			AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
			{
				if (UUECPSettingsBridge* Self = WeakSelf.Get())
					Self->GitAction(TEXT("refreshStatus"), TEXT(""));
			});
		});
	}
	else if (Action == TEXT("commitAll"))
	{
		GitAction(TEXT("commitWithMessage"), TEXT("Quick commit from plugin"));
	}
	else if (Action == TEXT("refreshLog"))
	{
		FString BranchFilter;
		if (!Param.IsEmpty() && Param != TEXT("--all"))
			BranchFilter = FString::Printf(TEXT(" \"%s\""), *Param);
		else if (Param == TEXT("--all"))
			BranchFilter = TEXT(" --all");

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, ProjectDir, BranchFilter]()
		{
			FString StdOut, StdErr; int32 RetCode;
			FString LogArgs = TEXT("log --format=%H|%an|%ai|%s -n 20") + BranchFilter;
			GitTools::RunGitCommand(LogArgs, ProjectDir, StdOut, StdErr, RetCode);

			TArray<FString> Lines;
			StdOut.ParseIntoArrayLines(Lines);
			FString CommitsJs = TEXT("[");
			for (int32 i = 0; i < Lines.Num(); i++)
			{
				TArray<FString> Parts;
				Lines[i].ParseIntoArray(Parts, TEXT("|"), false);
				if (Parts.Num() >= 4)
				{
					if (i > 0) CommitsJs += TEXT(",");
					CommitsJs += FString::Printf(TEXT("{hash:'%s',author:'%s',date:'%s',message:'%s'}"),
						*Parts[0].Left(12),
						*Parts[1].Replace(TEXT("'"), TEXT("\\'")),
						*Parts[2].Left(10),
						*Parts[3].Replace(TEXT("'"), TEXT("\\'")));
				}
			}
			CommitsJs += TEXT("]");
			ExecJsAsync(FString::Printf(TEXT("if(typeof onGitLog==='function')onGitLog({commits:%s})"), *CommitsJs));
		});
	}
	else if (Action == TEXT("refreshBranches"))
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, ProjectDir]()
		{
			FString StdOut, StdErr; int32 Ret;
			GitTools::RunGitCommand(TEXT("branch -a"), ProjectDir, StdOut, StdErr, Ret);

			TArray<FString> Lines;
			StdOut.ParseIntoArrayLines(Lines);
			FString BranchJs = TEXT("[");
			bool bFirst = true;
			for (int32 i = 0; i < Lines.Num(); i++)
			{
				FString L = Lines[i].TrimStartAndEnd();
				if (L.IsEmpty() || L.Contains(TEXT("->"))) continue;
				bool bCurrent = L.StartsWith(TEXT("*"));
				FString Name = L.Replace(TEXT("* "), TEXT("")).TrimStartAndEnd();
				bool bRemote = Name.StartsWith(TEXT("remotes/"));
				if (bRemote)
				{
					Name = Name.Mid(8);
					FString LocalName = Name.StartsWith(TEXT("origin/")) ? Name.Mid(7) : Name;
					bool bHasLocal = false;
					for (const FString& OtherL : Lines)
					{
						FString OtherTrimmed = OtherL.TrimStartAndEnd().Replace(TEXT("* "), TEXT(""));
						if (!OtherTrimmed.StartsWith(TEXT("remotes/")) && OtherTrimmed == LocalName)
						{ bHasLocal = true; break; }
					}
					if (bHasLocal) continue;
				}
				if (!bFirst) BranchJs += TEXT(",");
				bFirst = false;
				BranchJs += FString::Printf(TEXT("{name:'%s',current:%s,remote:%s}"),
					*Name.Replace(TEXT("'"), TEXT("\\'")),
					bCurrent ? TEXT("true") : TEXT("false"),
					bRemote ? TEXT("true") : TEXT("false"));
			}
			BranchJs += TEXT("]");
			ExecJsAsync(FString::Printf(TEXT("if(typeof onBranches==='function')onBranches(%s)"), *BranchJs));
		});
	}
	else if (Action == TEXT("switchBranch"))
	{
		if (Param.IsEmpty()) return;
		FString StdOut, StdErr; int32 Ret;

		FString CheckoutTarget = Param;
		if (Param.StartsWith(TEXT("origin/")))
		{
			FString LocalName = Param.Mid(7);
			FString BranchOut, BranchErr; int32 BranchRet;
			GitTools::RunGitCommand(FString::Printf(TEXT("rev-parse --verify \"%s\""), *LocalName), FPaths::ProjectDir(), BranchOut, BranchErr, BranchRet);
			if (BranchRet == 0)
			{
				CheckoutTarget = LocalName;
			}
			else
			{
				GitTools::RunGitCommand(FString::Printf(TEXT("checkout -b \"%s\" \"%s\""), *LocalName, *Param), FPaths::ProjectDir(), StdOut, StdErr, Ret);
				if (Ret == 0)
				{
					ExecJs(FString::Printf(TEXT("gitNotify('Switched to %s (tracking %s)')"), *EscJs(LocalName), *EscJs(Param)));
					GitAction(TEXT("refreshStatus"), TEXT(""));
					GitAction(TEXT("refreshBranches"), TEXT(""));
					GitAction(TEXT("refreshLog"), TEXT(""));
					GitAction(TEXT("getChangedFiles"), TEXT(""));
					return;
				}
				CheckoutTarget = LocalName;
			}
		}

		GitTools::RunGitCommand(FString::Printf(TEXT("checkout \"%s\""), *CheckoutTarget), FPaths::ProjectDir(), StdOut, StdErr, Ret);
		if (Ret == 0)
		{
			ExecJs(FString::Printf(TEXT("gitNotify('Switched to %s')"), *EscJs(CheckoutTarget)));
		}
		else if (StdErr.Contains(TEXT("would be overwritten")))
		{
			FString S1, E1; int32 R1;
			GitTools::RunGitCommand(TEXT("stash push -m \"Auto-stash for branch switch\""), FPaths::ProjectDir(), S1, E1, R1);
			if (R1 == 0)
			{
				GitTools::RunGitCommand(FString::Printf(TEXT("checkout \"%s\""), *CheckoutTarget), FPaths::ProjectDir(), StdOut, StdErr, Ret);
				if (Ret == 0)
				{
					FString S2, E2; int32 R2;
					GitTools::RunGitCommand(TEXT("stash pop"), FPaths::ProjectDir(), S2, E2, R2);
					ExecJs(FString::Printf(TEXT("gitNotify('Switched to %s (changes carried over)')"), *EscJs(CheckoutTarget)));
				}
				else
				{
					FString S3, E3; int32 R3;
					GitTools::RunGitCommand(TEXT("stash pop"), FPaths::ProjectDir(), S3, E3, R3);
					ExecJs(FString::Printf(TEXT("gitNotify('Cannot switch to %s','err')"), *EscJs(CheckoutTarget)));
				}
			}
			else
			{
				ExecJs(TEXT("gitNotify('Cannot switch: failed to stash changes','err')"));
			}
		}
		else
		{
			FString ErrClean = StdErr.Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("'"), TEXT("")).Left(150);
			ExecJs(FString::Printf(TEXT("gitNotify('Cannot switch: %s','err')"), *EscJs(ErrClean)));
		}
		GitAction(TEXT("refreshStatus"), TEXT(""));
		GitAction(TEXT("refreshBranches"), TEXT(""));
		GitAction(TEXT("refreshLog"), TEXT(""));
		GitAction(TEXT("getChangedFiles"), TEXT(""));
	}
	else if (Action == TEXT("createBranch"))
	{
		if (Param.IsEmpty()) return;
		FString StdOut, StdErr; int32 Ret;
		GitTools::RunGitCommand(FString::Printf(TEXT("checkout -b \"%s\""), *Param), FPaths::ProjectDir(), StdOut, StdErr, Ret);
		FString Msg = Ret == 0 ? FString::Printf(TEXT("Created and switched to %s"), *Param) : FString::Printf(TEXT("Failed: %s"), *StdErr.Left(100).Replace(TEXT("\n"), TEXT(" ")));
		ExecJs(FString::Printf(TEXT("gitNotify('%s')"), *EscJs(Msg)));
		GitAction(TEXT("refreshStatus"), TEXT(""));
		GitAction(TEXT("refreshBranches"), TEXT(""));
	}
	else if (Action == TEXT("mergeBranch"))
	{
		if (Param.IsEmpty()) return;
		FString StdOut, StdErr; int32 Ret;
		GitTools::RunGitCommand(FString::Printf(TEXT("merge \"%s\""), *Param), FPaths::ProjectDir(), StdOut, StdErr, Ret);
		if (Ret == 0)
		{
			FString PushOut, PushErr; int32 PushRet;
			GitTools::RunGitCommand(TEXT("push -u origin HEAD"), FPaths::ProjectDir(), PushOut, PushErr, PushRet);
			if (PushRet == 0)
			{
				ExecJs(FString::Printf(TEXT("gitNotify('Merged %s and pushed successfully!')"), *EscJs(Param)));
			}
			else
			{
				ExecJs(FString::Printf(TEXT("gitNotify('Merged %s locally. Push failed — push manually.','warn')"), *EscJs(Param)));
			}
		}
		else if (StdErr.Contains(TEXT("CONFLICT")) || StdOut.Contains(TEXT("CONFLICT")))
		{
			ExecJs(TEXT("gitNotify('Merge conflicts detected! Resolve conflicts in your files, then commit.','warn')"));
		}
		else
		{
			FString ErrClean = StdErr.Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("'"), TEXT("")).Left(150);
			ExecJs(FString::Printf(TEXT("gitNotify('Merge failed: %s','err')"), *EscJs(ErrClean)));
		}
		GitAction(TEXT("refreshStatus"), TEXT(""));
		GitAction(TEXT("refreshLog"), TEXT(""));
		GitAction(TEXT("refreshBranches"), TEXT(""));
		GitAction(TEXT("getChangedFiles"), TEXT(""));
	}
	else if (Action == TEXT("openUrl"))
	{
		if (!Param.IsEmpty())
			FPlatformProcess::LaunchURL(*Param, nullptr, nullptr);
	}
	else if (Action == TEXT("savePrefs"))
	{
		FString SettingsPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GitSettings.json"));
		FFileHelper::SaveStringToFile(Param, *SettingsPath);
	}
	else if (Action == TEXT("loadPrefs"))
	{
		FString SettingsPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GitSettings.json"));
		FString Json;
		if (FFileHelper::LoadFileToString(Json, *SettingsPath))
		{
			ExecJs(FString::Printf(TEXT("if(typeof onGitPrefs==='function')onGitPrefs(%s)"), *Json));
		}
	}
	else if (Action == TEXT("getChangedFiles"))
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ExecJsAsync, ProjectDir]()
		{
			FString StdOut, StdErr; int32 Ret;
			GitTools::RunGitCommand(TEXT("status --porcelain"), ProjectDir, StdOut, StdErr, Ret);

			TArray<FString> Lines;
			StdOut.ParseIntoArrayLines(Lines);
			FString FilesJs = TEXT("[");
			bool bFirst = true;
			for (const FString& Line : Lines)
			{
				if (Line.Len() < 3) continue;
				FString Code = Line.Left(2).TrimStartAndEnd();
				FString File = Line.Mid(3).TrimStartAndEnd();
				FString Status = Code.Contains(TEXT("M")) ? TEXT("modified") :
				                 Code.Contains(TEXT("A")) ? TEXT("added") :
				                 Code.Contains(TEXT("D")) ? TEXT("deleted") :
				                 Code.Contains(TEXT("R")) ? TEXT("renamed") :
				                 Code.Contains(TEXT("?")) ? TEXT("untracked") : Code;
				if (!bFirst) FilesJs += TEXT(",");
				bFirst = false;
				FilesJs += FString::Printf(TEXT("{file:'%s',status:'%s',code:'%s'}"),
					*File.Replace(TEXT("'"), TEXT("\\'")), *Status, *Code);
			}
			FilesJs += TEXT("]");
			ExecJsAsync(FString::Printf(TEXT("if(typeof onChangedFiles==='function')onChangedFiles(%s)"), *FilesJs));
		});
	}
	else if (Action == TEXT("commitWithMessage"))
	{
		FString Title = Param.IsEmpty() ? TEXT("Commit from plugin") : Param;
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakSelf, ExecJsAsync, EscJs, ProjectDir, Title]()
		{
			FString StatusOut, StatusErr; int32 StatusRet;
			GitTools::RunGitCommand(TEXT("status --porcelain"), ProjectDir, StatusOut, StatusErr, StatusRet);

			FString Body;
			int32 ModCount = 0, AddCount = 0, DelCount = 0, UntrackedCount = 0;
			TArray<FString> StatusLines;
			StatusOut.ParseIntoArrayLines(StatusLines);
			TArray<FString> FileList;
			for (const FString& L : StatusLines)
			{
				if (L.Len() < 3) continue;
				FString Code = L.Left(2);
				FString BaseName = FPaths::GetCleanFilename(L.Mid(3).TrimStartAndEnd());
				if (Code.Contains(TEXT("M"))) { ModCount++; FileList.Add(TEXT("  M ") + BaseName); }
				else if (Code.Contains(TEXT("A"))) { AddCount++; FileList.Add(TEXT("  A ") + BaseName); }
				else if (Code.Contains(TEXT("D"))) { DelCount++; FileList.Add(TEXT("  D ") + BaseName); }
				else if (Code.Contains(TEXT("?"))) { UntrackedCount++; FileList.Add(TEXT("  + ") + BaseName); }
			}
			if (FileList.Num() > 0)
			{
				Body += TEXT("\n\nFiles:\n");
				for (const FString& F : FileList) Body += F + TEXT("\n");
				Body += FString::Printf(TEXT("\nSummary: %d modified, %d added, %d deleted, %d new"),
					ModCount, AddCount, DelCount, UntrackedCount);
			}

			TArray<FString> NoFiles;
			FString Json, Err;
			GitTools::HandleGitCommit(Title + Body, NoFiles, true, Json, Err);
			FString Msg = Err.IsEmpty() ? TEXT("Committed!") : FString::Printf(TEXT("Commit failed: %s"), *Err.Left(200));
			ExecJsAsync(FString::Printf(TEXT("gitNotify(\"%s\")"), *EscJs(Msg)));

			AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
			{
				if (UUECPSettingsBridge* Self = WeakSelf.Get())
				{
					Self->GitAction(TEXT("refreshStatus"), TEXT(""));
					Self->GitAction(TEXT("refreshLog"), TEXT(""));
					Self->GitAction(TEXT("getChangedFiles"), TEXT(""));
				}
			});
		});
	}
}

static FString P4JsonStr(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	return Out;
}

void UUECPSettingsBridge::PerforceAction(const FString& Action, const FString& Param)
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;
	TWeakPtr<SWebBrowser> WeakBrowser = BrowserRef;

	if (Action == TEXT("checkAccess"))
	{
		auto& _sm = FEditorProfileSync::Get();
		bool bHasAccess = (_sm.GetEditorStateHash() & 0x5C0E) != 0 && _sm.GetEditorStateHash() != 0;
		Browser->ExecuteJavascript(FString::Printf(TEXT("if(typeof onScAccess==='function')onScAccess(%s)"),
			bHasAccess ? TEXT("true") : TEXT("false")));
		return;
	}
	if (Action == TEXT("openSettings"))
	{
		ISourceControlModule::Get().ShowLoginDialog(FSourceControlLoginClosed(), ELoginWindowMode::Modeless);
		return;
	}
	if (Action == TEXT("openUrl"))
	{
		if (!Param.IsEmpty()) FPlatformProcess::LaunchURL(*Param, nullptr, nullptr);
		return;
	}
	if (Action == TEXT("refreshStatus"))
	{
		FString Json, Err;
		PerforceTools::HandleP4ConnectionInfo(Json, Err);
		if (Err.IsEmpty())
			Browser->ExecuteJavascript(TEXT("if(typeof onP4Status==='function')onP4Status(") + Json + TEXT(")"));
		else
		{
			FString ErrEsc = Err; ErrEsc.ReplaceInline(TEXT("\""), TEXT("\\\""));
			Browser->ExecuteJavascript(FString::Printf(TEXT("p4Notify(\"%s\",'err')"), *ErrEsc));
		}
		return;
	}

	ISourceControlModule& SCModule = ISourceControlModule::Get();
	if (!SCModule.IsEnabled()) return;
	ISourceControlProvider& Provider = SCModule.GetProvider();

	if (Action == TEXT("refreshOpenedFiles"))
	{
		TSharedRef<FUpdatePendingChangelistsStatus, ESPMode::ThreadSafe> Op =
			ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
		Op->SetUpdateAllChangelists(true);
		Op->SetUpdateFilesStates(true);

		Provider.Execute(Op, EConcurrency::Asynchronous,
			FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type)
			{
				ISourceControlProvider& Prov = ISourceControlModule::Get().GetProvider();

				TArray<FSourceControlChangelistRef> EmptyCLs;
				TArray<FSourceControlChangelistStateRef> CLStates;
				Prov.GetState(EmptyCLs, CLStates, EStateCacheUsage::Use);

				TArray<TSharedPtr<FJsonValue>> FilesArr;
				int32 EC = 0, AC = 0, DC = 0;

				for (const FSourceControlChangelistStateRef& CLState : CLStates)
				{
					for (const FSourceControlStateRef& FileState : CLState->GetFilesStates())
					{
						if (!FileState->IsCheckedOut() && !FileState->IsAdded() && !FileState->IsDeleted()) continue;
						auto FO = MakeShared<FJsonObject>();
						FString Rel = FileState->GetFilename();
						FPaths::MakePathRelativeTo(Rel, *FPaths::ProjectDir());
						FO->SetStringField(TEXT("file"), Rel);
						if      (FileState->IsAdded())   { FO->SetStringField(TEXT("status"), TEXT("add"));    AC++; }
						else if (FileState->IsDeleted()) { FO->SetStringField(TEXT("status"), TEXT("delete")); DC++; }
						else                             { FO->SetStringField(TEXT("status"), TEXT("edit"));   EC++; }
						FilesArr.Add(MakeShared<FJsonValueObject>(FO));
					}
				}

				auto Result = MakeShared<FJsonObject>();
				Result->SetArrayField(TEXT("files"), FilesArr);
				Result->SetNumberField(TEXT("edit_count"), EC);
				Result->SetNumberField(TEXT("add_count"), AC);
				Result->SetNumberField(TEXT("delete_count"), DC);
				Result->SetBoolField(TEXT("success"), true);
				FString Json = P4JsonStr(Result);

				if (auto B = WeakBrowser.Pin())
					B->ExecuteJavascript(TEXT("if(typeof onP4OpenedFiles==='function')onP4OpenedFiles(") + Json + TEXT(")"));
			}));
	}
	else if (Action == TEXT("refreshPendingCLs"))
	{
		TSharedRef<FGetPendingChangelists, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FGetPendingChangelists>();

		Provider.Execute(Op, EConcurrency::Asynchronous,
			FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type)
			{
				ISourceControlProvider& Prov = ISourceControlModule::Get().GetProvider();
				TArray<FSourceControlChangelistRef> EmptyCLs;
				TArray<FSourceControlChangelistStateRef> CLStates;
				Prov.GetState(EmptyCLs, CLStates, EStateCacheUsage::Use);

				TArray<TSharedPtr<FJsonValue>> CLArr;
				for (const FSourceControlChangelistStateRef& CLState : CLStates)
				{
					auto CLObj = MakeShared<FJsonObject>();
					CLObj->SetStringField(TEXT("id"),          CLState->GetChangelist()->GetIdentifier());
					CLObj->SetStringField(TEXT("description"), CLState->GetDescriptionText().ToString());
					CLObj->SetNumberField(TEXT("file_count"),    CLState->GetFilesStatesNum());
					CLObj->SetNumberField(TEXT("shelved_count"), CLState->GetShelvedFilesStatesNum());
					CLObj->SetBoolField(TEXT("is_default"), CLState->GetChangelist()->IsDefault());
					CLArr.Add(MakeShared<FJsonValueObject>(CLObj));
				}
				auto Result = MakeShared<FJsonObject>();
				Result->SetArrayField(TEXT("changelists"), CLArr);
				Result->SetBoolField(TEXT("success"), true);
				FString Json = P4JsonStr(Result);

				if (auto B = WeakBrowser.Pin())
					B->ExecuteJavascript(TEXT("if(typeof onP4PendingCLs==='function')onP4PendingCLs(") + Json + TEXT(")"));
			}));
	}
	else if (Action == TEXT("refreshHistory"))
	{
		TSharedRef<FGetSubmittedChangelists, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FGetSubmittedChangelists>();
		Op->SetPaginationLimit(20);
		Op->SetOwnedFilter(true);

		Provider.Execute(Op, EConcurrency::Asynchronous,
			FSourceControlOperationComplete::CreateLambda([WeakBrowser, Op](const FSourceControlOperationRef&, ECommandResult::Type Res)
			{
				TArray<TSharedPtr<FJsonValue>> CLArr;
				if (Res == ECommandResult::Succeeded)
				{
					for (const FSourceControlChangelistRef& CL : Op->GetSubmittedChangelists())
					{
						auto CLObj = MakeShared<FJsonObject>();
						CLObj->SetStringField(TEXT("id"), CL->GetIdentifier());
						CLArr.Add(MakeShared<FJsonValueObject>(CLObj));
					}
				}
				auto Result = MakeShared<FJsonObject>();
				Result->SetArrayField(TEXT("changelists"), CLArr);
				Result->SetBoolField(TEXT("success"), Res == ECommandResult::Succeeded);
				FString Json = P4JsonStr(Result);

				if (auto B = WeakBrowser.Pin())
					B->ExecuteJavascript(TEXT("if(typeof onP4History==='function')onP4History(") + Json + TEXT(")"));
			}));
	}
	else if (Action == TEXT("submit"))
	{
		if (Param.IsEmpty()) { Browser->ExecuteJavascript(TEXT("p4Notify('Enter a changelist description','warn')")); return; }
		Browser->ExecuteJavascript(TEXT("p4Notify('Submitting\u2026','warn')"));

		TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOp = ISourceControlOperation::Create<FCheckIn>();
		CheckInOp->SetDescription(FText::FromString(Param));
		TArray<FString> NoFiles;

		Provider.Execute(CheckInOp, NoFiles, EConcurrency::Asynchronous,
			FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type Res)
			{
				bool bOk = Res == ECommandResult::Succeeded;
				if (auto B = WeakBrowser.Pin())
				{
					B->ExecuteJavascript(bOk ? TEXT("p4Notify('Submitted successfully!')") : TEXT("p4Notify('Submit failed','err')"));
					if (bOk) B->ExecuteJavascript(TEXT("refreshP4All()"));
				}
			}));
	}
	else if (Action == TEXT("sync") || Action == TEXT("syncForce"))
	{
		Browser->ExecuteJavascript(TEXT("p4Notify('Syncing\u2026','warn')"));

		TSharedRef<FSync, ESPMode::ThreadSafe> SyncOp = ISourceControlOperation::Create<FSync>();
		SyncOp->SetHeadRevisionFlag(true);
		if (Action == TEXT("syncForce")) SyncOp->SetForce(true);
		TArray<FString> ContentFiles = { FPaths::ProjectContentDir() };

		Provider.Execute(SyncOp, ContentFiles, EConcurrency::Asynchronous,
			FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type Res)
			{
				bool bOk = Res == ECommandResult::Succeeded;
				if (auto B = WeakBrowser.Pin())
				{
					B->ExecuteJavascript(bOk ? TEXT("p4Notify('Synced to latest!')") : TEXT("p4Notify('Sync failed','err')"));
					if (bOk) B->ExecuteJavascript(TEXT("bridgeP4('refreshOpenedFiles')"));
				}
			}));
	}
	else if (Action == TEXT("shelve"))
	{
		Browser->ExecuteJavascript(TEXT("p4Notify('Shelving\u2026','warn')"));

		TSharedRef<FShelve, ESPMode::ThreadSafe> ShelveOp = ISourceControlOperation::Create<FShelve>();
		ShelveOp->SetDescription(FText::FromString(Param.IsEmpty() ? TEXT("Shelved from plugin") : Param));

		Provider.Execute(ShelveOp, EConcurrency::Asynchronous,
			FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type Res)
			{
				bool bOk = Res == ECommandResult::Succeeded;
				if (auto B = WeakBrowser.Pin())
				{
					B->ExecuteJavascript(bOk ? TEXT("p4Notify('Shelved successfully!')") : TEXT("p4Notify('Shelve failed','err')"));
					if (bOk) B->ExecuteJavascript(TEXT("bridgeP4('refreshPendingCLs')"));
				}
			}));
	}
	else if (Action == TEXT("unshelve"))
	{
		Browser->ExecuteJavascript(TEXT("p4Notify('Unshelving\u2026','warn')"));

		TSharedRef<FUnshelve, ESPMode::ThreadSafe> UnshelveOp = ISourceControlOperation::Create<FUnshelve>();
		TArray<FString> NoFiles;

		Provider.Execute(UnshelveOp, NoFiles, EConcurrency::Asynchronous,
			FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type Res)
			{
				bool bOk = Res == ECommandResult::Succeeded;
				if (auto B = WeakBrowser.Pin())
				{
					B->ExecuteJavascript(bOk ? TEXT("p4Notify('Unshelved successfully!')") : TEXT("p4Notify('Unshelve failed','err')"));
					if (bOk) B->ExecuteJavascript(TEXT("bridgeP4('refreshOpenedFiles');bridgeP4('refreshPendingCLs')"));
				}
			}));
	}
	else if (Action == TEXT("revert") || Action == TEXT("revertUnchanged"))
	{
		Browser->ExecuteJavascript(TEXT("p4Notify('Reverting\u2026','warn')"));

		TArray<FString> NoFiles;
		if (Action == TEXT("revertUnchanged"))
		{
			TSharedRef<FRevertUnchanged, ESPMode::ThreadSafe> RevertOp = ISourceControlOperation::Create<FRevertUnchanged>();
			Provider.Execute(RevertOp, NoFiles, EConcurrency::Asynchronous,
				FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type Res)
				{
					bool bOk = Res == ECommandResult::Succeeded;
					if (auto B = WeakBrowser.Pin())
					{
						B->ExecuteJavascript(bOk ? TEXT("p4Notify('Reverted unchanged files!')") : TEXT("p4Notify('Revert failed','err')"));
						if (bOk) B->ExecuteJavascript(TEXT("bridgeP4('refreshOpenedFiles')"));
					}
				}));
		}
		else
		{
			TSharedRef<FRevert, ESPMode::ThreadSafe> RevertOp = ISourceControlOperation::Create<FRevert>();
			Provider.Execute(RevertOp, NoFiles, EConcurrency::Asynchronous,
				FSourceControlOperationComplete::CreateLambda([WeakBrowser](const FSourceControlOperationRef&, ECommandResult::Type Res)
				{
					bool bOk = Res == ECommandResult::Succeeded;
					if (auto B = WeakBrowser.Pin())
					{
						B->ExecuteJavascript(bOk ? TEXT("p4Notify('Reverted!')") : TEXT("p4Notify('Revert failed','err')"));
						if (bOk) B->ExecuteJavascript(TEXT("bridgeP4('refreshOpenedFiles')"));
					}
				}));
		}
	}
}

static FString ACPEscJs(const FString& In)
{
	return In.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
}

void UUECPSettingsBridge::RequestMcpStatus()
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;

	IUECPMcpInfoService& Mcp = IUECPCoreModule::Get().GetMcpInfoService();
	const int32   Port        = Mcp.GetHttpPort();
	const int32   BrokerPort  = Mcp.GetBrokerPort();
	const bool    bIsBroker   = Mcp.IsBroker();
	const FString Token       = Mcp.GetSessionToken();
	const bool    bLan        = Mcp.IsLanAccessEnabled();
	const FString ProjectName = FApp::GetProjectName();

	const FString HostHint  = bLan ? TEXT("<your-machine-hostname-or-ip>") : TEXT("localhost");
	const FString McpUrl    = FString::Printf(TEXT("http://%s:%d/mcp"),        *HostHint, Port);
	const FString HealthUrl = FString::Printf(TEXT("http://%s:%d/mcp/health"), *HostHint, Port);

	const int32 BrokerPortToUse = bIsBroker ? BrokerPort : Mcp.GetConfiguredBrokerPort();
	const bool  bUseBroker = BrokerPortToUse > 0;
	const FString PreferredUrl = bUseBroker
		? FString::Printf(TEXT("http://%s:%d/uecp/%s/mcp"), *HostHint, BrokerPortToUse, *ProjectName)
		: McpUrl;

	const FString EntryName = FString::Printf(TEXT("uecp-%s"), *ProjectName.ToLower());
	const FString ConfigSnippet = FString::Printf(
		TEXT("{\n  \"mcpServers\": {\n    \"%s\": {\n      \"type\": \"http\",\n      \"url\": \"%s\",\n      \"headers\": {\n        \"Authorization\": \"Bearer %s\"\n      }\n    }\n  }\n}"),
		*EntryName, *PreferredUrl, *Token);

	const FString CodexTomlSnippet = FString::Printf(
		TEXT("[mcp_servers.%s]\nurl = \"%s\"\nhttp_headers = { Authorization = \"Bearer %s\" }\n"),
		*EntryName, *PreferredUrl, *Token);

	const FString GeminiJsonSnippet = FString::Printf(
		TEXT("{\n  \"mcpServers\": {\n    \"%s\": {\n      \"httpUrl\": \"%s\",\n      \"headers\": {\n        \"Authorization\": \"Bearer %s\"\n      }\n    }\n  }\n}"),
		*EntryName, *PreferredUrl, *Token);

	const FString OpencodeJsonSnippet = FString::Printf(
		TEXT("{\n  \"$schema\": \"https://opencode.ai/config.json\",\n  \"mcp\": {\n    \"%s\": {\n      \"type\": \"remote\",\n      \"url\": \"%s\",\n      \"headers\": {\n        \"Authorization\": \"Bearer %s\"\n      },\n      \"enabled\": true\n    }\n  }\n}"),
		*EntryName, *PreferredUrl, *Token);

	TArray<TSharedPtr<FJsonValue>> CliCmds;
	const auto AddCli = [&CliCmds](const TCHAR* Name, const FString& Cmd)
	{
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("client"),  Name);
		E->SetStringField(TEXT("command"), Cmd);
		CliCmds.Add(MakeShared<FJsonValueObject>(E));
	};
	AddCli(TEXT("Claude Code"), FString::Printf(
		TEXT("claude mcp add --scope user --header \"Authorization: Bearer %s\" --transport http %s %s"),
		*Token, *EntryName, *PreferredUrl));

	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("port"),                Port);
	O->SetNumberField(TEXT("brokerPort"),          BrokerPortToUse);
	O->SetBoolField  (TEXT("isBroker"),            bIsBroker);
	O->SetStringField(TEXT("projectName"),         ProjectName);
	O->SetStringField(TEXT("token"),               Token);
	O->SetBoolField  (TEXT("lan"),                 bLan);
	O->SetStringField(TEXT("mcpUrl"),              PreferredUrl);
	O->SetStringField(TEXT("directUrl"),           McpUrl);
	O->SetStringField(TEXT("healthUrl"),           HealthUrl);
	O->SetStringField(TEXT("claudeConfigSnippet"),  ConfigSnippet);
	O->SetStringField(TEXT("codexTomlSnippet"),     CodexTomlSnippet);
	O->SetStringField(TEXT("geminiJsonSnippet"),    GeminiJsonSnippet);
	O->SetStringField(TEXT("opencodeJsonSnippet"),  OpencodeJsonSnippet);
	O->SetArrayField (TEXT("cliCommands"),          CliCmds);

	FString Body;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(O, W);

	Browser->ExecuteJavascript(FString::Printf(
		TEXT("if(typeof onMcpStatus==='function')onMcpStatus(%s)"), *Body));
}

void UUECPSettingsBridge::OnMcpBrokerStateChanged()
{
	RequestMcpStatus();
}

void UUECPSettingsBridge::OnExtensionStateChangedForUserMcp(FName ExtensionId)
{
	const FString UserId = IUECPUserMcpService::TryGetUserMcpId(ExtensionId);
	if (UserId.IsEmpty()) return;
	PushUserMcpServers();
}

static FString MaskMcpHeaderSecret(const FString& V)
{
	if (V.IsEmpty()) return FString();
	FString Prefix;
	for (const TCHAR* Scheme : { TEXT("Bearer "), TEXT("Basic "), TEXT("Token ") })
	{
		if (V.StartsWith(Scheme)) { Prefix = Scheme; break; }
	}
	const FString Secret = V.Mid(Prefix.Len());
	const FString Tail   = Secret.Len() > 8 ? Secret.Right(4) : FString();
	return Prefix + TEXT("••••") + Tail;
}

void UUECPSettingsBridge::PushUserMcpServers()
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid() || !IUECPCoreModule::IsAvailable()) return;

	IUECPUserMcpService& UserMcp = IUECPCoreModule::Get().GetUserMcpService();
	IUECPExtensionService& Ext   = IUECPCoreModule::Get().GetExtensionService();

	const TArray<FUECPUserMcpServerConfig> Servers = UserMcp.ListServers();

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FUECPUserMcpServerConfig& C : Servers)
	{
		const FName ExtId = IUECPUserMcpService::MakeExtensionId(C.Id);
		const EUECPExtensionState State = Ext.GetExtensionState(ExtId);
		const TSharedPtr<FUECPMcpClient> Client = Ext.GetMcpClient(ExtId);
		const int32 ToolCount = Client.IsValid() && Client->IsConnected() ? Client->GetTools().Num() : 0;
		const FString LastError = Ext.GetMcpLastError(ExtId);

		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),            C.Id);
		O->SetStringField(TEXT("displayName"),   C.DisplayName);
		O->SetStringField(TEXT("description"),   C.Description);
		O->SetStringField(TEXT("command"),       C.Command);
		O->SetStringField(TEXT("cwd"),           C.Cwd);
		O->SetStringField(TEXT("url"),           C.Url);
		O->SetStringField(TEXT("transport"),     C.Url.IsEmpty() ? TEXT("stdio") : TEXT("http"));
		O->SetBoolField  (TEXT("enabled"),       C.bEnabled);
		O->SetBoolField  (TEXT("alwaysConfirm"), C.bAlwaysConfirm);
		O->SetBoolField  (TEXT("useOAuth"),      C.bUseOAuth);
		O->SetBoolField  (TEXT("hasOAuthGrant"), UserMcp.HasOAuthGrant(C.Id));

		TArray<TSharedPtr<FJsonValue>> ArgVals;
		for (const FString& A : C.Args) ArgVals.Add(MakeShared<FJsonValueString>(A));
		O->SetArrayField(TEXT("args"), ArgVals);

		const TSharedRef<FJsonObject> EnvObj = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& E : C.Env) EnvObj->SetStringField(E.Key, E.Value);
		O->SetObjectField(TEXT("env"), EnvObj);

		const TSharedRef<FJsonObject> HeadersObj = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& H : C.Headers) HeadersObj->SetStringField(H.Key, MaskMcpHeaderSecret(H.Value));
		O->SetObjectField(TEXT("headers"), HeadersObj);

		O->SetStringField(TEXT("state"),     LexToString(State));
		O->SetNumberField(TEXT("toolCount"), ToolCount);
		O->SetStringField(TEXT("lastError"), LastError);

		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("servers"), Arr);

	FString Body;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root, W);

	Browser->ExecuteJavascript(FString::Printf(
		TEXT("if(typeof onUserMcpServers==='function')onUserMcpServers(%s)"), *Body));
}

void UUECPSettingsBridge::RequestUserMcpServers()
{
	PushUserMcpServers();
}

void UUECPSettingsBridge::AddUserMcpServersFromBlock(const FString& JsonBlock)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	TMap<FString, FString> Errors;
	const TArray<FString> Added = IUECPCoreModule::Get().GetUserMcpService()
		.AddFromMcpServersBlock(JsonBlock, Errors);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> AddedArr;
	for (const FString& Id : Added) AddedArr.Add(MakeShared<FJsonValueString>(Id));
	Root->SetArrayField(TEXT("added"), AddedArr);

	const TSharedRef<FJsonObject> ErrObj = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& E : Errors) ErrObj->SetStringField(E.Key, E.Value);
	Root->SetObjectField(TEXT("errors"), ErrObj);

	FString Body;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root, W);

	if (auto Browser = BrowserRef.Pin())
	{
		Browser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof onUserMcpAddResult==='function')onUserMcpAddResult(%s)"), *Body));
	}

	PushUserMcpServers();
}

void UUECPSettingsBridge::UpdateUserMcpServer(const FString& Json)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("UpdateUserMcpServer: invalid JSON"));
		return;
	}

	FUECPUserMcpServerConfig Cfg;
	Obj->TryGetStringField(TEXT("id"),            Cfg.Id);
	Obj->TryGetStringField(TEXT("displayName"),   Cfg.DisplayName);
	Obj->TryGetStringField(TEXT("description"),   Cfg.Description);
	Obj->TryGetStringField(TEXT("command"),       Cfg.Command);
	Obj->TryGetStringField(TEXT("cwd"),           Cfg.Cwd);
	Obj->TryGetStringField(TEXT("url"),           Cfg.Url);
	Obj->TryGetBoolField  (TEXT("enabled"),       Cfg.bEnabled);
	Obj->TryGetBoolField  (TEXT("alwaysConfirm"), Cfg.bAlwaysConfirm);
	Obj->TryGetBoolField  (TEXT("useOAuth"),      Cfg.bUseOAuth);

	const TArray<TSharedPtr<FJsonValue>>* ArgsArr = nullptr;
	if (Obj->TryGetArrayField(TEXT("args"), ArgsArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *ArgsArr)
		{
			if (V.IsValid() && V->Type == EJson::String) Cfg.Args.Add(V->AsString());
		}
	}

	const TSharedPtr<FJsonObject>* EnvObj = nullptr;
	if (Obj->TryGetObjectField(TEXT("env"), EnvObj) && EnvObj && EnvObj->IsValid())
	{
		for (const auto& E : (*EnvObj)->Values)
		{
			const FString K(*E.Key);
			const FString V = E.Value.IsValid() && E.Value->Type == EJson::String ? E.Value->AsString() : FString();
			Cfg.Env.Add(TPair<FString, FString>(K, V));
		}
	}

	const FUECPUserMcpServerConfig* Existing = IUECPCoreModule::Get().GetUserMcpService().FindServer(Cfg.Id);

	const TSharedPtr<FJsonObject>* HeadersObj = nullptr;
	if (Obj->TryGetObjectField(TEXT("headers"), HeadersObj) && HeadersObj && HeadersObj->IsValid())
	{
		for (const auto& H : (*HeadersObj)->Values)
		{
			const FString K(*H.Key);
			FString V = H.Value.IsValid() && H.Value->Type == EJson::String ? H.Value->AsString() : FString();
			if (Existing && V.Contains(TEXT("•")))
			{
				for (const TPair<FString, FString>& EH : Existing->Headers)
				{
					if (EH.Key == K) { V = EH.Value; break; }
				}
			}
			Cfg.Headers.Add(TPair<FString, FString>(K, V));
		}
	}

	if (Existing)
	{
		if (Cfg.OAuthIssuer.IsEmpty())   Cfg.OAuthIssuer   = Existing->OAuthIssuer;
		if (Cfg.OAuthClientId.IsEmpty()) Cfg.OAuthClientId = Existing->OAuthClientId;
	}

	FString Err;
	if (!IUECPCoreModule::Get().GetUserMcpService().UpsertServer(Cfg, Err))
	{
		UE_LOG(LogTemp, Warning, TEXT("UpdateUserMcpServer '%s': %s"), *Cfg.Id, *Err);
	}

	PushUserMcpServers();
}

void UUECPSettingsBridge::SetUserMcpServerEnabled(const FString& Id, bool bEnabled)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	FString Err;
	IUECPCoreModule::Get().GetUserMcpService().SetServerEnabled(Id, bEnabled, Err);
	PushUserMcpServers();
}

void UUECPSettingsBridge::RestartUserMcpServer(const FString& Id)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	IUECPCoreModule::Get().GetUserMcpService().RestartServer(Id);
	PushUserMcpServers();
}

void UUECPSettingsBridge::RemoveUserMcpServer(const FString& Id)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	IUECPCoreModule::Get().GetUserMcpService().RemoveServer(Id);
	PushUserMcpServers();
}

namespace
{
	void ExecUserMcpTestResult(const TSharedPtr<SWebBrowser>& Browser, const FString& Id,
		const TCHAR* Status, int32 ToolCount, const FString& Error)
	{
		if (!Browser.IsValid()) return;
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),        Id);
		O->SetStringField(TEXT("status"),    Status);
		O->SetNumberField(TEXT("toolCount"), ToolCount);
		O->SetStringField(TEXT("error"),     Error);
		FString Body;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(O, W);
		Browser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof onUserMcpTestResult==='function')onUserMcpTestResult(%s)"), *Body));
	}
}

void UUECPSettingsBridge::TestUserMcpServer(const FString& Id)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	const FUECPUserMcpServerConfig* Cfg = IUECPCoreModule::Get().GetUserMcpService().FindServer(Id);
	if (!Cfg)
	{
		ExecUserMcpTestResult(BrowserRef.Pin(), Id, TEXT("fail"), 0, TEXT("Server not found."));
		return;
	}

	FUECPMcpServerSpec Spec;
	Spec.Command = Cfg->Command;
	Spec.Args    = Cfg->Args;
	Spec.Env     = Cfg->Env;
	Spec.Cwd     = Cfg->Cwd;
	Spec.Url     = Cfg->Url;
	Spec.Headers = Cfg->Headers;
	Spec.LogTag  = Cfg->Id;

	ExecUserMcpTestResult(BrowserRef.Pin(), Id, TEXT("testing"), 0, FString());

	TSharedPtr<FUECPMcpClient> Client = MakeShared<FUECPMcpClient>();
	TWeakPtr<SWebBrowser> BrowserWeak = BrowserRef;
	Client->StartAsync(Spec,
		[Client, BrowserWeak, Id](bool bOk, FString Error)
		{
			const int32 ToolCount = (bOk && Client->IsConnected()) ? Client->GetTools().Num() : 0;
			ExecUserMcpTestResult(BrowserWeak.Pin(), Id,
				bOk ? TEXT("ok") : TEXT("fail"), ToolCount, Error);
			Client->Stop();
		}, 12.0f);
}

void UUECPSettingsBridge::OAuthSignInUserMcpServer(const FString& Id)
{
	if (!IUECPCoreModule::IsAvailable()) return;

	TWeakPtr<SWebBrowser> BrowserWeak = BrowserRef;
	TWeakObjectPtr<UUECPSettingsBridge> WeakThis(this);
	IUECPCoreModule::Get().GetUserMcpService().SignInOAuth(Id,
		[BrowserWeak, WeakThis, Id](bool bOk, FString Error)
		{
			if (auto Browser = BrowserWeak.Pin())
			{
				const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("id"),    Id);
				O->SetBoolField  (TEXT("ok"),    bOk);
				O->SetStringField(TEXT("error"), Error);
				FString Body;
				const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
				FJsonSerializer::Serialize(O, W);
				Browser->ExecuteJavascript(FString::Printf(
					TEXT("if(typeof onUserMcpOAuthResult==='function')onUserMcpOAuthResult(%s)"), *Body));
			}
			if (WeakThis.IsValid()) WeakThis->PushUserMcpServers();
		});

	PushUserMcpServers();
}

void UUECPSettingsBridge::OAuthSignOutUserMcpServer(const FString& Id)
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPCoreModule::Get().GetUserMcpService().SignOutOAuth(Id);
	PushUserMcpServers();
}

void UUECPSettingsBridge::SetMcpLanAccess(bool bEnabled)
{
	IUECPCoreModule::Get().GetMcpInfoService().SetLanAccessEnabled(bEnabled);

	FNotificationInfo Info(FText::Format(
		NSLOCTEXT("UECP", "MCPLanRestartRequired",
			"MCP LAN access {0}. Restart the editor for the change to take effect."),
		FText::FromString(bEnabled ? TEXT("enabled") : TEXT("disabled"))));
	Info.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	RequestMcpStatus();
}

void UUECPSettingsBridge::RotateMcpToken()
{
	IUECPCoreModule::Get().GetMcpInfoService().RotateSessionToken();

	FNotificationInfo Info(NSLOCTEXT("UECP", "MCPTokenRotated",
		"MCP session token rotated. Copy the new config from Settings → MCP Server "
		"and paste it into Claude Desktop / Cursor / any other MCP client."));
	Info.ExpireDuration = 10.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	RequestMcpStatus();
}

void UUECPSettingsBridge::RevealClaudeDesktopExtension()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
	if (!Plugin.IsValid()) return;

	const FString File = Plugin->GetBaseDir() / TEXT("Resources/ClaudeDesktopExtension/uecp-bridge.mcpb");
	const FString Target = FPaths::FileExists(File) ? File : FPaths::GetPath(File);
	FPlatformProcess::ExploreFolder(*Target);
}

void UUECPSettingsBridge::RequestACPCatalog()
{
	PushACPCatalog();
}

void UUECPSettingsBridge::RefreshACPCatalog()
{
	IUECPCoreModule::Get().GetACPRegistryService().RefreshCatalog( true);
	PushACPCatalog();
}

void UUECPSettingsBridge::InstallACPAgent(const FString& AgentId)
{
	TWeakObjectPtr<UUECPSettingsBridge> WeakSelf(this);
	const FString IdCopy = AgentId;

	FUECPACPInstallProgress OnProgress;
	OnProgress.BindLambda([WeakSelf, IdCopy](const FString& Stage, float Frac)
	{
		if (auto* Self = WeakSelf.Get())
			Self->PushACPInstallProgress(IdCopy, Stage, Frac,  false,  false);
	});

	FUECPACPInstallComplete OnComplete;
	OnComplete.BindLambda([WeakSelf, IdCopy](bool bOk, const FUECPACPInstallMarker& )
	{
		if (auto* Self = WeakSelf.Get())
		{
			Self->PushACPInstallProgress(IdCopy, bOk ? TEXT("Installed") : TEXT("Failed"),
				1.0f,  true, bOk);
			Self->PushACPCatalog();

			if (bOk) IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(IdCopy);
		}
	});

	IUECPCoreModule::Get().GetACPRegistryService().InstallAgent(AgentId, OnProgress, OnComplete);
}

void UUECPSettingsBridge::UninstallACPAgent(const FString& AgentId)
{
	IUECPCoreModule::Get().GetACPRegistryService().UninstallAgent(AgentId);
	if (auto W = OwnerWidget.Pin()) W->AgentAuthStates.Remove(AgentId);
	PushACPCatalog();
}

static bool BridgeRunAndCapture(const FString& Exe, const FString& Args, int32 TimeoutSec,
	FString& OutStdout, int32& OutExitCode)
{
	void* PipeRead = nullptr; void* PipeWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite)) return false;

	FProcHandle Proc = FPlatformProcess::CreateProc(
		*Exe, *Args,  false,  true,  true,
		nullptr, 0, nullptr, PipeWrite, nullptr);
	if (!Proc.IsValid())
	{
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		return false;
	}

	const double Deadline = FPlatformTime::Seconds() + TimeoutSec;
	while (FPlatformProcess::IsProcRunning(Proc))
	{
		OutStdout.Append(FPlatformProcess::ReadPipe(PipeRead));
		if (FPlatformTime::Seconds() > Deadline)
		{
			FPlatformProcess::TerminateProc(Proc,  true);
			break;
		}
		FPlatformProcess::Sleep(0.05f);
	}
	OutStdout.Append(FPlatformProcess::ReadPipe(PipeRead));
	FPlatformProcess::GetProcReturnCode(Proc, &OutExitCode);
	FPlatformProcess::CloseProc(Proc);
	FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	return true;
}

static FString DetectLoginSubcommand(const FString& Cmd)
{
	FString HelpOut; int32 Exit = -1;
	if (!BridgeRunAndCapture(Cmd, TEXT("--help"),  5, HelpOut, Exit))
	{
		return TEXT("login");
	}

	const FString Lower = HelpOut.ToLower();

	struct FCand { const TCHAR* Needle; const TCHAR* Result; };
	static const FCand Candidates[] = {
		{ TEXT("auth login"),   TEXT("auth login") },
		{ TEXT("auth signin"),  TEXT("auth signin") },
		{ TEXT("auth sign-in"), TEXT("auth sign-in") },
		{ TEXT("signin"),       TEXT("signin")     },
		{ TEXT("sign-in"),      TEXT("sign-in")    },
		{ TEXT("login"),        TEXT("login")      },
		{ TEXT("auth"),         TEXT("auth")       },
	};

	for (const FCand& C : Candidates)
	{
		if (Lower.Contains(C.Needle)) return C.Result;
	}
	return TEXT("login");
}

void UUECPSettingsBridge::SpawnAuthSubprocess(const FString& AgentId,
	const FString& Cmd, const FString& ArgsLine)
{
	UE_LOG(LogTemp, Log, TEXT("[ACP] Sign-in spawn for '%s': %s %s"),
		*AgentId, *Cmd, *ArgsLine);

	FProcHandle Proc = FPlatformProcess::CreateProc(
		*Cmd, *ArgsLine,  true,  false,  false,
		nullptr, 0, nullptr, nullptr, nullptr);

	if (!Proc.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ACP] Sign-in: spawn failed for '%s'"), *AgentId);
		return;
	}

	const FString AgentIdCopy = AgentId;
	TWeakObjectPtr<UUECPSettingsBridge> WeakSelf = this;
	Async(EAsyncExecution::Thread, [Proc, AgentIdCopy, WeakSelf]() mutable
	{
		const double Deadline = FPlatformTime::Seconds() + 600.0;
		while (FPlatformProcess::IsProcRunning(Proc) && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.5f);
		}
		int32 Exit = -1;
		FPlatformProcess::GetProcReturnCode(Proc, &Exit);
		FPlatformProcess::CloseProc(Proc);

		AsyncTask(ENamedThreads::GameThread, [AgentIdCopy, WeakSelf, Exit]()
		{
			UE_LOG(LogTemp, Log, TEXT("[ACP] Sign-in subprocess for '%s' exited code=%d"),
				*AgentIdCopy, Exit);
			if (auto Self = WeakSelf.Get())
			{
				if (auto W = Self->OwnerWidget.Pin())
				{
					W->AgentAuthStates.Remove(AgentIdCopy);
					W->AgentDiscoveryAttempted.Remove(AgentIdCopy);
				}
				Self->PushACPCatalog();
				IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(AgentIdCopy);
			}
		});
	});
}

void UUECPSettingsBridge::SignInACPAgent(const FString& AgentId)
{
	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	const FUECPACPInstallMarker Marker = Reg.GetInstallMarker(AgentId);
	if (Marker.AgentId.IsEmpty() || Marker.EntrypointCommand.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ACP] SignInACPAgent: no install marker for '%s'"), *AgentId);
		return;
	}

	const FString Sub = DetectLoginSubcommand(Marker.EntrypointCommand);
	UE_LOG(LogTemp, Log, TEXT("[ACP] Sign-in: --help suggests '%s' for '%s'"),
		*Sub, *AgentId);
	SpawnAuthSubprocess(AgentId, Marker.EntrypointCommand, Sub);
}

void UUECPSettingsBridge::SignInACPAgentCustom(const FString& AgentId, const FString& Subcommand)
{
	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	const FUECPACPInstallMarker Marker = Reg.GetInstallMarker(AgentId);
	if (Marker.AgentId.IsEmpty() || Marker.EntrypointCommand.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ACP] SignInACPAgentCustom: no install marker for '%s'"), *AgentId);
		return;
	}
	FString Trimmed = Subcommand.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) Trimmed = TEXT("login");
	SpawnAuthSubprocess(AgentId, Marker.EntrypointCommand, Trimmed);
}

void UUECPSettingsBridge::OpenTerminalForACPAgent(const FString& AgentId)
{
	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	const FUECPACPInstallMarker Marker = Reg.GetInstallMarker(AgentId);
	if (Marker.AgentId.IsEmpty() || Marker.EntrypointCommand.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ACP] OpenTerminalForACPAgent: no install marker for '%s'"), *AgentId);
		return;
	}

	const FString Cwd = FPaths::GetPath(Marker.EntrypointCommand);

#if PLATFORM_WINDOWS
	const FString Exe  = TEXT("cmd.exe");
	const FString Args = FString::Printf(TEXT("/K cd /D \"%s\""), *Cwd);
#elif PLATFORM_MAC
	const FString Exe  = TEXT("/usr/bin/open");
	const FString Args = FString::Printf(TEXT("-a Terminal \"%s\""), *Cwd);
#else
	const FString Exe  = TEXT("xterm");
	const FString Args = FString::Printf(TEXT("-e \"cd '%s' && exec $SHELL\""), *Cwd);
#endif

	UE_LOG(LogTemp, Log, TEXT("[ACP] Open terminal for '%s' at %s"), *AgentId, *Cwd);
	FProcHandle Proc = FPlatformProcess::CreateProc(
		*Exe, *Args,  true,  false,  false,
		nullptr, 0, nullptr, nullptr, nullptr);
	if (!Proc.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ACP] Open terminal: spawn failed for '%s'"), *AgentId);
	}
	else
	{
		FPlatformProcess::CloseProc(Proc);
	}
}

void UUECPSettingsBridge::RecheckACPAgent(const FString& AgentId)
{
	if (auto W = OwnerWidget.Pin())
	{
		W->AgentAuthStates.Remove(AgentId);
		W->AgentDiscoveryAttempted.Remove(AgentId);
	}
	PushACPCatalog();
	IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(AgentId);
	UE_LOG(LogTemp, Log, TEXT("[ACP] Recheck triggered for '%s'"), *AgentId);
}

void UUECPSettingsBridge::SelectACPAgentForSlot(const FString& AgentId)
{
	const double T0 = FPlatformTime::Seconds();
	FApiKeySlot Slot = FApiKeyManager::Get().GetActiveSlot();
	Slot.Provider = AgentId;
	FApiKeyManager::Get().SetSlot(FApiKeyManager::Get().GetActiveSlotIndex(), Slot);
	const double T1 = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[ACP] active slot provider set to '%s' (SetSlot=%.0f ms)"),
		*AgentId, (T1 - T0) * 1000.0);
	PushACPCatalog();

	if (auto W = OwnerWidget.Pin())
	{
		const FUECPACPAgentConfigSnapshot* Snap = W->CachedAgentConfigs.Find(AgentId);
		const bool bHaveModels = Snap && Snap->Models.Num() > 0;
		const SUECPMainWidget::FACPAgentAuthState* AuthState = W->AgentAuthStates.Find(AgentId);
		const bool bAuthBlocked = AuthState && AuthState->bRequired;
		if (bHaveModels)
		{
			PushACPAgentConfig(AgentId);
		}
		else if (!bAuthBlocked
			&& !W->AgentDiscoveryAttempted.Contains(AgentId)
			&& IUECPCoreModule::Get().GetACPRegistryService().IsInstalled(AgentId))
		{
			W->AgentDiscoveryAttempted.Add(AgentId);
			const FString AgentIdCopy = AgentId;
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([AgentIdCopy](float ) -> bool
				{
					IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(AgentIdCopy);
					return false;
				}),
				 0.25f);
		}
	}
	const double Tend = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("[ACP] SelectACPAgentForSlot('%s') total game-thread time = %.0f ms"),
		*AgentId, (Tend - T0) * 1000.0);
}

void UUECPSettingsBridge::OnRegistryCatalogChanged(bool )
{
	PushACPCatalog();
}

void UUECPSettingsBridge::PushACPCatalog()
{
	const double T0 = FPlatformTime::Seconds();
	if (auto W = OwnerWidget.Pin()) W->RefreshAgentProvidersFromRegistry();
	const double T1 = FPlatformTime::Seconds();

	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	const TArray<FUECPACPAgentEntry>& Agents = Reg.GetAgents();

	TArray<FUECPACPAgentEntry> DiskInstalled = Reg.GetInstalledAgentsFromDisk();

	TMap<FString, FUECPACPAgentEntry> Merged;
	for (const FUECPACPAgentEntry& E : DiskInstalled) Merged.Add(E.Id, E);
	for (const FUECPACPAgentEntry& E : Agents)        Merged.Add(E.Id, E);

	TMap<FString, FString> InstalledVersions;
	for (const FUECPACPAgentEntry& E : DiskInstalled) InstalledVersions.Add(E.Id, E.Version);

	const FString ActiveProvider = FApiKeyManager::Get().GetActiveSlot().Provider;

	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Reserve(Merged.Num());
	for (const TPair<FString, FUECPACPAgentEntry>& Pair : Merged)
	{
		const FUECPACPAgentEntry& E = Pair.Value;
		const bool bInstalled = Reg.IsInstalled(E.Id);
		const bool bInCatalog = Agents.ContainsByPredicate(
			[&](const FUECPACPAgentEntry& A) { return A.Id == E.Id; });
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),          E.Id);
		O->SetStringField(TEXT("name"),        E.Name);
		O->SetStringField(TEXT("version"),     E.Version);
		if (bInstalled)
		{
			const FString* IV = InstalledVersions.Find(E.Id);
			O->SetStringField(TEXT("installedVersion"), IV ? *IV : E.Version);
		}
		O->SetStringField(TEXT("description"), E.Description);
		O->SetStringField(TEXT("icon"),        E.IconUrl);
		O->SetStringField(TEXT("repository"),  E.RepositoryUrl);
		O->SetBoolField  (TEXT("installed"),   bInstalled);
		O->SetBoolField  (TEXT("active"),      bInstalled && E.Id == ActiveProvider);
		O->SetBoolField  (TEXT("hasNpx"),      !E.NpxPackage.IsEmpty());
		O->SetBoolField  (TEXT("hasBinary"),   E.Binaries.Num() > 0);
		O->SetBoolField  (TEXT("inCatalog"),   bInCatalog);

		if (auto WShellAuth = OwnerWidget.Pin())
		{
			const SUECPMainWidget::FACPAgentAuthState* Auth = WShellAuth->AgentAuthStates.Find(E.Id);
			const bool bAuth = Auth && Auth->bRequired;
			O->SetBoolField(TEXT("authRequired"), bAuth);
			if (Auth && !Auth->MethodId.IsEmpty())
				O->SetStringField(TEXT("authMethodId"), Auth->MethodId);

			if (bAuth && bInstalled)
			{
				const FUECPACPInstallMarker IM = Reg.GetInstallMarker(E.Id);
				if (!IM.EntrypointCommand.IsEmpty())
					O->SetStringField(TEXT("entrypointCommand"), IM.EntrypointCommand);
			}
		}

		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	FString Body;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Arr, W); W->Close();

	UE_LOG(LogTemp, Log, TEXT("[ACP] PushACPCatalog: emitting %d agents (catalog=%d, disk=%d, %d chars JSON)"),
		Merged.Num(), Agents.Num(), DiskInstalled.Num(), Body.Len());

	auto Browser = BrowserRef.Pin();
	const double T2 = FPlatformTime::Seconds();
	if (Browser.IsValid())
	{
		Browser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof onACPCatalog==='function')onACPCatalog(%s)"), *Body));
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[ACP] PushACPCatalog: settings browser not bound; skipping JS push but continuing to discovery"));
	}
	const double T3 = FPlatformTime::Seconds();
	const double TotalMs   = (T3 - T0) * 1000.0;
	const double RefreshMs = (T1 - T0) * 1000.0;
	const double BuildMs   = (T2 - T1) * 1000.0;
	const double JsMs      = (T3 - T2) * 1000.0;
	if (TotalMs > 30.0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ACP] PushACPCatalog slow: total=%.0f ms (refresh=%.0f build=%.0f js=%.0f, %d chars)"),
			TotalMs, RefreshMs, BuildMs, JsMs, Body.Len());
	}

	if (!ActiveProvider.IsEmpty() && Reg.IsInstalled(ActiveProvider))
	{
		auto WShell = OwnerWidget.Pin();
		const FUECPACPAgentConfigSnapshot* Snap =
			WShell.IsValid() ? WShell->CachedAgentConfigs.Find(ActiveProvider) : nullptr;
		const SUECPMainWidget::FACPAgentAuthState* AuthState =
			WShell.IsValid() ? WShell->AgentAuthStates.Find(ActiveProvider) : nullptr;
		const bool bAuthBlocked = AuthState && AuthState->bRequired;

		const bool bAlreadyAttempted = WShell.IsValid()
			&& WShell->AgentDiscoveryAttempted.Contains(ActiveProvider);

		if (Snap && Snap->Models.Num() > 0)
		{
			PushACPAgentConfig(ActiveProvider);
		}
		else if (!bAuthBlocked && !bAlreadyAttempted)
		{
			if (WShell.IsValid()) WShell->AgentDiscoveryAttempted.Add(ActiveProvider);
			const FString DeferredId = ActiveProvider;
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([DeferredId](float ) -> bool
				{
					IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(DeferredId);
					return false;
				}),
				 0.25f);
		}
	}
}

static FString GetACPSlotSection(int32 SlotIndex)
{
	return FString::Printf(TEXT("BpGeneratorUltimate.ACP.Slot%d"), SlotIndex);
}

void UUECPSettingsBridge::SetACPAgentConfig(const FString& AgentId, const FString& Key, const FString& Value)
{
	const int32 Slot = FApiKeyManager::Get().GetActiveSlotIndex();
	const FString Section = GetACPSlotSection(Slot);

	FString KeyName;
	if (Key == TEXT("model"))      KeyName = FString::Printf(TEXT("%s.Model"),  *AgentId);
	else if (Key == TEXT("mode"))  KeyName = FString::Printf(TEXT("%s.Mode"),   *AgentId);
	else                           KeyName = FString::Printf(TEXT("%s.Option.%s"), *AgentId, *Key);

	GConfig->SetString(*Section, *KeyName, *Value, FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());

	if (auto W = OwnerWidget.Pin())
	{
		if (FUECPACPAgentConfigSnapshot* Snap = W->CachedAgentConfigs.Find(AgentId))
		{
			if (Key == TEXT("model"))       Snap->CurrentModel = Value;
			else if (Key == TEXT("mode"))   Snap->CurrentMode  = Value;
			else
			{
				for (FUECPACPAgentConfigSection& Sec : Snap->OtherSections)
				{
					if (Sec.Id == Key) { Sec.CurrentValue = Value; break; }
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ACP] slot %d agent '%s' config: %s = %s"), Slot, *AgentId, *Key, *Value);
}

void UUECPSettingsBridge::PushACPAgentConfig(const FString& AgentId)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	const FUECPACPAgentConfigSnapshot* Snap = W->CachedAgentConfigs.Find(AgentId);
	if (!Snap) return;

	const int32 Slot = FApiKeyManager::Get().GetActiveSlotIndex();
	const FString Section = GetACPSlotSection(Slot);
	FString SavedModel, SavedMode;
	GConfig->GetString(*Section, *FString::Printf(TEXT("%s.Model"), *AgentId), SavedModel, FSettingsManager::GetGlobalConfigPath());
	GConfig->GetString(*Section, *FString::Printf(TEXT("%s.Mode"),  *AgentId), SavedMode,  FSettingsManager::GetGlobalConfigPath());
	const FString CurModel = SavedModel.IsEmpty() ? Snap->CurrentModel : SavedModel;
	const FString CurMode  = SavedMode.IsEmpty()  ? Snap->CurrentMode  : SavedMode;

	auto MakeArr = [](const TArray<FUECPACPConfigOption>& Opts)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FUECPACPConfigOption& O : Opts)
		{
			const TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
			V->SetStringField(TEXT("id"),          O.Id);
			V->SetStringField(TEXT("name"),        O.Name);
			V->SetStringField(TEXT("description"), O.Description);
			Out.Add(MakeShared<FJsonValueObject>(V));
		}
		return Out;
	};

	TArray<TSharedPtr<FJsonValue>> SectionArr;
	for (const FUECPACPAgentConfigSection& Sec : Snap->OtherSections)
	{
		FString SavedVal;
		const FString OptKey = FString::Printf(TEXT("%s.Option.%s"), *AgentId, *Sec.Id);
		GConfig->GetString(*Section, *OptKey, SavedVal, FSettingsManager::GetGlobalConfigPath());
		const FString CurVal = SavedVal.IsEmpty() ? Sec.CurrentValue : SavedVal;

		const TSharedRef<FJsonObject> SObj = MakeShared<FJsonObject>();
		SObj->SetStringField(TEXT("id"),           Sec.Id);
		SObj->SetStringField(TEXT("name"),         Sec.Name);
		SObj->SetStringField(TEXT("currentValue"), CurVal);
		SObj->SetArrayField (TEXT("options"),      MakeArr(Sec.Options));
		SectionArr.Add(MakeShared<FJsonValueObject>(SObj));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField (TEXT("models"),       MakeArr(Snap->Models));
	Root->SetArrayField (TEXT("modes"),        MakeArr(Snap->Modes));
	Root->SetArrayField (TEXT("sections"),     SectionArr);
	Root->SetStringField(TEXT("currentModel"), CurModel);
	Root->SetStringField(TEXT("currentMode"),  CurMode);

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root, Writer); Writer->Close();

	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;
	const double Tx = FPlatformTime::Seconds();
	Browser->ExecuteJavascript(FString::Printf(
		TEXT("if(typeof onACPAgentConfig==='function')onACPAgentConfig(\"%s\",%s)"),
		*ACPEscJs(AgentId), *Body));
	const double Ty = FPlatformTime::Seconds();
	const double JsMs = (Ty - Tx) * 1000.0;
	if (JsMs > 30.0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ACP] PushACPAgentConfig slow for '%s': js=%.0f ms (%d models, %d sections, %d chars)"),
			*AgentId, JsMs, Snap->Models.Num(), Snap->OtherSections.Num(), Body.Len());
	}
}

void UUECPSettingsBridge::PushACPInstallProgress(const FString& AgentId, const FString& Stage,
	float Fraction, bool bDone, bool bSuccess)
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;
	Browser->ExecuteJavascript(FString::Printf(
		TEXT("if(typeof onACPInstallProgress==='function')onACPInstallProgress(\"%s\",\"%s\",%.2f,%s,%s)"),
		*ACPEscJs(AgentId), *ACPEscJs(Stage), Fraction,
		bDone    ? TEXT("true") : TEXT("false"),
		bSuccess ? TEXT("true") : TEXT("false")));
}

void UUECPSettingsBridge::PushCliStatusToJs(bool bClaude, bool bClaudeAuth, bool bCodex, bool bCopilot, bool bGemini, bool bGhAuthed,
	const FString& NodeVer, const FString& NpmVer)
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;

	auto MakeRuntime = [](const FString& Ver) -> TSharedPtr<FJsonObject>
	{
		auto O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("found"), !Ver.IsEmpty());
		if (!Ver.IsEmpty()) O->SetStringField(TEXT("ver"), Ver);
		return O;
	};

	auto O = MakeShared<FJsonObject>();
	auto CA = MakeShared<FJsonObject>(); CA->SetBoolField(TEXT("found"), bClaude); CA->SetBoolField(TEXT("authFound"), bClaudeAuth);
	auto CO = MakeShared<FJsonObject>(); CO->SetBoolField(TEXT("found"), bCodex);
	auto CP = MakeShared<FJsonObject>(); CP->SetBoolField(TEXT("found"), bCopilot); CP->SetBoolField(TEXT("githubAuthed"), bGhAuthed);
	auto GA = MakeShared<FJsonObject>(); GA->SetBoolField(TEXT("found"), bGemini);
	O->SetObjectField(TEXT("claude"), CA);
	O->SetObjectField(TEXT("codex"), CO);
	O->SetObjectField(TEXT("copilot"), CP);
	O->SetObjectField(TEXT("gemini"), GA);
	O->SetObjectField(TEXT("node"), MakeRuntime(NodeVer));
	O->SetObjectField(TEXT("npm"), MakeRuntime(NpmVer));

	FString Json = JsonStr(O);
	Browser->ExecuteJavascript(TEXT("if(typeof onCliStatus==='function')onCliStatus(") + Json + TEXT(")"));
}

void UUECPSettingsBridge::OpenExternalUrl(const FString& Url)
{
	if (!Url.IsEmpty())
		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
}

void UUECPSettingsBridge::PushInstallResultToJs(const FString& ProviderName, bool bSuccess, const FString& Message)
{
	auto Browser = BrowserRef.Pin();
	if (!Browser.IsValid()) return;

	FString EscProvider = ProviderName.Replace(TEXT("\""), TEXT("\\\""));
	FString EscMsg = Message.Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT("\\n"));
	FString Js = FString::Printf(TEXT("if(typeof onInstallResult==='function')onInstallResult(\"%s\",%s,\"%s\")"),
		*EscProvider, bSuccess ? TEXT("true") : TEXT("false"), *EscMsg);
	Browser->ExecuteJavascript(Js);
}

static FString GetSettingsHtml()
{
	FString Html;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
	if (Plugin.IsValid())
	{
		FString HtmlPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UI"), TEXT("settings_ui.html"));
		if (!FFileHelper::LoadFileToString(Html, *HtmlPath))
		{
			Html = TEXT("<html><body style='background:#111;color:#f44;font-family:sans-serif;padding:40px'><h2>Settings UI not found</h2><p>Expected: ") + HtmlPath + TEXT("</p></body></html>");
		}
	}
	else
	{
		Html = TEXT("<html><body style='background:#111;color:#f44;font-family:sans-serif;padding:40px'><h2>Plugin not found</h2></body></html>");
	}
	return Html;
}

FString UUECPSettingsBridge::BuildSettingsHtmlDataUri()
{
	FString Html = GetSettingsHtml();

	bool bSettingsTourSeen = false;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("SettingsTourSeen"), bSettingsTourSeen,
		FSettingsManager::GetGlobalConfigPath());
	const FString TourInject = FString::Printf(
		TEXT("<script>window._settingsTourSeen=%s;</script></head>"),
		bSettingsTourSeen ? TEXT("true") : TEXT("false"));
	Html.ReplaceInline(TEXT("</head>"), *TourInject, ESearchCase::IgnoreCase);

	TArray<uint8> Bytes;
	FTCHARToUTF8 Conv(*Html);
	Bytes.Append((const uint8*)Conv.Get(), Conv.Length());
	FString Encoded = FBase64::Encode(Bytes);
	return TEXT("data:text/html;base64,") + Encoded;
}

FString UUECPSettingsBridge::RequestPluginInfo()
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate")))
	{
		const FPluginDescriptor& Desc = Plugin->GetDescriptor();
		Out->SetStringField(TEXT("name"),        Desc.FriendlyName);
		Out->SetStringField(TEXT("plugin_id"),   Plugin->GetName());
		Out->SetStringField(TEXT("version"),     Desc.VersionName);
		Out->SetStringField(TEXT("description"), Desc.Description);
		Out->SetStringField(TEXT("author"),      Desc.CreatedBy);
	}

	Out->SetStringField(TEXT("ue_version"), FEngineVersion::Current().ToString());

#if UE_BUILD_SHIPPING
	Out->SetStringField(TEXT("build_config"), TEXT("Shipping"));
#elif UE_BUILD_TEST
	Out->SetStringField(TEXT("build_config"), TEXT("Test"));
#elif UE_BUILD_DEBUG
	Out->SetStringField(TEXT("build_config"), TEXT("Debug"));
#else
	Out->SetStringField(TEXT("build_config"), TEXT("Development"));
#endif

	// Axivor AI: no external vendor links. Empty values hide the anchors in the Info panel.
	Out->SetStringField(TEXT("docs_url"),    TEXT(""));
	Out->SetStringField(TEXT("discord_url"), TEXT(""));
	Out->SetStringField(TEXT("website_url"), TEXT(""));

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Out, Writer);
	return Json;
}

void UUECPSettingsBridge::FetchMeshProviders()
{
	auto WeakBrowser = BrowserRef;

	const FString Url = FFreeTierConfigManager::Get().GetRemoteBaseUrl() + TEXT("/rest/v1/mesh_providers?select=name,display_name,endpoint,model,payload_style,notes&active=eq.true&order=sort_order");
	const FString AnonKey = FFreeTierConfigManager::Get().GetServiceRegistrationKey();

	TSharedPtr<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("apikey"), AnonKey);
	Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AnonKey));
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->OnProcessRequestComplete().BindLambda([WeakBrowser](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		auto Browser = WeakBrowser.Pin();
		if (!Browser.IsValid()) return;

		FString Json;
		if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
		{
			Json = Resp->GetContentAsString();
		}
		else
		{
			Json = TEXT("[{\"name\":\"meshy\",\"display_name\":\"Meshy AI\",\"endpoint\":\"https://api.meshy.ai/openapi/v2/text-to-3d\",\"model\":\"meshy-6\",\"payload_style\":\"meshy\",\"notes\":\"Get API key at meshy.ai\"},"
			            "{\"name\":\"tripo\",\"display_name\":\"Tripo3D\",\"endpoint\":\"https://api.tripo3d.ai/v2/openapi/task\",\"model\":\"\",\"payload_style\":\"tripo\",\"notes\":\"Get API key at tripo3d.ai\"},"
			            "{\"name\":\"hunyuan\",\"display_name\":\"Hunyuan3D\",\"endpoint\":\"https://api.wavespeed.ai/api/v3/wavespeed-ai/hunyuan3d-v3/text-to-3d\",\"model\":\"\",\"payload_style\":\"hunyuan\",\"notes\":\"Via WaveSpeed AI\"},"
			            "{\"name\":\"custom\",\"display_name\":\"Custom / Local\",\"endpoint\":\"\",\"model\":\"\",\"payload_style\":\"custom\",\"notes\":\"Point to any compatible 3D generation API\"}]");
		}

		Json.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Json.ReplaceInline(TEXT("'"), TEXT("\\'"));
		Browser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof onMeshProviders==='function')onMeshProviders(%s)"), *Json));
	});
	Req->ProcessRequest();
}

#undef LOCTEXT_NAMESPACE
