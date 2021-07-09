#ifndef __APPLE__
	#define __APPLE__
#endif

#include "resource.h" // This is your plugin's resource.h.
#include <AudioUnit/AudioUnit.r>

#define UseExtendedThingResource 1

#include <CoreServices/CoreServices.r>

// This is a define used to indicate that a component has no static data that would mean
// that no more than one instance could be open at a time - never been true for AUs.
#ifndef cmpThreadSafeOnMac
	#define cmpThreadSafeOnMac 0x10000000
#endif

#undef TARGET_REZ_MAC_X86
#if defined(__i386__) || defined(i386_YES)
	#define TARGET_REZ_MAC_X86 1
#else
	#define TARGET_REZ_MAC_X86 0
#endif

#undef TARGET_REZ_MAC_X86_64
#if defined(__x86_64__) || defined(x86_64_YES)
	#define TARGET_REZ_MAC_X86_64 1
#else
	#define TARGET_REZ_MAC_X86_64 0
#endif

#undef TARGET_REZ_MAC_ARM64
#if defined(__arm64__) || defined(ARM64_YES)
	#define TARGET_REZ_MAC_ARM64 1
#else
	#define TARGET_REZ_MAC_ARM64 0
#endif

#if TARGET_OS_MAC
	#if TARGET_REZ_MAC_ARM64 && TARGET_REZ_MAC_X86_64
		#define TARGET_REZ_FAT_COMPONENTS_2 1
		#define Target_PlatformType         platformArm64NativeEntryPoint
		#define Target_SecondPlatformType   platformX86_64NativeEntryPoint
	#elif TARGET_REZ_MAC_X86 && TARGET_REZ_MAC_X86_64
		#define TARGET_REZ_FAT_COMPONENTS_2 1
		#define Target_PlatformType         platformIA32NativeEntryPoint
		#define Target_SecondPlatformType   platformX86_64NativeEntryPoint
	#elif TARGET_REZ_MAC_X86
		#define Target_PlatformType         platformIA32NativeEntryPoint
	#elif TARGET_REZ_MAC_X86_64
		#define Target_PlatformType         platformX86_64NativeEntryPoint
	#elif TARGET_REZ_MAC_ARM64
		#define Target_PlatformType         platformArm64NativeEntryPoint
	#else
		#error "You gotta target something!"
	#endif
	#define Target_CodeResType 'dlle'
	#define TARGET_REZ_USE_DLLE 1
#else
	#error "Get a real platform type!"
#endif // TARGET_OS_MAC

#ifndef TARGET_REZ_FAT_COMPONENTS_2
	#define TARGET_REZ_FAT_COMPONENTS_2 0
#endif

// ----------------

#define PLUG_PUBLIC_NAME PLUG_NAME

#define RES_ID 1000
#define RES_NAME PLUG_MFR ": " PLUG_PUBLIC_NAME

resource 'STR ' (RES_ID, purgeable)
{
	RES_NAME
};

resource 'STR ' (RES_ID + 1, purgeable)
{
	PLUG_PUBLIC_NAME " AU"
};

resource 'dlle' (RES_ID)
{
	PLUG_ENTRY_STR
};

#ifndef PLUG_DOES_MIDI
	#define PLUG_DOES_MIDI (PLUG_DOES_MIDI_IN || PLUG_DOES_MIDI_OUT)
#endif

resource 'thng' (RES_ID, RES_NAME)
{
	#if PLUG_IS_INST
		kAudioUnitType_MusicDevice,
	#elif PLUG_DOES_MIDI
		kAudioUnitType_MusicEffect,
	#else
		kAudioUnitType_Effect,
	#endif

	PLUG_UNIQUE_ID,
	PLUG_MFR_ID,
	0, 0, 0, 0, // no 68K
	'STR ',	RES_ID,
	'STR ',	RES_ID + 1,
	0, 0, // icon
	PLUG_VER,
	componentHasMultiplePlatforms | componentDoAutoVersion,
	0,

	{
		cmpThreadSafeOnMac,
		Target_CodeResType, RES_ID,
		Target_PlatformType,

		#if TARGET_REZ_FAT_COMPONENTS_2
			cmpThreadSafeOnMac,
			Target_CodeResType, RES_ID,
			Target_SecondPlatformType,
		#endif
	}
};

#undef RES_ID

#if TARGET_REZ_MAC_X86

#define RES_ID 2000
#undef RES_NAME
#define RES_NAME PLUG_MFR ": " PLUG_PUBLIC_NAME " Carbon View"

resource 'STR ' (RES_ID, purgeable)
{
	RES_NAME
};

resource 'STR ' (RES_ID + 1, purgeable)
{
	PLUG_PUBLIC_NAME " AU Carbon View"
};

resource 'dlle' (RES_ID)
{
	PLUG_VIEW_ENTRY_STR
};

resource 'thng' (RES_ID, RES_NAME)
{
	kAudioUnitCarbonViewComponentType,
	PLUG_UNIQUE_ID,
	PLUG_MFR_ID,
	0, 0, 0, 0, // no 68K
	'STR ', RES_ID,
	'STR ', RES_ID + 1,
	0, 0, // icon
	PLUG_VER,
	componentHasMultiplePlatforms | componentDoAutoVersion,
	0,

	{
		cmpThreadSafeOnMac,
		Target_CodeResType, RES_ID,
		Target_PlatformType,

		#if TARGET_REZ_FAT_COMPONENTS_2
			cmpThreadSafeOnMac,
			Target_CodeResType, RES_ID,
			Target_SecondPlatformType,
		#endif
	}
};

#undef RES_ID

#endif // TARGET_REZ_MAC_X86
