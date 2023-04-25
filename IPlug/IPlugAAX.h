#pragma once

#include "IPlugBase.h"

#include "aax-sdk/Interfaces/AAX_CEffectParameters.h"
#include "aax-sdk/Interfaces/AAX_Exception.h"
#include "aax-sdk/Interfaces/AAX_ICollection.h"
#include "aax-sdk/Interfaces/AAX_IEffectDescriptor.h"

class IPlugAAX: public IPlugBase
{
public:
	// Use IPLUG_CTOR instead of calling directly (defined in IPlug_include_in_plug_hdr.h).
	IPlugAAX(
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

	void BeginInformHostOfParamChange(int idx, bool lockMutex = true) {}
	void InformHostOfParamChange(int idx, double normalizedValue, bool lockMutex = true) {}
	void EndInformHostOfParamChange(int idx, bool lockMutex = true) {}

	void InformHostOfProgramChange() {}

	double GetSamplePos() { return 0.0; } // Samples since start of project.
	double GetTempo() { return 120.0; }
	void GetTimeSig(int* pNum, int* pDenom) { *pNum = *pDenom = 0; }

	// Whether the plugin is being used for offline rendering.
	bool IsRenderingOffline() { return false; }

	// Tell the host that the graphics resized.
	// Should be called only by the graphics object when it resizes itself.
	void ResizeGraphics(int w, int h) {}

protected:
	void HostSpecificInit() {}

	// To-do: Implement MIDI output.
	bool SendMidiMsg(const IMidiMsg* pMsg) { return false; }
	bool SendSysEx(const ISysEx* pSysEx) { return false; }

private:
	AAX_CEffectParameters* mEffectParams;

public:
	static AAX_Result AAXDescribeEffect(AAX_IEffectDescriptor* pPlugDesc, const char* name, const char* shortName,
		int uniqueID, int mfrID, int plugDoes, void* createProc);

	AAX_CEffectParameters* AAX_CALLBACK AAXCreateParams(IPlugAAX* pPlug);
	AAX_Result AAXEffectInit(AAX_CParameterManager* pParamMgr, const AAX_IController* pHost);

	static void AAX_CALLBACK AAXAlgProcessFunc(void* const instBegin[], const void* const pInstEnd);
	void AAXUpdateParam(AAX_CParamID id, double value, AAX_EUpdateSource src);
};
