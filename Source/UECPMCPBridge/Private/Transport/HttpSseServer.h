// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "IMcpTransport.h"

#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"

class IHttpRouter;
struct FHttpServerRequest;

class FHttpSseServer final : public IMcpTransport
{
public:

	FHttpSseServer(int32 InBasePort = 30000, int32 InMaxAttempts = 10);

	virtual bool Start() override;
	virtual void Stop() override;
	virtual const TCHAR* GetName() const override { return TEXT("http"); }

	int32 GetBoundPort() const { return BoundPort; }

private:
	bool HandleMcpRequest    (const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete);
	bool HandleSseRequest    (const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete);
	bool HandleHealthRequest (const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete);

	bool ValidateBearer(const FHttpServerRequest& Req, const FHttpResultCallback& OnComplete) const;

	int32                   BasePort     = 30000;
	int32                   MaxAttempts  = 10;
	int32                   BoundPort    = 0;
	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle        McpRoute;
	FHttpRouteHandle        SseRoute;
	FHttpRouteHandle        HealthRoute;
};
