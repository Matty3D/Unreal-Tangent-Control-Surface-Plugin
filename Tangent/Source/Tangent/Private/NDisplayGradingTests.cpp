#include "EngineHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "ResetCache.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentNDisplayCombinedTest, "Tangent.Grading.NDisplayCombined",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentNDisplayCombinedTest::RunTest(const FString&)
{
	if (GEditor->IsTransactionActive()) { AddError(TEXT("Run outside an active transaction")); return false; }
	FModuleManager::Get().LoadModule(TEXT("DisplayCluster"));
	UClass* rootClass = LoadObject<UClass>(nullptr, TEXT("/Script/DisplayCluster.DisplayClusterRootActor"));
	UClass* configClass = LoadObject<UClass>(nullptr, TEXT("/Script/DisplayClusterConfiguration.DisplayClusterConfigurationData"));
	if (!TestNotNull(TEXT("Load nDisplay root class"), rootClass) || !TestNotNull(TEXT("Load config class"), configClass)) return false;
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	AActor* actor = world->SpawnActor<AActor>(rootClass);
	actor->SetFlags(RF_Transactional);
	UObject* config = NewObject<UObject>(actor, configClass, NAME_None, RF_Transactional);
	FObjectPropertyBase* configProperty = FindFProperty<FObjectPropertyBase>(rootClass, TEXT("CurrentConfigData"));
	if (!TestNotNull(TEXT("Root owns current config"), configProperty)) { world->DestroyWorld(false); return false; }
	// Native nDisplay CDOs have no embedded config. A real configured actor does;
	// supply a transient template and empty scene/cluster for reconstruction.
	UObject* cdo = rootClass->GetDefaultObject();
	TStrongObjectPtr<UObject> originalDefault(configProperty->GetObjectPropertyValue_InContainer(cdo));
	UObject* defaults = NewObject<UObject>(GetTransientPackage(), configClass, NAME_None, RF_Transient);
	auto InitializeConfig = [](UObject* object)
	{
		for (const TCHAR* field : { TEXT("Scene"), TEXT("Cluster") })
		{
			FObjectPropertyBase* property = FindFProperty<FObjectPropertyBase>(object->GetClass(), field);
			if (property) property->SetObjectPropertyValue_InContainer(object,
				NewObject<UObject>(object, property->PropertyClass, NAME_None, RF_Transient | RF_Transactional));
		}
	};
	InitializeConfig(defaults);
	InitializeConfig(config);
	configProperty->SetObjectPropertyValue_InContainer(cdo, defaults);
	ON_SCOPE_EXIT { configProperty->SetObjectPropertyValue_InContainer(cdo, originalDefault.Get()); };
	configProperty->SetObjectPropertyValue_InContainer(actor, config);
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	helper->_selectedActor = actor;
	PropertyData* stage = helper->FetchPropertyData(actor, FName(TEXT("CurrentConfigData.StageSettings")));
	FStructProperty* stageProperty = stage ? CastField<FStructProperty>(stage->Prop) : nullptr;
	if (!TestNotNull(TEXT("Resolve actual stage settings"), stageProperty)) { helper->_selectedActor = nullptr; world->DestroyWorld(false); return false; }
	FArrayProperty* array = FindFProperty<FArrayProperty>(stageProperty->Struct, TEXT("PerViewportColorGrading"));
	if (!TestNotNull(TEXT("Per-viewport array exists"), array)) { helper->_selectedActor = nullptr; world->DestroyWorld(false); return false; }
	FScriptArrayHelper entries(array, array->ContainerPtrToValuePtr<void>(stage->DataPtr));
	entries.Resize(2);
	FStructProperty* element = CastFieldChecked<FStructProperty>(array->Inner);
	FTextProperty* name = FindFProperty<FTextProperty>(element->Struct, TEXT("Name"));
	if (name) name->SetPropertyValue_InContainer(entries.GetRawPtr(1), FText::FromString(TEXT("Wall B")));
	helper->_nDisplayViewportIndex = 1;
	FString selectedName;
	TestEqual(TEXT("Selector discovers grading groups"), helper->NDisplayViewportCount(&selectedName), 2);
	TestEqual(TEXT("Selector reads group name"), selectedName, FString(TEXT("Wall B")));
	int32 configNotifications = 0;
	FDelegateHandle listener = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
		[&](UObject* object, FPropertyChangedEvent&) { if (object == config) ++configNotifications; });
	for (uint32 scope : { 0x00039000u, 0x00038000u })
		for (uint32 section = 1; section <= 4; ++section)
			for (uint32 setting = 1; setting <= 5; ++setting)
			{
				const uint32 id = scope | (section << 8) | (setting << 4) | 1;
				TestTrue(TEXT("New IDs dispatch as XYZ Combined controls"), TargetId::IsGradingXYZ(id) && TargetId::IsNDisplayGrading(id));
				PropertyData* data = helper->FetchNDisplayGrading(id);
				if (!TestNotNull(TEXT("Resolve every scope/section/setting"), data)) continue;
				TestTrue(TEXT("Real configuration UObject is value owner"), data->EditObjects.Last().Get() == config);
				if (scope == 0x00038000)
					TestEqual(TEXT("Viewport notifications retain index"), data->ArrayIndices.FindRef(TEXT("PerViewportColorGrading")), 1);
				const bool offset = setting == 5;
				*static_cast<FVector4*>(data->DataPtr) = offset ? FVector4(0, 0, 0, 0.37) : FVector4(1, 1, 1, 0.37);
				for (uint32 axis = 1; axis <= 3; ++axis)
				{
					TestTrue(TEXT("Each Combined axis changes the vector"), helper->ApplyNDisplayGradingXYZ((id & ~0xFu) | axis, 0.1f));
					data = helper->FetchNDisplayGrading(id);
					const FVector4 result = *static_cast<FVector4*>(data->DataPtr);
					TestEqual(TEXT("Combined preserves luminance W"), result.W, 0.37);
					TestTrue(TEXT("Stored RGB finite"), FMath::IsFinite(result.X) && FMath::IsFinite(result.Y) && FMath::IsFinite(result.Z));
				}
			}
	TestTrue(TEXT("Config receives chained edit notifications"), configNotifications > 0);
	helper->OnPostChangeCompletion();
	TestFalse(TEXT("Combined edit transaction completes"), GEditor->IsTransactionActive());
	const uint32 clusterGain = 0x00039143;
	PropertyData* gain = helper->FetchNDisplayGrading(clusterGain);
	const FVector4 beforeUndo = *static_cast<FVector4*>(gain->DataPtr);
	TestTrue(TEXT("Start independent edit"), helper->ApplyNDisplayGradingXYZ(clusterGain, 0.2f));
	helper->OnPostChangeCompletion();
	GEditor->UndoTransaction();
	helper->_selectedActor = actor; // Undo can clear editor selection in a preview world.
	gain = helper->FetchNDisplayGrading(clusterGain);
	TestTrue(TEXT("Undo restores nDisplay vector"), gain && static_cast<FVector4*>(gain->DataPtr)->Equals(beforeUndo, 1.e-5));
	GEditor->RedoTransaction();
	helper->_selectedActor = actor;
	gain = helper->FetchNDisplayGrading(clusterGain);
	TestTrue(TEXT("Redo restores brightness"), gain && static_cast<FVector4*>(gain->DataPtr)->Y > beforeUndo.Y);
	// Reset values must not leak between grading groups using the same mapped control.
	const uint32 viewportValue = 0x00038143;
	helper->_nDisplayWheelInitials.Empty();
	helper->_nDisplayViewportIndex = 0;
	*static_cast<FVector4*>(helper->FetchNDisplayGrading(viewportValue)->DataPtr) = FVector4(0.5, 0.5, 0.5, 0.25);
	TestTrue(TEXT("Edit first group"), helper->ApplyNDisplayGradingXYZ(viewportValue, 0.1f));
	helper->_nDisplayViewportIndex = 1;
	*static_cast<FVector4*>(helper->FetchNDisplayGrading(viewportValue)->DataPtr) = FVector4(1, 1, 1, 0.75);
	TestTrue(TEXT("Edit second group"), helper->ApplyNDisplayGradingXYZ(viewportValue, 0.1f));
	ResetCache::TapNow(viewportValue);
	TestTrue(TEXT("Reset second group"), helper->ApplyNDisplayGradingXYZ(viewportValue, 0));
	TestTrue(TEXT("Second group has its own baseline"), FMath::IsNearlyEqual(static_cast<FVector4*>(helper->FetchNDisplayGrading(viewportValue)->DataPtr)->X, 1.0, 1.e-5));
	helper->_nDisplayViewportIndex = 0;
	TestTrue(TEXT("Reset first group"), helper->ApplyNDisplayGradingXYZ(viewportValue, 0));
	TestTrue(TEXT("First group baseline retained"), FMath::IsNearlyEqual(static_cast<FVector4*>(helper->FetchNDisplayGrading(viewportValue)->DataPtr)->X, 0.5, 1.e-5));
	ResetCache::TapNow(viewportValue);
	TestTrue(TEXT("Double reset uses neutral brightness"), helper->ApplyNDisplayGradingXYZ(viewportValue, 0));
	TestTrue(TEXT("Neutral value is one"), FMath::IsNearlyEqual(static_cast<FVector4*>(helper->FetchNDisplayGrading(viewportValue)->DataPtr)->X, 1.0, 1.e-5));
	helper->OnPostChangeCompletion();
	// Native individual controls share the wheel's scopes and selector, and use
	// the same reflected-property writer for feedback, resets and notifications.
	for (uint32 family : { 0x00037000u, 0x00036000u, 0x00035000u, 0x00034000u })
		for (uint32 section = 1; section <= 4; ++section)
			for (uint32 setting = 1; setting <= 5; ++setting)
				for (uint32 channel = 1; channel <= (family >= 0x36000 ? 4u : 3u); ++channel)
				{
					const uint32 id = family | (section << 8) | (setting << 4) | channel;
					TestTrue(TEXT("Individual ID classified without wheel dispatch"), TargetId::IsNDisplayParameter(id) && !TargetId::IsGradingXYZ(id) && !TargetId::IsGradingHSV(id));
					PropertyData* control = helper->FetchNDisplayControl(actor, id);
					float reported = 0;
					TestTrue(TEXT("Every native color channel resolves and reads"), control && control->GetParameterValue(reported));
					TestTrue(TEXT("Every native color channel can be adjusted"), helper->ApplyNDisplayParameter(id, 0.001f, reported));
				}
	float reported = 0;
	gain = helper->FetchNDisplayGrading(clusterGain);
	*static_cast<FVector4*>(gain->DataPtr) = FVector4(0.4, 0.6, 0.8, 0.75);
	TestTrue(TEXT("Adjust native gain R"), helper->ApplyNDisplayParameter(0x00037141, 0.1f, reported));
	FVector4 color = *static_cast<FVector4*>(helper->FetchNDisplayGrading(clusterGain)->DataPtr);
	TestTrue(TEXT("R changes independently"), FMath::IsNearlyEqual(color.X, 0.5, 1.e-5) && color.Y == 0.6 && color.Z == 0.8 && color.W == 0.75);
	*static_cast<FVector4*>(helper->FetchNDisplayGrading(clusterGain)->DataPtr) = FVector4(1, 1, 1, 0.75);
	TestTrue(TEXT("Set native gain H on gray"), helper->ApplyNDisplayParameter(0x00035141, 120.f, reported));
	TestTrue(TEXT("Native saturation shares achromatic hue"), helper->ApplyNDisplayParameter(0x00035142, 0.5f, reported));
	color = *static_cast<FVector4*>(helper->FetchNDisplayGrading(clusterGain)->DataPtr);
	TestTrue(TEXT("Native HSV produces green and preserves Y"), FMath::IsNearlyEqual(color.X, 0.5, 1.e-5) && FMath::IsNearlyEqual(color.Y, 1.0, 1.e-5) && FMath::IsNearlyEqual(color.Z, 0.5, 1.e-5) && color.W == 0.75);
	for (uint32 scope : { 0x00037000u, 0x00036000u })
	{
		for (uint32 scalar = 1; scalar <= 3; ++scalar)
		{
			const uint32 id = scope | 0xF00 | scalar;
			PropertyData* field = helper->FetchNDisplayControl(actor, id);
			float before = 0;
			TestTrue(TEXT("White balance/exposure resolves"), field && field->GetParameterValue(before));
			const float delta = scalar == 1 ? 100.f : 0.1f;
			TestTrue(TEXT("White balance/exposure adjusts"), helper->ApplyNDisplayParameter(id, delta, reported));
			TestTrue(TEXT("Scalar feedback reports stored value"), FMath::IsNearlyEqual(reported, before + delta, 1.e-4f));
		}
	}
	// Selecting another group must not reuse a native channel's reset baseline.
	helper->_nDisplayResetTargets.Empty();
	helper->_nDisplayViewportIndex = 0;
	PropertyData* exposure = helper->FetchNDisplayControl(actor, 0x00036F03);
	CastFieldChecked<FFloatProperty>(exposure->Prop)->SetFloatingPointPropertyValue(exposure->DataPtr, 2.f);
	TestTrue(TEXT("Adjust viewport zero exposure"), helper->ApplyNDisplayParameter(0x00036F03, 0.25f, reported));
	helper->_nDisplayViewportIndex = 1;
	exposure = helper->FetchNDisplayControl(actor, 0x00036F03);
	float secondInitial = 0;
	exposure->GetParameterValue(secondInitial);
	TestTrue(TEXT("Adjust viewport one exposure"), helper->ApplyNDisplayParameter(0x00036F03, 0.25f, reported));
	TestTrue(TEXT("Reset viewport one exposure"), helper->ApplyNDisplayParameter(0x00036F03, 0, reported));
	TestTrue(TEXT("Reset is per property path"), FMath::IsNearlyEqual(reported, secondInitial, 1.e-4f));
	PropertyData* freeze = helper->FetchPropertyData(actor, static_cast<uint32>(TargetId::NDisplayFreezeViewports));
	FString menuText;
	TestTrue(TEXT("Freeze maps to native outer viewport field"), freeze && freeze->PropertyPath.ToString() == TEXT("CurrentConfigData.StageSettings.bFreezeRenderOuterViewports"));
	TestTrue(TEXT("Freeze toggle on"), freeze && freeze->AdjustMenuValue(helper.Get(), actor, 1, menuText));
	freeze = helper->FetchPropertyData(actor, static_cast<uint32>(TargetId::NDisplayFreezeViewports));
	TestTrue(TEXT("Native freeze enabled"), CastFieldChecked<FBoolProperty>(freeze->Prop)->GetPropertyValue(freeze->DataPtr));
	TestTrue(TEXT("Freeze toggle off"), freeze->AdjustMenuValue(helper.Get(), actor, 1, menuText));
	freeze = helper->FetchPropertyData(actor, static_cast<uint32>(TargetId::NDisplayFreezeViewports));
	TestFalse(TEXT("Native freeze disabled"), CastFieldChecked<FBoolProperty>(freeze->Prop)->GetPropertyValue(freeze->DataPtr));
	helper->OnPostChangeCompletion();
	// Undo/redo can replace array storage; re-resolve before removing entries.
	// Fixed viewport banks ignore the shared selector and remain separately bindable.
	helper->_nDisplayViewportIndex = 1;
	PropertyData* fixedZero = helper->FetchNDisplayGrading(0x01038141);
	PropertyData* fixedOne = helper->FetchNDisplayGrading(0x02038141);
	TestTrue(TEXT("Static viewport zero resolves entry zero"), fixedZero && fixedZero->ArrayIndices.FindRef(TEXT("PerViewportColorGrading")) == 0);
	TestTrue(TEXT("Static viewport one resolves entry one"), fixedOne && fixedOne->ArrayIndices.FindRef(TEXT("PerViewportColorGrading")) == 1);
	if (fixedZero && fixedOne)
	{
		*static_cast<FVector4*>(fixedZero->DataPtr) = FVector4(1, 1, 1, 0.25);
		*static_cast<FVector4*>(fixedOne->DataPtr) = FVector4(0.5, 0.5, 0.5, 0.75);
		TestTrue(TEXT("Fixed viewport control adjusts entry zero"), helper->ApplyNDisplayParameter(0x01036141, 0.1f, reported));
		TestTrue(TEXT("Entry one is untouched"), static_cast<FVector4*>(helper->FetchNDisplayGrading(0x02038141)->DataPtr)->X == 0.5);
	}
	// Use real ICVFX component classes, with independently named targets.
	UClass* cameraClass = LoadObject<UClass>(nullptr, TEXT("/Script/DisplayCluster.DisplayClusterICVFXCameraComponent"));
	TestNotNull(TEXT("ICVFX class exists"), cameraClass);
	if (cameraClass)
	{
		UActorComponent* f2 = NewObject<UActorComponent>(actor, cameraClass, TEXT("F2"), RF_Transactional);
		UActorComponent* f1 = NewObject<UActorComponent>(actor, cameraClass, TEXT("F1"), RF_Transactional);
		actor->AddInstanceComponent(f2); actor->AddInstanceComponent(f1);
		helper->_nDisplayCameraTargetsLoaded = true; // Tests must not consume user-saved selectors.
		TestTrue(TEXT("Camera one defaults to first sorted name"), helper->NDisplayCamera(actor, 0) == f1);
		TestTrue(TEXT("Camera two defaults to second sorted name"), helper->NDisplayCamera(actor, 1) == f2);
		for (uint32 bank : { 3u, 4u })
		{
			const uint32 tag = bank << 24;
			for (uint32 section = 1; section <= 4; ++section)
				for (uint32 setting = 1; setting <= 5; ++setting)
				{
					const uint32 wheelId = tag | 0x00039001 | (section << 8) | (setting << 4);
					TestTrue(TEXT("Camera Combined ID dispatches"), TargetId::IsGradingXYZ(wheelId) && TargetId::IsNDisplayGrading(wheelId));
					TestNotNull(TEXT("All camera grading vectors resolve"), helper->FetchNDisplayGrading(wheelId));
					for (uint32 family : { 0x37000u, 0x35000u })
						for (uint32 channel = 1; channel <= (family == 0x37000 ? 4u : 3u); ++channel)
							TestNotNull(TEXT("Camera individual channels resolve"), helper->FetchNDisplayControl(actor, tag | family | (section << 8) | (setting << 4) | channel));
				}
			for (uint32 scalar = 1; scalar <= 4; ++scalar)
			{
				TestNotNull(TEXT("Camera temperature/tint/exposure/overscan resolves"), helper->FetchNDisplayControl(actor, tag | 0x00037F00 | scalar));
				TestTrue(TEXT("Camera scalar adjusts"), helper->ApplyNDisplayParameter(tag | 0x00037F00 | scalar, 0.01f, reported));
			}
			for (uint32 flag = 1; flag <= 3; ++flag)
			{
				PropertyData* toggle = helper->FetchPropertyData(actor, tag | 0x00040090 | flag);
				TestTrue(TEXT("Camera inner/border/chromakey menu toggles"), toggle && toggle->AdjustMenuValue(helper.Get(), actor, 1, menuText));
			}
		}
		PropertyData* cameraGainOne = helper->FetchNDisplayGrading(0x03039141);
		PropertyData* cameraGainTwo = helper->FetchNDisplayGrading(0x04039141);
		if (cameraGainOne && cameraGainTwo)
		{
			*static_cast<FVector4*>(cameraGainOne->DataPtr) = FVector4(1, 1, 1, 0.2);
			*static_cast<FVector4*>(cameraGainTwo->DataPtr) = FVector4(0.5, 0.5, 0.5, 0.8);
			TestTrue(TEXT("Camera one RGB edits independently"), helper->ApplyNDisplayParameter(0x03037141, 0.1f, reported));
			TestEqual(TEXT("Camera two remains unchanged"), static_cast<FVector4*>(helper->FetchNDisplayGrading(0x04039141)->DataPtr)->X, 0.5);
			TestTrue(TEXT("Camera one Combined works"), helper->ApplyNDisplayGradingXYZ(0x03039141, 0.1f));
			TestEqual(TEXT("Camera Combined preserves fourth component"), static_cast<FVector4*>(helper->FetchNDisplayGrading(0x03039141)->DataPtr)->W, 0.2);
		}
		helper->_nDisplayCameraTargets[0] = TEXT("F2");
		TestTrue(TEXT("Camera bank can select any named component"), helper->NDisplayCamera(actor, 0) == f2);
		helper->_nDisplayCameraTargets[0] = TEXT("RemovedCamera");
		TestFalse(TEXT("Missing named camera never retargets silently"), helper->ApplyNDisplayParameter(0x03037141, 0.1f, reported));
		helper->_nDisplayCameraTargets[0] = TEXT("F1");
	}
	helper->OnPostChangeCompletion();
	stage = helper->FetchPropertyData(actor, FName(TEXT("CurrentConfigData.StageSettings")));
	FScriptArrayHelper currentEntries(array, array->ContainerPtrToValuePtr<void>(stage->DataPtr));
	currentEntries.Resize(0);
	TestFalse(TEXT("Deleted group rejects native channel input"), helper->ApplyNDisplayParameter(0x00036141, 0.1f, reported));
	TestFalse(TEXT("Deleted array entry rejects input"), helper->ApplyNDisplayGradingXYZ(viewportValue, 0.1f));
	helper->_saveWorldActive.AtomicSet(true);
	TestFalse(TEXT("Save blocks edits"), helper->ApplyNDisplayGradingXYZ(clusterGain, 0.1f));
	helper->_saveWorldActive.AtomicSet(false);
	TestFalse(TEXT("Invalid target rejected"), helper->ApplyNDisplayGradingXYZ(0x0003914F, 0.1f));
	helper->_selectedActor = nullptr;
	TestFalse(TEXT("No selection rejects input"), helper->ApplyNDisplayGradingXYZ(clusterGain, 0.1f));
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(listener);
	world->DestroyWorld(false);
	return true;
}
#endif
