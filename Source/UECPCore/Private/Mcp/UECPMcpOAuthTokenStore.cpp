// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMcpOAuthTokenStore.h"
#include "UECPMcpOAuthCrypto.h"
#include "UECPCoreModule.h"
#include "Managers/SettingsManager.h"
#include "Misc/AES.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_MAC
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogUECPMcpOAuth, Log, All);

namespace
{
	constexpr int32 GAesBlockSize = 16;

	void StoreFillRandom(uint8* Out, int32 N)
	{
		int32 i = 0;
		while (i < N)
		{
			const FGuid G = FGuid::NewGuid();
			const uint32 Words[4] = { G.A, G.B, G.C, G.D };
			for (int32 w = 0; w < 4 && i < N; ++w)
				for (int32 b = 0; b < 4 && i < N; ++b)
					Out[i++] = (uint8)((Words[w] >> (b * 8)) & 0xFF);
		}
	}

	bool AesEncrypt(const uint8 Key[32], const TArray<uint8>& Plain, TArray<uint8>& Out)
	{
		FAES::FAESKey K;
		FMemory::Memcpy(K.Key, Key, 32);
		Out = Plain;
		const int32 Pad = GAesBlockSize - (Out.Num() % GAesBlockSize);
		for (int32 i = 0; i < Pad; ++i) Out.Add((uint8)Pad);
		FAES::EncryptData(Out.GetData(), Out.Num(), K);
		return true;
	}

	bool AesDecrypt(const uint8 Key[32], const TArray<uint8>& Cipher, TArray<uint8>& Out)
	{
		if (Cipher.Num() == 0 || (Cipher.Num() % GAesBlockSize) != 0) return false;
		FAES::FAESKey K;
		FMemory::Memcpy(K.Key, Key, 32);
		Out = Cipher;
		FAES::DecryptData(Out.GetData(), Out.Num(), K);
		const uint8 Pad = Out.Num() > 0 ? Out.Last() : 0;
		if (Pad == 0 || Pad > GAesBlockSize || Pad > Out.Num()) return false;
		for (int32 i = Out.Num() - Pad; i < Out.Num(); ++i)
		{
			if (Out[i] != Pad) return false;
		}
		Out.SetNum(Out.Num() - Pad);
		return true;
	}

#if PLATFORM_MAC
	bool MacGetMasterKey(uint8 Out[32])
	{
		CFStringRef Svc  = CFSTR("com.blueprintslab.uecp.mcpoauth");
		CFStringRef Acct = CFSTR("master-key");

		const void* QKeys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit };
		const void* QVals[] = { kSecClassGenericPassword, Svc, Acct, kCFBooleanTrue, kSecMatchLimitOne };
		CFDictionaryRef Query = CFDictionaryCreate(nullptr, QKeys, QVals, 5,
			&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFTypeRef Result = nullptr;
		const OSStatus St = SecItemCopyMatching(Query, &Result);
		CFRelease(Query);

		if (St == errSecSuccess && Result)
		{
			CFDataRef Data = (CFDataRef)Result;
			const bool bOk = CFDataGetLength(Data) == 32;
			if (bOk) FMemory::Memcpy(Out, CFDataGetBytePtr(Data), 32);
			CFRelease(Result);
			return bOk;
		}

		StoreFillRandom(Out, 32);
		CFDataRef KeyData = CFDataCreate(nullptr, Out, 32);
		const void* AKeys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData };
		const void* AVals[] = { kSecClassGenericPassword, Svc, Acct, KeyData };
		CFDictionaryRef Add = CFDictionaryCreate(nullptr, AKeys, AVals, 4,
			&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		const OSStatus AddSt = SecItemAdd(Add, nullptr);
		CFRelease(Add);
		CFRelease(KeyData);
		return AddSt == errSecSuccess;
	}
#endif

#if !PLATFORM_WINDOWS
	bool GetLocalAesKey(uint8 Out[32])
	{
#if PLATFORM_MAC
		return MacGetMasterKey(Out);
#else
		FString Seed = FPlatformMisc::GetLoginId();
		if (Seed.IsEmpty()) Seed = FPlatformMisc::GetOperatingSystemId();
		Seed += TEXT("|uecp-mcp-oauth-v1");
		UECPMcpOAuthCrypto::DeriveKey32(Seed, Out);
		return true;
#endif
	}
#endif

