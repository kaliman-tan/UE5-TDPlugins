#include "OSCActorSubsystem.h"

#include "OSCActor.h"
#include "OSCActorFunctionLibrary.h"
#include "OSCActorModule.h"
#include "OSCCineCameraActor.h"
#include "OSCManager.h"
#include "OSCServer.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

UOSCActorSettings::UOSCActorSettings(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

void UOSCActorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UOSCActorSettings* Settings = GetDefault<UOSCActorSettings>();

	OSCServer = NewObject<UOSCServer>(this, FName("OSCActorServer"));
	if (!IsValid(OSCServer))
	{
		UE_LOG(LogTemp, Error, TEXT("OSCActorSubsystem: Failed to create OSC server"));
		return;
	}

	OSCServer->SetAddress(FString("0.0.0.0"), Settings->OSCReceivePort);
	OSCServer->SetTickInEditor(true);
	OSCServer->Listen();

	if (!OSCServer->IsActive())
	{
		UE_LOG(LogTemp, Error, TEXT("OSCActorSubsystem: Failed to bind OSC server on port %d. Port may already be in use by another editor instance."), Settings->OSCReceivePort);
		return;
	}

	OSCServer->OnOscBundleReceived.AddDynamic(this, &UOSCActorSubsystem::OnOscBundleReceived);

	// Use engine-level core ticker to pump OSC packets every frame.
	// FTickableGameObject has complex conditions in editor mode (requires world tick),
	// but FTSTicker::GetCoreTicker() fires every engine loop iteration unconditionally.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UOSCActorSubsystem::TickOSCServer));

	UE_LOG(LogTemp, Log, TEXT("OSCActorSubsystem: OSC server started on port %d"), Settings->OSCReceivePort);
}

void UOSCActorSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();

	if (IsValid(OSCServer))
	{
		OSCServer->OnOscBundleReceived.RemoveDynamic(this, &UOSCActorSubsystem::OnOscBundleReceived);
		OSCServer->Stop();
		OSCServer->ConditionalBeginDestroy();
		OSCServer = nullptr;
	}

	Super::Deinitialize();
}

bool UOSCActorSubsystem::TickOSCServer(float DeltaTime)
{
	if (IsValid(OSCServer))
	{
		OSCServer->PumpPacketQueue(nullptr);
	}
	return true;
}

void UOSCActorSubsystem::UpdateActorReference(UActorComponent* Component_)
{
	// Game-world (PIE) components take priority over editor-world components.
	// TickComponent runs every frame in both worlds during PIE, so without this guard,
	// the editor component would constantly overwrite the PIE component in the map,
	// causing PIE actors to never receive OSC data.
	auto IsGameWorldPreferred = [](UActorComponent* Existing, UActorComponent* Incoming) -> bool
	{
		UWorld* ExistingWorld = Existing->GetWorld();
		UWorld* IncomingWorld = Incoming->GetWorld();
		return ExistingWorld && IncomingWorld && ExistingWorld->IsGameWorld() && !IncomingWorld->IsGameWorld();
	};

	if (UOSCActorComponent* Actor = Cast<UOSCActorComponent>(Component_))
	{
		if (!Actor->ObjectName.IsEmpty())
		{
			if (UOSCActorComponent** Existing = OSCActorComponentMap.Find(Actor->ObjectName))
				if (IsValid(*Existing) && IsGameWorldPreferred(*Existing, Actor))
					return;
			OSCActorComponentMap.Add(Actor->ObjectName, Actor);
		}
	}
	else if (UOSCCineCameraComponent* Camera = Cast<UOSCCineCameraComponent>(Component_))
	{
		if (!Camera->ObjectName.IsEmpty())
		{
			if (UOSCCineCameraComponent** Existing = OSCCameraComponentMap.Find(Camera->ObjectName))
				if (IsValid(*Existing) && IsGameWorldPreferred(*Existing, Camera))
					return;
			OSCCameraComponentMap.Add(Camera->ObjectName, Camera);
		}
	}
}

