// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "OSCActor.h"
#include "Subsystems/EngineSubsystem.h"
#include "OSCServer.h"
#include "OSCBundle.h"
#include "OSCCineCameraActor.h"
#include "OSCActorSubsystem.generated.h"

UCLASS(config=Project, defaultconfig)
class UOSCActorSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	
	UPROPERTY(EditAnywhere, config, Category = OSCActor)
	int OSCReceivePort = 7000;

	UPROPERTY(EditAnywhere, config, Category = OSCActor)
	float SensorAspectRatio = 16.0 / 9.0;
};

UCLASS()
class OSCACTOR_API UOSCActorSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:

	UPROPERTY(Category = "OSCActor", EditAnywhere, BlueprintReadOnly)
	int32 FrameNumber = 0;

	void UpdateActorReference(UActorComponent* Component_);
	void RemoveActorReference(UActorComponent* Component_);

protected:

	// TWeakObjectPtr (not raw pointers) so a destroyed component (e.g. PIE stop) never
	// leaves a dangling pointer here; IsValid()/Get() stay safe even after the object is freed.
	TMap<FString, TWeakObjectPtr<UOSCActorComponent>> OSCActorComponentMap;
	TMap<FString, TWeakObjectPtr<UOSCCineCameraComponent>> OSCCameraComponentMap;

	UPROPERTY()
	class UOSCServer* OSCServer;

	FTSTicker::FDelegateHandle TickHandle;

	// TODO: PumpPacketQueue is deprecated in UE5.8 but needed for editor-mode OSC reception.
	// Remove when UE provides an editor-compatible alternative.
	bool TickOSCServer(float DeltaTime);

	UFUNCTION()
	void OnOscBundleReceived(const FOSCBundle& Bundle, const FString& IPAddress, int32 Port);
};
