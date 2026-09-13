// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPLicenseService.h"
#include "Utils/EditorRuntime.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "FUECPLicenseServiceImpl"

bool FUECPLicenseServiceImpl::IsSessionActive() const
{
	return EditorReadiness::IsSessionActive();
}

bool FUECPLicenseServiceImpl::IsGraphContextReady() const
{
	return EditorReadiness::IsGraphContextReady();
}

bool FUECPLicenseServiceImpl::IsMCPContextValid() const
{
	return EditorReadiness::IsMCPContextValid();
}

bool FUECPLicenseServiceImpl::IsFeatureAvailable(FName ) const
{
	return EditorReadiness::IsSessionActive();
}

FText FUECPLicenseServiceImpl::GetUnavailableReason(FName ) const
{
	if (!EditorReadiness::IsSessionActive())
	{
		return LOCTEXT("SessionInactive", "This feature isn't available for the current editor session.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
