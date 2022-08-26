#pragma once

// Include this file in the main header for your plugin.

#include "resource.h" // This is your plugin's resource.h.

#ifdef VST2_API
	#include "IPlugVST2.h"
	typedef IPlugVST2 IPlug;
	#define API_EXT "vst"
#elif defined AU_API
	#include "IPlugAU.h"
	typedef IPlugAU IPlug;
	#define API_EXT "audiounit"
#elif defined CLAP_API
	#include "IPlugCLAP.h"
	typedef IPlugCLAP IPlug;
	#define API_EXT "clap"
#else
	#error "No API defined!"
#endif

#ifndef PLUG_PRODUCT
	#define PLUG_PRODUCT ""
#endif

#if PLUG_DOES_MIDI
	#define PLUG_DOES_MIDI_IN  1
	#define PLUG_DOES_MIDI_OUT 1
#elif defined PLUG_DOES_MIDI
	#define PLUG_DOES_MIDI_IN  0
	#define PLUG_DOES_MIDI_OUT 0
#endif

#define IPLUG_CTOR(nParams, nPresets, instanceInfo) IPlug( \
	instanceInfo, \
	nParams, \
	PLUG_CHANNEL_IO, \
	nPresets, \
	PLUG_NAME, \
	PLUG_PRODUCT, \
	PLUG_MFR, \
	PLUG_VER, \
	PLUG_UNIQUE_ID, \
	PLUG_MFR_ID, \
	PLUG_LATENCY, \
	(PLUG_IS_INST ? kPlugIsInst : 0) | \
	(PLUG_DOES_MIDI_IN ? kPlugDoesMidiIn : 0) | \
	(PLUG_DOES_MIDI_OUT ? kPlugDoesMidiOut : 0) \
)
