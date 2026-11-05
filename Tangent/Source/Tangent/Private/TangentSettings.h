#pragma once

#include "Engine/DeveloperSettings.h"
#include "TangentSettings.generated.h"

/** Project-wide options for Tangent panel controls. */
UCLASS(Config=Engine, DefaultConfig, meta=(DisplayName="Tangent"))
class UTangentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTangentSettings()
	{
		CategoryName = TEXT("Plugins");
		SectionName = TEXT("Tangent");
	}

	/** Respect Unreal property min/max ranges when adjusting or resetting mapped controls.
	 * Changes take effect immediately. Disabling this still preserves numeric storage
	 * limits and explicit plugin bounds.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Property Limits", meta=(DisplayName="Enforce Property Limits"))
	bool bEnforcePropertyLimits = true;

	/** Move the grading wheel with controls mapped to HSV H and S; V remains unchanged. */
	UPROPERTY(Config, EditAnywhere, Category="Color Wheel", meta=(DisplayName="Use HSV Controls for Color Wheel Movement"))
	bool bHSVColorWheelMovement = false;
};
