// Tangent Panels Plugin for Unreal Editor
// Copyright 2024 Tangent Wave Ltd.
// SVN: $Revision: 382 $

#include "PropertyData.h"
#include "ResetCache.h"
#include "TangentSettings.h"
#include "String/LexFromString.h"

FLinearColor PropertyData::GetGradingHSV()
{
	const FVector4& rgb = *static_cast<FVector4*>(DataPtr);
	if (!GradingState->HSV.IsSet() || rgb != GradingState->RGB)
	{
		GradingState->HSV = FLinearColor(rgb.X, rgb.Y, rgb.Z).LinearRGBToHSV();
		GradingState->RGB = rgb;
	}
	return GradingState->HSV.GetValue();
}

bool PropertyData::ClampParameterValue(double& value) const
{
	if (!FMath::IsFinite(value)) return false;
	double lower = -TNumericLimits<double>::Max();
	double upper = TNumericLimits<double>::Max();
	if (CastField<FFloatProperty>(Prop))
	{
		lower = -TNumericLimits<float>::Max();
		upper = TNumericLimits<float>::Max();
	}
	else if (CastField<FByteProperty>(Prop)) { lower = 0; upper = 255; }
	else if (CastField<FIntProperty>(Prop)) { lower = MIN_int32; upper = MAX_int32; }

	// A vector/colour component can carry its limits on the enclosing struct property.
	// Stop at object references: limits on a reference do not constrain its members.
	auto ReadLimit = [this](const TCHAR* key, double& limit)
	{
		auto Read = [key, &limit](const FProperty* property)
		{
			return property && LexTryParseString(limit, *property->GetMetaData(key)) && FMath::IsFinite(limit);
		};
		if (Read(Prop)) return true;
		for (int32 i = PropertyChain.Num() - 1; i >= 0; --i)
		{
			FProperty* parent = PropertyChain[i];
			if (parent == Prop) continue;
			if (!CastField<FStructProperty>(parent) && !CastField<FArrayProperty>(parent)) break;
			if (Read(parent)) return true;
		}
		return false;
	};
	if (GetDefault<UTangentSettings>()->bEnforcePropertyLimits)
	{
		double limit;
		// Hard bounds take precedence; UI-only ranges still constrain a panel encoder.
		if (ReadLimit(TEXT("ClampMin"), limit) || ReadLimit(TEXT("UIMin"), limit)) lower = FMath::Max(lower, limit);
		if (ReadLimit(TEXT("ClampMax"), limit) || ReadLimit(TEXT("UIMax"), limit)) upper = FMath::Min(upper, limit);
	}
	if (Clipped)
	{
		if (!FMath::IsFinite(Minimum) || !FMath::IsFinite(Maximum)) return false;
		lower = FMath::Max(lower, Minimum);
		upper = FMath::Min(upper, Maximum);
	}
	if (CastField<FIntProperty>(Prop) || CastField<FByteProperty>(Prop))
	{
		lower = FMath::CeilToDouble(lower);
		upper = FMath::FloorToDouble(upper);
	}
	// Conflicting ranges have no legal value; leave the property untouched.
	if (lower > upper) return false;
	value = FMath::Clamp(value, lower, upper);
	return true;
}



