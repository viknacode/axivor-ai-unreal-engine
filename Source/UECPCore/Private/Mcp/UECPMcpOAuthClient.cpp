// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMcpOAuthClient.h"
#include "UECPMcpOAuthCrypto.h"
#include "UECPMcpOAuthTokenStore.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeBool.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"

#include "Common/TcpListener.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogUECPMcpOAuthFlow, Log, All);

namespace
{
	const TCHAR* GMcpProtocolVersion = TEXT("2025-06-18");

	constexpr int32 GLoopbackFirstPort = 47629;
	constexpr int32 GLoopbackPortCount = 8;

	struct FHttpSyncResult
	{
		bool    bOk = false;
		int32   Status = 0;
		FString Body;
		FString WwwAuthenticate;
	};

	FHttpSyncResult HttpSync(const FString& Verb, const FString& Url,
		const TArray<TPair<FString, FString>>& Headers, const FString& Body, double TimeoutSec = 20.0)
	{
		FHttpSyncResult R;
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetVerb(Verb);
		Req->SetURL(Url);
		for (const TPair<FString, FString>& H : Headers)
		{
			if (!H.Key.IsEmpty()) Req->SetHeader(H.Key, H.Value);
		}
		if (!Body.IsEmpty()) Req->SetContentAsString(Body);
		Req->SetTimeout(TimeoutSec);

		bool bDone = false;
		Req->OnProcessRequestComplete().BindLambda(
			[&](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
			{
				bDone = true;
				if (bConnected && Resp.IsValid())
				{
					R.Status          = Resp->GetResponseCode();
					R.Body            = Resp->GetContentAsString();
					R.WwwAuthenticate = Resp->GetHeader(TEXT("WWW-Authenticate"));
					R.bOk             = R.Status >= 200 && R.Status < 300;
				}
			});

		if (!Req->ProcessRequest()) return R;

		const bool bGT = IsInGameThread();
		const double Deadline = FPlatformTime::Seconds() + TimeoutSec + 1.0;
		while (!bDone && FPlatformTime::Seconds() < Deadline)
		{
			if (bGT) FHttpModule::Get().GetHttpManager().Tick(0.01f);
			FPlatformProcess::Sleep(0.01f);
		}
		if (!bDone) Req->CancelRequest();
		return R;
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, O);
		return O;
	}

	FString GetOrigin(const FString& Url)
	{
		int32 SchemeEnd = INDEX_NONE;
		if (!Url.FindChar(TEXT(':'), SchemeEnd)) return Url;
		int32 HostStart = SchemeEnd + 1;
		while (HostStart < Url.Len() && Url[HostStart] == TEXT('/')) ++HostStart;
		int32 PathStart = HostStart;
		while (PathStart < Url.Len() && Url[PathStart] != TEXT('/') && Url[PathStart] != TEXT('?') && Url[PathStart] != TEXT('#')) ++PathStart;
		return Url.Left(PathStart);
	}

	FString GetPath(const FString& Url)
	{
		const FString Origin = GetOrigin(Url);
		FString Rest = Url.Mid(Origin.Len());
		int32 Cut;
		if (Rest.FindChar(TEXT('?'), Cut)) Rest = Rest.Left(Cut);
		if (Rest.FindChar(TEXT('#'), Cut)) Rest = Rest.Left(Cut);
		return Rest;
	}

	FString CanonicalResource(const FString& Url)
	{
		FString Res = GetOrigin(Url).ToLower() + GetPath(Url);
		while (Res.EndsWith(TEXT("/"))) Res = Res.LeftChop(1);
		return Res;
	}

	FString TrimTrailingSlash(FString S)
	{
		while (S.EndsWith(TEXT("/"))) S = S.LeftChop(1);
		return S;
	}

