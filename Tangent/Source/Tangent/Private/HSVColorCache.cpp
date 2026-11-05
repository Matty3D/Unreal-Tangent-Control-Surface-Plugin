// Tangent Panels Plugin for Unreal Editor
// Copyright 2022 Tangent Wave Ltd.
#include "HSVColorCache.h"

HSVColorCache::HSVColorCache() = default;

void HSVColorCache::getHSV(ALight* light, FLinearColor& color)
{
	if (!IsValid(light)) { color = FLinearColor::Black; return; }
	const double now = FPlatformTime::Seconds();
	const FLinearColor rgb = light->GetLightColor();
	// Compare actual stored RGB to detect undo, Multi-User and external edits.
	if (_light.Get() != light || now - _timestamp >= 0.5 || !_sourceRGB.Equals(rgb, 0.000001f))
	{
		_light = light;
		_sourceRGB = rgb;
		_colorCache = rgb.LinearRGBToHSV();
		_timestamp = now;
	}
	color = _colorCache;
}

void HSVColorCache::setHSV(ALight* light, const FLinearColor& color)
{
	if (!IsValid(light)) return;
	_light = light;
	_colorCache = color;
	_sourceRGB = light->GetLightColor();
	_timestamp = FPlatformTime::Seconds();
}