// helper with property based parameter value changes,  must only be used in the game thread by the caller, if the delta is zero 
// the value is reset, the associated actor instance is required to trigger editor ui updates, returns true if the value was modified
bool PropertyData::AdjustParameterValue(PropertyChangeHelper* helper, AActor* actor, uint32 targetId, float delta, float &newValue)
{
	if (HSVComponent != INDEX_NONE)
	{
		FLinearColor hsv = GetGradingHSV();
		if (HSVComponent < 2 && GetDefault<UTangentSettings>()->bHSVColorWheelMovement)
		{
			// Match the grading wheel's orientation and nonlinear saturation radius.
#if (ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
			const double rotation = PI / 3.0;
			const double xSign = -1.0;
#else
			const double rotation = 0.0;
			const double xSign = 1.0;
#endif
			auto ToXY = [rotation, xSign](const FLinearColor& color)
			{
				const double angle = FMath::DegreesToRadians(static_cast<double>(color.R)) - rotation;
				const double radius = FMath::Pow(FMath::Clamp(static_cast<double>(color.G), 0.0, 1.0), 1.0 / 2.4);
				return FVector2D(xSign * FMath::Cos(angle) * radius, FMath::Sin(angle) * radius);
			};
			FVector2D xy = ToXY(hsv);
			double axis = HSVComponent == 0 ? xy.X : xy.Y;
			if (delta == 0)
			{
				if (ResetCache::IsSingleTap(targetId))
				{
					if (!ResetCache::GetValue(targetId, actor, &axis)) return false;
				}
				else
				{
					if (!DefaultDataPtr) return false;
					const FVector4& initial = *static_cast<FVector4*>(DefaultDataPtr);
					const FVector2D defaultXY = ToXY(FLinearColor(initial.X, initial.Y, initial.Z).LinearRGBToHSV());
					axis = HSVComponent == 0 ? defaultXY.X : defaultXY.Y;
				}
			}
			else
			{
				ResetCache::CacheInitialValue(targetId, actor, axis);
				// Hue deltas use degrees while saturation deltas are normalized.
				axis += HSVComponent == 0 ? delta / 360.0 : -delta;
			}
			if (!FMath::IsFinite(axis)) return false;
			if (HSVComponent == 0) xy.X = axis; else xy.Y = axis;
			if (xy.SizeSquared() > 1.0) xy.Normalize();
			if (!xy.IsNearlyZero())
			{
				double angle = FMath::Atan2(xy.Y, xSign * xy.X) + rotation;
				if (angle < 0.0) angle += 2.0 * PI;
				hsv.R = FMath::Fmod(FMath::RadiansToDegrees(angle), 360.0);
			}
			hsv.G = FMath::Pow(xy.Size(), 2.4);
			const FLinearColor rgb = hsv.HSVToLinearRGB();
			FVector4 updated(rgb.R, rgb.G, rgb.B, static_cast<FVector4*>(DataPtr)->W);
			if (!helper->OnPrePropertyChange(actor, *this, TEXT("Edit color grading wheel"))) return false;
			*static_cast<FVector4*>(DataPtr) = updated;
			GradingState->RGB = updated;
			GradingState->HSV = hsv;
			newValue = hsv.Component(HSVComponent);
			helper->OnPostPropertyChange(actor, *this);
			return true;
		}
		double value = hsv.Component(HSVComponent);
		if (delta == 0)
		{
			if (ResetCache::IsSingleTap(targetId))
			{
				if (!ResetCache::GetValue(targetId, actor, &value)) return false;
			}
			else
			{
				if (!DefaultDataPtr) return false;
				const FVector4& rgb = *static_cast<FVector4*>(DefaultDataPtr);
				value = FLinearColor(rgb.X, rgb.Y, rgb.Z).LinearRGBToHSV().Component(HSVComponent);
			}
		}
		else
		{
			ResetCache::CacheInitialValue(targetId, actor, value);
			value += delta;
		}
		if (!FMath::IsFinite(value)) return false;
		if (HSVComponent == 0) value = FMath::Clamp(value, 0.0, 359.999);
		else if (HSVComponent == 1) value = FMath::Clamp(value, 0.0, 1.0);
		else if (!ClampParameterValue(value)) return false;
		hsv.Component(HSVComponent) = static_cast<float>(value);
		const FLinearColor rgb = hsv.HSVToLinearRGB();
		FVector4 updated(rgb.R, rgb.G, rgb.B, static_cast<FVector4*>(DataPtr)->W);
		if (!helper->OnPrePropertyChange(actor, *this, TEXT("Edit color grading HSV"))) return false;
		*static_cast<FVector4*>(DataPtr) = updated;
		GradingState->RGB = updated;
		GradingState->HSV = hsv; // Retain hue/saturation when RGB is achromatic or black.
		newValue = static_cast<float>(value);
		helper->OnPostPropertyChange(actor, *this);
		return true;
	}
	if (delta == 0 && !DefaultDataPtr && !ResetCache::IsSingleTap(targetId)) return false;
	// prepare a text description for the transaction that will be used if a new one is required, using the user friendly property name
	FString description = "Edit " + Prop->GetDisplayNameText().ToString();

	// try to convert the property to a float type
	if (FFloatProperty* floatProp = CastField<FFloatProperty>(Prop))
	{
		double value;

		// a zero delta triggers a reset to default
		if (delta == 0)
		{
			// the reset action was registered when it first came in, check now if this is a single or double tap, a single tap may reset
			// to a cached value from before any changes were made if that is available - if no cached value is available we do nothing, a double
			// tap will always reset to the default taken from the property itself
			if (ResetCache::IsSingleTap(targetId))
			{
				// so this is a single tap reset, check for a cached value to apply for this target and actor, if we get one we will apply it as the new value
				if (!ResetCache::GetValue(targetId, actor, &value))
				{
					// there is no pre-change cached value to use so simply return as there is nothing more to do 
					return false;
				}
			}
			else
			{ 
				// fetch the default value to be applied and passed back as the reset
				value = floatProp->GetFloatingPointPropertyValue(DefaultDataPtr);
			}
		}
		else
		{
			// fetch the current value
			value = floatProp->GetFloatingPointPropertyValue(DataPtr);

			// we store the first pre-change value as a potential single tap reset in a cache, pass it to the cache singleton which will only
			// store it if we haven't already cached a value for this target and actor combo
			ResetCache::CacheInitialValue(targetId, actor, value);

			// now we can add the given delta to get the new value to apply
			value += delta;

		}

		double constrained = static_cast<double>(value);
		if (!ClampParameterValue(constrained)) return false;
		value = static_cast<decltype(value)>(constrained);

		// we need to mark the target actor as about to be modified
		if (!helper->OnPrePropertyChange(actor, *this, description))
		{
			return false;
		}

		// now we can set the new property value
		floatProp->SetFloatingPointPropertyValue(DataPtr, value);

		// Capture the stored value before editor notifications can reconstruct its owner.
		newValue = (float) floatProp->GetFloatingPointPropertyValue(DataPtr);
		helper->OnPostPropertyChange(actor, *this);

		// return with success
		return true;
	}
	// we support double properties
	else if (FDoubleProperty* doubleProp = CastField<FDoubleProperty>(Prop))
	{
		double value;

		// a zero delta triggers a reset to default
		if (delta == 0)
		{
			// the reset action was registered when it first came in, check now if this is a single or double tap, a single tap may reset
			// to a cached value from before any changes were made if that is available - if no cached value is available we do nothing, a double
			// tap will always reset to the default taken from the property itself
			if (ResetCache::IsSingleTap(targetId))
			{
				// so this is a single tap reset, check for a cached value to apply for this target and actor, if we get one we will apply it as the new value
				if (!ResetCache::GetValue(targetId, actor, &value))
				{
					// there is no pre-change cached value to use so simply return as there is nothing more to do 
					return false;
				}
			}
			else
			{ 
				// fetch the default value to be applied and passed back as the reset
				value = doubleProp->GetFloatingPointPropertyValue(DefaultDataPtr);
			}
		}
		else
		{
			// fetch the current value
			value = doubleProp->GetFloatingPointPropertyValue(DataPtr);

			// we store the first pre-change value as a potential single tap reset in a cache, pass it to the cache singleton which will only
			// store it if we haven't already cached a value for this target and actor combo
			ResetCache::CacheInitialValue(targetId, actor, value);

			// now we can add the given delta to get the new value to apply
			value += delta;

		}

		double constrained = static_cast<double>(value);
		if (!ClampParameterValue(constrained)) return false;
		value = static_cast<decltype(value)>(constrained);

		// we need to mark the target actor as about to be modified
		if (!helper->OnPrePropertyChange(actor, *this, description))
		{
			return false;
		}

		// now we can set the new property value
		doubleProp->SetFloatingPointPropertyValue(DataPtr, value);

		// Capture the stored value before editor notifications can reconstruct its owner.
		newValue = (float) doubleProp->GetFloatingPointPropertyValue(DataPtr);
		helper->OnPostPropertyChange(actor, *this);

		// return with success
		return true;
	}
	// we support unsigned byte properties
	else if (FByteProperty* byteProp = CastField<FByteProperty>(Prop))
	{
		uint64 value;

		// a zero delta triggers a reset to default
		if (delta == 0)
		{
			// the reset action was registered when it first came in, check now if this is a single or double tap, a single tap may reset
			// to a cached value from before any changes were made if that is available - if no cached value is available we do nothing, a double
			// tap will always reset to the default taken from the property itself
			if (ResetCache::IsSingleTap(targetId))
			{
				// so this is a single tap reset, check for a cached value to apply for this target and actor, if we get one we will apply it as the new value
				if (!ResetCache::GetValue(targetId, actor, &value))
				{
					// there is no pre-change cached value to use so simply return as there is nothing more to do 
					return false;
				}
			}
			else
			{ 
				// fetch the default value to be applied and passed back as the reset
				value = byteProp->GetUnsignedIntPropertyValue(DefaultDataPtr);
			}
		}
		else
		{
			// fetch the current value
			value = byteProp->GetUnsignedIntPropertyValue(DataPtr);

			// we store the first pre-change value as a potential single tap reset in a cache, pass it to the cache singleton which will only
			// store it if we haven't already cached a value for this target and actor combo
			ResetCache::CacheInitialValue(targetId, actor, value);

			double adjusted = static_cast<double>(value) + delta;
			if (!ClampParameterValue(adjusted)) return false;
			value = static_cast<uint64>(adjusted);
		}

		double constrained = static_cast<double>(value);
		if (!ClampParameterValue(constrained)) return false;
		value = static_cast<decltype(value)>(constrained);

		// we need to mark the target actor as about to be modified
		if (!helper->OnPrePropertyChange(actor, *this, description))
		{
			return false;
		}

		// now we can set the new property value
		byteProp->SetIntPropertyValue(DataPtr, value);

		// Capture the stored value before editor notifications can reconstruct its owner.
		newValue = (float) byteProp->GetUnsignedIntPropertyValue(DataPtr);
		helper->OnPostPropertyChange(actor, *this);

		// return with success
		return true;
	}
	// we support signed 32-bit int properties
	else if (FIntProperty* intProp = CastField<FIntProperty>(Prop))
	{
		int64 value;

		// a zero delta triggers a reset to default
		if (delta == 0)
		{
			// the reset action was registered when it first came in, check now if this is a single or double tap, a single tap may reset
			// to a cached value from before any changes were made if that is available - if no cached value is available we do nothing, a double
			// tap will always reset to the default taken from the property itself
			if (ResetCache::IsSingleTap(targetId))
			{
				// so this is a single tap reset, check for a cached value to apply for this target and actor, if we get one we will apply it as the new value
				if (!ResetCache::GetValue(targetId, actor, &value))
				{
					// there is no pre-change cached value to use so simply return as there is nothing more to do 
					return false;
				}
			}
			else
			{ 
				// fetch the default value to be applied and passed back as the reset
				value = intProp->GetSignedIntPropertyValue(DefaultDataPtr);
			}
		}
		else
		{
			// fetch the current value
			value = intProp->GetSignedIntPropertyValue(DataPtr);

			// we store the first pre-change value as a potential single tap reset in a cache, pass it to the cache singleton which will only
			// store it if we haven't already cached a value for this target and actor combo
			ResetCache::CacheInitialValue(targetId, actor, value);

			// now we can add the given delta to get the new value to apply
			double adjusted = static_cast<double>(value) + delta;
			if (!ClampParameterValue(adjusted)) return false;
			value = static_cast<int64>(adjusted);

		}

		double constrained = static_cast<double>(value);
		if (!ClampParameterValue(constrained)) return false;
		value = static_cast<decltype(value)>(constrained);

		// we need to mark the target actor as about to be modified
		if (!helper->OnPrePropertyChange(actor, *this, description))
		{
			return false;
		}

		// now we can set the new property value
		intProp->SetIntPropertyValue(DataPtr, value);

		// Capture the stored value before editor notifications can reconstruct its owner.
		newValue = (float) intProp->GetSignedIntPropertyValue(DataPtr);
		helper->OnPostPropertyChange(actor, *this);

		// return with success
		return true;
	}

	// getting here means the property is not a float value, return to say we did not change the value
	return false;
}