	void ParseQuery(const FString& Query, TMap<FString, FString>& Out)
	{
		TArray<FString> Pairs;
		Query.ParseIntoArray(Pairs, TEXT("&"), true);
		for (const FString& P : Pairs)
		{
			FString K, V;
			if (P.Split(TEXT("="), &K, &V)) Out.Add(K, FGenericPlatformHttp::UrlDecode(V));
			else Out.Add(P, FString());
		}
	}

	FString ExtractResourceMetadata(const FString& WwwAuth)
	{
		const FString Key = TEXT("resource_metadata=");
		int32 At = WwwAuth.Find(Key, ESearchCase::IgnoreCase);
		if (At == INDEX_NONE) return FString();
		FString Rest = WwwAuth.Mid(At + Key.Len()).TrimStart();
		if (Rest.StartsWith(TEXT("\"")))
		{
			Rest.RightChopInline(1);
			int32 End;
			if (Rest.FindChar(TEXT('"'), End)) return Rest.Left(End);
			return FString();
		}
		int32 End;
		if (Rest.FindChar(TEXT(','), End)) return Rest.Left(End).TrimEnd();
		return Rest.TrimEnd();
	}

	class FLoopbackCatcher
	{
	public:
		~FLoopbackCatcher() { Stop(); }

		bool Start(int32& OutPort)
		{
			for (int32 P = GLoopbackFirstPort; P < GLoopbackFirstPort + GLoopbackPortCount; ++P)
			{
				const FIPv4Endpoint Endpoint(FIPv4Address(127, 0, 0, 1), (uint16)P);
				TUniquePtr<FTcpListener> Candidate = MakeUnique<FTcpListener>(
					Endpoint, FTimespan::FromMilliseconds(50), false);
				if (Candidate->IsActive())
				{
					Candidate->OnConnectionAccepted().BindRaw(this, &FLoopbackCatcher::OnAccept);
					Listener = MoveTemp(Candidate);
					BoundPort = P;
					OutPort = P;
					return true;
				}
			}
			return false;
		}

		bool Wait(double TimeoutSec, FString& OutQuery)
		{
			const double Deadline = FPlatformTime::Seconds() + TimeoutSec;
			while (!bGot && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.05f);
			}
			if (!bGot) return false;
			FScopeLock SL(&Mx);
			OutQuery = Query;
			return true;
		}

		void Stop()
		{
			if (Listener.IsValid())
			{
				Listener->Stop();
				Listener.Reset();
			}
		}

		int32 GetPort() const { return BoundPort; }

	private:
		bool OnAccept(FSocket* Socket, const FIPv4Endpoint& )
		{
			if (!Socket) return false;

			Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(3));
			uint8 Buf[4096];
			int32 Read = 0;
			FString Request;
			if (Socket->Recv(Buf, sizeof(Buf) - 1, Read) && Read > 0)
			{
				Buf[Read] = 0;
				Request = FString(StringCast<TCHAR>(reinterpret_cast<const ANSICHAR*>(Buf)).Get());
			}

			FString CapturedQuery;
			{
				int32 SpaceA, SpaceB;
				if (Request.FindChar(TEXT(' '), SpaceA))
				{
					FString AfterVerb = Request.Mid(SpaceA + 1);
					if (AfterVerb.FindChar(TEXT(' '), SpaceB))
					{
						FString Target = AfterVerb.Left(SpaceB);
						int32 Q;
						if (Target.FindChar(TEXT('?'), Q)) CapturedQuery = Target.Mid(Q + 1);
					}
				}
			}

			TMap<FString, FString> Probe;
			ParseQuery(CapturedQuery, Probe);
			const bool bRealRedirect = Probe.Contains(TEXT("code")) || Probe.Contains(TEXT("error"));

