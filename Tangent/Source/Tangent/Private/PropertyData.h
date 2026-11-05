// Tangent Panels Plugin for Unreal Editor
// Copyright 2024 Tangent Wave Ltd.
// SVN: $Revision: 382 $

#pragma once

#include "CoreMinimal.h"
#include "PropertyChangeHelper.h"
#include "UObject/StructOnScope.h"

// property data store that we use to have all useful information for a ue property to hand for adjusting property path based values
class PropertyData
{
public:
	PropertyData(const FName propertyPath, FProperty* prop, void* dataPtr, void* defaultDataPtr) : PropertyPath(propertyPath), Prop(prop), DataPtr(dataPtr), DefaultDataPtr(defaultDataPtr), Clipped(false), Minimum(0.0), Maximum(0.0) {};

	bool AdjustParameterValue(PropertyChangeHelper* helper, AActor* actor, uint32 targetId, float delta, float &newValue);
	bool GetParameterValue(float& value);
	FLinearColor GetGradingHSV();

	bool SetMenuValue(PropertyChangeHelper* helper, AActor* actor, bool value, FString& newStrValue);
	bool AdjustMenuValue(PropertyChangeHelper* helper, AActor* actor, int32 step, FString& newStrValue);
	bool GetMenuValueAsString(FString& strValue);

private:
	bool ClampParameterValue(double& value) const;

public:
	// Virtual HSV channels use the enclosing RGB vector as their reflected property.
	int32 HSVComponent = INDEX_NONE;
	struct FGradingState
	{
		TOptional<FLinearColor> HSV;
		FVector4 RGB = FVector4(0, 0, 0, 0);
	};
	TSharedPtr<FGradingState> GradingState = MakeShared<FGradingState>();
	// Keep object identity and the reflected chain, never just an interior value pointer.
	TArray<TWeakObjectPtr<UObject>> EditObjects;
	TArray<int32> ObjectChainStarts;
	TArray<FProperty*> PropertyChain;
	TMap<FString, int32> ArrayIndices;
	TSharedPtr<FStructOnScope> DefaultStructStorage;
	FName		PropertyPath;
	FProperty*	Prop;
	void*		DataPtr;
	void*		DefaultDataPtr;
	bool		Clipped;
	double		Minimum,
				Maximum;
};