// helper with property based parameter values, returns true if the value was available and passed back
bool PropertyData::GetParameterValue(float &value)
{
	if (HSVComponent != INDEX_NONE)
	{
		value = GetGradingHSV().Component(HSVComponent);
		return true;
	}
	// try to convert the property to a float type
	if (FFloatProperty* floatProp = CastField<FFloatProperty>(Prop))
	{
		// fetch the current value and pass it back with success
		value = (float) floatProp->GetFloatingPointPropertyValue(DataPtr);
		return true;
	}
	// we support double properties
	else if (FDoubleProperty* doubleProp = CastField<FDoubleProperty>(Prop))
	{
		// fetch the current value and pass it back with success
		value = (float) doubleProp->GetFloatingPointPropertyValue(DataPtr);
		return true;
	}
	// we support unsigned byte properties
	else if (FByteProperty* byteProp = CastField<FByteProperty>(Prop))
	{
		// fetch the current value as a unsigned int as that is what byte properties support
		value = (float) byteProp->GetUnsignedIntPropertyValue(DataPtr);
		return true;
	}
	// we support signed 32-bit int properties
	else if (FIntProperty* intProp = CastField<FIntProperty>(Prop))
	{
		// fetch the current value as a signed int and pass it back with success
		value = (float) intProp->GetSignedIntPropertyValue(DataPtr);
		return true;
	}

	// getting here means the value is not available
	return false;
}

