#pragma once

#include "IPlugBase.h"
#include "VST2_SDK/aeffectx.h"

class IPlugVST2: public IPlugBase
{
public:
	// Use IPLUG_CTOR instead of calling directly (defined in IPlug_include_in_plug_hdr.h).
	IPlugVST2(
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

	// Default implementation to mimic original IPlug VST2 behavior.
	void OnActivate(const bool active) { if (!active) Reset(); }

	bool AllocStateChunk(int chunkSize = -1);
	bool AllocBankChunk(int chunkSize = -1);

	void SetPresetName(int idx, const char* name);

	void BeginInformHostOfParamChange(int idx, bool lockMutex = true);
	void InformHostOfParamChange(int idx, double normalizedValue, bool lockMutex = true);
	void EndInformHostOfParamChange(int idx, bool lockMutex = true);

	void InformHostOfProgramChange();

	double GetSamplePos(); // Samples since start of project.
	double GetTempo();
	void GetTimeSig(int* pNum, int* pDenom);
	int GetHost(); // GetHostVersion() is inherited.

	// Whether the plugin is being used for offline rendering.
	bool IsRenderingOffline();

	// Tell the host that the graphics resized.
	// Should be called only by the graphics object when it resizes itself.
	void ResizeGraphics(int w, int h);

protected:
	void HostSpecificInit();
	void AttachGraphics(IGraphics* pGraphics);  
	void SetLatency(int samples);
	bool SendMidiMsg(const IMidiMsg* pMsg);
	bool SendSysEx(const ISysEx* pSysEx);
	inline audioMasterCallback GetHostCallback() const { return mHostCallback; }
	int GetMaxParamStrLen() const;

private:
	template <class SAMPLETYPE>
	void VSTPrepProcess(const SAMPLETYPE* const* inputs, SAMPLETYPE* const* outputs, VstInt32 nFrames);

	ERect mEditRect;
	audioMasterCallback mHostCallback;

	bool SendVSTEvent(VstEvent* pEvent);

	VstIntPtr VSTVendorSpecific(VstInt32 idx, VstIntPtr value, void* ptr, float opt);
	VstIntPtr VSTPlugCanDo(const char* ptr);

	VstSpeakerArrangement mInputSpkrArr, mOutputSpkrArr;

	bool mHostSpecificInitDone;

	enum { VSTEXT_NONE = 0, VSTEXT_COCKOS, VSTEXT_COCOA }; // List of VST extensions supported by host.
	int mHasVSTExtensions;

	ByteChunk mState;     // Persistent storage if the host asks for plugin state.
	ByteChunk mBankState; // Persistent storage if the host asks for bank state.

	AEffect mAEffect;

public:
	static VstIntPtr VSTCALLBACK VSTDispatcher(AEffect* pEffect, VstInt32 opCode, VstInt32 idx, VstIntPtr value, void* ptr, float opt);
	static void VSTCALLBACK VSTProcess(AEffect* pEffect, float** inputs, float** outputs, VstInt32 nFrames); // Deprecated.
	static void VSTCALLBACK VSTProcessReplacing(AEffect* pEffect, float** inputs, float** outputs, VstInt32 nFrames);
	static void VSTCALLBACK VSTProcessDoubleReplacing(AEffect* pEffect, double** inputs, double** outputs, VstInt32 nFrames);
	static float VSTCALLBACK VSTGetParameter(AEffect* pEffect, VstInt32 idx);
	static void VSTCALLBACK VSTSetParameter(AEffect* pEffect, VstInt32 idx, float value);
	inline AEffect* GetAEffect() { return &mAEffect; }
};
