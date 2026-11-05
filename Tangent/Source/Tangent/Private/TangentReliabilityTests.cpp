#include "EngineHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "TangentEngine.h"
#include "TangentTransport.h"
#include "Camera/CameraActor.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "ResetCache.h"
#include "Common/TcpSocketBuilder.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentPartialSendTest, "Tangent.Comms.PartialWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTangentPartialSendTest::RunTest(const FString&)
{
	FTangentPendingSend pending;
	pending.Bytes = {0, 1, 2, 3, 4, 5, 6, 7};
	TArray<uint8> received;
	for (int32 amount : {2, 0, 1, 5})
	{
		TestTrue(TEXT("Short write or would-block is resumable"), pending.Advance([&](const uint8* data, int32 remaining, int32& sent)
		{
			sent = FMath::Min(amount, remaining);
			received.Append(data, sent);
			return true;
		}));
	}
	TestTrue(TEXT("Frame completed"), pending.IsComplete());
	TestTrue(TEXT("Every byte delivered once in order"), received == pending.Bytes);
	pending.Reset(); pending.Bytes = {1, 2, 3};
	TestFalse(TEXT("Socket failure is propagated"), pending.Advance([](const uint8*, int32, int32&) { return false; }));
	TestEqual(TEXT("Failure does not advance the frame"), pending.Offset, 0);
	return true;
}