// special case helper to set a specific boolean property value, must only be used in the game thread by the caller, if the value
// is changed the function returns true and the new string version is passed back
bool PropertyData::SetMenuValue(PropertyChangeHelper* helper, AActor* actor, bool value, FString& newStrValue)
{
	// prepare a text description for the transaction that will be used if a new one is required, using the user friendly property name
	FString description = "Edit " + Prop->GetDisplayNameText().ToString();

	// this special case function requires a boolean property
	if (FBoolProperty* boolProp = CastField<FBoolProperty>(Prop))
	{
		// we need to mark the target actor as about to be modified
		if (!helper->OnPrePropertyChange(actor, *this, description))
		{
			return false;
		}

		// apply the new state
		boolProp->SetPropertyValue(DataPtr, value);

		// complete the transaction by saying the edit is now done
		helper->OnPostPropertyChange(actor, *this);

		// return a value string based on the boolean state
		newStrValue = value? FString("On"): FString("Off");
		return true;
	}

	// getting here means the value is not available
	return false;
}

// changes the menu property value by the required step value +/-1, if the step is 0 the value is reset, must only be used in the game thread
// by the caller, if the value is changed the function returns true and the new string version is passed back
bool PropertyData::AdjustMenuValue(PropertyChangeHelper* helper, AActor* actor, int32 step, FString& newStrValue)
{
	if (step == 0 && !DefaultDataPtr) return false;
	FNumericProperty* numProp = nullptr;
	UEnum* enumData = nullptr;

	// prepare a text description for the transaction that will be used if a new one is required, using the user friendly property name
	FString description = "Edit " + Prop->GetDisplayNameText().ToString();

	// we support boolean properties as a toggle, also enumerated values via the enum property type and also the byte property
	// type, they share a lot of the same processing but with some slight variation in set up
	if (FBoolProperty* boolProp = CastField<FBoolProperty>(Prop))
	{
		// fetch the bool value and toggle it
		bool value = !boolProp->GetPropertyValue(DataPtr);

		// we need to mark the target actor as about to be modified
		if (!helper->OnPrePropertyChange(actor, *this, description))
		{
			return false;
		}

		// apply the new state
		boolProp->SetPropertyValue(DataPtr, value);

		// complete the transaction by saying the edit is now done
		helper->OnPostPropertyChange(actor, *this);

		// return a value string based on the boolean state
		newStrValue = value? FString("On"): FString("Off");
		return true;
	}
	else if (FEnumProperty* enumProp = CastField<FEnumProperty>(Prop))
	{
		// fetch the enum data to process
		enumData = enumProp->GetEnum();

		// enum properties are linked to another property that has the actual value
		numProp = enumProp->GetUnderlyingProperty();
	}
	else if (FByteProperty* byteProp = CastField<FByteProperty>(Prop))
	{
		// fetch the enum data to process
		enumData = byteProp->Enum;

		// the byte prop itself is the target property in this context
		numProp = byteProp;
	}

	// if the casting was valid and we have the required data we can continue with the adjustment of the value for enumerated types
	if (numProp && enumData)
	{
		int64 value;

		// a zero step triggers a reset, otherwise the enumerated value is adjusted
		if (step == 0)
		{
			// this is a reset call, prepare the default value
			value = numProp->GetSignedIntPropertyValue(DefaultDataPtr);
		}
		else
		{
			// this is a step adjusted call, start with the current value
			value = numProp->GetSignedIntPropertyValue(DataPtr);

			// convert the value to the enumeration index and fetch the number of enumeration defined values
			int32	index = enumData->GetIndexByValue(value),
					count = enumData->NumEnums();

			// in unreal it is standard to automatically append a special case MAX value to the defined data, if this is the case then
			// we need to discount it from the index range we use for wrapping at the boundary steps
			if (enumData->ContainsExistingMax())
			{
				// this enum has a max definition so decrement the count for index range handling
				count--;
			}

			// modify the index of the value by the correct step direction
			if (step > 0)
			{
				// step up one index allowing for reaching the end of the definitions in which case we wrap round to the first
				index = (index == (count - 1)) ? 0 : index + 1;
			}
			else
			{
				// step down ine index allowing for being at the first defined value in which case we wrap around to the last
				index = (index == 0) ? count - 1 : index - 1;
			}

			// convert the new index into its associated value as we need this to apply to the property
			value = enumData->GetValueByIndex(index);
		}

		// we need to mark the target actor as about to be modified
		if (!helper->OnPrePropertyChange(actor, *this, description))
		{
			return false;
		}

		// apply the new property value
		numProp->SetIntPropertyValue(DataPtr, value);

		// Capture the stored value before editor notifications can reconstruct its owner.
		value = numProp->GetSignedIntPropertyValue(DataPtr);
		newStrValue = enumData->GetNameStringByValue(value);
		helper->OnPostPropertyChange(actor, *this);

		// return with success
		return true;
	}

	// getting here means the value is not available
	return false;
}