			const FString Html = bRealRedirect
				? FString(TEXT("<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign-in complete</title></head>")
					TEXT("<body style=\"font-family:system-ui,sans-serif;background:#1e1e1e;color:#eee;display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">")
					TEXT("<div style=\"text-align:center\"><h2>Sign-in complete</h2><p>You can close this tab and return to Unreal.</p></div></body></html>"))
				: FString(TEXT("<!doctype html><html><body>Waiting for sign-in…</body></html>"));
			const TCHAR* StatusLine = bRealRedirect ? TEXT("200 OK") : TEXT("400 Bad Request");
			const FString Resp = FString::Printf(
				TEXT("HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"),
				StatusLine, FTCHARToUTF8(*Html).Length(), *Html);
			const FTCHARToUTF8 RespUtf8(*Resp);
			int32 Sent = 0;
			Socket->Send(reinterpret_cast<const uint8*>(RespUtf8.Get()), RespUtf8.Length(), Sent);

			if (!bRealRedirect) return true;

			{
				FScopeLock SL(&Mx);
				Query = CapturedQuery;
			}
			bGot = true;
			return false;
		}

		TUniquePtr<FTcpListener> Listener;
		FCriticalSection         Mx;
		FString                  Query;
		FThreadSafeBool          bGot{ false };
		int32                    BoundPort = 0;
	};

	struct FAsMetadata
	{
		FString Issuer;
		FString AuthorizationEndpoint;
		FString TokenEndpoint;
		FString RegistrationEndpoint;
		FString Resource;
		TArray<FString> CodeChallengeMethods;
		TArray<FString> ScopesSupported;
		bool HasS256() const { return CodeChallengeMethods.Contains(TEXT("S256")); }
	};

	FString MinimalInitializeFrame()
	{
		return FString::Printf(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"%s\",\"capabilities\":{},\"clientInfo\":{\"name\":\"UECP\",\"version\":\"1.0\"}}}"),
			GMcpProtocolVersion);
	}

	bool DiscoverAuthServer(const FString& ServerUrl, FAsMetadata& Out, FString& OutScopeHint, FString& OutError)
	{
		TArray<TPair<FString, FString>> Hdrs = {
			{ TEXT("Content-Type"), TEXT("application/json") },
			{ TEXT("Accept"), TEXT("application/json, text/event-stream") },
			{ TEXT("MCP-Protocol-Version"), GMcpProtocolVersion },
		};
		const FHttpSyncResult Probe = HttpSync(TEXT("POST"), ServerUrl, Hdrs, MinimalInitializeFrame());

		TArray<FString> PrmCandidates;
		const FString FromHeader = ExtractResourceMetadata(Probe.WwwAuthenticate);
		if (!FromHeader.IsEmpty()) PrmCandidates.Add(FromHeader);
		const FString Origin = GetOrigin(ServerUrl);
		const FString Path   = GetPath(ServerUrl);
		if (!Path.IsEmpty() && Path != TEXT("/"))
			PrmCandidates.Add(Origin + TEXT("/.well-known/oauth-protected-resource") + Path);
		PrmCandidates.Add(Origin + TEXT("/.well-known/oauth-protected-resource"));

		FString Issuer;
		FString PrmResource;
		for (const FString& Cand : PrmCandidates)
		{
			const FHttpSyncResult Prm = HttpSync(TEXT("GET"), Cand, {}, FString());
			if (!Prm.bOk)
			{
				UE_LOG(LogUECPMcpOAuthFlow, Verbose, TEXT("OAuth discovery: PRM %s -> HTTP %d"), *Cand, Prm.Status);
				continue;
			}
			const TSharedPtr<FJsonObject> O = ParseObject(Prm.Body);
			if (!O.IsValid()) continue;
			const TArray<TSharedPtr<FJsonValue>>* Servers = nullptr;
			if (O->TryGetArrayField(TEXT("authorization_servers"), Servers) && Servers && Servers->Num() > 0)
			{
				Issuer = (*Servers)[0]->AsString();
				O->TryGetStringField(TEXT("resource"), PrmResource);
				const TArray<TSharedPtr<FJsonValue>>* Scopes = nullptr;
				if (O->TryGetArrayField(TEXT("scopes_supported"), Scopes) && Scopes)
				{
					TArray<FString> S;
					for (const TSharedPtr<FJsonValue>& V : *Scopes) S.Add(V->AsString());
					OutScopeHint = FString::Join(S, TEXT(" "));
				}
				break;
			}
		}
		Out.Resource = PrmResource.IsEmpty() ? CanonicalResource(ServerUrl) : PrmResource;

		if (Issuer.IsEmpty())
		{
			if (Probe.bOk)
			{
				OutError = FString::Printf(TEXT("The server responded HTTP %d without requiring authorization — it does not appear to use OAuth. Untick \"Use OAuth sign-in\" (use a static header instead) or check the URL."), Probe.Status);
			}
			else
			{
				OutError = TEXT("Could not discover the OAuth authorization server (no protected-resource metadata at the WWW-Authenticate hint or the well-known endpoints). The server may not support OAuth, or needs a different URL.");
			}
			UE_LOG(LogUECPMcpOAuthFlow, Warning, TEXT("OAuth discovery failed for %s: %s"), *ServerUrl, *OutError);
			return false;
		}

		const FString NormIssuer = TrimTrailingSlash(Issuer);
		const FString IssOrigin   = GetOrigin(NormIssuer);
		const FString IssPath     = GetPath(NormIssuer);
		TArray<FString> AsCandidates;
		AsCandidates.Add(IssOrigin + TEXT("/.well-known/oauth-authorization-server") + IssPath);
		AsCandidates.Add(IssOrigin + TEXT("/.well-known/openid-configuration") + IssPath);
		if (!IssPath.IsEmpty())
		{
			AsCandidates.Add(NormIssuer + TEXT("/.well-known/oauth-authorization-server"));
			AsCandidates.Add(NormIssuer + TEXT("/.well-known/openid-configuration"));
		}

		for (const FString& Cand : AsCandidates)
		{
			const FHttpSyncResult As = HttpSync(TEXT("GET"), Cand, {}, FString());
			if (!As.bOk) continue;
			const TSharedPtr<FJsonObject> O = ParseObject(As.Body);
			if (!O.IsValid()) continue;

			FString MetaIssuer;
			O->TryGetStringField(TEXT("issuer"), MetaIssuer);
			if (!MetaIssuer.IsEmpty() && TrimTrailingSlash(MetaIssuer) != TrimTrailingSlash(Issuer)) continue;

			O->TryGetStringField(TEXT("authorization_endpoint"), Out.AuthorizationEndpoint);
			O->TryGetStringField(TEXT("token_endpoint"),         Out.TokenEndpoint);
			O->TryGetStringField(TEXT("registration_endpoint"),  Out.RegistrationEndpoint);
			Out.Issuer = MetaIssuer.IsEmpty() ? Issuer : MetaIssuer;

			const TArray<TSharedPtr<FJsonValue>>* Methods = nullptr;
			if (O->TryGetArrayField(TEXT("code_challenge_methods_supported"), Methods) && Methods)
				for (const TSharedPtr<FJsonValue>& V : *Methods) Out.CodeChallengeMethods.Add(V->AsString());

			const TArray<TSharedPtr<FJsonValue>>* Scopes = nullptr;
			if (O->TryGetArrayField(TEXT("scopes_supported"), Scopes) && Scopes)
				for (const TSharedPtr<FJsonValue>& V : *Scopes) Out.ScopesSupported.Add(V->AsString());

			if (!Out.AuthorizationEndpoint.IsEmpty() && !Out.TokenEndpoint.IsEmpty()) break;
		}

		if (Out.AuthorizationEndpoint.IsEmpty() || Out.TokenEndpoint.IsEmpty())
		{
			OutError = TEXT("Authorization-server metadata was incomplete (no authorize/token endpoint).");
			return false;
		}
		if (!Out.HasS256())
		{
			OutError = TEXT("The authorization server does not advertise PKCE S256 support — refusing to continue (required by the MCP spec).");
			return false;
		}
		return true;
	}

	bool RegisterClient(const FString& RegistrationEndpoint, const FString& RedirectUri, FString& OutClientId, FString& OutError)
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("client_name"), TEXT("Ultimate Engine Co-Pilot"));
		Body->SetStringField(TEXT("application_type"), TEXT("native"));
		Body->SetStringField(TEXT("token_endpoint_auth_method"), TEXT("none"));
		{
			TArray<TSharedPtr<FJsonValue>> Uris = { MakeShared<FJsonValueString>(RedirectUri) };
			Body->SetArrayField(TEXT("redirect_uris"), Uris);
			TArray<TSharedPtr<FJsonValue>> Grants = {
				MakeShared<FJsonValueString>(TEXT("authorization_code")),
				MakeShared<FJsonValueString>(TEXT("refresh_token")) };
			Body->SetArrayField(TEXT("grant_types"), Grants);
			TArray<TSharedPtr<FJsonValue>> Resp = { MakeShared<FJsonValueString>(TEXT("code")) };
			Body->SetArrayField(TEXT("response_types"), Resp);
		}
		FString BodyStr;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
		FJsonSerializer::Serialize(Body, W);

		const FHttpSyncResult R = HttpSync(TEXT("POST"), RegistrationEndpoint,
			{ { TEXT("Content-Type"), TEXT("application/json") } }, BodyStr);
		if (!R.bOk)
		{
			OutError = FString::Printf(TEXT("Dynamic client registration failed (HTTP %d): %s"), R.Status, *R.Body.Left(300));
			return false;
		}
		const TSharedPtr<FJsonObject> O = ParseObject(R.Body);
		if (!O.IsValid() || !O->TryGetStringField(TEXT("client_id"), OutClientId) || OutClientId.IsEmpty())
		{
			OutError = TEXT("Client registration response had no client_id.");
			return false;
		}
		return true;
	}

	bool ParseTokenResponse(const FString& Body, const FString& Issuer, const FString& ClientId,
		const FString& Resource, const FString& TokenEndpoint, const FString& PriorRefresh,
		FUECPMcpOAuthRecord& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject> O = ParseObject(Body);
		if (!O.IsValid()) { OutError = TEXT("Token response was not JSON."); return false; }
		FString Access;
		if (!O->TryGetStringField(TEXT("access_token"), Access) || Access.IsEmpty())
		{
			FString Err; O->TryGetStringField(TEXT("error"), Err);
			OutError = Err.IsEmpty() ? TEXT("Token response had no access_token.") : FString::Printf(TEXT("Token error: %s"), *Err);
			return false;
		}
		Out.AccessToken   = Access;
		Out.Issuer        = Issuer;
		Out.ClientId      = ClientId;
		Out.Resource      = Resource;
		Out.TokenEndpoint = TokenEndpoint;
		O->TryGetStringField(TEXT("scope"), Out.Scope);

		FString NewRefresh;
		O->TryGetStringField(TEXT("refresh_token"), NewRefresh);
		Out.RefreshToken = NewRefresh.IsEmpty() ? PriorRefresh : NewRefresh;

		double ExpiresIn = 3600.0;
		O->TryGetNumberField(TEXT("expires_in"), ExpiresIn);
		Out.ExpiresAtUtc = FDateTime::UtcNow() + FTimespan::FromSeconds(ExpiresIn);
		return true;
	}

	FCriticalSection GRefreshLock;
}

