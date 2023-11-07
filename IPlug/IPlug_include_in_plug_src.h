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

	IPlugVST2* const pPlug = new PLUG_CLASS_NAME((void*)hostCallback);

	pPlug->EnsureDefaultPreset();
	AEffect* const pEffect = pPlug->GetAEffect();
	pEffect->numPrograms = wdl_max(pEffect->numPrograms, 1);

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

const unsigned int* GetIPlugVST2UniqueGUID()
{
	#ifdef PLUG_UNIQUE_GUID
	static const unsigned int guid[4] = { PLUG_UNIQUE_GUID };
	return guid;
	#else
	return NULL;
	#endif
}

#elif defined(VST3_API)

#include "VST3_SDK/pluginterfaces/base/iplugincompatibility.h"
#include "VST3_SDK/public.sdk/source/main/pluginfactory.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <assert.h>
#include <string.h>

extern "C" {

#ifdef __APPLE__
SMTG_EXPORT_SYMBOL bool bundleEntry(CFBundleRef) { return true; }
SMTG_EXPORT_SYMBOL bool bundleExit(void) { return true; }
#endif

// BEGIN_FACTORY_DEF(PLUG_MFR, PLUG_MFR_URL, PLUG_MFR_EMAIL)
SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
	if (!Steinberg::gPluginFactory)
	{
		static Steinberg::PFactoryInfo factoryInfo
		(
			PLUG_MFR,

			#ifdef PLUG_MFR_URL
			PLUG_MFR_URL,
			#else
			"",
			#endif

			#ifdef PLUG_MFR_EMAIL
			"mailto:" PLUG_MFR_EMAIL,
			#else
			"",
			#endif

			Steinberg::Vst::kDefaultFactoryFlags
		);

		Steinberg::gPluginFactory = new Steinberg::CPluginFactory(factoryInfo);

		const Steinberg::FUID plugUID(PLUG_UNIQUE_GUID);
		static const Steinberg::uint32 classFlags = 0;

		/* DEF_CLASS2(
			INLINE_UID_FROM_FUID(plugUID),
			PClassInfo::kManyInstances,
			kVstAudioEffectClass,
			PLUG_NAME,
			classFlags,
			Vst::PlugType::kFx,
			VERSIONINFO_STR,
			kVstVersionString,
			IPlugVST3::VSTCreateInstance
		) */
		Steinberg::TUID lcid = INLINE_UID_FROM_FUID(plugUID);

		static Steinberg::PClassInfo2 componentClass
		(
			lcid,
			Steinberg::PClassInfo::kManyInstances,
			kVstAudioEffectClass,
			PLUG_NAME,
			classFlags,

			#if PLUG_IS_INST
			Steinberg::Vst::PlugType::kInstrument,
			#else
			Steinberg::Vst::PlugType::kFx,
			#endif

			NULL,
			VERSIONINFO_STR,
			kVstVersionString
		);

		Steinberg::gPluginFactory->registerClass(&componentClass, IPlugVST3::VSTCreateInstance);

		#ifdef VST3_COMPAT_GUID

		const Steinberg::FUID compatUID(VST3_COMPAT_GUID);

		/* DEF_CLASS2(
			INLINE_UID_FROM_FUID(compatUID),
			PClassInfo::kManyInstances,
			kPluginCompatibilityClass,
			PLUG_NAME,
			classFlags,
			Vst::PlugType::kFx,
			VERSIONINFO_STR,
			kVstVersionString,
			IPlugVST3::VSTCreateCompatibilityInstance
		) */

		Steinberg::TUID ccid = INLINE_UID_FROM_FUID(compatUID);

		static Steinberg::PClassInfo2 compatClass
		(
			ccid,
			Steinberg::PClassInfo::kManyInstances,
			kPluginCompatibilityClass,
			PLUG_NAME,
			classFlags,

			#if PLUG_IS_INST
			Steinberg::Vst::PlugType::kInstrument,
			#else
			Steinberg::Vst::PlugType::kFx,
			#endif

			NULL,
			VERSIONINFO_STR,
			kVstVersionString
		);

		Steinberg::gPluginFactory->registerClass(&compatClass, IPlugVST3::VSTCreateCompatibilityInstance);

		#endif // VST3_COMPAT_GUID

	// END_FACTORY
	}
	else Steinberg::gPluginFactory->addRef();

	return Steinberg::gPluginFactory;
}

} // extern "C"

