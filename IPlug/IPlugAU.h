#pragma once

#include "IPlugBase.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreServices/CoreServices.h>

// Argh!
#define AudioSampleType Float32
#define kAudioFormatFlagsCanonical (kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked)

#if MAC_OS_X_VERSION_MAX_ALLOWED <= MAC_OS_X_VERSION_10_4
	typedef Float32 AudioUnitParameterValue;
	// typedef OSStatus (*AUMIDIOutputCallback)(void*, const AudioTimeStamp*, UInt32, const struct MIDIPacketList*);
	// struct AUMIDIOutputCallbackStruct { AUMIDIOutputCallback midiOutputCallback; void* userData; };
#endif

#include "WDL/heapbuf.h"
#include "WDL/ptrlist.h"
#include "WDL/wdlstring.h"
#include "WDL/wdltypes.h"

class IPlugAU: public IPlugBase
{
public:
	// Use IPLUG_CTOR instead of calling directly (defined in IPlug_include_in_plug_hdr.h).
	IPlugAU(
		void* instanceInfo,
		int nParams,
		const char* channelIOStr,
		int nPresets,
		const char* effectName,
		const char* productName,
		const char* mfrName,
		int vendorVersion,
		int uniqueID,
		int mfrID,
		int latency,
		int plugDoes
	);

	// ----------------------------------------
	// See IPlugBase for the full list of methods that your plugin class can implement.

	// Default implementation to mimic original IPlug AU behavior.
	void OnBypass(bool /* bypassed */) { Reset(); }

	bool AllocStateChunk(int chunkSize = -1);
	bool AllocBankChunk(int chunkSize = -1);

	void BeginInformHostOfParamChange(int idx);
	void InformHostOfParamChange(int idx, double normalizedValue);
	void EndInformHostOfParamChange(int idx);

	// TN: Implemented in IPlugAU.cpp, but commented out for reasons unknown.
	void InformHostOfProgramChange() {}

	double GetSamplePos(); // Samples since start of project.
	double GetTempo();
	void GetTimeSig(int* pNum, int* pDenom);
	int GetHost(); // GetHostVersion() is inherited.

	// Whether the plugin is being used for offline rendering.
	bool IsRenderingOffline() { return IsOffline(); }

	// Tell the host that the graphics resized.
	// Should be called only by the graphics object when it resizes itself.
	void ResizeGraphics(int w, int h) {}

	enum EAUInputType
	{
		eNotConnected = 0,
		eDirectFastProc,
		eDirectNoFastProc,
		eRenderCallback
	};

protected:
	void HostSpecificInit() { GetHost(); }
	void SetBlockSize(int blockSize);
	void SetLatency(int samples);

	// TN: Implemented in IPlugAU.cpp, but reportedly there are no AU hosts
	// that support MIDI out.
	bool SendMidiMsg(const IMidiMsg* /* pMsg */) { return false; }
	bool SendSysEx(const ISysEx* /* pSysEx */) { return false; }

private:
	WDL_FastString mOSXBundleID, mCocoaViewFactoryClassName;
	ComponentInstance mCI;
	double WDL_FIXALIGN mRenderTimestamp, mTempo;
	HostCallbackInfo mHostCallbacks;

	// InScratchBuf is only needed if the upstream connection is a callback.
	// OutScratchBuf is only needed if the downstream connection fails to give us a buffer.
	WDL_TypedBuf<AudioSampleType> mInScratchBuf, mOutScratchBuf;
	WDL_PtrList_DeleteOnDestroy<AURenderCallbackStruct> mRenderNotify;
	// AUMIDIOutputCallbackStruct mMidiCallback;
	WDL_HeapBuf mBufList;

	// Every stereo pair of plugin input or output is a bus.
	// Buses can have zero host channels if the host hasn't connected the bus at all,
	// one host channel if the plugin supports mono and the host has supplied a mono stream,
	// or two host channels if the host has supplied a stereo stream.
	struct BusChannels
	{
		bool mConnected;
		int mNHostChannels, mNPlugChannels, mPlugChannelStartIdx;
	};
	WDL_PtrList_DeleteOnDestroy<BusChannels> mInBuses, mOutBuses;
	BusChannels* GetBus(AudioUnitScope scope, AudioUnitElement busIdx) const;
	static int NHostChannelsConnected(const WDL_PtrList<BusChannels>* pBuses);
	void ClearConnections();

	struct InputBusConnection
	{
		void* mUpstreamObj;
		AudioUnit mUpstreamUnit;
		int mUpstreamBusIdx;
		AudioUnitRenderProc mUpstreamRenderProc;
		AURenderCallbackStruct mUpstreamRenderCallback;
		int mInputType;
	};
	WDL_PtrList_DeleteOnDestroy<InputBusConnection> mInBusConnections;

	bool CheckLegalIO() const;
	void AssessInputConnections();

	void UpdateSampleRate(double sampleRate);
	void UpdateBlockSize(int blockSize);

	ByteChunk mState; // Persistent storage if the host asks for plugin state.

	struct PropertyListener
	{
		AudioUnitPropertyID mPropID;
		AudioUnitPropertyListenerProc mListenerProc;
		void* mProcArgs;
	};
	WDL_PtrList_DeleteOnDestroy<PropertyListener> mPropertyListeners;

	ComponentResult GetProperty(AudioUnitPropertyID propID, AudioUnitScope scope, AudioUnitElement element,
		UInt32* pDataSize, Boolean* pWriteable, void* pData);
	ComponentResult SetProperty(AudioUnitPropertyID propID, AudioUnitScope scope, AudioUnitElement element,
		UInt32* pDataSize, const void* pData);
	ComponentResult GetProc(AudioUnitElement element, UInt32* pDataSize, void* pData) const;
	ComponentResult GetState(CFDictionaryRef* ppDict);
	ComponentResult SetState(CFDictionaryRef pDict);
	void InformListeners(AudioUnitPropertyID propID, AudioUnitScope scope);

public:
	static ComponentResult IPlugAUEntry(ComponentParameters *params, void* pVPlug);

	#ifndef IPLUG_NO_CARBON_SUPPORT
	static ComponentResult IPlugAUCarbonViewEntry(ComponentParameters *params, void* pView);
	#endif

	static ComponentResult GetParamProc(void* pPlug, AudioUnitParameterID paramID, AudioUnitScope scope, AudioUnitElement element,
		AudioUnitParameterValue* pValue);
	static ComponentResult SetParamProc(void* pPlug, AudioUnitParameterID paramID, AudioUnitScope scope, AudioUnitElement element,
		AudioUnitParameterValue value, UInt32 offsetFrames);
	static ComponentResult RenderProc(void* pPlug, AudioUnitRenderActionFlags* pFlags, const AudioTimeStamp* pTimestamp,
		UInt32 outputBusIdx, UInt32 nFrames, AudioBufferList* pBufferList);
}
WDL_FIXALIGN;

IPlugAU* MakeIPlugAU();
