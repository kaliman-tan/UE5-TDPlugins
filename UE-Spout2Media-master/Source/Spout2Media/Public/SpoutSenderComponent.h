#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpoutSenderComponent.generated.h"

class UMediaCapture;
class USpout2MediaOutput;
class UTextureRenderTarget2D;

UENUM(BlueprintType)
enum class ESpoutCaptureMode : uint8
{
	// SceneCaptureComponent2D の RenderTarget を送信
	// 主な用途: OSCCineCameraActor + SceneCaptureComponent2D の映像を TD に渡す
	RenderTarget UMETA(DisplayName="Render Target"),

	// ゲームのメインビューポート全体を送信
	Viewport     UMETA(DisplayName="Viewport"),
};

UCLASS(ClassGroup=(Spout2), meta=(BlueprintSpawnableComponent), DisplayName="Spout Sender")
class SPOUT2MEDIA_API USpoutSenderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpoutSenderComponent();

	// TD 側で受け取る Spout Sender 名
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2")
	FString SenderName = TEXT("UE5");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2")
	ESpoutCaptureMode CaptureMode = ESpoutCaptureMode::RenderTarget;

	// RenderTarget モード: SceneCaptureComponent2D の RenderTarget を指定
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2",
		meta=(EditCondition="CaptureMode==ESpoutCaptureMode::RenderTarget", EditConditionHides))
	UTextureRenderTarget2D* RenderTarget;

	// Viewport モードの送信解像度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2",
		meta=(EditCondition="CaptureMode==ESpoutCaptureMode::Viewport", EditConditionHides))
	FIntPoint OutputSize = FIntPoint(1920, 1080);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2")
	bool bAutoCapture = true;

	UFUNCTION(BlueprintCallable, Category="Spout2")
	void StartCapture();

	UFUNCTION(BlueprintCallable, Category="Spout2")
	void StopCapture();

	UFUNCTION(BlueprintCallable, Category="Spout2")
	bool IsCapturing() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FRTSenderContext;
	TSharedPtr<FRTSenderContext, ESPMode::ThreadSafe> RTSenderContext;

	bool bCapturing = false;

	void DoRTSend();

	UPROPERTY(Transient)
	USpout2MediaOutput* MediaOutput;

	UPROPERTY(Transient)
	UMediaCapture* MediaCapture;

#if WITH_EDITOR
	FTSTicker::FDelegateHandle EditorTickHandle;
	bool EditorTick(float DeltaTime);
#endif
};
