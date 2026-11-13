// nDisplay Combined mappings use the same wheel coordinates as the built-in
// Color Correct mappings, with reflected paths for config UObject notifications.
#include "EngineHelper.h"
#include "ResetCache.h"
#include "Async/Async.h"
#include "Misc/ConfigCacheIni.h"

TArray<UActorComponent*> UEngineHelper::NDisplayCameras(AActor* actor)
{
	TArray<UActorComponent*> cameras;
	UClass* type = FindObject<UClass>(nullptr, TEXT("/Script/DisplayCluster.DisplayClusterICVFXCameraComponent"));
	if (IsValid(actor) && type)
		for (UActorComponent* component : actor->GetComponents())
			if (IsValid(component) && component->IsA(type)) cameras.Add(component);
	cameras.Sort([](const UActorComponent& a, const UActorComponent& b) { return a.GetName() < b.GetName(); });
	return cameras;
}

UActorComponent* UEngineHelper::NDisplayCamera(AActor* actor, int32 bank)
{
	if (bank < 0 || bank > 1) return nullptr;
	if (!_nDisplayCameraTargetsLoaded)
	{
		_nDisplayCameraTargetsLoaded = true;
		for (int32 slot = 0; slot < 2; ++slot)
		{
			FString saved;
			if (GConfig && GConfig->GetString(TEXT("Tangent.NDisplayTargets"), *FString::Printf(TEXT("Camera%d"), slot + 1), saved, GEditorPerProjectIni))
				_nDisplayCameraTargets[slot] = FName(*saved);
		}
	}
	const TArray<UActorComponent*> cameras = NDisplayCameras(actor);
	if (_nDisplayCameraTargets[bank].IsNone()) return cameras.IsValidIndex(bank) ? cameras[bank] : nullptr;
	for (UActorComponent* camera : cameras) if (camera->GetFName() == _nDisplayCameraTargets[bank]) return camera;
	return nullptr; // A removed named camera must never silently retarget another component.
}

void UEngineHelper::StepNDisplayCameraTarget(int32 bank, int32 step, bool reset)
{
	if (bank < 0 || bank > 1) return;
	AsyncTask(ENamedThreads::GameThread, [this, bank, step, reset]()
	{
		const TArray<UActorComponent*> cameras = NDisplayCameras(_selectedActor);
		if (!cameras.IsEmpty())
		{
			const int32 current = cameras.IndexOfByKey(NDisplayCamera(_selectedActor, bank));
			const int32 index = reset ? FMath::Min(bank, cameras.Num() - 1)
				: static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(FMath::Max(current, 0)) + step, 0, cameras.Num() - 1));
			_nDisplayCameraTargets[bank] = cameras[index]->GetFName();
			if (GConfig)
			{
				GConfig->SetString(TEXT("Tangent.NDisplayTargets"), *FString::Printf(TEXT("Camera%d"), bank + 1), *cameras[index]->GetName(), GEditorPerProjectIni);
				GConfig->Flush(false, GEditorPerProjectIni);
			}
		}
		RequestNDisplayCameraTarget(bank);
		ActorPropertyChangedEvent.Broadcast();
	});
}

void UEngineHelper::RequestNDisplayCameraTarget(int32 bank)
{
	if (bank < 0 || bank > 1) return;
	AsyncTask(ENamedThreads::GameThread, [this, bank]()
	{
		UActorComponent* camera = NDisplayCamera(_selectedActor, bank);
		FString name = camera ? camera->GetName() : TEXT("Camera unavailable");
		MenuStringEvent.Broadcast(bank == 0 ? TargetId::NDisplayCamera1Target : TargetId::NDisplayCamera2Target, name);
	});
}