void UOSCActorSubsystem::RemoveActorReference(UActorComponent* Component_)
{
	if (UOSCActorComponent* Actor = Cast<UOSCActorComponent>(Component_))
	{
		// Only remove if THIS component is the registered one (not a PIE vs editor mismatch)
		UOSCActorComponent** Existing = OSCActorComponentMap.Find(Actor->ObjectName);
		if (Existing && *Existing == Actor)
			OSCActorComponentMap.Remove(Actor->ObjectName);
	}
	else if (UOSCCineCameraComponent* Camera = Cast<UOSCCineCameraComponent>(Component_))
	{
		UOSCCineCameraComponent** Existing = OSCCameraComponentMap.Find(Camera->ObjectName);
		if (Existing && *Existing == Camera)
			OSCCameraComponentMap.Remove(Camera->ObjectName);
	}
}

// Read active bool from OSC message regardless of whether TD sent float, int32, or bool.
static bool GetActiveBool(const FOSCMessage& Message)
{
	float F = 0.0f;
	if (UOSCManager::GetFloat(Message, 0, F)) return F != 0.0f;

	int32 I = 0;
	if (UOSCManager::GetInt32(Message, 0, I)) return I != 0;

	bool B = false;
	UOSCManager::GetBool(Message, 0, B);
	return B;
}

