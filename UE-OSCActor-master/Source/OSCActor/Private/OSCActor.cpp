#include "OSCActor.h"

#include "Async/ParallelFor.h"
#include "OSCActorSubsystem.h"

static float getSample(const TArray<float>& c, int index, float default_value = 0)
{
	if (c.Num() == 0) return default_value;
	return c[index];
}

// ===================================================================================

UOSCActorComponent::UOSCActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
	bTickInEditor = true;
}

void UOSCActorComponent::BeginDestroy()
{
	UOSCActorSubsystem* S = GEngine->GetEngineSubsystem<UOSCActorSubsystem>();
	if (S)
		S->RemoveActorReference(this);

	Super::BeginDestroy();
}

void UOSCActorComponent::OnRegister()
{
	Super::OnRegister();

	UOSCActorSubsystem* S = GEngine->GetEngineSubsystem<UOSCActorSubsystem>();
	if (S)
		S->UpdateActorReference(this);
}

void UOSCActorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CachedSubsystem)
		CachedSubsystem = GEngine->GetEngineSubsystem<UOSCActorSubsystem>();

	if (CachedSubsystem)
		CachedSubsystem->UpdateActorReference(this);
}

float UOSCActorComponent::GetOSCParam(const FString& Key, float DefaultValue)
{
	if (!Params.Contains(Key))
		return DefaultValue;

	return Params[Key];
}

const TArray<float>& UOSCActorComponent::GetOSCMultiSampleParam(const FString& Key)
{
	auto Iter = MultiSampleParams.Find(Key);
	if (!Iter)
	{
		static const TArray<float> a;
		return a;
	}

	const auto& s = *Iter;
	return s.Samples;
}

