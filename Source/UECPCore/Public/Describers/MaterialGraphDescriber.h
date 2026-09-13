// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UEdGraphNode;

class UECPCORE_API FMaterialGraphDescriber
{
public:

	FString Describe(UMaterial* Material);

private:

	TSet<UEdGraphNode*> VisitedNodes;
};
