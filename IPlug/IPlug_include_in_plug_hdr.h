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
#else
	#error "No API defined!"
#endif

#if defined _WIN32
  #include "IGraphicsWin.h"
  #define EXPORT __declspec(dllexport)
#elif defined __APPLE__
  #include "IGraphicsMac.h"
  #define EXPORT __attribute__((visibility("default")))
  #ifndef BUNDLE_DOMAIN
    #define BUNDLE_DOMAIN "com." BUNDLE_MFR
  #endif
  #define BUNDLE_ID BUNDLE_DOMAIN "." API_EXT "." BUNDLE_NAME
#else
  #error "No OS defined!"
#endif
