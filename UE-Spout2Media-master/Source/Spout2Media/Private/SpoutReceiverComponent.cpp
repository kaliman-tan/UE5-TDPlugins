#include "SpoutReceiverComponent.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11on12.h>
#include "Spout.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "Engine/TextureRenderTarget2D.h"
#include "RHICommandList.h"

static spoutSenderNames SpoutRecvSenders;

struct USpoutReceiverComponent::FRecvContext
{
	ID3D11Device*        D3D11Device     = nullptr;
	ID3D11DeviceContext* DeviceContext   = nullptr;
	ID3D11On12Device*    D3D11on12Device = nullptr;

	FRecvContext()
	{
		const FString RHIName = GDynamicRHI->GetName();
		if (RHIName == TEXT("D3D12"))
		{
			ID3D12Device* Dev12 = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
			ID3D12CommandQueue* UEQueue = static_cast<ID3D12CommandQueue*>(GDynamicRHI->RHIGetNativeGraphicsQueue());
			IUnknown* Queues[] = { UEQueue };
			verify(D3D11On12CreateDevice(Dev12, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				nullptr, 0,
				UEQueue ? Queues : nullptr, UEQueue ? 1 : 0,
				0, &D3D11Device, &DeviceContext, nullptr) == S_OK);
			verify(D3D11Device->QueryInterface(__uuidof(ID3D11On12Device),
				reinterpret_cast<void**>(&D3D11on12Device)) == S_OK);
		}
		else
		{
			D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			D3D11Device->GetImmediateContext(&DeviceContext);
		}
	}

	~FRecvContext()
	{
		if (D3D11on12Device) { D3D11on12Device->Release(); D3D11on12Device = nullptr; }
		if (DeviceContext)   { DeviceContext->Release();   DeviceContext   = nullptr; }
		if (D3D11Device)     { D3D11Device->Release();     D3D11Device     = nullptr; }
	}

	void Receive(HANDLE SharedHandle, FTextureRHIRef DestTex, FRHICommandListImmediate& RHICmdList)
	{
		if (!DeviceContext || !DestTex.IsValid()) return;

		ID3D11Resource* SrcTex = nullptr;
		if (D3D11Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Resource), (void**)&SrcTex) != S_OK)
			return;

		const FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("D3D11"))
		{
			ID3D11Texture2D* DstNative = static_cast<ID3D11Texture2D*>(DestTex->GetNativeResource());
			DeviceContext->CopyResource(DstNative, SrcTex);
			DeviceContext->Flush();
		}
		else if (RHIName == TEXT("D3D12") && D3D11on12Device)
		{
			ID3D12Resource* Res12 = static_cast<ID3D12Resource*>(DestTex->GetNativeResource());
			if (!Res12) { SrcTex->Release(); return; }

			// Transition to a known state via UE's RHI before D3D11On12 acquires the resource
			RHICmdList.Transition({ FRHITransitionInfo(DestTex, ERHIAccess::Unknown, ERHIAccess::CopyDest) });

			ID3D11Resource* DstWrapper = nullptr;
			D3D11_RESOURCE_FLAGS RF = {};
			HRESULT WR = D3D11on12Device->CreateWrappedResource(Res12, &RF,
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_COPY_DEST,
				__uuidof(ID3D11Resource), (void**)&DstWrapper);
			if (WR == S_OK)
			{
				D3D11on12Device->AcquireWrappedResources(&DstWrapper, 1);
				DeviceContext->CopyResource(DstWrapper, SrcTex);
				D3D11on12Device->ReleaseWrappedResources(&DstWrapper, 1);
				DeviceContext->Flush();
				DstWrapper->Release();
			}

			// Transition back to SRV so UE can sample from it
			RHICmdList.Transition({ FRHITransitionInfo(DestTex, ERHIAccess::CopyDest, ERHIAccess::SRVGraphics) });
		}

		SrcTex->Release();
	}
};

//////////////////////////////////////////////////////////////////////////

USpoutReceiverComponent::USpoutReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USpoutReceiverComponent::OnRegister()
{
	Super::OnRegister();
	UWorld* World = GetWorld();
	if (World && !World->IsGameWorld() && bAutoConnect)
	{
		Connect();
#if WITH_EDITOR
		EditorTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &USpoutReceiverComponent::EditorTick));
#endif
	}
}

void USpoutReceiverComponent::OnUnregister()
{
	UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
#if WITH_EDITOR
		if (EditorTickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(EditorTickHandle);
			EditorTickHandle.Reset();
		}
#endif
		Disconnect();
	}
	Super::OnUnregister();
}

#if WITH_EDITOR
bool USpoutReceiverComponent::EditorTick(float DeltaTime)
{
	DoSpoutReceive();
	return true;
}
#endif

void USpoutReceiverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoConnect)
		Connect();
}

void USpoutReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Disconnect();
	Super::EndPlay(EndPlayReason);
}

void USpoutReceiverComponent::Connect()
{
	bReceiving = true;
	bWasConnected = false;
	SetComponentTickEnabled(true);
}

void USpoutReceiverComponent::Disconnect()
{
	bReceiving = false;
	bWasConnected = false;
	SetComponentTickEnabled(false);
	FlushRenderingCommands();
	RecvContext.Reset();
}

bool USpoutReceiverComponent::IsConnected() const
{
	return bReceiving && bWasConnected;
}

void USpoutReceiverComponent::DoSpoutReceive()
{
	if (!bReceiving || !OutputRenderTarget) return;

	unsigned int W = 0, H = 0;
	HANDLE ShareHandle = nullptr;
	DXGI_FORMAT Fmt = DXGI_FORMAT_UNKNOWN;

	const bool bFound = SpoutRecvSenders.FindSender(
		TCHAR_TO_ANSI(*SenderName), W, H, ShareHandle, reinterpret_cast<DWORD&>(Fmt));

	if (!bFound) return;

	if (!bWasConnected)
	{
		bWasConnected = true;
		OnConnected.Broadcast();
	}

	FTextureRenderTargetResource* RTResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	ENQUEUE_RENDER_COMMAND(SpoutReceive)([this, RTResource, ShareHandle](FRHICommandListImmediate& RHICmdList)
	{
		FTextureRHIRef DestTex = RTResource->GetRenderTargetTexture();
		if (!DestTex.IsValid()) return;

		if (!RecvContext)
			RecvContext = MakeShared<FRecvContext, ESPMode::ThreadSafe>();

		RecvContext->Receive(ShareHandle, DestTex, RHICmdList);
	});
}

void USpoutReceiverComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	DoSpoutReceive();
}