void UOSCActorSubsystem::OnOscBundleReceived(const FOSCBundle& Bundle, const FString& IPAddress, int32 Port)
{
	static const FMatrix ROT_YAW_90 = FRotationMatrix::Make(FRotator(0, 90, 0));

	const UOSCActorSettings* Settings = GetDefault<UOSCActorSettings>();

	const auto Messages = UOSCManager::GetMessagesFromBundle(Bundle);

	if (Messages.Num() == 0)
		return;

	// Single pass: reset valid entries and collect stale keys simultaneously.
	// Avoids GetKeys() which allocates and copies all keys upfront.
	TArray<FString> InvalidKeys;
	for (auto& Pair : OSCActorComponentMap)
	{
		if (!IsValid(Pair.Value))
			InvalidKeys.Add(Pair.Key);
		else
		{
			Pair.Value->Params.Reset();
			Pair.Value->MultiSampleParams.Reset();
		}
	}
	for (const auto& Key : InvalidKeys)
		OSCActorComponentMap.Remove(Key);

	TArray<float> OutValues;

	for (const auto& Message : Messages)
	{
		const auto Address = Message.GetAddress().GetFullPath();

		TArray<FString> Comp;
		if (!Address.ParseIntoArray(Comp, TEXT("/"), true) || Comp.Num() < 2)
			continue;

		const auto& Name = Comp[1];

		if (Comp[0] == "obj")
		{
			if (Comp.Num() < 3)
				continue;

			auto It = OSCActorComponentMap.Find(Name);
			if (!It)
				continue;

			UOSCActorComponent* Component = *It;
			if (!IsValid(Component))
				continue;

			AActor* Actor = Component->GetOwner();
			if (!IsValid(Actor))
				continue;

			const auto& Type = Comp[2];

			if (Type == "active")
			{
				const bool Value = GetActiveBool(Message);

				Actor->SetActorHiddenInGame(!Value);
#if WITH_EDITOR
				Actor->SetIsTemporarilyHiddenInEditor(!Value);
#endif
			}
			else if (Type == "TRS")
			{
				OutValues.Reset();
				UOSCManager::GetAllFloats(Message, OutValues);

				const float* a = OutValues.GetData();
				FMatrix M = UOSCActorFunctionLibrary::TRSToMatrix(
					a[0], a[1], a[2],
					a[3], a[4], a[5],
					a[6], a[7], a[8]
				);

				M = UOSCActorFunctionLibrary::ConvertGLtoUE4Matrix(M);
				M = ROT_YAW_90 * M;

				Actor->SetActorRelativeTransform(FTransform(M));
			}
			else if (Type == "ss")
			{
				if (Comp.Num() < 4)
					continue;

				const auto& ParName = Comp[3];

				OutValues.Reset();
				UOSCManager::GetAllFloats(Message, OutValues);

				float v = OutValues.Last();

				Component->Params.Add(ParName, v);
			}
			else if (Type == "ms")
			{
				if (Comp.Num() < 4)
					continue;

				const auto& ParName = Comp[3];

				FChannelData Data;
				UOSCManager::GetAllFloats(Message, Data.Samples);

				Component->MultiSampleParams.Add(ParName, Data);
			}
		}
		else if (Comp[0] == "cam")
		{
			if (Comp.Num() < 3)
				continue;

			auto It = OSCCameraComponentMap.Find(Name);
			if (!It)
				continue;

			UOSCCineCameraComponent* OSCCameraCompoent = *It;
			if (!IsValid(OSCCameraCompoent))
				continue;

			ACineCameraActor* Camera = Cast<ACineCameraActor>(OSCCameraCompoent->GetOwner());
			if (!IsValid(Camera))
				continue;

			const auto& Type = Comp[2];

			if (Type == "active")
			{
				const bool Value = GetActiveBool(Message);

				Camera->SetActorHiddenInGame(!Value);
#if WITH_EDITOR
				Camera->SetIsTemporarilyHiddenInEditor(!Value);
#endif
			}
			else if (Type == "TRS")
			{
				OutValues.Reset();
				UOSCManager::GetAllFloats(Message, OutValues);

				const float* a = OutValues.GetData();
				FMatrix M = UOSCActorFunctionLibrary::TRSToMatrix(
					a[0], a[1], a[2],
					a[3], a[4], a[5],
					a[6], a[7], a[8]
				);

				M = UOSCActorFunctionLibrary::ConvertGLtoUE4Matrix(M);

				Camera->SetActorRelativeTransform(FTransform(M));
			}
			else if (Type == "focal")
			{
				float Value;
				UOSCManager::GetFloat(Message, 0, Value);
				Camera->GetCineCameraComponent()->SetCurrentFocalLength(Value);
			}
			else if (Type == "aperture")
			{
				float Value;
				UOSCManager::GetFloat(Message, 0, Value);
				FCameraFilmbackSettings FilmbackSettings;
				FilmbackSettings.SensorWidth = Value;
				FilmbackSettings.SensorHeight = Value / Settings->SensorAspectRatio;
				FilmbackSettings.SensorAspectRatio = Settings->SensorAspectRatio;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
				Camera->GetCineCameraComponent()->SetFilmback(FilmbackSettings);
#else
				FilmbackSettings.SensorAspectRatio = FilmbackSettings.SensorWidth / FilmbackSettings.SensorHeight;
				Camera->GetCineCameraComponent()->Filmback = FilmbackSettings;
#endif
			}
			else if (Type == "winx")
			{
				float Value;
				UOSCManager::GetFloat(Message, 0, Value);
				OSCCameraCompoent->WindowXY.X = Value * 2;
			}
			else if (Type == "winy")
			{
				float Value;
				UOSCManager::GetFloat(Message, 0, Value);
				OSCCameraCompoent->WindowXY.Y = Value * 2;
			}
		}
		else if (Comp[0] == "sys")
		{
			if (Name == "frame_number")
			{
				int Value;
				UOSCManager::GetInt32(Message, 0, Value);
				FrameNumber = Value;
			}
		}
	}

	// Update MultiSampleNum to minimum amount of Samples
	for (auto& Iter : OSCActorComponentMap)
	{
		auto O = Iter.Value;
		if (!IsValid(O))
			continue;

		int MultiSampleNum = 100000000;

		for (const auto& It : O->MultiSampleParams)
		{
			int n = It.Value.Samples.Num();
			if (n > 0)
				MultiSampleNum = std::min(n, MultiSampleNum);
		}

		if (MultiSampleNum == 100000000)
			MultiSampleNum = 0;

		O->MultiSampleNum = MultiSampleNum;

		if (O->UpdateFromOSC.IsBound())
		{
#if WITH_EDITOR
			FEditorScriptExecutionGuard ScriptGuard;
#endif
			O->UpdateFromOSC.Broadcast();
		}
	}
}
