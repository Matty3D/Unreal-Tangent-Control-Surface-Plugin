// Tangent Panels Plugin for Unreal Editor
// Copyright 2022 Tangent Wave Ltd.
// SVN: $Revision: 264 $

#pragma once

#include "CoreMinimal.h"
#include "Engine/Light.h"

/**
 * 
 */
class HSVColorCache
{
public:
	HSVColorCache();
	void getHSV(ALight *light, FLinearColor &color);
	void setHSV(ALight* light, const FLinearColor& color);

private:
	TWeakObjectPtr<ALight> _light;
	double _timestamp = 0;
	FLinearColor _colorCache = FLinearColor::Black;
	FLinearColor _sourceRGB = FLinearColor::Black;
};