bool UEngineHelper::NDisplayGradingPrefix(UObject* object, uint32 targetId, FString& prefix)
{
	const uint32 bank = targetId >> 24;
	AActor* actor = Cast<AActor>(object);
	if (!IsValid(actor) || bank > 4) return false;
	if (bank >= 3)
	{
		UActorComponent* camera = NDisplayCamera(actor, bank - 3);
		if (!camera) return false;
		prefix = FString::Printf(TEXT("@%s.CameraSettings.AllNodesColorGrading.ColorGradingSettings"), *camera->GetName());
		return true;
	}
	if (!actor->GetClass()->FindPropertyByName(TEXT("CurrentConfigData"))) return false;
	const uint32 family = (targetId >> 12) & 0xF;
	const bool viewport = bank > 0 || family == 4 || family == 6 || family == 8;
	FString scope = TEXT("EntireClusterColorGrading");
	if (viewport)
	{
		const int32 index = bank > 0 ? static_cast<int32>(bank - 1) : _nDisplayViewportIndex;
		if (object != _selectedActor || index < 0 || index >= NDisplayViewportCount()) return false;
		scope = FString::Printf(TEXT("PerViewportColorGrading[%d]"), index);
	}
	prefix = FString::Printf(TEXT("CurrentConfigData.StageSettings.%s.ColorGradingSettings"), *scope);
	return true;
}

PropertyData* UEngineHelper::FetchNDisplayControl(UObject* object, uint32 targetId)
{
	if (!TargetId::IsNDisplayParameter(targetId) && !TargetId::IsNDisplayCameraMenu(targetId)) return nullptr;
	FString prefix;
	if (!NDisplayGradingPrefix(object, targetId, prefix)) return nullptr;
	const uint32 family = (targetId >> 12) & 0xF;
	const bool hsv = family == 4 || family == 5;
	const uint32 section = (targetId >> 8) & 0xF;
	const uint32 setting = (targetId >> 4) & 0xF;
	const uint32 component = targetId & 0xF;
	if (TargetId::IsNDisplayCameraMenu(targetId) || ((targetId >> 24) >= 3 && !hsv && section == 0xF && setting == 0 && component == 4))
	{
		UActorComponent* camera = NDisplayCamera(Cast<AActor>(object), (targetId >> 24) - 3);
		static const TCHAR* members[] = { TEXT("bEnable"), TEXT("Border.Enable"), TEXT("Chromakey.bEnable"), TEXT("CustomFrustum.FieldOfViewMultiplier") };
		if (!camera || component < 1 || component > 4) return nullptr;
		return FetchPropertyData(object, FString::Printf(TEXT("@%s.CameraSettings.%s"), *camera->GetName(), members[component - 1]));
	}
	FString member;
	if (!hsv && section == 0xF && setting == 0 && component >= 1 && component <= 3)
	{
		static const TCHAR* scalars[] = { TEXT("WhiteBalance.WhiteTemp"), TEXT("WhiteBalance.WhiteTint"), TEXT("AutoExposureBias") };
		member = scalars[component - 1];
	}
	else
	{
		if (section < 1 || section > 4 || setting < 1 || setting > 5 || component < 1 || component > (hsv ? 3u : 4u)) return nullptr;
		static const TCHAR* sections[] = { TEXT("Global"), TEXT("Shadows"), TEXT("Midtones"), TEXT("Highlights") };
		static const TCHAR* settings[] = { TEXT("Saturation"), TEXT("Contrast"), TEXT("Gamma"), TEXT("Gain"), TEXT("Offset") };
		static const TCHAR* rgbChannels[] = { TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT("W") };
		static const TCHAR* hsvChannels[] = { TEXT("HSV_H"), TEXT("HSV_S"), TEXT("HSV_V") };
		member = FString::Printf(TEXT("%s.%s.%s"), sections[section - 1], settings[setting - 1],
			hsv ? hsvChannels[component - 1] : rgbChannels[component - 1]);
	}
	return FetchPropertyData(object, prefix + TEXT(".") + member);
}