void FUECPMcpOAuthClient::BeginInteractiveAuth(
	const FString& ServerId, const FString& ServerUrl,
	const FString& KnownClientId, const FString& KnownIssuer,
	TFunction<void(const FAuthResult&)> OnComplete)
{
	Async(EAsyncExecution::Thread,
		[ServerId, ServerUrl, KnownClientId, KnownIssuer, OnComplete = MoveTemp(OnComplete)]()
		{
			FAuthResult Result;

			FAsMetadata Meta;
			FString ScopeHint;
			if (!DiscoverAuthServer(ServerUrl, Meta, ScopeHint, Result.Error))
			{
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}

			FLoopbackCatcher Catcher;
			int32 Port = 0;
			if (!Catcher.Start(Port))
			{
				Result.Error = TEXT("Could not bind a localhost port for the OAuth redirect.");
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}
			const FString RedirectUri = FString::Printf(TEXT("http://127.0.0.1:%d/callback"), Port);

			FString ClientId = KnownClientId;
			if (ClientId.IsEmpty())
			{
				if (Meta.RegistrationEndpoint.IsEmpty())
				{
					Result.Error = TEXT("This server has no client registration endpoint and no client id is configured. Paste a pre-registered OAuth client id in the server settings.");
					AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
					return;
				}
				if (!RegisterClient(Meta.RegistrationEndpoint, RedirectUri, ClientId, Result.Error))
				{
					AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
					return;
				}
			}

			const FString Verifier  = UECPMcpOAuthCrypto::GenerateCodeVerifier();
			const FString Challenge = UECPMcpOAuthCrypto::CodeChallengeS256(Verifier);
			const FString State     = UECPMcpOAuthCrypto::RandomUrlToken(24);
			const FString Resource  = Meta.Resource;
			const FString Scope     = ScopeHint.IsEmpty() ? FString::Join(Meta.ScopesSupported, TEXT(" ")) : ScopeHint;

			FString AuthUrl = Meta.AuthorizationEndpoint;
			AuthUrl += AuthUrl.Contains(TEXT("?")) ? TEXT("&") : TEXT("?");
			AuthUrl += TEXT("response_type=code");
			AuthUrl += TEXT("&client_id=")     + FGenericPlatformHttp::UrlEncode(ClientId);
			AuthUrl += TEXT("&redirect_uri=")  + FGenericPlatformHttp::UrlEncode(RedirectUri);
			AuthUrl += TEXT("&code_challenge=") + Challenge + TEXT("&code_challenge_method=S256");
			AuthUrl += TEXT("&state=")         + State;
			AuthUrl += TEXT("&resource=")      + FGenericPlatformHttp::UrlEncode(Resource);
			if (!Scope.IsEmpty()) AuthUrl += TEXT("&scope=") + FGenericPlatformHttp::UrlEncode(Scope);

			FPlatformProcess::LaunchURL(*AuthUrl, nullptr, nullptr);

			FString Query;
			if (!Catcher.Wait(300.0, Query))
			{
				Result.Error = TEXT("Timed out waiting for the sign-in to complete in the browser.");
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}
			Catcher.Stop();

			TMap<FString, FString> Params;
			ParseQuery(Query, Params);
			if (Params.Contains(TEXT("error")))
			{
				Result.Error = FString::Printf(TEXT("Authorization denied: %s"), *Params[TEXT("error")]);
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}
			if (Params.FindRef(TEXT("state")) != State)
			{
				Result.Error = TEXT("OAuth state mismatch — aborting (possible cross-site request).");
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}
			const FString RetIss = Params.FindRef(TEXT("iss"));
			if (!RetIss.IsEmpty() && TrimTrailingSlash(RetIss) != TrimTrailingSlash(Meta.Issuer))
			{
				Result.Error = TEXT("OAuth issuer mismatch — aborting (possible mix-up attack).");
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}
			const FString Code = Params.FindRef(TEXT("code"));
			if (Code.IsEmpty())
			{
				Result.Error = TEXT("No authorization code in the redirect.");
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}

			FString TokenBody;
			TokenBody += TEXT("grant_type=authorization_code");
			TokenBody += TEXT("&code=")          + FGenericPlatformHttp::UrlEncode(Code);
			TokenBody += TEXT("&redirect_uri=")  + FGenericPlatformHttp::UrlEncode(RedirectUri);
			TokenBody += TEXT("&client_id=")     + FGenericPlatformHttp::UrlEncode(ClientId);
			TokenBody += TEXT("&code_verifier=") + FGenericPlatformHttp::UrlEncode(Verifier);
			TokenBody += TEXT("&resource=")      + FGenericPlatformHttp::UrlEncode(Resource);

			const FHttpSyncResult Tok = HttpSync(TEXT("POST"), Meta.TokenEndpoint,
				{ { TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded") } }, TokenBody);
			if (!Tok.bOk)
			{
				Result.Error = FString::Printf(TEXT("Token exchange failed (HTTP %d): %s"), Tok.Status, *Tok.Body.Left(300));
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}

			FUECPMcpOAuthRecord Rec;
			if (!ParseTokenResponse(Tok.Body, Meta.Issuer, ClientId, Resource, Meta.TokenEndpoint, FString(), Rec, Result.Error))
			{
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}

			if (!FUECPMcpOAuthTokenStore::Get().PutRecord(ServerId, Rec))
			{
				Result.Error = TEXT("Signed in, but the token could not be saved to disk — please try again.");
				AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
				return;
			}
			Result.bOk      = true;
			Result.Issuer   = Meta.Issuer;
			Result.ClientId = ClientId;
			UE_LOG(LogUECPMcpOAuthFlow, Log, TEXT("OAuth sign-in complete for '%s' (issuer %s)"), *ServerId, *Meta.Issuer);
			AsyncTask(ENamedThreads::GameThread, [OnComplete, Result]() { OnComplete(Result); });
		});
}

bool FUECPMcpOAuthClient::EnsureFreshToken(const FString& ServerId, double SkewSeconds)
{
	FScopeLock SL(&GRefreshLock);

	FUECPMcpOAuthTokenStore& Store = FUECPMcpOAuthTokenStore::Get();
	FUECPMcpOAuthRecord Rec;
	if (!Store.GetRecord(ServerId, Rec) || !Rec.HasAccessToken()) return false;
	if (!Rec.NeedsRefresh(SkewSeconds)) return true;
	if (!Rec.HasRefreshToken() || Rec.TokenEndpoint.IsEmpty()) return false;

	FString Body;
	Body += TEXT("grant_type=refresh_token");
	Body += TEXT("&refresh_token=") + FGenericPlatformHttp::UrlEncode(Rec.RefreshToken);
	Body += TEXT("&client_id=")     + FGenericPlatformHttp::UrlEncode(Rec.ClientId);
	if (!Rec.Resource.IsEmpty()) Body += TEXT("&resource=") + FGenericPlatformHttp::UrlEncode(Rec.Resource);

	const FHttpSyncResult R = HttpSync(TEXT("POST"), Rec.TokenEndpoint,
		{ { TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded") } }, Body, 10.0);
	if (!R.bOk)
	{
		UE_LOG(LogUECPMcpOAuthFlow, Warning, TEXT("OAuth refresh failed for '%s' (HTTP %d) — re-auth required"), *ServerId, R.Status);
		return false;
	}

	FUECPMcpOAuthRecord NewRec;
	FString Err;
	if (!ParseTokenResponse(R.Body, Rec.Issuer, Rec.ClientId, Rec.Resource, Rec.TokenEndpoint, Rec.RefreshToken, NewRec, Err))
	{
		UE_LOG(LogUECPMcpOAuthFlow, Warning, TEXT("OAuth refresh parse failed for '%s': %s"), *ServerId, *Err);
		return false;
	}
	Store.PutRecord(ServerId, NewRec);
	return true;
}

bool FUECPMcpOAuthClient::HasUsableGrant(const FString& ServerId)
{
	FUECPMcpOAuthRecord Rec;
	if (!FUECPMcpOAuthTokenStore::Get().GetRecord(ServerId, Rec)) return false;
	return Rec.HasAccessToken() && (!Rec.NeedsRefresh(0.0) || Rec.HasRefreshToken());
}

void FUECPMcpOAuthClient::Disconnect(const FString& ServerId)
{
	FUECPMcpOAuthTokenStore::Get().Remove(ServerId);
}