// inner class function to help with menu based values, returns true if the string value was available and passed back
bool PropertyData::GetMenuValueAsString(FString &strValue)
{
	FNumericProperty* numProp = nullptr;
	UEnum* enumData = nullptr;

	// we support boolean properties, also enumerated values via the enum property type and also the byte property
	// type, they share a lot of the same processing but with some slight variation in set up
	if (FBoolProperty* boolProp = CastField<FBoolProperty>(Prop))
	{
		// fetch the bool value
		bool value = boolProp->GetPropertyValue(DataPtr);

		// return a value string based on the boolean state
		strValue = value ? FString("On") : FString("Off");
		return true;
	}
	else if (FEnumProperty* enumProp = CastField<FEnumProperty>(Prop))
	{
		// fetch the enum data to process
		enumData = enumProp->GetEnum();

		// enum properties are linked to another property that has the actual value
		numProp = enumProp->GetUnderlyingProperty();
	}
	else if (FByteProperty* byteProp = CastField<FByteProperty>(Prop))
	{
		// fetch the enum data to process
		enumData = byteProp->Enum;

		// the byte prop itself is the target property in this context
		numProp = byteProp;
	}

	// if the casting was valid and we have the required data we can continue
	if (numProp && enumData)
	{
		// fetch the current integer value
		int64 value = numProp->GetSignedIntPropertyValue(DataPtr);

		// get the string (without the scope prefix) and return with success
		strValue = enumData->GetNameStringByValue(value);
		return true;
	}

	// getting here means the value is not available
	return false;
}