	FString RecordToJson(const FUECPMcpOAuthRecord& R)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("access_token"),  R.AccessToken);
		O->SetStringField(TEXT("refresh_token"), R.RefreshToken);
		O->SetStringField(TEXT("scope"),         R.Scope);
		O->SetStringField(TEXT("issuer"),        R.Issuer);
		O->SetStringField(TEXT("client_id"),     R.ClientId);
		O->SetStringField(TEXT("resource"),      R.Resource);
		O->SetStringField(TEXT("token_endpoint"),R.TokenEndpoint);
		O->SetStringField(TEXT("expires_at"),    R.ExpiresAtUtc.ToIso8601());
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(O, W);
		return Out;
	}

	bool RecordFromJson(const FString& Json, FUECPMcpOAuthRecord& R)
	{
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, O) || !O.IsValid()) return false;
		O->TryGetStringField(TEXT("access_token"),   R.AccessToken);
		O->TryGetStringField(TEXT("refresh_token"),  R.RefreshToken);
		O->TryGetStringField(TEXT("scope"),          R.Scope);
		O->TryGetStringField(TEXT("issuer"),         R.Issuer);
		O->TryGetStringField(TEXT("client_id"),      R.ClientId);
		O->TryGetStringField(TEXT("resource"),       R.Resource);
		O->TryGetStringField(TEXT("token_endpoint"), R.TokenEndpoint);
		FString Iso;
		if (O->TryGetStringField(TEXT("expires_at"), Iso))
		{
			FDateTime::ParseIso8601(*Iso, R.ExpiresAtUtc);
		}
		return true;
	}
}

FUECPMcpOAuthTokenStore& FUECPMcpOAuthTokenStore::Get()
{
	static FUECPMcpOAuthTokenStore Instance;
	return Instance;
}

bool FUECPMcpOAuthTokenStore::HasOsSecretStore()
{
#if PLATFORM_WINDOWS || PLATFORM_MAC
	return true;
#else
	return false;
#endif
}

FString FUECPMcpOAuthTokenStore::GetStorePath()
{
	return FPaths::Combine(FSettingsManager::GetGlobalDataDir(), TEXT("user_mcp_oauth.bin"));
}

bool FUECPMcpOAuthTokenStore::ProtectBytes(const TArray<uint8>& Plain, TArray<uint8>& OutBlob)
{
	OutBlob.Reset();
#if PLATFORM_WINDOWS
	DATA_BLOB In;
	In.pbData = const_cast<BYTE*>(Plain.GetData());
	In.cbData = (DWORD)Plain.Num();
	DATA_BLOB Out;
	FMemory::Memzero(&Out, sizeof(Out));
	if (!CryptProtectData(&In, L"UECP MCP OAuth", nullptr, nullptr, nullptr,
			CRYPTPROTECT_UI_FORBIDDEN, &Out))
	{
		return false;
	}
	OutBlob.Append(reinterpret_cast<const uint8*>(Out.pbData), (int32)Out.cbData);
	LocalFree(Out.pbData);
	return true;
#else
	uint8 Key[32];
	if (!GetLocalAesKey(Key)) return false;
	return AesEncrypt(Key, Plain, OutBlob);
#endif
}

bool FUECPMcpOAuthTokenStore::UnprotectBytes(const TArray<uint8>& Blob, TArray<uint8>& OutPlain)
{
	OutPlain.Reset();
#if PLATFORM_WINDOWS
	DATA_BLOB In;
	In.pbData = const_cast<BYTE*>(Blob.GetData());
	In.cbData = (DWORD)Blob.Num();
	DATA_BLOB Out;
	FMemory::Memzero(&Out, sizeof(Out));
	if (!CryptUnprotectData(&In, nullptr, nullptr, nullptr, nullptr,
			CRYPTPROTECT_UI_FORBIDDEN, &Out))
	{
		return false;
	}
	OutPlain.Append(reinterpret_cast<const uint8*>(Out.pbData), (int32)Out.cbData);
	LocalFree(Out.pbData);
	return true;
#else
	uint8 Key[32];
	if (!GetLocalAesKey(Key)) return false;
	return AesDecrypt(Key, Blob, OutPlain);
#endif
}

