#pragma once

#include "Engine/DeveloperSettings.h"
#include "GameFramework/Actor.h"
#include "TangentSettings.generated.h"


USTRUCT()
struct FTangentActorClassModeRule
{
    GENERATED_BODY()

    UPROPERTY(Config, EditAnywhere, Category="Automatic Mode Switching")
    TSubclassOf<AActor> ActorClass;

    UPROPERTY(Config, EditAnywhere, Category="Automatic Mode Switching")
    FName ModeName;
};

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

	/** Delay after the last panel property edit before its undo transaction is closed. */
	UPROPERTY(Config, EditAnywhere, Category="Transactions", meta=(DisplayName="Transaction Close Delay (Seconds)", ClampMin="0.0", UIMin="0.0", UIMax="5.0"))
	float TransactionCloseDelay = 0.5f;

	/** Actor classes can select named modes from the Tangent mapper. */
    UPROPERTY(Config, EditAnywhere, Category="Automatic Mode Switching", meta=(DisplayName="Actor Class Mode Rules"))
    TArray<FTangentActorClassModeRule> ActorClassModeRules;

};
