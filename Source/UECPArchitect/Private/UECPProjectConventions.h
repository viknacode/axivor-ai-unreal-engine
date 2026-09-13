// Axivor AI — project conventions digest injected into the Architect system prompt.
#pragma once

#include "CoreMinimal.h"

namespace UECPProjectConventions
{
	// Compact "=== PROJECT CONVENTIONS ===" block: engine/project, grid & snapping, default
	// game mode/maps, top-level content folders with counts, dominant asset-name prefixes,
	// key gameplay plugins, and whether a scanner index exists. Cached for ~2 minutes.
	FString BuildBlock();
	void Invalidate();
}
