// Copyright Epic Games, Inc. All Rights Reserved.

#include "OSCActorModule.h"
#include "OSCActorSubsystem.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#endif

#define LOCTEXT_NAMESPACE "FOSCActorModule"

void FOSCActorModule::StartupModule()
{
#if WITH_EDITOR
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->RegisterSettings(
			"Project",
			"Plugins",
			"OSCActor",
			FText::FromString(TEXT("OSCActor")),
			FText::FromString(TEXT("")),
			GetMutableDefault<UOSCActorSettings>()
		);
	}
#endif
}

void FOSCActorModule::ShutdownModule()
{
#if WITH_EDITOR
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->UnregisterSettings(
			"Project",
			"Plugins",
			"OSCActor"
		);
	}
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FOSCActorModule, OSCActor)