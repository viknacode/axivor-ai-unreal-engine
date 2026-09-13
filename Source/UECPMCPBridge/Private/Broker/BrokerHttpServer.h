// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "../Transport/IMcpTransport.h"

#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"

class IHttpRouter;
struct FHttpServerRequest;
class FInstanceRegistryWatcher;

class FBrokerHttpServer final : public IMcpTransport
{
public:
	FBrokerHttpServer(int32 InPort, const FInstanceRegistryWatcher* InRegistry);

	virtual bool Start() override;
	virtual void Stop() override;
	virtual const TCHAR* GetName() const override { return TEXT("broker-http"); }

	int32 GetBoundPort() const { return BoundPort; }

private:
	bool HandleRequest(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete);
	void HandleDiscovery(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete) const;
	void ForwardToWorker(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete,
		const FString& WorkerUrl);

	const FInstanceRegistryWatcher* Registry = nullptr;
	int32                   Port      = 0;
	int32                   BoundPort = 0;
	TSharedPtr<IHttpRouter> Router;
	FDelegateHandle         PreprocessorHandle;
};