IPlug* MakeIPlugVST3(void* const instanceInfo)
{
	static WDL_Mutex sMutex;
	sMutex.Enter();

	IPlugVST3* const pPlug = new PLUG_CLASS_NAME(instanceInfo);

	sMutex.Leave();
	return pPlug;
}

int GetVST3CompatibilityGUIDs(
	#ifdef VST3_COMPAT_OLD_GUIDS
	char* const pNew, const Steinberg::char8** const ppOld
	#else
	char* /* pNew */, const Steinberg::char8** /* ppOld */
	#endif
) {
	#ifdef VST3_COMPAT_OLD_GUIDS
	static const Steinberg::char8 guids[] = { VST3_COMPAT_OLD_GUIDS };

	static const int nOld = ((int)sizeof(guids) / sizeof(guids[0]) - 1) / 32;
	assert(sizeof(guids) == nOld * 32 + 1);

	const Steinberg::FUID plugUID(PLUG_UNIQUE_GUID);
	plugUID.toTUID(pNew);

	*ppOld = guids;

	#else
	static const int nOld = 0;
	#endif

	return nOld;
}

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

		pPlug->EnsureDefaultPreset();
		pClap = pPlug->GetTheClap();
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

#elif defined AAX_API

static AAX_CEffectParameters* AAX_CALLBACK MakeIPlugAAX()
{
	static WDL_Mutex sMutex;
	sMutex.Enter();

	IPlugAAX* const pPlug = new PLUG_CLASS_NAME(NULL);
	AAX_CEffectParameters* const pEffectParams = pPlug->AAXCreateParams(pPlug);

	pPlug->EnsureDefaultPreset();

	sMutex.Leave();
	return pEffectParams;
}

AAX_Result GetEffectDescriptions(AAX_ICollection* pCollection)
{
	AAX_CheckedResult err;

	#ifndef PLUG_SHORT_NAME

	const char* shortName = NULL;
	static const int maxLen = 16;

	static const int bufSize = maxLen + 1;
	char buf[bufSize];

	if (strlen(PLUG_NAME) >= bufSize)
	{
		shortName = buf;
		lstrcpyn_safe(buf, PLUG_NAME, bufSize);
	}

	#else

	assert(strlen(PLUG_SHORT_NAME) <= 16); // maxLen
	static const char* const shortName = PLUG_SHORT_NAME;

	#endif

	AAX_IEffectDescriptor* const pPlugDesc = pCollection->NewDescriptor();
	if (pPlugDesc)
	{
		const int plugDoes =
			(PLUG_IS_INST ? IPlugBase::kPlugIsInst : 0) |
			(PLUG_DOES_MIDI_IN ? IPlugBase::kPlugDoesMidiIn : 0);

		err = IPlugAAX::AAXDescribeEffect(pPlugDesc, PLUG_NAME, shortName,
			PLUG_UNIQUE_ID, PLUG_MFR_ID, PLUG_LATENCY, plugDoes, (void*)MakeIPlugAAX);

		err = pCollection->AddEffect(BUNDLE_DOMAIN ".aaxplugin." BUNDLE_NAME, pPlugDesc);
	}
	else
	{
		err = AAX_ERROR_NULL_OBJECT;
	}

	err = pCollection->SetManufacturerName(PLUG_MFR);
	err = pCollection->AddPackageName(PLUG_NAME);
	if (shortName) err = pCollection->AddPackageName(shortName);
	err = pCollection->SetPackageVersion(PLUG_VER);

	return err;
}

#else
	#error "No API defined!"
#endif