bool UEngineHelper::ApplyNDisplayParameter(uint32 targetId, float delta, float& value)
{
	check(IsInGameThread());
	if (_saveWorldActive || !FMath::IsFinite(delta)) return false;
	PropertyData* data = FetchNDisplayControl(_selectedActor, targetId);
	if (!data) return false;
	// One mapped ID follows different per-viewport entries. Keep each entry's
	// reset baseline and double-tap timing separate, while reporting the real ID.
	const FString key = FString::Printf(TEXT("%u:%s"), _selectedActor->GetUniqueID(), *data->PropertyPath.ToString());
	uint32* resetTarget = _nDisplayResetTargets.Find(key);
	if (!resetTarget) resetTarget = &_nDisplayResetTargets.Add(key, _nextNDisplayResetTarget++);
	if (delta == 0) ResetCache::TapNow(*resetTarget);
	return data->AdjustParameterValue(this, _selectedActor, *resetTarget, delta, value);
}

void UEngineHelper::UpdateNDisplayParameter(uint32 targetId, float delta)
{
	AsyncTask(ENamedThreads::GameThread, [this, targetId, delta]()
	{
		float value;
		if (ApplyNDisplayParameter(targetId, delta, value)) ParameterValueEvent.Broadcast(targetId, value);
	});
}

void UEngineHelper::RequestNDisplayParameter(uint32 targetId)
{
	AsyncTask(ENamedThreads::GameThread, [this, targetId]()
	{
		PropertyData* data = FetchNDisplayControl(_selectedActor, targetId);
		float value;
		if (data && data->GetParameterValue(value)) ParameterValueEvent.Broadcast(targetId, value);
	});
}

int32 UEngineHelper::NDisplayViewportCount(FString* selectedName)
{
	if (!IsValid(_selectedActor) || !_selectedActor->GetClass()->FindPropertyByName(TEXT("CurrentConfigData"))) return 0;
	PropertyData* stage = FetchPropertyData(_selectedActor, FName(TEXT("CurrentConfigData.StageSettings")));
	FStructProperty* structure = stage ? CastField<FStructProperty>(stage->Prop) : nullptr;
	FArrayProperty* array = structure ? FindFProperty<FArrayProperty>(structure->Struct, TEXT("PerViewportColorGrading")) : nullptr;
	if (!array) return 0;
	FScriptArrayHelper entries(array, array->ContainerPtrToValuePtr<void>(stage->DataPtr));
	if (selectedName && entries.IsValidIndex(_nDisplayViewportIndex))
	{
		FStructProperty* element = CastField<FStructProperty>(array->Inner);
		FTextProperty* name = element ? FindFProperty<FTextProperty>(element->Struct, TEXT("Name")) : nullptr;
		*selectedName = name ? name->GetPropertyValue_InContainer(entries.GetRawPtr(_nDisplayViewportIndex)).ToString() : FString();
	}
	return entries.Num();
}

void UEngineHelper::StepNDisplayViewportGroup(int32 step, bool reset)
{
	AsyncTask(ENamedThreads::GameThread, [this, step, reset]()
	{
		const int32 count = NDisplayViewportCount();
		if (reset) _nDisplayViewportIndex = 0;
		else if (count > 0)
			_nDisplayViewportIndex = static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(_nDisplayViewportIndex) + step, 0, count - 1));
		RequestNDisplayViewportGroup();
		ActorPropertyChangedEvent.Broadcast(); // Refresh native channel/scalar displays for the selected group.
	});
}

void UEngineHelper::RequestNDisplayViewportGroup()
{
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		FString name;
		const int32 count = NDisplayViewportCount(&name);
		FString label = count == 0 ? TEXT("No groups") : _nDisplayViewportIndex >= count ? TEXT("Group removed")
			: FString::Printf(TEXT("%d: %s"), _nDisplayViewportIndex, name.IsEmpty() ? TEXT("Unnamed") : *name);
		MenuStringEvent.Broadcast(TargetId::NDisplayViewportGroup, label);
	});
}

