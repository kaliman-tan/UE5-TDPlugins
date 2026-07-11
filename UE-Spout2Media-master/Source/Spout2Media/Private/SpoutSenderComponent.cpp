#include "SpoutSenderComponent.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11on12.h>
#include "Spout.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "Spout2MediaOutput.h"
#include "MediaCapture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHICommandList.h"

struct USpoutSenderComponent::FRTSenderContext
{
	std::string  SenderName_str;
	uint32       Width    = 0;
	uint32       Height   = 0;
	DXGI_FORMAT  DXFormat = DXGI_FORMAT_UNKNOWN;

	ID3D11Device*        D3D11Device     = nullptr;
	ID3D11DeviceContext* DeviceContext   = nullptr;
	ID3D11On12Device*    D3D11on12Device = nullptr;

	// cache wrapped resources per native D3D12 resource pointer
	TMap<void*, ID3D11Resource*> WrappedResourceMap;

	spoutSenderNames Senders;
	spoutDirectX     SDX;

	ID3D11Texture2D* SendingTexture = nullptr;
	HANDLE           SharedHandle   = nullptr;

	FRTSenderContext(const FString& Name, uint32 W, uint32 H, DXGI_FORMAT Fmt)
		: Width(W), Height(H), DXFormat(Fmt)
	{
		SenderName_str = TCHAR_TO_ANSI(*Name);

		const FString RHIName = GDynamicRHI->GetName();
		if (RHIName == TEXT("D3D12"))
		{
			ID3D12Device*       Dev12   = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
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

		verify(Senders.CreateSender(SenderName_str.c_str(), Width, Height, SharedHandle, Fmt));
		verify(SDX.CreateSharedDX11Texture(D3D11Device, Width, Height, Fmt, &SendingTexture, SharedHandle));
	}

	~FRTSenderContext()
	{
		for (auto& Pair : WrappedResourceMap)
		{
			if (D3D11on12Device && Pair.Value)
				D3D11on12Device->ReleaseWrappedResources(&Pair.Value, 1);
			if (Pair.Value)
				Pair.Value->Release();
		}
		WrappedResourceMap.Reset();

		if (SendingTexture)  { SendingTexture->Release();  SendingTexture  = nullptr; }
		if (D3D11on12Device) { D3D11on12Device->Release(); D3D11on12Device = nullptr; }
		if (DeviceContext)   { DeviceContext->Release();   DeviceContext   = nullptr; }
		if (D3D11Device)     { D3D11Device->Release();     D3D11Device     = nullptr; }
	}

	void Send(FTextureRHIRef InTexture, FRHICommandListImmediate& RHICmdList)
	{
		if (!DeviceContext || !SendingTexture) return;

		const FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("D3D11"))
		{
			ID3D11Texture2D* SrcTex = static_cast<ID3D11Texture2D*>(InTexture->GetNativeResource());
			DeviceContext->CopyResource(SendingTexture, SrcTex);
			DeviceContext->Flush();
		}
		else if (RHIName == TEXT("D3D12") && D3D11on12Device)
		{
			void* Key = InTexture->GetNativeResource();

			ID3D11Resource* Wrapped = nullptr;
			if (ID3D11Resource** Found = WrappedResourceMap.Find(Key))
			{
				Wrapped = *Found;
			}
			else
			{
				ID3D12Resource* Res12 = static_cast<ID3D12Resource*>(Key);
				D3D11_RESOURCE_FLAGS RF = {};
				// InState: CopySrc — we transition the RT to this state via RHICmdList before calling Send
				// OutState: RTV — SceneCaptureComponent2D will render to it again next frame
				if (D3D11on12Device->CreateWrappedResource(Res12, &RF,
					D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET,
					__uuidof(ID3D11Resource), reinterpret_cast<void**>(&Wrapped)) != S_OK)
					return;
				WrappedResourceMap.Add(Key, Wrapped);
			}

			// Transition source RT to CopySrc via UE's RHI so state tracker stays consistent
			RHICmdList.Transition({ FRHITransitionInfo(InTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc) });

			D3D11on12Device->AcquireWrappedResources(&Wrapped, 1);
			DeviceContext->CopyResource(SendingTexture, static_cast<ID3D11Texture2D*>(Wrapped));
			D3D11on12Device->ReleaseWrappedResources(&Wrapped, 1);
			DeviceContext->Flush();

			// Transition back to RTV for next SceneCapture render
			RHICmdList.Transition({ FRHITransitionInfo(InTexture, ERHIAccess::CopySrc, ERHIAccess::RTV) });

			Senders.UpdateSender(SenderName_str.c_str(), Width, Height, SharedHandle);
		}
	}
};