namespace
{
	RxContext Packet(MessageBuilder& builder)
	{
		builder.Build();
		uint8* data = nullptr; int32 length = 0;
		builder.GetMessage(data, length);
		RxContext packet;
		packet.ResetForMessage(length - IPCComms::HeaderLen);
		uint8* dest; int32 expected;
		packet.GetReceiveBuffer(dest, expected);
		FMemory::Memcpy(dest, data + IPCComms::HeaderLen, expected);
		packet.AdjustForDataReceived(expected);
		return packet;
	}
	class FButtonRecorder : public FGenericApplicationMessageHandler
	{
	public:
		int32 Pressed = 0, Released = 0;
		bool OnControllerButtonPressed(FGamepadKeyNames::Type, FPlatformUserId, FInputDeviceId, bool repeat) override
		{ if (!repeat) ++Pressed; return true; }
		bool OnControllerButtonReleased(FGamepadKeyNames::Type, FPlatformUserId, FInputDeviceId, bool) override
		{ ++Released; return true; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentCommsTest, "Tangent.Comms.SaveAndReconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTangentCommsTest::RunTest(const FString&)
{
	UEngineHelper* helper = nullptr;
	{
		TangentEngine engine;
		helper = engine._engineHelper;
		engine._connected = true; // No real Hub is touched by this test.
		TSharedRef<FButtonRecorder> recorder = MakeShared<FButtonRecorder>();
		engine._tangentInputDevice = MakeShared<FTangentInput>(recorder);
		engine._tangentInputDevice->OnButtonChange(0, true);
		engine._tangentInputDevice->SendControllerEvents();
		TestEqual(TEXT("Initial press delivered"), recorder->Pressed, 1);
		helper->_saveWorldActive.AtomicSet(true);
		MessageBuilder release;
		release.WriteUInt32(0x3F); release.WriteUInt32(ControlType::Button);
		release.WriteUInt32(0); release.WriteInt32(0); release.WriteFloat32(0);
		MessageBuilder mode;
		mode.WriteUInt32(0x09); mode.WriteUInt32(1234);
		const RxContext releasePacket = Packet(release), modePacket = Packet(mode);
		auto producer = Async(EAsyncExecution::Thread, [&]()
		{
			return engine.QueueReceived(releasePacket) && engine.QueueReceived(modePacket);
		});
		TestTrue(TEXT("Worker queues packets during a save"), producer.Get());
		engine.TickCommands(0);
		TestEqual(TEXT("Save retains incoming messages"), engine._incoming.Num(), 2);
		helper->_saveWorldActive.AtomicSet(false);
		engine.TickCommands(0);
		engine._tangentInputDevice->SendControllerEvents();
		TestEqual(TEXT("Release survives save"), recorder->Released, 1);
		TestEqual(TEXT("Mode update survives save"), engine._currentMode, uint32(1234));
		TestFalse(TEXT("Response is queued rather than sent on editor thread"), engine._outgoing.IsEmpty());
		engine.QueueReceived(modePacket);
		engine.ResetHubComms();
		TestTrue(TEXT("Reconnect discards old incoming commands"), engine._incoming.IsEmpty());
		TestTrue(TEXT("Reconnect discards old outgoing frames"), engine._outgoing.IsEmpty());
		engine.SendToHub(mode);
		TestTrue(TEXT("Disconnected feedback does not accumulate"), engine._outgoing.IsEmpty());
		engine._connected = true;
		MessageBuilder handshake;
		handshake.WriteUInt32(1); handshake.WriteUInt32(7); handshake.WriteUInt32(0);
		helper->_saveWorldActive.AtomicSet(true);
		engine.QueueReceived(Packet(handshake)); engine.TickCommands(0);
		TestEqual(TEXT("Handshake retained during save"), engine._incoming.Num(), 1);
		helper->_saveWorldActive.AtomicSet(false); engine.TickCommands(0);
		TestTrue(TEXT("Handshake produces definition and capabilities"), engine._outgoing.Num() >= 2);
		engine.ResetHubComms();

		// Exercise the real nonblocking connect/send path against an ephemeral
		// loopback listener, never the user's Hub port.
		ISocketSubsystem* sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		FSocket* listener = FTcpSocketBuilder(TEXT("TangentTestListener")).AsNonBlocking()
			.BoundToAddress(FIPv4Address::InternalLoopback).BoundToPort(0).Listening(1).Build();
		if (TestNotNull(TEXT("Loopback listener"), listener))
		{
			TSharedRef<FInternetAddr> address = sockets->CreateInternetAddr();
			listener->GetAddress(*address);
			Async(EAsyncExecution::Thread, [&]() { engine.ConnectHubComms(address->GetPort()); }).Wait();
			TestTrue(TEXT("Nonblocking connection established"), engine._connected);
			FSocket* peer = listener->Accept(TEXT("TangentTestPeer"));
			if (TestNotNull(TEXT("Loopback accepted connection"), peer))
			{
				peer->SetNonBlocking(true);
				engine.SendToHub(mode);
				TestTrue(TEXT("Socket worker flushes queued frame"), Async(EAsyncExecution::Thread, [&]() { return engine.FlushOutgoing(); }).Get());
				uint8 received[64]; int32 count = 0;
				TestTrue(TEXT("Peer receives data"), peer->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(1)));
				TestTrue(TEXT("Read complete mode frame"), peer->Recv(received, sizeof(received), count));
				uint8* expected; int32 length; mode.GetMessage(expected, length);
				TestEqual(TEXT("Frame length"), count, length);
				TestTrue(TEXT("Frame contents"), count == length && FMemory::Memcmp(received, expected, length) == 0);
				peer->Close(); sockets->DestroySocket(peer);
			}
			Async(EAsyncExecution::Thread, [&]() { engine.ResetHubComms(); }).Wait();
			listener->Close(); sockets->DestroySocket(listener);
		}
	}
	helper->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentLightCacheTest, "Tangent.Color.LightCacheIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTangentLightCacheTest::RunTest(const FString&)
{
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	APointLight* first = world->SpawnActor<APointLight>();
	APointLight* second = world->SpawnActor<APointLight>();
	first->SetLightColor(FLinearColor::Red); second->SetLightColor(FLinearColor::Blue);
	HSVColorCache cache; FLinearColor hsv;
	cache.getHSV(first, hsv);
	TestTrue(TEXT("First read is initialized from the light"), hsv.Equals(first->GetLightColor().LinearRGBToHSV()));
	const FLinearColor precise(0.0123f, 1.f, 1.f);
	cache.setHSV(first, precise); cache.getHSV(first, hsv);
	TestTrue(TEXT("Same light keeps fine-grained HSV"), hsv.Equals(precise));
	cache.getHSV(second, hsv);
	TestTrue(TEXT("Switching lights invalidates cached color immediately"), hsv.Equals(second->GetLightColor().LinearRGBToHSV()));
	second->SetLightColor(FLinearColor::Green); cache.getHSV(second, hsv);
	TestTrue(TEXT("External RGB change invalidates cached color"), hsv.Equals(second->GetLightColor().LinearRGBToHSV()));
	world->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentCacheLifecycleTest, "Tangent.Lifecycle.PropertyCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTangentCacheLifecycleTest::RunTest(const FString&)
{
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	UWorld* other = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ACameraActor* camera = world->SpawnActor<ACameraActor>();
	ACameraActor* otherCamera = other->SpawnActor<ACameraActor>();
	const FName path(TEXT("CameraComponent.PostProcessSettings.AutoExposureBias"));
	PropertyData* data = helper->FetchPropertyData(camera, path);
	if (!TestNotNull(TEXT("Property is cached"), data)) return false;
	TWeakPtr<PropertyData::FGradingState> allocation(data->GradingState);
	TestEqual(TEXT("Repeated lookup reuses the entry"), helper->FetchPropertyData(camera, path), data);
	helper->FetchPropertyData(otherCamera, path);
	world->DestroyWorld(false);
	TestEqual(TEXT("World cleanup preserves only unrelated cache"), helper->_propertyDataCache.Num(), 1);
	TestFalse(TEXT("World cleanup actually frees owned data"), allocation.IsValid());
	FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
	context.SetCurrentWorld(other);
	other->DestroyActor(otherCamera);
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("GC removes expired object entries"), helper->_propertyDataCache.IsEmpty());
	other->DestroyWorld(false);
	GEngine->DestroyWorldContext(other);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTangentTransformThreadTest, "Tangent.Transforms.GameThreadDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTangentTransformThreadTest::RunTest(const FString&)
{
	TStrongObjectPtr<UEngineHelper> helper(NewObject<UEngineHelper>());
	UWorld* world = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	ACameraActor* actor = world->SpawnActor<ACameraActor>();
	helper->_selectedActor = actor;
	helper->_transformReference = UEngineHelper::TransformReference::World;
	ResetCache::Clear();
	auto worker = Async(EAsyncExecution::Thread, [&]()
	{
		helper->MoveSelected(FVector(10, 0, 0));
		helper->RotateSelected(FRotator(0, 20, 0));
		helper->ScaleSelected(FVector(1, 0, 0));
	});
	worker.Wait();
	TestTrue(TEXT("Worker does not modify actors"), actor->GetActorLocation().IsZero());
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestTrue(TEXT("Move is applied on game thread"), actor->GetActorLocation().Equals(FVector(10, 0, 0)));
	TestTrue(TEXT("Rotation is applied on game thread"), FMath::IsNearlyEqual(actor->GetActorRotation().Yaw, 20.0));
	TestTrue(TEXT("Scale is applied on game thread"), actor->GetActorScale3D().Equals(FVector(2, 1, 1)));
	helper->ResetMoveSelected(UEngineHelper::AxisX);
	helper->ResetRotateSelected(UEngineHelper::AxisZ);
	helper->ResetScaleSelected(UEngineHelper::AxisX);
	TestTrue(TEXT("Reset uses pre-edit location"), actor->GetActorLocation().IsZero());
	TestTrue(TEXT("Reset uses pre-edit rotation"), actor->GetActorRotation().IsNearlyZero());
	TestTrue(TEXT("Reset uses pre-edit scale"), actor->GetActorScale3D().Equals(FVector::OneVector));
	helper->OnPostChangeCompletion();
	world->DestroyWorld(false);
	ResetCache::Clear();
	return true;
}
#endif
