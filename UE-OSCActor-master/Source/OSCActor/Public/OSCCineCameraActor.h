// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "OSCCineCameraActor.generated.h"


UCLASS()
class OSCACTOR_API UOSCCineCameraComponent : public UCineCameraComponent
{
	GENERATED_BODY()
public:

	UOSCCineCameraComponent();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSCActor")
	FVector2f WindowXY;

	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;

public:

	virtual void BeginDestroy() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	
	UPROPERTY(Category = "OSCActor", EditAnywhere, BlueprintReadWrite)
	FString ObjectName;

private:
	UPROPERTY()
	class UOSCActorSubsystem* CachedSubsystem = nullptr;
};

UCLASS()
class OSCACTOR_API AOSCCineCameraActor : public ACineCameraActor
{
	GENERATED_BODY()
	
public:

	AOSCCineCameraActor(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "OSCActor")
	void CopyCameraSettingToSceneCaptureComponent2D(USceneCaptureComponent2D* SceneCaptureComponent);

	UFUNCTION(BlueprintCallable, Category = "OSCActor")
	UOSCCineCameraComponent* GetOSCCineCameraComponent() const { return OSCCineCameraComponent; }

protected:

	// Tick in editor (non-PIE) too, mirroring AOSCActor. This lets the Blueprint Event Tick
	// (which calls CopyCameraSettingToSceneCaptureComponent2D) run in editor mode, so the
	// SceneCapture->Spout output tracks the camera transform/FOV/PostProcess (incl. Manual
	// exposure) in editor mode. Without it, editor-mode Spout output falls back to the
	// SceneCapture's own auto-exposure and lags on lighting changes.
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:

	class UOSCCineCameraComponent* OSCCineCameraComponent;
};
