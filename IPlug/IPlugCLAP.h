#pragma once

#include "IPlugBase.h"
#include "clap/clap.h"

#include "WDL/heapbuf.h"
#include "WDL/wdltypes.h"

#ifndef CLAP_ABI
	#define CLAP_ABI
#endif

class IPlugCLAP: public IPlugBase
{
public:
	// Use IPLUG_CTOR instead of calling directly (defined in IPlug_include_in_plug_hdr.h).
	IPlugCLAP(
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

	void BeginInformHostOfParamChange(int idx, bool lockMutex = true);
	void InformHostOfParamChange(int idx, double normalizedValue, bool lockMutex = true);
	void EndInformHostOfParamChange(int idx, bool lockMutex = true);

	void InformHostOfProgramChange() {}

	double GetSamplePos(); // Samples since start of project.
	double GetTempo();
	void GetTimeSig(int* pNum, int* pDenom);

	// Whether the plugin is being used for offline rendering.
	bool IsRenderingOffline() { return IsOffline(); }

	// Tell the host that the graphics resized.
	// Should be called only by the graphics object when it resizes itself.
	void ResizeGraphics(int w, int h);

protected:
	void HostSpecificInit() {}

	bool SendMidiMsg(const IMidiMsg* pMsg);
	bool SendSysEx(const ISysEx* pSysEx);

private:
	void ProcessInputEvents(const clap_input_events* pInEvents, uint32_t nEvents, uint32_t nFrames);
	void ProcessParamEvent(const clap_event_param_value* pEvent);

	enum EParamChange
	{
		kParamChangeValue = 0,
		kParamChangeBegin,
		kParamChangeEnd
	};

	void AddParamChange(int change, int idx);
	void PushOutputEvents(const clap_output_events* pOutEvents);

	void PushParamChanges(const clap_output_events* pOutEvents, const unsigned int* pParamChanges, int nChanges) const;
	static void PushMidiMsgs(const clap_output_events* pOutEvents, const IMidiMsg* pMidiOut, int nMidiMsgs, const unsigned char* pSysExBuf);

	static bool DoesMIDIInOut(const IPlugCLAP* pPlug, bool isInput);

	ByteChunk mState; // Persistent storage if the host asks for plugin state.

	uint32_t mTransportFlags;
	clap_sectime mSongPos;
	double WDL_FIXALIGN mTempo;
	uint16_t mTimeSig[2];

	bool mPushIt; // Push it real good.

	WDL_TypedBuf<unsigned int> mParamChanges;
	WDL_TypedBuf<IMidiMsg> mMidiOut;
	WDL_HeapBuf mSysExBuf;

	clap_plugin mClapPlug;
	const clap_host* mClapHost;

	void (*mRequestFlush)(const clap_host* host);
	bool (*mRequestResize)(const clap_host* host, uint32_t width, uint32_t height);

	void* mGUIParent;
	int mGUIWidth, mGUIHeight;

	uint32_t mNoteNameCount;
	char mNoteNameTbl[128];

public:
	inline clap_plugin* GetTheClap() { return &mClapPlug; }

	static bool CLAP_ABI ClapEntryInit(const char* path) { return true; }
	static void CLAP_ABI ClapEntryDeinit() {}
	static const void* CLAP_ABI ClapEntryGetFactory(const char* id);

	static bool CLAP_ABI ClapInit(const clap_plugin* pPlug);
	static void CLAP_ABI ClapDestroy(const clap_plugin* pPlug);
	static bool CLAP_ABI ClapActivate(const clap_plugin* pPlug, double sampleRate, uint32_t minBufSize, uint32_t maxBufSize);
	static void CLAP_ABI ClapDeactivate(const clap_plugin* pPlug) {}
	static bool CLAP_ABI ClapStartProcessing(const clap_plugin* pPlug);
	static void CLAP_ABI ClapStopProcessing(const clap_plugin* pPlug);
	static void CLAP_ABI ClapReset(const clap_plugin* pPlug);
	static clap_process_status CLAP_ABI ClapProcess(const clap_plugin* pPlug, const clap_process* pProcess);
	static const void* CLAP_ABI ClapGetExtension(const clap_plugin* pPlug, const char* id);
	static void CLAP_ABI ClapOnMainThread(const clap_plugin* pPlug) {}

	static uint32_t CLAP_ABI ClapParamsCount(const clap_plugin* pPlug);
	static bool CLAP_ABI ClapParamsGetInfo(const clap_plugin* pPlug, uint32_t idx, clap_param_info* pInfo);
	static bool CLAP_ABI ClapParamsGetValue(const clap_plugin* pPlug, clap_id idx, double* pValue);
	static bool CLAP_ABI ClapParamsValueToText(const clap_plugin* pPlug, clap_id idx, double value, char* buf, uint32_t bufSize);
	static bool CLAP_ABI ClapParamsTextToValue(const clap_plugin* pPlug, clap_id idx, const char* str, double* pValue);
	static void CLAP_ABI ClapParamsFlush(const clap_plugin* pPlug, const clap_input_events* pInEvents, const clap_output_events* pOutEvents);

	static bool CLAP_ABI ClapStateSave(const clap_plugin* pPlug, const clap_ostream* pStream);
	static bool CLAP_ABI ClapStateLoad(const clap_plugin* pPlug, const clap_istream* pStream);

	static uint32_t CLAP_ABI ClapAudioPortsCount(const clap_plugin* pPlug, bool isInput);
	static bool CLAP_ABI ClapAudioPortsGet(const clap_plugin* pPlug, uint32_t idx, bool isInput, clap_audio_port_info* pInfo);

	static uint32_t CLAP_ABI ClapLatencyGet(const clap_plugin* pPlug);

	static bool CLAP_ABI ClapRenderHasHardRealtimeRequirement(const clap_plugin* pPlug) { return false; }
	static bool CLAP_ABI ClapRenderSet(const clap_plugin* pPlug, clap_plugin_render_mode mode);

	static uint32_t CLAP_ABI ClapNotePortsCount(const clap_plugin* pPlug, bool isInput);
	static bool CLAP_ABI ClapNotePortsGet(const clap_plugin* pPlug, uint32_t idx, bool isInput, clap_note_port_info* pInfo);

	static uint32_t CLAP_ABI ClapNoteNameCount(const clap_plugin* pPlug);
	static bool CLAP_ABI ClapNoteNameGet(const clap_plugin* pPlug, uint32_t idx, clap_note_name* pName);

	static bool CLAP_ABI ClapGUIIsAPISupported(const clap_plugin* pPlug, const char* id, bool isFloating);
	static bool CLAP_ABI ClapGUIGetPreferredAPI(const clap_plugin* pPlug, const char** pID, bool* pIsFloating);
	static bool CLAP_ABI ClapGUICreate(const clap_plugin* pPlug, const char* id, bool isFloating);
	static void CLAP_ABI ClapGUIDestroy(const clap_plugin* pPlug);
	static bool CLAP_ABI ClapGUISetScale(const clap_plugin* pPlug, double scale) { return false; }
	static bool CLAP_ABI ClapGUIGetSize(const clap_plugin* pPlug, uint32_t* pWidth, uint32_t* pHeight);
	static bool CLAP_ABI ClapGUICanResize(const clap_plugin* pPlug) { return false; }
	static bool CLAP_ABI ClapGUIGetResizeHints(const clap_plugin* pPlug, clap_gui_resize_hints* pHints) { return false; }
	static bool CLAP_ABI ClapGUIAdjustSize(const clap_plugin* pPlug, uint32_t* pWidth, uint32_t* pHeight) { return false; }
	static bool CLAP_ABI ClapGUISetSize(const clap_plugin* pPlug, uint32_t width, uint32_t height) { return false; }
	static bool CLAP_ABI ClapGUISetParent(const clap_plugin* pPlug, const clap_window* pWindow);
	static bool CLAP_ABI ClapGUISetTransient(const clap_plugin* pPlug, const clap_window* pWindow) { return false; }
	static void CLAP_ABI ClapGUISuggestTitle(const clap_plugin* pPlug, const char* title) {}
	static bool CLAP_ABI ClapGUIShow(const clap_plugin* pPlug);
	static bool CLAP_ABI ClapGUIHide(const clap_plugin* pPlug);
}
WDL_FIXALIGN;