PropertyData* UEngineHelper::FetchNDisplayGrading(uint32 targetId)
{
	if (!TargetId::IsNDisplayGrading(targetId)) return nullptr;
	FString prefix;
	if (!NDisplayGradingPrefix(_selectedActor, targetId, prefix)) return nullptr;
	const uint32 component = TargetId::GradingComponent(targetId);
	const uint32 section = (targetId >> 8) & 0xF;
	const uint32 setting = (targetId >> 4) & 0xF;
	if (component < 1 || component > 3 || section < 1 || section > 4 || setting < 1 || setting > 5) return nullptr;
	static const TCHAR* sections[] = { TEXT("Global"), TEXT("Shadows"), TEXT("Midtones"), TEXT("Highlights") };
	static const TCHAR* settings[] = { TEXT("Saturation"), TEXT("Contrast"), TEXT("Gamma"), TEXT("Gain"), TEXT("Offset") };
	const FString path = FString::Printf(TEXT("%s.%s.%s"), *prefix, sections[section - 1], settings[setting - 1]);
	PropertyData* data = FetchPropertyData(_selectedActor, path);
	FStructProperty* structure = data ? CastField<FStructProperty>(data->Prop) : nullptr;
	return structure && structure->Struct == TBaseStructure<FVector4>::Get() ? data : nullptr;
}

bool UEngineHelper::ApplyNDisplayGradingXYZ(uint32 targetId, float delta)
{
	check(IsInGameThread());
	if (_saveWorldActive || !FMath::IsFinite(delta)) return false;
	PropertyData* data = FetchNDisplayGrading(targetId);
	if (!data) return false;
	// Snapshot before notifications: listeners may refresh property caches.
	const PropertyData edit = *data;
	const FVector4 current = *static_cast<FVector4*>(edit.DataPtr);
	const bool offset = TargetId::IsGradingOffset(targetId);
	FLinearColor rgb(current.X, current.Y, current.Z, 1);
	if (offset) { rgb.R += 1; rgb.G += 1; rgb.B += 1; }
	FLinearColor hsv = rgb.LinearRGBToHSV();
	FVector2D xy;
	ConvertHSToXY(hsv, xy);
	const uint32 axis = TargetId::GradingComponent(targetId);
	double value = axis == 1 ? xy.X : axis == 2 ? xy.Y : hsv.B;
	const FString resetKey = FString::Printf(TEXT("%u:%s:%u"), _selectedActor->GetUniqueID(), *edit.PropertyPath.ToString(), axis);
	const double initial = value;
	if (delta == 0)
	{
		if (ResetCache::IsSingleTap(targetId))
		{
			const double* cached = _nDisplayWheelInitials.Find(resetKey);
			if (!cached) return false;
			value = *cached;
		}
		else value = axis == 3 ? 1.0 : 0.0;
	}
	else value += axis == 2 ? -delta : delta;
	if (axis == 1) xy.X = value;
	else if (axis == 2) xy.Y = value;
	else hsv.B = static_cast<float>(value);
	ConvertXYToHS(xy, hsv);
	hsv.R = FMath::Fmod(hsv.R + 360.f, 360.f);
	hsv.G = FMath::Clamp(hsv.G, 0.f, 1.f);
	hsv.B = FMath::Clamp(hsv.B, 0.f, 2.f);
	rgb = hsv.HSVToLinearRGB();
	if (offset) { rgb.R -= 1; rgb.G -= 1; rgb.B -= 1; }
	const FVector4 updated(rgb.R, rgb.G, rgb.B, current.W);
	if (!OnPrePropertyChange(_selectedActor, edit, TEXT("Edit nDisplay color wheel"))) return false;
	if (delta != 0 && !_nDisplayWheelInitials.Contains(resetKey)) _nDisplayWheelInitials.Add(resetKey, initial);
	*static_cast<FVector4*>(edit.DataPtr) = updated;
	OnPostPropertyChange(_selectedActor, edit);
	return true;
}