//////////////////////////////////////////////////////////////////////////

USpoutSenderComponent::USpoutSenderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USpoutSenderComponent::OnRegister()
{
	Super::OnRegister();
	UWorld* World = GetWorld();
	if (World && !World->IsGameWorld() && bAutoCapture && CaptureMode == ESpoutCaptureMode::RenderTarget)
	{
		StartCapture();
#if WITH_EDITOR
		EditorTickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &USpoutSenderComponent::EditorTick));
#endif
	}
}

void USpoutSenderComponent::OnUnregister()
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
		StopCapture();
	}
	Super::OnUnregister();
}

#if WITH_EDITOR
bool USpoutSenderComponent::EditorTick(float DeltaTime)
{
	DoRTSend();
	return true;
}
#endif

void USpoutSenderComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoCapture)
		StartCapture();
}

void USpoutSenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopCapture();
	Super::EndPlay(EndPlayReason);
}

void USpoutSenderComponent::DoRTSend()
{
	if (!bCapturing || CaptureMode != ESpoutCaptureMode::RenderTarget || !RenderTarget) return;

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	FString Name = SenderName;

	ENQUEUE_RENDER_COMMAND(SpoutRTSend)([this, RTResource, Name](FRHICommandListImmediate& RHICmdList)
	{
		FTextureRHIRef Texture = RTResource->GetRenderTargetTexture();
		if (!Texture.IsValid()) return;

		const uint32 W = Texture->GetSizeX();
		const uint32 H = Texture->GetSizeY();

		DXGI_FORMAT Fmt = DXGI_FORMAT_UNKNOWN;
		switch (Texture->GetFormat())
		{
		case PF_B8G8R8A8:      Fmt = DXGI_FORMAT_B8G8R8A8_UNORM;       break;
		case PF_FloatRGBA:     Fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;   break;
		case PF_A32B32G32R32F: Fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;   break;
		case PF_A2B10G10R10:   Fmt = DXGI_FORMAT_R10G10B10A2_UNORM;    break;
		default: return;
		}

		if (!RTSenderContext
			|| RTSenderContext->Width    != W
			|| RTSenderContext->Height   != H
			|| RTSenderContext->DXFormat != Fmt)
		{
			RTSenderContext.Reset();
			RTSenderContext = MakeShared<FRTSenderContext, ESPMode::ThreadSafe>(Name, W, H, Fmt);
		}

		RTSenderContext->Send(Texture, RHICmdList);
	});
}

void USpoutSenderComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	DoRTSend();
}

void USpoutSenderComponent::StartCapture()
{
	StopCapture();
	bCapturing = true;

	if (CaptureMode == ESpoutCaptureMode::RenderTarget)
	{
		SetComponentTickEnabled(true);
	}
	else
	{
		MediaOutput = NewObject<USpout2MediaOutput>(this);
		MediaOutput->SenderName = SenderName;
		MediaOutput->OutputSize = OutputSize;

		MediaCapture = MediaOutput->CreateMediaCapture();
		if (MediaCapture)
		{
			FMediaCaptureOptions Options;
			MediaCapture->CaptureActiveSceneViewport(Options);
		}
	}
}

void USpoutSenderComponent::StopCapture()
{
	bCapturing = false;
	SetComponentTickEnabled(false);

	FlushRenderingCommands();
	RTSenderContext.Reset();

	if (MediaCapture)
	{
		MediaCapture->StopCapture(false);
		MediaCapture = nullptr;
	}
	MediaOutput = nullptr;
}

bool USpoutSenderComponent::IsCapturing() const
{
	if (CaptureMode == ESpoutCaptureMode::RenderTarget)
		return bCapturing;

	return MediaCapture && MediaCapture->GetState() == EMediaCaptureState::Capturing;
}
