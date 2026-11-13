// Regression coverage for object/array property mappings and editor transactions.
#include "EngineHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor/Transactor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "ResetCache.h"
#include "Widgets/ColorGrading/SColorGradingComponentViewer.h"
#include "Layout/WidgetPath.h"
#include "TangentSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"

namespace
{
	class FLimitTestChanges : public PropertyChangeHelper
	{
	public:
		bool OnPrePropertyChange(AActor*, const PropertyData&, const FString&) override { return true; }
		void OnPostPropertyChange(AActor*, const PropertyData&) override {}
		void OnPreChange(AActor*, FProperty*, const FString&) override {}
		void OnPostChange(AActor*, FProperty*) override {}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentLimitSettingTest, "Tangent.Settings.PropertyLimitsToggleAndPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentLimitSettingTest::RunTest(const FString&)
{
	UTangentSettings* settings = GetMutableDefault<UTangentSettings>();
	TGuardValue<bool> restoreSetting(settings->bEnforcePropertyLimits, true);
	TestEqual(TEXT("Project Settings container"), settings->GetContainerName(), FName(TEXT("Project")));
	TestEqual(TEXT("Plugins category"), settings->GetCategoryName(), FName(TEXT("Plugins")));
	TestEqual(TEXT("Tangent section"), settings->GetSectionName(), FName(TEXT("Tangent")));
	TestEqual(TEXT("Engine config"), settings->GetClass()->ClassConfigName, FName(TEXT("Engine")));
	TestTrue(TEXT("Project default config flag"), settings->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig));
	TestEqual(TEXT("DefaultEngine.ini destination"), FPaths::GetCleanFilename(settings->GetDefaultConfigFilename()), FString(TEXT("DefaultEngine.ini")));

	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ON_SCOPE_EXIT { world->DestroyWorld(false); };
	ACameraActor* actor = world->SpawnActor<ACameraActor>();
	FLimitTestChanges changes;
	float number = 0, defaults = 10, reported = 0;
	uint32 target = 0xFB000000;
	TUniquePtr<FFloatProperty> property(static_cast<FFloatProperty*>(FFloatProperty::Construct(actor->GetClass(), TEXT("TangentLimitToggle"), RF_NoFlags)));
	property->SetMetaData(TEXT("ClampMin"), TEXT("-1"));
	property->SetMetaData(TEXT("ClampMax"), TEXT("1"));
	PropertyData data(TEXT("Toggle"), property.Get(), &number, &defaults);
	auto Adjust = [&](float delta) { return data.AdjustParameterValue(&changes, actor, ++target, delta, reported); };
	TestTrue(TEXT("Enabled adjustment"), Adjust(10));
	TestEqual(TEXT("Enabled clamps"), number, 1.f);
	settings->bEnforcePropertyLimits = false;
	TestTrue(TEXT("Disable affects the existing mapping immediately"), Adjust(10));
	TestEqual(TEXT("Disabled permits values above max"), number, 11.f);
	TestTrue(TEXT("Disabled lower adjustment"), Adjust(-30));
	TestEqual(TEXT("Disabled permits values below min"), number, -19.f);
	++target; ResetCache::TapNow(target); ResetCache::TapNow(target);
	TestTrue(TEXT("Reset with limits disabled"), data.AdjustParameterValue(&changes, actor, target, 0, reported));
	TestEqual(TEXT("Disabled reset restores out-of-range default"), number, defaults);
	property->RemoveMetaData(TEXT("ClampMin")); property->RemoveMetaData(TEXT("ClampMax"));
	property->SetMetaData(TEXT("UIMin"), TEXT("0")); property->SetMetaData(TEXT("UIMax"), TEXT("1"));
	TestTrue(TEXT("Disabled UI range adjustment"), Adjust(10));
	TestEqual(TEXT("Disabled also bypasses UI-only bounds"), number, 20.f);
	data.Clipped = true; data.Minimum = -2; data.Maximum = 2;
	TestTrue(TEXT("Explicit plugin bounds remain active"), Adjust(10));
	TestEqual(TEXT("Explicit bounds still clamp"), number, 2.f);
	data.Clipped = false;
	settings->bEnforcePropertyLimits = true;
	TestTrue(TEXT("Re-enable affects the same mapping immediately"), Adjust(10));
	TestEqual(TEXT("Re-enabled clamps"), number, 1.f);

