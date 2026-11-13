#include "EngineHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentCCRHSVTest, "Tangent.Properties.ColorCorrectionRegionNativeHSV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTangentCCRHSVTest::RunTest(const FString&)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	AColorCorrectionRegion* actor = world->SpawnActor<AColorCorrectionRegion>();
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	helper->_selectedActor = actor;
	FVector4& gain = actor->ColorGradingSettings.Global.Gain;
	gain = FVector4(1, 1, 1, 0.75);
	helper->UpdateGradingHSV(TargetId::CCRGlobalGainS, 0.25f);
	FLinearColor hsv = FLinearColor(gain.X, gain.Y, gain.Z).LinearRGBToHSV();
	TestTrue(TEXT("S means saturation after removing wheel reinterpretation"), FMath::IsNearlyEqual(hsv.G, 0.25f, 0.001f));
	helper->UpdateGradingHSV(TargetId::CCRGlobalGainH, 90.f);
	hsv = FLinearColor(gain.X, gain.Y, gain.Z).LinearRGBToHSV();
	TestTrue(TEXT("H means hue in degrees"), FMath::IsNearlyEqual(hsv.R, 90.f, 0.01f));
	TestTrue(TEXT("Hue preserves saturation"), FMath::IsNearlyEqual(hsv.G, 0.25f, 0.001f));
	helper->UpdateGradingHSV(TargetId::CCRGlobalGainV, 0.25f);
	hsv = FLinearColor(gain.X, gain.Y, gain.Z).LinearRGBToHSV();
	TestTrue(TEXT("V remains brightness"), FMath::IsNearlyEqual(hsv.B, 1.25f, 0.001f));
	helper->OnPostChangeCompletion();
	helper->_selectedActor = nullptr;
	world->DestroyWorld(false);
	return true;
}
#endif
