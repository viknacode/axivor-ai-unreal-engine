// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/UpdateManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Compression.h"

FUpdateManager& FUpdateManager::Get()
{
	static FUpdateManager Instance;
	return Instance;
}

void FUpdateManager::Initialize()
{
}

void FUpdateManager::CheckForUpdates(TFunction<void(bool bHasUpdates, int32 NumUpdates)> Callback)
{
	if (Callback) Callback( false,  0);
}

bool FUpdateManager::IsChecking() const
{
	return false;
}

FString FUpdateManager::GetStatusJson() const
{
	FString PluginVersion = TEXT("unknown");
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate")))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}
	// Remote update checking is not wired up in this build. Report it explicitly as
	// disabled rather than "Never", which reads like a pending-but-working check.
	return FString::Printf(
		TEXT("{\"last_checked\":\"Disabled\",\"update_check_disabled\":true,\"assets\":[],\"checking\":false,\"plugin_version\":\"%s\"}"),
		*PluginVersion);
}

void FUpdateManager::_InvalidateAssetCache()
{
}

static const uint8 _umCacheSalt[] = {
    0x4C,0x21,0x6F,0x18,0x33,0x57,0x0A,0x71,0x2E,0x42,0x65,0x19,
    0x37,0x5C,0x08,0x6B,0x24,0x4F,0x73,0x12,0x39,0x58,0x0C,0x69,
    0x2A,0x47,0x6D,0x14,0x3B,0x5A,0x06,0x77,0x21,0x4B,0x7B,0x16,
    0x3D,0x52,0x04,0x75,0x28,0x4D,0x79,0x10,0x35,0x5E,0x0E,0x73,
    0x2C,0x49,0x71,0x1A,0x33,0x56,0x02,0x7B,0x26,0x41,0x6F,0x1E,
    0x3F,0x54,0x0A,0x7D
};

static TArray<uint8> _umBlend(const TArray<uint8>& Data)
{
    TArray<uint8> Out;
    Out.SetNumUninitialized(Data.Num());
    const int32 KeyLen = sizeof(_umCacheSalt);
    for (int32 i = 0; i < Data.Num(); ++i)
        Out[i] = Data[i] ^ _umCacheSalt[i % KeyLen];
    return Out;
}

static TArray<uint8> _umUnpackRevision(const TArray<uint8>& Encoded)
{
    if (Encoded.Num() < 5) return TArray<uint8>();
    TArray<uint8> Combined = _umBlend(Encoded);

    const uint32 OrigSize =
        static_cast<uint32>(Combined[0])        |
        (static_cast<uint32>(Combined[1]) <<  8) |
        (static_cast<uint32>(Combined[2]) << 16) |
        (static_cast<uint32>(Combined[3]) << 24);

    if (OrigSize == 0 || OrigSize > 16 * 1024 * 1024) return TArray<uint8>();

    const int32 CompressedSize = Combined.Num() - 4;
    TArray<uint8> Plain;
    Plain.SetNumUninitialized(static_cast<int32>(OrigSize));
    if (!FCompression::UncompressMemory(NAME_Zlib, Plain.GetData(), Plain.Num(),
                                         Combined.GetData() + 4, CompressedSize))
    {
        return TArray<uint8>();
    }
    return Plain;
}

static FString _umBytesToString(const TArray<uint8>& Bytes)
{
    if (Bytes.Num() == 0) return FString();
    TArray<uint8> ZTerm = Bytes;
    ZTerm.Add(0);
    return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(ZTerm.GetData())));
}

FString FUpdateManager::ReadCachedPath(const FString& AbsolutePath)
{
    TArray<uint8> Raw;
    if (!FFileHelper::LoadFileToArray(Raw, *AbsolutePath)) return FString();

    TArray<uint8> Plain = _umUnpackRevision(Raw);
    if (Plain.Num() == 0) return FString();

    return _umBytesToString(Plain);
}

FString FUpdateManager::LoadCachedRevision(const FString& LogicalName) const
{
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
    if (!Plugin.IsValid()) return FString();

    const FString Ext = TEXT(".uecpdat");
    const FString Base = FPaths::Combine(Plugin->GetContentDir(), TEXT("AI"), LogicalName + Ext);
    return ReadCachedPath(Base);
}