	settings->bEnforcePropertyLimits = false;
	TUniquePtr<FByteProperty> byteProperty(static_cast<FByteProperty*>(FByteProperty::Construct(actor->GetClass(), TEXT("TangentLimitToggleByte"), RF_NoFlags)));
	uint8 byte = 250;
	PropertyData byteData(TEXT("ByteToggle"), byteProperty.Get(), &byte, nullptr);
	TestTrue(TEXT("Disabled still accepts safe byte adjustment"), byteData.AdjustParameterValue(&changes, actor, ++target, 1000, reported));
	TestEqual(TEXT("Disabled still prevents byte overflow"), byte, uint8(255));

	// Exercise Unreal's default-config save and reload without touching the project's config.
	const FString configFile = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("TangentSetting-"), TEXT(".ini"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*configFile); };
	if (TestTrue(TEXT("Save disabled setting"), settings->TryUpdateDefaultConfigFile(configFile)))
	{
		FConfigFile serialized;
		serialized.Read(configFile);
		bool savedValue = true;
		TestTrue(TEXT("Setting has an INI entry"), serialized.GetBool(TEXT("/Script/Tangent.TangentSettings"), TEXT("bEnforcePropertyLimits"), savedValue));
		TestFalse(TEXT("Disabled value is persisted"), savedValue);
		settings->bEnforcePropertyLimits = true;
		settings->ReloadConfig(UTangentSettings::StaticClass(), *configFile);
		TestFalse(TEXT("Reload restores the persisted choice"), settings->bEnforcePropertyLimits);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentWorldCleanupTest, "Tangent.Lifecycle.WorldCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentWorldCleanupTest::RunTest(const FString&)
{
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ACameraActor* camera = world->SpawnActor<ACameraActor>();
	TWeakObjectPtr<UWorld> oldWorld(world);
	TWeakObjectPtr<ACameraActor> oldCamera(camera);
	helper->UpdateSelectionCaches(camera);
	helper->UpdateSelectionCaches(nullptr);
	TestEqual(TEXT("Deselecting preserves a live camera for relative transforms"), helper->_lastSelectedCamera.Get(), camera);
	helper->_selectedActor = camera;
	helper->_selectedComponent = camera->GetCameraComponent();

	UWorld* unrelatedWorld = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	unrelatedWorld->DestroyWorld(false);
	TestEqual(TEXT("Unrelated world cleanup preserves selection"), helper->_selectedActor, static_cast<AActor*>(camera));
	TestEqual(TEXT("Unrelated world cleanup preserves camera"), helper->_lastSelectedCamera.Get(), camera);

	world->DestroyWorld(false);
	TestNull(TEXT("World cleanup releases selected actor"), helper->_selectedActor);
	TestNull(TEXT("World cleanup releases selected component"), helper->_selectedComponent);
	TestFalse(TEXT("World cleanup clears cached camera"), helper->_lastSelectedCamera.IsValid());
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Helper does not retain the old camera"), oldCamera.IsValid());
	TestFalse(TEXT("Helper does not retain the old world"), oldWorld.IsValid());

	// A weak cache must also allow GC without relying on cleanup notification order.
	world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	camera = world->SpawnActor<ACameraActor>();
	oldWorld = world;
	oldCamera = camera;
	helper->UpdateSelectionCaches(camera);
	FWorldDelegates::OnWorldCleanup.RemoveAll(helper.Get());
	world->DestroyWorld(false);
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Camera cache alone cannot retain a camera"), oldCamera.IsValid());
	TestFalse(TEXT("Camera cache alone cannot retain a world"), oldWorld.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentPropertyLimitsTest, "Tangent.Properties.NumericLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentPropertyLimitsTest::RunTest(const FString&)
{
	TGuardValue<bool> enforceLimits(GetMutableDefault<UTangentSettings>()->bEnforcePropertyLimits, true);
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ACameraActor* actor = world->SpawnActor<ACameraActor>();
	FLimitTestChanges changes;
	float reported = 0;
	uint32 target = 0xFA000000;
	// Synthetic reflected fields exercise Blueprint-style user metadata without altering assets.
	TUniquePtr<FFloatProperty> floatPropertyStorage(static_cast<FFloatProperty*>(FFloatProperty::Construct(actor->GetClass(), TEXT("TangentTestFloat"), RF_NoFlags)));
	FFloatProperty& floatProperty = *floatPropertyStorage;
	floatProperty.SetMetaData(TEXT("ClampMin"), TEXT("-2"));
	floatProperty.SetMetaData(TEXT("ClampMax"), TEXT("3"));
	floatProperty.SetMetaData(TEXT("UIMin"), TEXT("0"));
	floatProperty.SetMetaData(TEXT("UIMax"), TEXT("1"));
	float number = 0, defaults = 100;
	PropertyData data(TEXT("TangentTestFloat"), &floatProperty, &number, &defaults);
	auto Adjust = [&](float delta)
	{
		return data.AdjustParameterValue(&changes, actor, ++target, delta, reported);
	};
	TestTrue(TEXT("Upper bound adjustment succeeds"), Adjust(100));
	TestEqual(TEXT("Hard max takes precedence over UI max"), number, 3.f);
	TestEqual(TEXT("Panel feedback uses clamped value"), reported, number);
	TestTrue(TEXT("Repeated overshoot"), Adjust(100));
	TestEqual(TEXT("Repeated overshoot stays at max"), number, 3.f);
	TestTrue(TEXT("Reverse away from limit"), Adjust(-0.25f));
	TestEqual(TEXT("Reverse responds immediately"), number, 2.75f);
	TestTrue(TEXT("Lower bound adjustment"), Adjust(-100));
	TestEqual(TEXT("Hard min takes precedence over UI min"), number, -2.f);
	data.Clipped = true; data.Minimum = -1; data.Maximum = 2;
	TestTrue(TEXT("Explicit mapping max"), Adjust(100));
	TestEqual(TEXT("Narrower mapping max wins"), number, 2.f);
	TestTrue(TEXT("Explicit mapping min"), Adjust(-100));
	TestEqual(TEXT("Narrower mapping min wins"), number, -1.f);
	data.Minimum = -100; data.Maximum = 100;
	TestTrue(TEXT("Wide mapping cannot bypass property"), Adjust(100));
	TestEqual(TEXT("Property max still applies"), number, 3.f);
	data.Minimum = 10; data.Maximum = 20;
	TestFalse(TEXT("Disjoint ranges are rejected"), Adjust(1));
	TestEqual(TEXT("Disjoint ranges do not write"), number, 3.f);
	data.Clipped = false;
	++target; ResetCache::TapNow(target); ResetCache::TapNow(target);
	TestTrue(TEXT("Reset is constrained"), data.AdjustParameterValue(&changes, actor, target, 0, reported));
	TestEqual(TEXT("Out-of-range default is clamped"), number, 3.f);
	floatProperty.RemoveMetaData(TEXT("ClampMin")); floatProperty.RemoveMetaData(TEXT("ClampMax"));
	TestTrue(TEXT("UI-only range is enforced"), Adjust(-100));
	TestEqual(TEXT("UI-only minimum"), number, 0.f);
	TestTrue(TEXT("UI-only upper bound"), Adjust(100));
	TestEqual(TEXT("UI-only maximum"), number, 1.f);
	floatProperty.RemoveMetaData(TEXT("UIMax"));
	TestTrue(TEXT("One-sided lower range"), Adjust(100));
	TestEqual(TEXT("Missing upper bound stays free"), number, 101.f);
	floatProperty.RemoveMetaData(TEXT("UIMin"));
	TestTrue(TEXT("Unbounded field stays unbounded"), Adjust(-200));
	TestEqual(TEXT("Unbounded negative value"), number, -99.f);
	TUniquePtr<FStructProperty> vectorPropertyStorage(static_cast<FStructProperty*>(FStructProperty::Construct(actor->GetClass(), TEXT("TangentTestVector"), RF_NoFlags)));
	FStructProperty& vectorProperty = *vectorPropertyStorage;
	vectorProperty.SetMetaData(TEXT("UIMin"), TEXT("-4"));
	vectorProperty.SetMetaData(TEXT("UIMax"), TEXT("4"));
	data.PropertyChain = { &vectorProperty, &floatProperty };
	TestTrue(TEXT("Struct component inherits range"), Adjust(1000));
	TestEqual(TEXT("Struct component maximum"), number, 4.f);
	TestTrue(TEXT("Struct component lower range"), Adjust(-1000));
	TestEqual(TEXT("Struct component minimum"), number, -4.f);
	data.PropertyChain.Empty();
	floatProperty.SetMetaData(TEXT("ClampMin"), TEXT("not-a-number"));
	TestTrue(TEXT("Malformed metadata does not become zero"), Adjust(-100));
	TestEqual(TEXT("Malformed metadata is ignored"), number, -104.f);

	TUniquePtr<FDoubleProperty> doublePropertyStorage(static_cast<FDoubleProperty*>(FDoubleProperty::Construct(actor->GetClass(), TEXT("TangentTestDouble"), RF_NoFlags)));
	FDoubleProperty& doubleProperty = *doublePropertyStorage;
	doubleProperty.SetMetaData(TEXT("ClampMax"), TEXT("12.5"));
	double doubleNumber = 1;
	PropertyData doubleData(TEXT("Double"), &doubleProperty, &doubleNumber, nullptr);
	TestTrue(TEXT("Double upper bound"), doubleData.AdjustParameterValue(&changes, actor, ++target, 100, reported));
	TestEqual(TEXT("Double stores limit"), doubleNumber, 12.5);

	TUniquePtr<FIntProperty> intPropertyStorage(static_cast<FIntProperty*>(FIntProperty::Construct(actor->GetClass(), TEXT("TangentTestInt"), RF_NoFlags)));
	FIntProperty& intProperty = *intPropertyStorage;
	int32 integer = MAX_int32;
	PropertyData intData(TEXT("Int"), &intProperty, &integer, nullptr);
	TestTrue(TEXT("Int does not wrap"), intData.AdjustParameterValue(&changes, actor, ++target, 1.e20f, reported));
	TestEqual(TEXT("Int native maximum"), integer, MAX_int32);
	intProperty.SetMetaData(TEXT("ClampMin"), TEXT("-2.5"));
	intProperty.SetMetaData(TEXT("ClampMax"), TEXT("3.5"));
	TestTrue(TEXT("Integer metadata maximum"), intData.AdjustParameterValue(&changes, actor, ++target, 1, reported));
	TestEqual(TEXT("Round upper bound inward"), integer, 3);
	TestTrue(TEXT("Integer metadata minimum"), intData.AdjustParameterValue(&changes, actor, ++target, -100, reported));
	TestEqual(TEXT("Round lower bound inward"), integer, -2);

	TUniquePtr<FByteProperty> bytePropertyStorage(static_cast<FByteProperty*>(FByteProperty::Construct(actor->GetClass(), TEXT("TangentTestByte"), RF_NoFlags)));
	FByteProperty& byteProperty = *bytePropertyStorage;
	uint8 byte = 200;
	PropertyData byteData(TEXT("Byte"), &byteProperty, &byte, nullptr);
	TestTrue(TEXT("Byte huge delta"), byteData.AdjustParameterValue(&changes, actor, ++target, 1.e20f, reported));
	TestEqual(TEXT("Byte native maximum"), byte, uint8(255));
	byteData.Clipped = true; byteData.Minimum = -100; byteData.Maximum = 500;
	TestTrue(TEXT("Byte mapping cannot wrap native range"), byteData.AdjustParameterValue(&changes, actor, ++target, -1.e20f, reported));
	TestEqual(TEXT("Byte native minimum"), byte, uint8(0));

	FProperty* cameraProperty = FindFProperty<FProperty>(UCameraComponent::StaticClass(), TEXT("FieldOfView"));
	if (TestNotNull(TEXT("Resolve native camera property"), cameraProperty))
	{
		PropertyData cameraData(TEXT("CameraComponent.FieldOfView"), cameraProperty,
			cameraProperty->ContainerPtrToValuePtr<void>(actor->GetCameraComponent()), nullptr);
		TestTrue(TEXT("Native camera maximum"), cameraData.AdjustParameterValue(&changes, actor, ++target, 1000, reported));
		TestEqual(TEXT("Camera hard maximum"), actor->GetCameraComponent()->FieldOfView, 360.f);
	}
	world->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentNDisplayTrainingTest, "Tangent.Properties.NDisplayColorGradingTraining",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentNDisplayTrainingTest::RunTest(const FString&)
{
	// Load the actual nDisplay class through reflection to keep this plugin's
	// normal operation independent of nDisplay being enabled.
	FModuleManager::Get().LoadModule(TEXT("DisplayCluster"));
	UClass* cameraClass = LoadObject<UClass>(nullptr, TEXT("/Script/DisplayCluster.DisplayClusterICVFXCameraComponent"));
	if (!TestNotNull(TEXT("Load actual ICVFX camera component class"), cameraClass)) return false;
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	AActor* actor = world->SpawnActor<AActor>();
	UActorComponent* f1 = NewObject<UActorComponent>(actor, cameraClass, TEXT("F1"), RF_Transactional);
	UActorComponent* f2 = NewObject<UActorComponent>(actor, cameraClass, TEXT("F2"), RF_Transactional);
	actor->AddInstanceComponent(f1);
	actor->AddInstanceComponent(f2);
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	helper->_selectedActor = actor;
	FName captured;
	int32 responses = 0, refreshes = 0;
	FDelegateHandle trained = UEngineHelper::TrainResponseEvent.AddLambda(
		[&](int32, FName, FName path, float, float, float) { captured = path; ++responses; });
	FDelegateHandle refreshed = UEngineHelper::ActorPropertyChangedEvent.AddLambda([&]() { ++refreshes; });
	for (UActorComponent* component : { f1, f2 })
	{
		const FString path = TEXT("@") + component->GetName() + TEXT(".CameraSettings.AllNodesColorGrading.ColorGradingSettings.AutoExposureBias");
		PropertyData* data = helper->FetchPropertyData(actor, path);
		if (!TestNotNull(TEXT("Resolve actual ICVFX Exposure Compensation"), data)) continue;
		TestEqual(TEXT("Owner is the selected F1/F2 component"), data->EditObjects.Last().Get(), static_cast<UObject*>(component));
		FEditPropertyChain chain;
		for (FProperty* property : data->PropertyChain) chain.AddTail(property);
		chain.SetActivePropertyNode(data->Prop);
		chain.SetActiveMemberPropertyNode(data->PropertyChain[0]);
		helper->StartTrainingForPanelControl(0);
		static_cast<UObject*>(component)->PreEditChange(chain);
		const int32 before = responses;
		// An unrelated event must not consume the saved training chain.
		FPropertyChangedEvent unrelated(FindFProperty<FProperty>(USceneComponent::StaticClass(), TEXT("RelativeLocation")), EPropertyChangeType::ValueSet);
		helper->OnObjectPropertyChanged(component, unrelated);
		TestEqual(TEXT("Unrelated field does not finish training"), responses, before);
		*static_cast<float*>(data->DataPtr) = component == f1 ? 1.25f : -2.5f;
		// Follow the real object notification route; no Details-finished callback.
		FPropertyChangedEvent event(data->Prop, component == f1 ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet);
		FPropertyChangedChainEvent changed(chain, event);
		component->PostEditChangeChainProperty(changed);
		TestEqual(TEXT("Global component notification completes training once"), responses, before + 1);
		TestEqual(TEXT("Mapping retains distinct F1/F2 identity"), captured, FName(*path));
		float value = 0;
		data = helper->FetchPropertyData(actor, captured);
		TestTrue(TEXT("Mapped exposure can be read"), data && data->GetParameterValue(value));
		TestEqual(TEXT("Mapped exposure reads the edited value"), value, component == f1 ? 1.25f : -2.5f);
		if (data)
		{
			TestTrue(TEXT("Mapped exposure can be adjusted"), data->AdjustParameterValue(helper.Get(), actor, 0xFD000000, 0.5f, value));
			helper->OnPostChangeCompletion();
			TestFalse(TEXT("Component gesture closes its transaction"), GEditor->IsTransactionActive());
			TestEqual(TEXT("Adjustment reaches the right component"), *static_cast<float*>(data->DataPtr), component == f1 ? 1.75f : -2.f);
			TestEqual(TEXT("Panel edits do not retrigger training"), responses, before + 1);
		}
	}
	TestTrue(TEXT("Component edits refresh panel values"), refreshes >= 2);
	AddExpectedMessagePlain(TEXT("Tangent: cannot resolve property '@Missing."), ELogVerbosity::Warning);
	TestNull(TEXT("Missing component does not fall back to a different camera"), helper->FetchPropertyData(actor, FString(TEXT("@Missing.CameraSettings.AllNodesColorGrading.ColorGradingSettings.AutoExposureBias"))));
	UEngineHelper::TrainResponseEvent.Remove(trained);
	UEngineHelper::ActorPropertyChangedEvent.Remove(refreshed);
	world->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentPropertyPathTest, "Tangent.Properties.ArrayTrainingAndResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentPropertyPathTest::RunTest(const FString&)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ACameraActor* actor = world->SpawnActor<ACameraActor>();
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	helper->_selectedActor = actor;
	auto& entries = actor->GetCameraComponent()->PostProcessSettings.WeightedBlendables.Array;
	entries.Add(FWeightedBlendable(2.f, nullptr));
	const FName path(TEXT("CameraComponent.PostProcessSettings.WeightedBlendables.Array[0].Weight"));
	PropertyData* data = helper->FetchPropertyData(actor, path);
	if (TestNotNull(TEXT("Resolve an indexed float through a component reference"), data))
	{
		float value = 0;
		TestTrue(TEXT("Read array value"), data->GetParameterValue(value));
		TestEqual(TEXT("Read the chosen entry"), value, 2.f);
		TestEqual(TEXT("Retain the actual value owner"), data->EditObjects.Last().Get(), static_cast<UObject*>(actor->GetCameraComponent()));
		TestNotNull(TEXT("New array entries have an initialized struct default"), data->DefaultDataPtr);

		FEditPropertyChain chain;
		for (int32 i = data->ObjectChainStarts.Last(); i < data->PropertyChain.Num(); ++i)
			chain.AddTail(data->PropertyChain[i]);
		helper->StartTrainingForPanelControl(0); // Encoder control type.
		helper->OnPreObjectPropertyChanged(actor->GetCameraComponent(), chain);
		FName captured;
		FDelegateHandle handle = UEngineHelper::TrainResponseEvent.AddLambda(
			[&](int32, FName, FName trainedPath, float, float, float) { captured = trainedPath; });
		FPropertyChangedEvent event(data->Prop, EPropertyChangeType::ValueSet);
		TArray<TMap<FString, int32>> indices;
		indices.Add(data->ArrayIndices);
		event.SetArrayIndexPerObject(indices);
		event.ObjectIteratorIndex = 0;
		FPropertyChangedChainEvent chainEvent(chain, event);
		#if UE_VERSION_OLDER_THAN(5, 8, 0)
		helper->OnTrainingDetailsFinished(event);
#else
		helper->OnPostObjectPropertyChanged(actor->GetCameraComponent(), chainEvent);
#endif
		UEngineHelper::TrainResponseEvent.Remove(handle);
		TestEqual(TEXT("Training includes the edited array index"), captured, path);

		entries.Reserve(100); // Deliberately invalidate the old interior pointer.
		entries[0].Weight = 3.f;
		data = helper->FetchPropertyData(actor, path);
		TestTrue(TEXT("Resolve after array reallocation"), data && data->GetParameterValue(value));
		TestEqual(TEXT("Read current array storage"), value, 3.f);
		entries.Empty();
		AddExpectedError(TEXT("Tangent: cannot resolve property"), EAutomationExpectedErrorFlags::Contains, 1);
		TestNull(TEXT("Removed entry is rejected"), helper->FetchPropertyData(actor, path));
	}
	helper->_selectedActor = nullptr;
	world->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentHSVMappingTest, "Tangent.Properties.HSVMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentHSVMappingTest::RunTest(const FString&)
{
	using namespace UE::ColorGrading;
	EColorGradingComponent component = EColorGradingComponent::Hue;
	TSharedRef<SColorGradingComponentViewer> slider = SNew(UE::ColorGrading::SColorGradingComponentViewer)
		.Component_Lambda([&]() { return component; });
	TSharedRef<SColorGradingComponentViewer> unqualifiedSlider = SNew(SColorGradingComponentViewer)
		.Component_Lambda([&]() { return component; });
	FWidgetPath widgetPath;
	widgetPath.Widgets.AddWidget(FArrangedWidget(slider, FGeometry()));
	FWidgetPath unqualifiedPath;
	unqualifiedPath.Widgets.AddWidget(FArrangedWidget(unqualifiedSlider, FGeometry()));
	TestEqual(TEXT("Production widget name retains its namespace"), slider->GetTypeAsString(),
		FString(TEXT("UE::ColorGrading::SColorGradingComponentViewer")));
	const EColorGradingComponent components[] = { EColorGradingComponent::Hue, EColorGradingComponent::Saturation,
		EColorGradingComponent::Value, EColorGradingComponent::Red, EColorGradingComponent::Green,
		EColorGradingComponent::Blue, EColorGradingComponent::Luminance };
	const TCHAR* channels[] = { TEXT("HSV_H"), TEXT("HSV_S"), TEXT("HSV_V"), TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT("W") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(components); ++i)
	{
		component = components[i];
		TestEqual(TEXT("Identify active channel from real Slate slider"), UEngineHelper::TrainingColorComponent(widgetPath), FString(channels[i]));
		TestEqual(TEXT("Unqualified widget construction is also supported"), UEngineHelper::TrainingColorComponent(unqualifiedPath), FString(channels[i]));
	}
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ACameraActor* actor = world->SpawnActor<ACameraActor>();
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	helper->_selectedActor = actor;
	FLimitTestChanges changes;
	auto& gain = actor->GetCameraComponent()->PostProcessSettings.ColorGain;
	const FString base(TEXT("CameraComponent.PostProcessSettings.ColorGain"));
	float value = 0;
	uint32 target = 0xFC000000;
	auto Fetch = [&](const TCHAR* channel) { return helper->FetchPropertyData(actor, base + TEXT(".") + channel); };
	PropertyData* vector = helper->FetchPropertyData(actor, base);
	FEditPropertyChain chain;
	for (int32 i = vector->ObjectChainStarts.Last(); i < vector->PropertyChain.Num(); ++i)
		chain.AddTail(vector->PropertyChain[i]);
	FName captured;
	FDelegateHandle listener = UEngineHelper::TrainResponseEvent.AddLambda(
		[&](int32, FName, FName path, float, float, float) { captured = path; });
	for (const TCHAR* channel : { TEXT("HSV_H"), TEXT("HSV_S"), TEXT("HSV_V"), TEXT("W"), TEXT("X") })
	{
		helper->StartTrainingForPanelControl(0);
		helper->OnPreObjectPropertyChanged(actor->GetCameraComponent(), chain);
		// Supply the component captured from the active Slate slider.
		helper->_trainingPaths.FindChecked(actor->GetCameraComponent()).ColorComponent = channel;
		FPropertyChangedEvent event(vector->Prop, EPropertyChangeType::ValueSet);
		FPropertyChangedChainEvent changed(chain, event);
		helper->OnTrainingDetailsFinished(changed);
		TestEqual(TEXT("Whole-vector notification trains selected channel"), captured, FName(*(base + TEXT(".") + channel)));
	}
	for (const TCHAR* storage : { TEXT("X"), TEXT("Y"), TEXT("Z") })
	{
		PropertyData* child = helper->FetchPropertyData(actor, base + TEXT(".") + storage);
		FEditPropertyChain childChain;
		for (int32 i = child->ObjectChainStarts.Last(); i < child->PropertyChain.Num(); ++i)
			childChain.AddTail(child->PropertyChain[i]);
		helper->StartTrainingForPanelControl(0);
		helper->OnPreObjectPropertyChanged(actor->GetCameraComponent(), childChain);
		helper->_trainingPaths.FindChecked(actor->GetCameraComponent()).ColorComponent = TEXT("HSV_H");
		FPropertyChangedEvent event(child->Prop, EPropertyChangeType::ValueSet);
		FPropertyChangedChainEvent changed(childChain, event);
		helper->OnTrainingDetailsFinished(changed);
		TestEqual(TEXT("Any first RGB child notification trains selected HSV slider"), captured, FName(*(base + TEXT(".HSV_H"))));
	}
	UEngineHelper::TrainResponseEvent.Remove(listener);
	gain = FVector4(1, 1, 1, 0.75);
	PropertyData* hue = Fetch(TEXT("HSV_H"));
	PropertyData* saturation = Fetch(TEXT("HSV_S"));
	PropertyData* brightness = Fetch(TEXT("HSV_V"));
	if (TestNotNull(TEXT("Resolve hue"), hue) && TestNotNull(TEXT("Resolve saturation"), saturation)
		&& TestNotNull(TEXT("Resolve value"), brightness))
	{
		TestTrue(TEXT("Set hue on grey"), hue->AdjustParameterValue(&changes, actor, ++target, 120, value));
		TestTrue(TEXT("Hue feedback survives property re-resolution"), Fetch(TEXT("HSV_H"))->GetParameterValue(value));
		TestEqual(TEXT("Achromatic hue retained"), value, 120.f);
		TestTrue(TEXT("Add saturation using retained hue"), saturation->AdjustParameterValue(&changes, actor, ++target, 0.5f, value));
		TestTrue(TEXT("Result is green"), FMath::IsNearlyEqual(gain.X, 0.5) && FMath::IsNearlyEqual(gain.Y, 1.0) && FMath::IsNearlyEqual(gain.Z, 0.5));
		TestEqual(TEXT("Luminance is preserved"), gain.W, 0.75);
		TestTrue(TEXT("Value uses vector UI maximum"), brightness->AdjustParameterValue(&changes, actor, ++target, 100, value));
		TestEqual(TEXT("Value capped at gain max"), value, 2.f);
		TestTrue(TEXT("Saturation limited to one"), saturation->AdjustParameterValue(&changes, actor, ++target, 100, value));
		TestEqual(TEXT("Saturation max"), value, 1.f);
		const uint32 resetTarget = ++target;
		TestTrue(TEXT("Adjust hue before reset"), hue->AdjustParameterValue(&changes, actor, resetTarget, 30, value));
		ResetCache::TapNow(resetTarget);
		TestTrue(TEXT("Single reset"), hue->AdjustParameterValue(&changes, actor, resetTarget, 0, value));
		TestEqual(TEXT("Reset restores initial hue"), value, 120.f);
		ResetCache::TapNow(resetTarget);
		TestTrue(TEXT("Double reset"), hue->AdjustParameterValue(&changes, actor, resetTarget, 0, value));
		TestEqual(TEXT("Double reset uses archetype hue"), value, 0.f);
		gain = FVector4(0, 0, 1, 0.25);
		TestTrue(TEXT("Read an external RGB edit"), Fetch(TEXT("HSV_H"))->GetParameterValue(value));
		TestEqual(TEXT("External RGB invalidates cache"), value, 240.f);
		TestEqual(TEXT("External luminance preserved"), gain.W, 0.25);

		FString invalid = TEXT("CameraComponent.PostProcessSettings.AutoExposureBias.HSV_H");
		AddExpectedError(TEXT("Tangent: cannot resolve property"), EAutomationExpectedErrorFlags::Contains, 1);
		TestNull(TEXT("Reject HSV on ordinary numeric property"), helper->FetchPropertyData(actor, invalid));
	}
	helper->_selectedActor = nullptr;
	world->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentTransactionTest, "Tangent.Transactions.NestedOwnerAndCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentTransactionTest::RunTest(const FString&)
{
	if (GEditor->IsTransactionActive())
	{
		AddError(TEXT("Run this test while the editor is idle, outside another transaction."));
		return false;
	}
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ACameraActor* actor = world->SpawnActor<ACameraActor>();
	actor->SetFlags(RF_Transactional);
	actor->GetCameraComponent()->SetFlags(RF_Transactional);
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	const FName path(TEXT("CameraComponent.PostProcessSettings.AutoExposureBias"));
	PropertyData* data = helper->FetchPropertyData(actor, path);
	if (TestNotNull(TEXT("Resolve component exposure"), data))
	{
		float value = 0;
		const float original = actor->GetCameraComponent()->PostProcessSettings.AutoExposureBias;
		TestTrue(TEXT("Adjust nested property"), data->AdjustParameterValue(helper.Get(), actor, 0x1234, 0.25f, value));
		TestTrue(TEXT("Transaction is active during a gesture"), GEditor->IsTransactionActive());
		TestTrue(TEXT("Nested value owner was captured"), GEditor->Trans->IsObjectInTransactionBuffer(actor->GetCameraComponent()));
		helper->_saveWorldActive.AtomicSet(true);
		helper->OnPostChangeCompletion();
		TestTrue(TEXT("Completion is retried during save"), GEditor->GetTimerManager()->IsTimerActive(helper->_postChangeTimer));
		helper->_saveWorldActive.AtomicSet(false);
		helper->OnPostChangeCompletion();
		TestFalse(TEXT("Panel transaction finishes"), GEditor->IsTransactionActive());
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo restores the nested value"), actor->GetCameraComponent()->PostProcessSettings.AutoExposureBias, original);
		GEditor->RedoTransaction();
		TestEqual(TEXT("Redo restores the edit"), actor->GetCameraComponent()->PostProcessSettings.AutoExposureBias, original + 0.25f);

		GEditor->BeginTransaction(FText::FromString(TEXT("Tangent test external edit")));
		data = helper->FetchPropertyData(actor, path);
		TestTrue(TEXT("Panel can join an existing transaction"), data && data->AdjustParameterValue(helper.Get(), actor, 0x1234, 0.25f, value));
		helper->OnPostChangeCompletion();
		TestTrue(TEXT("Panel does not close another tool's transaction"), GEditor->IsTransactionActive());
		GEditor->EndTransaction();
	}
	GEditor->GetTimerManager()->ClearTimer(helper->_postChangeTimer);
	world->DestroyWorld(false);
	return true;
}
#endif
