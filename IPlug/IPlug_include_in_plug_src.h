#pragma once

// Include this file in the main source for your plugin,
// after #including the main header for your plugin.

#ifdef _WIN32

#include "IGraphicsWin.h"
#define EXPORT __declspec(dllexport)

HINSTANCE gHInstance = NULL;

extern "C" BOOL WINAPI DllMain(HINSTANCE const hDllInst, DWORD /* fdwReason */, LPVOID /* res */)
{
	gHInstance = hDllInst;
	return TRUE;
}

IGraphics* MakeGraphics(IPlug* const pPlug, const int w, const int h, const int FPS = 0)
{
	IGraphicsWin* const pGraphics = new IGraphicsWin(pPlug, w, h, FPS);
	pGraphics->SetHInstance(gHInstance);
	return pGraphics;
}

#elif defined(__APPLE__)

#include "IGraphicsMac.h"
#define EXPORT __attribute__((visibility("default")))
#ifndef BUNDLE_DOMAIN
	#define BUNDLE_DOMAIN "com." BUNDLE_MFR
#endif
#define BUNDLE_ID BUNDLE_DOMAIN "." API_EXT "." BUNDLE_NAME

IGraphics* MakeGraphics(IPlug* const pPlug, const int w, const int h, const int FPS = 0)
{
	IGraphicsMac* const pGraphics = new IGraphicsMac(pPlug, w, h, FPS);
	pGraphics->SetBundleID(BUNDLE_ID);
	return pGraphics;
}

#else
	#error "No OS defined!"
#endif

#ifdef VST2_API

extern "C" {

EXPORT void* VSTPluginMain(audioMasterCallback const hostCallback)
{
	static WDL_Mutex sMutex;
	sMutex.Enter();

	AEffect* pEffect = NULL;
	IPlugVST2* const pPlug = new PLUG_CLASS_NAME((void*)hostCallback);
	if (pPlug)
	{
		pPlug->EnsureDefaultPreset();
		pEffect = pPlug->GetAEffect();
		pEffect->numPrograms = wdl_max(pEffect->numPrograms, 1);
	}

	sMutex.Leave();
	return pEffect;
}

#if defined(_WIN32) && !defined(_WIN64)
EXPORT int main(audioMasterCallback const hostCallback)
{
	return (VstIntPtr)VSTPluginMain(hostCallback);
}
#endif

} // extern "C"

#elif defined(AU_API)

IPlug* MakeIPlugAU()
{
	static WDL_Mutex sMutex;
	sMutex.Enter();

	static const char* const ID[] = { BUNDLE_ID, VIEW_CLASS_STR };
	IPlugAU* const pPlug = new PLUG_CLASS_NAME((void*)ID);

	sMutex.Leave();
	return pPlug;
}

extern "C" {

ComponentResult PLUG_ENTRY(ComponentParameters* const params, void* const pPlug)
{
	return IPlugAU::IPlugAUEntry(params, pPlug);
}

#ifndef IPLUG_NO_CARBON_SUPPORT
ComponentResult PLUG_VIEW_ENTRY(ComponentParameters* const params, void* const pView)
{
	return IPlugAU::IPlugAUCarbonViewEntry(params, pView);
}
#endif

void* PLUG_FACTORY(const AudioComponentDescription* const pDesc)
{
	return IPlugAU::IPlugAUFactory(pDesc);
}

} // extern "C"

#elif defined(CLAP_API)

#include <string.h>

extern "C" {

static const char* const sClapPlugID = BUNDLE_DOMAIN ".clap." BUNDLE_NAME;

uint32_t CLAP_ABI ClapFactoryGetPluginCount(const clap_plugin_factory* /* pFactory */)
{
	return 1;
}

const clap_plugin_descriptor* CLAP_ABI ClapFactoryGetPluginDescriptor(const clap_plugin_factory* /* pFactory */, const uint32_t index)
{
	if (index != 0) return NULL;

	static const char* const features[] =
	{
		#if PLUG_IS_INST
		CLAP_PLUGIN_FEATURE_INSTRUMENT,
		#else
		CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
		#endif

		CLAP_PLUGIN_FEATURE_STEREO,
		NULL
	};

	static const clap_plugin_descriptor desc =
	{
		CLAP_VERSION,
		sClapPlugID,
		PLUG_NAME,
		PLUG_MFR,
		NULL,
		NULL,
		NULL,
		VERSIONINFO_STR,
		NULL,
		(const char**)features
	};

	return &desc;
}

const clap_plugin* CLAP_ABI ClapFactoryCreatePlugin(const clap_plugin_factory* /* pFactory */, const clap_host* const pHost, const char* const id)
{
	clap_plugin* pClap = NULL;

	if (clap_version_is_compatible(pHost->clap_version) && !strcmp(id, sClapPlugID))
	{
		IPlugCLAP* const pPlug = new PLUG_CLASS_NAME((void*)pHost);
		if (pPlug)
		{
			pPlug->EnsureDefaultPreset();
			pClap = pPlug->GetTheClap();
		}
	}

	return pClap;
}

CLAP_EXPORT const clap_plugin_entry clap_entry =
{
	CLAP_VERSION,
	IPlugCLAP::ClapEntryInit,
	IPlugCLAP::ClapEntryDeinit,
	IPlugCLAP::ClapEntryGetFactory
};

} // extern "C"

#else
	#error "No API defined!"
#endif