void FUECPMcpOAuthTokenStore::Initialize()
{
	FScopeLock SL(&Lock);
	if (bInitialized) return;
	LoadLocked();
	bInitialized = true;
	if (!HasOsSecretStore())
	{
		UE_LOG(LogUECPMcpOAuth, Warning,
			TEXT("OAuth token store: no OS key store on this platform — tokens are obfuscated with a machine-derived key, not OS-protected."));
	}
}

void FUECPMcpOAuthTokenStore::LoadLocked()
{
	Records.Reset();
	const FString Path = GetStorePath();
	if (!IFileManager::Get().FileExists(*Path)) return;

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path)) return;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	const TSharedPtr<FJsonObject>* RecObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("records"), RecObj) || !RecObj || !RecObj->IsValid()) return;

	for (const auto& Pair : (*RecObj)->Values)
	{
		const FString Id(*Pair.Key);
		const FString B64 = (Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
			? Pair.Value->AsString() : FString();
		if (Id.IsEmpty() || B64.IsEmpty()) continue;

		TArray<uint8> Blob;
		if (!FBase64::Decode(B64, Blob)) continue;
		TArray<uint8> Plain;
		if (!UnprotectBytes(Blob, Plain) || Plain.Num() == 0) continue;

		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Plain.GetData()), Plain.Num());
		const FString RecJson(Conv.Length(), Conv.Get());
		FUECPMcpOAuthRecord Rec;
		if (RecordFromJson(RecJson, Rec)) Records.Add(Id, MoveTemp(Rec));
	}
}

bool FUECPMcpOAuthTokenStore::SaveLocked() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	const TSharedRef<FJsonObject> RecObj = MakeShared<FJsonObject>();

	for (const TPair<FString, FUECPMcpOAuthRecord>& Pair : Records)
	{
		const FString RecJson = RecordToJson(Pair.Value);
		const FTCHARToUTF8 Utf8(*RecJson);
		TArray<uint8> Plain;
		Plain.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

		TArray<uint8> Blob;
		if (!ProtectBytes(Plain, Blob) || Blob.Num() == 0)
		{
			UE_LOG(LogUECPMcpOAuth, Warning, TEXT("OAuth token store: failed to encrypt record for '%s'"), *Pair.Key);
			return false;
		}
		RecObj->SetStringField(Pair.Key, FBase64::Encode(Blob.GetData(), Blob.Num()));
	}
	Root->SetObjectField(TEXT("records"), RecObj);

	FString Out;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root, W)) return false;

	const FString Path = GetStorePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveStringToFile(Out, *Path))
	{
		UE_LOG(LogUECPMcpOAuth, Warning, TEXT("OAuth token store: failed to write %s"), *Path);
		return false;
	}
	return true;
}

bool FUECPMcpOAuthTokenStore::GetRecord(const FString& ServerId, FUECPMcpOAuthRecord& Out) const
{
	FScopeLock SL(&Lock);
	if (const FUECPMcpOAuthRecord* Found = Records.Find(ServerId))
	{
		Out = *Found;
		return true;
	}
	return false;
}

bool FUECPMcpOAuthTokenStore::PutRecord(const FString& ServerId, const FUECPMcpOAuthRecord& Rec)
{
	if (ServerId.IsEmpty()) return false;
	FScopeLock SL(&Lock);

	const bool bHadPrior = Records.Contains(ServerId);
	const FUECPMcpOAuthRecord Prior = bHadPrior ? Records[ServerId] : FUECPMcpOAuthRecord();

	Records.Add(ServerId, Rec);
	if (SaveLocked()) return true;

	if (bHadPrior) Records.Add(ServerId, Prior);
	else           Records.Remove(ServerId);
	return false;
}

void FUECPMcpOAuthTokenStore::Remove(const FString& ServerId)
{
	FScopeLock SL(&Lock);
	if (Records.Remove(ServerId) > 0) SaveLocked();
}

bool FUECPMcpOAuthTokenStore::HasRecord(const FString& ServerId) const
{
	FScopeLock SL(&Lock);
	return Records.Contains(ServerId);
}
