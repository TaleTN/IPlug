#pragma once

#include "IPlugBase.h"
#include "clap/clap.h"

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

	bool AllocStateChunk(int chunkSize = -1) { return false; }
	bool AllocBankChunk(int chunkSize = -1) { return false; }

	void BeginInformHostOfParamChange(int idx) {}
	void InformHostOfParamChange(int idx, double normalizedValue) {}
	void EndInformHostOfParamChange(int idx) {}

	void InformHostOfProgramChange() {}

	double GetSamplePos() { return 0.0; } // Samples since start of project.
	double GetTempo() { return 0.0; }
	void GetTimeSig(int* pNum, int* pDenom) { *pNum = *pDenom = 0; }

	// Whether the plugin is being used for offline rendering.
	bool IsRenderingOffline() { return false; }

	// Tell the host that the graphics resized.
	// Should be called only by the graphics object when it resizes itself.
	void ResizeGraphics(int w, int h) {}

protected:
	void HostSpecificInit() {}

	bool SendMidiMsg(const IMidiMsg* pMsg) { return false; }
	bool SendSysEx(const ISysEx* pSysEx) { return false; }

private:
	clap_plugin mClapPlug;
	const clap_host* mClapHost;

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
};
