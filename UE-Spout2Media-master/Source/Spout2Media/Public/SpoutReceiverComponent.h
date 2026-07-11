#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpoutReceiverComponent.generated.h"

class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpoutConnected);

UCLASS(ClassGroup=(Spout2), meta=(BlueprintSpawnableComponent), DisplayName="Spout Receiver")
class SPOUT2MEDIA_API USpoutReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpoutReceiverComponent();

	// TD 側の Spout Sender 名
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2")
	FString SenderName = TEXT("Spout");

	// Play 時に自動接続
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2")
	bool bAutoConnect = true;

	// 受信先レンダーターゲット（Content Browser で作成して割り当てる）
	// マテリアルパラメータに直接渡せる
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spout2")
	UTextureRenderTarget2D* OutputRenderTarget;

	// 接続成功時に発火
	UPROPERTY(BlueprintAssignable, Category="Spout2")
	FOnSpoutConnected OnConnected;

	UFUNCTION(BlueprintCallable, Category="Spout2")
	void Connect();

	UFUNCTION(BlueprintCallable, Category="Spout2")
	void Disconnect();

	UFUNCTION(BlueprintCallable, Category="Spout2")
	bool IsConnected() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FRecvContext;
	TSharedPtr<FRecvContext, ESPMode::ThreadSafe> RecvContext;

	bool bReceiving = false;
	bool bWasConnected = false;

	void DoSpoutReceive();

#if WITH_EDITOR
	FTSTicker::FDelegateHandle EditorTickHandle;
	bool EditorTick(float DeltaTime);
#endif
};
