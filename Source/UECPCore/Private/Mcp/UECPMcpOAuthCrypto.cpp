// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMcpOAuthCrypto.h"
#include "Misc/Base64.h"
#include "Misc/Guid.h"

namespace
{
	FORCEINLINE uint32 RotR(uint32 X, uint32 N) { return (X >> N) | (X << (32 - N)); }

	const uint32 GSha256K[64] = {
		0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
		0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
		0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
		0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
		0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
		0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
		0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
		0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
	};

	void Sha256Impl(const uint8* Data, int32 Len, uint8 Out[32])
	{
		uint32 H[8] = {
			0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
			0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
		};

		const uint64 BitLen = (uint64)Len * 8;
		TArray<uint8> Msg;
		Msg.Append(Data, Len);
		Msg.Add(0x80);
		while ((Msg.Num() % 64) != 56) Msg.Add(0x00);
		for (int32 i = 7; i >= 0; --i) Msg.Add((uint8)((BitLen >> (i * 8)) & 0xFF));

		for (int32 Chunk = 0; Chunk < Msg.Num(); Chunk += 64)
		{
			uint32 W[64];
			for (int32 i = 0; i < 16; ++i)
			{
				const int32 j = Chunk + i * 4;
				W[i] = ((uint32)Msg[j] << 24) | ((uint32)Msg[j + 1] << 16)
				     | ((uint32)Msg[j + 2] << 8) | ((uint32)Msg[j + 3]);
			}
			for (int32 i = 16; i < 64; ++i)
			{
				const uint32 s0 = RotR(W[i - 15], 7) ^ RotR(W[i - 15], 18) ^ (W[i - 15] >> 3);
				const uint32 s1 = RotR(W[i - 2], 17) ^ RotR(W[i - 2], 19) ^ (W[i - 2] >> 10);
				W[i] = W[i - 16] + s0 + W[i - 7] + s1;
			}

			uint32 a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
			for (int32 i = 0; i < 64; ++i)
			{
				const uint32 S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
				const uint32 ch = (e & f) ^ (~e & g);
				const uint32 t1 = h + S1 + ch + GSha256K[i] + W[i];
				const uint32 S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
				const uint32 maj = (a & b) ^ (a & c) ^ (b & c);
				const uint32 t2 = S0 + maj;
				h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
			}
			H[0] += a; H[1] += b; H[2] += c; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
		}

		for (int32 i = 0; i < 8; ++i)
		{
			Out[i * 4 + 0] = (uint8)((H[i] >> 24) & 0xFF);
			Out[i * 4 + 1] = (uint8)((H[i] >> 16) & 0xFF);
			Out[i * 4 + 2] = (uint8)((H[i] >> 8) & 0xFF);
			Out[i * 4 + 3] = (uint8)(H[i] & 0xFF);
		}
	}

	void FillRandom(uint8* Out, int32 N)
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
}

namespace UECPMcpOAuthCrypto
{
	void Sha256(const uint8* Data, int32 Len, uint8 Out[32])
	{
		Sha256Impl(Data, Len, Out);
	}

	FString Base64UrlEncode(const uint8* Data, int32 Len)
	{
		FString B64 = FBase64::Encode(Data, Len);
		B64.ReplaceInline(TEXT("+"), TEXT("-"));
		B64.ReplaceInline(TEXT("/"), TEXT("_"));
		B64.ReplaceInline(TEXT("="), TEXT(""));
		return B64;
	}

	FString Base64UrlEncode(const TArray<uint8>& Data)
	{
		return Base64UrlEncode(Data.GetData(), Data.Num());
	}

	FString GenerateCodeVerifier()
	{
		uint8 Bytes[48];
		FillRandom(Bytes, 48);
		return Base64UrlEncode(Bytes, 48);
	}

	FString CodeChallengeS256(const FString& Verifier)
	{
		const FTCHARToUTF8 Utf8(*Verifier);
		uint8 Digest[32];
		Sha256(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Digest);
		return Base64UrlEncode(Digest, 32);
	}

	FString RandomUrlToken(int32 NumBytes)
	{
		NumBytes = FMath::Clamp(NumBytes, 8, 256);
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(NumBytes);
		FillRandom(Bytes.GetData(), NumBytes);
		return Base64UrlEncode(Bytes);
	}

	void DeriveKey32(const FString& Seed, uint8 OutKey[32])
	{
		const FTCHARToUTF8 Utf8(*Seed);
		Sha256(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), OutKey);
	}
}