void UOSCActorComponent::UpdateInstancedStaticMesh(UInstancedStaticMeshComponent* InstancedStaticMesh,
	TArray<FString> InCustomDataChannels)
{
	if (MultiSampleNum == 0)
		return;

	const TArray<float>& tx = GetOSCMultiSampleParam("tx");
	const TArray<float>& ty = GetOSCMultiSampleParam("ty");
	const TArray<float>& tz = GetOSCMultiSampleParam("tz");

	const TArray<float>& rx = GetOSCMultiSampleParam("rx");
	const TArray<float>& ry = GetOSCMultiSampleParam("ry");
	const TArray<float>& rz = GetOSCMultiSampleParam("rz");

	const TArray<float>& sx = GetOSCMultiSampleParam("sx");
	const TArray<float>& sy = GetOSCMultiSampleParam("sy");
	const TArray<float>& sz = GetOSCMultiSampleParam("sz");

	const TArray<float>& vx = GetOSCMultiSampleParam("vx");
	const TArray<float>& vy = GetOSCMultiSampleParam("vy");
	const TArray<float>& vz = GetOSCMultiSampleParam("vz");

	const TArray<float>& ltx = GetOSCMultiSampleParam("ltx");
	const TArray<float>& lty = GetOSCMultiSampleParam("lty");
	const TArray<float>& ltz = GetOSCMultiSampleParam("ltz");

	const TArray<float>& lrx = GetOSCMultiSampleParam("lrx");
	const TArray<float>& lry = GetOSCMultiSampleParam("lry");
	const TArray<float>& lrz = GetOSCMultiSampleParam("lrz");

	const TArray<float>& lsx = GetOSCMultiSampleParam("lsx");
	const TArray<float>& lsy = GetOSCMultiSampleParam("lsy");
	const TArray<float>& lsz = GetOSCMultiSampleParam("lsz");

	// Store pointers instead of copying TArray<float> data
	TArray<const TArray<float>*> SrcCustomDataChannels;
	for (const FString& ChannelName : InCustomDataChannels)
	{
		const TArray<float>& a = GetOSCMultiSampleParam(ChannelName);
		if (a.Num() == MultiSampleNum)
			SrcCustomDataChannels.Add(&a);
	}

	const bool hasDirection = (vx.Num() > 0) && (vy.Num() > 0) && (vz.Num() > 0);
	const bool hasLocalTranslation = (ltx.Num() > 0) || (lty.Num() > 0) || (ltz.Num() > 0);
	const bool hasLocalRotation = (lrx.Num() > 0) || (lry.Num() > 0) || (lrz.Num() > 0);
	const bool hasLocalScale = (lsx.Num() > 0) || (lsy.Num() > 0) || (lsz.Num() > 0);

	// Reuse buffer across calls to avoid per-frame heap allocation
	InstanceDataBuffer.SetNum(MultiSampleNum);

	InstancedStaticMesh->SetSimulatePhysics(false);
	InstancedStaticMesh->DestroyPhysicsState();

	// Batch instance count management: O(n) instead of O(n²) RemoveInstance(0) loop
	const int32 CurrentCount = InstancedStaticMesh->GetInstanceCount();
	if (CurrentCount != MultiSampleNum)
	{
		if (CurrentCount < MultiSampleNum)
		{
			TArray<FTransform> PlaceholderTransforms;
			PlaceholderTransforms.SetNum(MultiSampleNum - CurrentCount);
			InstancedStaticMesh->AddInstances(PlaceholderTransforms, false);
		}
		else
		{
			TArray<int32> ToRemove;
			ToRemove.Reserve(CurrentCount - MultiSampleNum);
			for (int32 j = MultiSampleNum; j < CurrentCount; j++)
				ToRemove.Add(j);
			InstancedStaticMesh->RemoveInstances(ToRemove);
		}
	}

	const int32 NumCustomChannels = SrcCustomDataChannels.Num();
	InstancedStaticMesh->NumCustomDataFloats = NumCustomChannels;
	InstancedStaticMesh->PerInstanceSMCustomData.SetNum(NumCustomChannels * MultiSampleNum);
	float* CustomData = InstancedStaticMesh->PerInstanceSMCustomData.GetData();

	// Pre-combine constant coordinate conversion matrices to save 2 matrix multiplies per instance.
	// Original: ROT_YAW_90_T * (GL_TO_UE4 * T_scaled * GL_TO_UE4_T) * ROT_YAW_90
	// Becomes:  COMBINED_PRE * T_scaled * COMBINED_POST  (associativity)
	static const FMatrix GL_TO_UE4(FPlane(0, 0, -1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
	static const FMatrix GL_TO_UE4_T = GL_TO_UE4.GetTransposed();
	static const FMatrix ROT_YAW_90_T = FRotationMatrix::Make(FRotator(0, 90, 0));
	static const FMatrix ROT_YAW_90   = FRotationMatrix::Make(FRotator(0, -90, 0));
	static const FMatrix COMBINED_PRE  = ROT_YAW_90_T * GL_TO_UE4;
	static const FMatrix COMBINED_POST = GL_TO_UE4_T * ROT_YAW_90;

	// Each instance is independent — safe to compute in parallel.
	// Custom data uses index-based writes (i * NumCustomChannels + n) instead of pointer
	// increments so that each thread writes to a non-overlapping memory region.
	ParallelFor(MultiSampleNum, [&](int32 i)
	{
		FMatrix T = FMatrix::Identity;

		if (hasLocalScale)
		{
			T *= FScaleMatrix::Make(FVector(
				getSample(lsx, i, 1),
				getSample(lsy, i, 1),
				getSample(lsz, i, 1)
			));
		}

		if (hasLocalTranslation)
		{
			T *= FTranslationMatrix::Make(FVector(
				getSample(ltx, i),
				getSample(lty, i),
				getSample(ltz, i)
			));
		}

		if (hasLocalRotation)
		{
			T *= FRotationMatrix::Make(FRotator(
				-getSample(lry, i),
				getSample(lrz, i),
				-getSample(lrx, i)
			));
		}

		T *= FScaleMatrix::Make(FVector(
			getSample(sx, i, 1),
			getSample(sy, i, 1),
			getSample(sz, i, 1)
		));

		if (hasDirection)
		{
			FQuat RR = FQuat::FindBetweenNormals(
				FVector(0, 0, -1),
				FVector(
					getSample(vx, i, 0),
					getSample(vy, i, 0),
					getSample(vz, i, 0)
				).GetUnsafeNormal()
			);
			T *= FRotationMatrix::Make(RR);
		}
		else
		{
			T *= FRotationMatrix::Make(FRotator(
				-getSample(ry, i),
				getSample(rz, i),
				-getSample(rx, i)
			));
		}

		T *= FTranslationMatrix::Make(FVector(
			getSample(tx, i),
			getSample(ty, i),
			getSample(tz, i)
		));

		// Scale translation mm→cm, then apply pre-combined coordinate conversion
		T.SetOrigin(T.GetOrigin() * 100.0f);
		InstanceDataBuffer[i].Transform = COMBINED_PRE * T * COMBINED_POST;

		for (int32 n = 0; n < NumCustomChannels; n++)
			CustomData[i * NumCustomChannels + n] = (*SrcCustomDataChannels[n])[i];
	});

	// bTeleport=true: instance transforms come from OSC as discrete positions, not continuous
	// physical motion. Without this, newly-added instances (which start at the origin from
	// AddInstances above) get their first real transform interpreted as a huge one-frame
	// velocity, producing a motion-vector streak/ghost through TAA/TSR.
	InstancedStaticMesh->BatchUpdateInstancesData(0, MultiSampleNum, InstanceDataBuffer.GetData(), true, true);
}

// ===================================================================================

AOSCActor::AOSCActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, OSCActorComponent(CreateDefaultSubobject<UOSCActorComponent>(TEXT("OSCActorComponent")))
{
	PrimaryActorTick.bCanEverTick = true;
}

float AOSCActor::GetOSCParam(const FString& Key, float DefaultValue)
{
	return OSCActorComponent->GetOSCParam(Key, DefaultValue);
}

const TArray<float>& AOSCActor::GetOSCMultiSampleParam(const FString& Key)
{
	return OSCActorComponent->GetOSCMultiSampleParam(Key);
}

void AOSCActor::UpdateInstancedStaticMesh(UInstancedStaticMeshComponent* InstancedStaticMesh, TArray<FString> InCustomDataChannels)
{
	OSCActorComponent->UpdateInstancedStaticMesh(InstancedStaticMesh, InCustomDataChannels);
}
