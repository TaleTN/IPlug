#pragma once

#include "Containers.h"
#include "IPlugStructs.h"
#include "IParam.h"

#include <assert.h>

#include "WDL/mutex.h"
#include "WDL/wdlstring.h"
#include "WDL/wdltypes.h"

// All version ints are stored as 0xVVVVRRMM: V = version, R = revision, M = minor revision.

class IGraphics;

class IPlugBase
{
public:
	static const int kIPlugVersion = 0x010000;

	static const int kDefaultSampleRate = 44100;
	static const int kDefaultBlockSize = 1024;

	// Use IPLUG_CTOR instead of calling directly (defined in IPlug_include_in_plug_hdr.h).
	IPlugBase(
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

	enum EPlugDoes
	{
		kPlugIsInst = 1,

		kPlugDoesMidiIn = 2,
		kPlugDoesMidiOut = 4,
		kPlugDoesMidi = kPlugDoesMidiIn | kPlugDoesMidiOut
	};

	// ----------------------------------------
	// Your plugin class implements these.
	// There are default impls, mostly just for reference.

	virtual ~IPlugBase();

	// Mutex is already locked.
	virtual void Reset() {} // Called (at least) once.
	virtual void OnParamChange(int paramIdx) {}
	virtual void OnPresetChange(int presetIdx) {}

	// Default passthrough. Inputs and outputs are [nChannel][nSample].
	// Mutex is already locked.
	virtual void ProcessDoubleReplacing(const double* const* inputs, double* const* outputs, int nFrames);

	// Call GetGUI()->Rescale(wantScale) to set scale, then load bitmaps
	// that depend on GUI size. Return true to notify controls of rescale.
	virtual bool OnGUIRescale(int wantScale); // See IGraphics::EGUIScale.

	// In case the audio processing thread needs to do anything when the GUI opens
	// (like for example, set some state dependent initial values for controls).
	virtual void OnGUIOpen() {}
	virtual void OnGUIClose() {}

	// This is an idle call from the audio processing thread, as opposed to
	// IGraphics::OnGUIIdle which is called from the GUI thread.
	// Only active if IPLUG_USE_IDLE_CALLS is defined.
	#ifdef IPLUG_USE_IDLE_CALLS
	virtual void OnIdle() {}
	#endif

	// Not usually needed... Also different hosts have different interpretations of "activate".
	// Not all hosts will notify plugin on bypass. Mutex is already locked.
	virtual void OnActivate(bool active) {}
	virtual void OnBypass(bool bypass) {}
    
	virtual void ProcessMidiMsg(const IMidiMsg* pMsg) {}
	virtual void ProcessSysEx(const ISysEx* pSysEx) {}
	virtual bool MidiNoteName(int noteNumber, char* buf, int bufSize = 128);

	// Call these after adding all parameters, but before making any
	// presets, to customize/pre-allocate state chunk buffers.
	bool AllocPresetChunk(int chunkSize = -1);
	virtual bool AllocStateChunk(int chunkSize = -1) = 0;
	virtual bool AllocBankChunk(int chunkSize = -1) = 0;

	// Serializes internal presets, by default all non-global parameters.
	// Mutex is already locked.
	virtual bool SerializePreset(ByteChunk* pChunk);
	// Returns the new chunk position (endPos).
	virtual int UnserializePreset(const ByteChunk* pChunk, int startPos, int version = 0);

	// By default serializes all (global + preset) parameters.
	virtual bool SerializeState(ByteChunk* pChunk);
	virtual int UnserializeState(const ByteChunk* pChunk, int startPos);

	// By default serializes global parameters, followed by each preset
	// (in SerializePreset() format, but with extra info).
	virtual bool SerializeBank(ByteChunk* pChunk);
	virtual int UnserializeBank(const ByteChunk* pChunk, int startPos);

	// ----------------------------------------
	// Your plugin class, or a control class, can call these functions.

	int NParams() const { return mParams.GetSize(); }
	bool NParams(const int idx) const { return (unsigned int)idx < (unsigned int)NParams(); }

	IParam** GetParams() const { return mParams.GetList(); }
	template <class T> T* GetParam(const int idx) const { return (T*)mParams.Get(idx); }
	IParam* GetParam(const int idx) const { return mParams.Get(idx); }

	template <class T> T* AddParam(const int idx, T* const pParam)
	{
		#ifndef NDEBUG
		{
			// Parameters should be added in sequential order.
			const bool duplicateParam = idx >= NParams();
			const bool missingParam = idx <= NParams();
			assert(duplicateParam);
			assert(missingParam);
		}
		#endif

		return (T*)mParams.Add(pParam);
	}

	int NPresets() const { return mPresets.GetSize(); }
	bool NPresets(const int idx) const { return (unsigned int)idx < (unsigned int)NPresets(); }

	inline int GetCurrentPresetIdx() const { return mCurrentPresetIdx; }
	const char* GetPresetName(int idx = -1) const;

	virtual void SetPresetName(int idx, const char* name) {}
	void SetPresetName(const char* name) { SetPresetName(-1, name); }

	bool MakeDefaultPreset(const char* name = NULL, int nPresets = 1);

	bool RestorePreset(int idx = -1);
	bool RestorePreset(const char* name);

	inline int GetPresetChunkSize() const { return mPresetChunkSize; }

	inline WDL_Mutex* GetMutex() { return &mMutex; }
	inline IGraphics* GetGUI() const { return mGraphics; }

	const char* GetEffectName() const { return mEffectName.Get(); }
	const char* GetMfrName() const { return mMfrName.Get(); }
	const char* GetProductName() const { return mProductName.Get(); }

	// Decimal = VVVVRRMM, otherwise 0xVVVVRRMM.
	int GetEffectVersion(const bool decimal) const
	{
		return decimal ? GetDecimalVersion(mVersion) : mVersion;
	}

	char* GetEffectVersionStr(char* const buf, int bufSize = 14) const
	{
		return GetVersionStr(mVersion, buf, bufSize);
	}

	inline int GetUniqueID() const { return mUniqueID; }
	inline int GetMfrID() const { return mMfrID; }

	void SetParameterFromGUI(int idx, double normalizedValue);

	void OnParamReset(); // Calls OnParamChange(each param).
	void RedrawParamControls(); // Called after restoring state.

	// If a parameter change comes from the GUI, midi, or external input,
	// the host needs to be informed in case the changes are being automated.
	virtual void BeginInformHostOfParamChange(int idx, bool lockMutex = true) = 0;
	virtual void InformHostOfParamChange(int idx, double normalizedValue, bool lockMutex = true) = 0;
	virtual void EndInformHostOfParamChange(int idx, bool lockMutex = true) = 0;
	void BeginDelayedInformHostOfParamChange(int idx);
	void DelayEndInformHostOfParamChange(int idx);
	void EndDelayedInformHostOfParamChange(bool lockMutex = true);

	virtual void InformHostOfProgramChange() = 0;

	// ----------------------------------------
	// Useful stuff for your plugin class or an outsider to call,
	// most of which is implemented by the API class.

	inline double GetSampleRate() const { return mSampleRate; }
	inline int GetBlockSize() const { return mBlockSize; }
	inline int GetLatency() const { return mLatency; }
  
	// In ProcessDoubleReplacing you are always guaranteed to get valid pointers
	// to all the channels the plugin requested.  If the host hasn't connected all the pins,
	// the unconnected channels will be full of zeros.
	int NInChannels() const { return mInChannels.GetSize(); }
	int NOutChannels() const { return mOutChannels.GetSize(); }

	bool NInChannels(const int idx) const { return (unsigned int)idx < (unsigned int)NInChannels(); }
	bool NOutChannels(const int idx) const { return (unsigned int)idx < (unsigned int)NOutChannels(); }

	bool IsInChannelConnected(const int chIdx) const
	{
		const InChannel* const pInChannel = mInChannels.Get(chIdx);
		return pInChannel && pInChannel->mConnected;
	}

	bool IsOutChannelConnected(const int chIdx) const
	{
		const OutChannel* const pOutChannel = mOutChannels.Get(chIdx);
		return pOutChannel && pOutChannel->mConnected;
	}

	virtual double GetSamplePos() = 0; // Samples since start of project.
	virtual double GetTempo() = 0;
	virtual void GetTimeSig(int* pNum, int* pDenom) = 0;

	double GetSamplesPerBeat()
	{
		const double tempo = GetTempo();
		return tempo > 0.0 ? mSampleRate * 60.0 / tempo : 0.0;
	}

	// Whether the plugin is being used for offline rendering.
	virtual bool IsRenderingOffline() = 0;

	virtual int GetHost() { return mHost; } // See EHost in Hosts.h.
	int GetHostVersion(bool decimal); // Decimal = VVVVRRMM, otherwise 0xVVVVRRMM.
	char* GetHostVersionStr(char* buf, int bufSize = 14);
  
	// Tell the host that the graphics resized.
	// Should be called only by the graphics object when it resizes itself.
	virtual void ResizeGraphics(int w, int h) = 0;

	// Not fully supported. A call back from the host saying the user has resized the window.
	// If the plugin supports different sizes, it may wish to resize.
	// virtual void UserResizedWindow(const IRECT* pR) {}
    
	void EnsureDefaultPreset();

	#ifndef NDEBUG
	static void DebugLog(const char* format, ...);
	#else
	static inline void DebugLog(const char*, ...) {}
	#endif

protected:
	// ----------------------------------------
	// Useful stuff for your plugin class to call, implemented here or in the API class, or partly in both.

	template <class SRC, class DEST> void CastCopy(DEST* pDest, const SRC* pSrc, int n);
	template <class SRC, class DEST> static void CastCopyAccumulating(DEST* pDest, const SRC* pSrc, int n);

	struct ChannelIO
	{
		int mIn, mOut;
		ChannelIO(const int nIn, const int nOut): mIn(nIn), mOut(nOut) {}
	};
	WDL_TypedBuf<ChannelIO> mChannelIO;
	bool LegalIO(int nIn, int nOut) const; // -1 for either means check the other value only.
	void LimitToStereoIO();

	static int GetDecimalVersion(int version);
	static char* GetVersionStr(int version, char* buf, int bufSize = 14);

	void SetHost(const char* host, int version); // Version = 0xVVVVRRMM.
	virtual void HostSpecificInit() = 0;
  
	enum EPlugInit
	{
		kPlugInitSampleRate = 8,
		kPlugInitBlockSize = 16,
		kPlugInit = kPlugInitSampleRate | kPlugInitBlockSize
	};

	// Returns true if host has set both sample rate and block size.
	inline bool PlugInit(const int plugInit = kPlugInit) const
	{
		assert(plugInit == (plugInit & kPlugInit));
		return (mPlugFlags & plugInit) == plugInit;
	}

	enum EPlugFlags
	{
		kPlugFlagsActive = 32,
		kPlugFlagsBypass = 64,
		kPlugFlagsOffline = 128
	};

	inline bool IsActive() const { return !!(mPlugFlags & kPlugFlagsActive); }
	inline bool IsBypassed() const { return !!(mPlugFlags & kPlugFlagsBypass); }

	// Returns state after last IsRenderingOffline() call;
	// to update state call IsRenderingOffline().
	inline bool IsOffline() const { return !!(mPlugFlags & kPlugFlagsOffline); }

	virtual void AttachGraphics(IGraphics* pGraphics);

	virtual void SetSampleRate(const double sampleRate) { mSampleRate = sampleRate; }
	virtual void SetBlockSize(int blockSize);
	// If latency changes after initialization (often not supported by the host).
	virtual void SetLatency(const int samples) { mLatency = samples; }

	virtual bool SendMidiMsg(const IMidiMsg* pMsg) = 0;
	virtual bool SendMidiMsgs(const IMidiMsg* pMsgs, int n);
	virtual bool SendSysEx(const ISysEx* pSysEx) = 0;
	inline bool IsInst() const { return !!(mPlugFlags & kPlugIsInst); }

	inline bool DoesMIDI(const int plugDoes = kPlugDoesMidi) const
	{
		assert(plugDoes == (plugDoes & kPlugDoesMidi));
		return !!(mPlugFlags & plugDoes);
	}

	// MakePreset(name, param1, param2, ..., paramN)
	bool MakePreset(const char* name, ...);
	// MakePresetFromNamedParams(name, nParamsNamed, paramEnum1, paramVal1, paramEnum2, paramVal2, ..., paramEnumN, paramVal2)
	// nParamsNamed may be less than the total number of params.
	bool MakePresetFromNamedParams(const char* name, int nParamsNamed, ...);
	bool MakePresetFromChunk(const char* name, const ByteChunk* pChunk);
	inline void PopulateUninitializedPresets() { MakeDefaultPreset(NULL, -1); }

	// Define IPLUG_NO_STATE_CHUNKS for compatability with original IPlug
	// with state chunks disabled.
	static inline bool DoesStateChunks()
	{
		#ifdef IPLUG_NO_STATE_CHUNKS
		return false;
		#else
		return true;
		#endif
	}

	// Returns max chunk size that SerializePresets() will need.
	static int GetBankChunkSize(const int nPresets, const int presetChunkSize)
	{
		static const int extraInfo = (int)sizeof(int) + IPreset::kMaxNameLen + 1;
		return (extraInfo + presetChunkSize) * nPresets;
	}

	int GetParamsChunkSize(const int fromIdx, const int toIdx) const;

	// Will append if the chunk is already started.
	bool SerializeParams(int fromIdx, int toIdx /* up to but *not* including */, ByteChunk* pChunk) const;
	// Returns the new chunk position (endPos).
	int UnserializeParams(int fromIdx, int toIdx, const ByteChunk* pChunk, int startPos);

	// ----------------------------------------
	// Internal IPlug stuff (but API classes need to get at it).

	void InitPresetChunk(IPreset* pPreset, const char* name = NULL);
	void PruneUninitializedPresets();
	void ModifyCurrentPreset(const char* name = NULL); // Sets the currently active preset to whatever current params are.

	bool SerializePresets(int fromIdx, int toIdx /* up to but *not* including */, ByteChunk* pChunk) const;
	// Returns the new chunk position (endPos).
	int UnserializePresets(int fromIdx, int toIdx, const ByteChunk* pChunk, int startPos, int version = 0);

	#ifndef NDEBUG
	// Dump the current state as source code for a call to MakePresetFromNamedParams().
	void DumpPresetSrcCode(const char* const paramEnumNames[], const char* name = "name");
	#endif

	// Set connection state for n channels.
	// If a channel is connected, we expect a call to attach the buffers before each process call.
	// If a channel is not connected, we attach scratch buffers now and don't need to do anything else.
	void SetInputChannelConnections(int idx, int n, bool connected);
	void SetOutputChannelConnections(int idx, int n, bool connected);

	void AttachInputBuffers(int idx, int n, const double* const* ppData, int nFrames);
	void AttachOutputBuffers(int idx, int n, double* const* ppData);
	void ProcessBuffers(double /* sampleType */, const int nFrames) { ProcessDoubleReplacing(mInData.Get(), mOutData.Get(), nFrames); }
	void PassThroughBuffers(double /* sampleType */, const int nFrames) { ProcessDoubleReplacing(mInData.Get(), mOutData.Get(), nFrames); }
	void AttachInputBuffers(int idx, int n, const float* const* ppData, int nFrames);
	void AttachOutputBuffers(int idx, int n, float* const* ppData);
	void ProcessBuffers(float /* sampleType */, int nFrames);
	void ProcessBuffersAccumulating(float /* sampleType */, int nFrames);
	void PassThroughBuffers(float /* sampleType */, int nFrames);

	WDL_PtrList_DeleteOnDestroy<IParam> mParams;
	WDL_PtrList_DeleteOnDestroy<IPreset> mPresets;
	int mCurrentPresetIdx, mParamChangeIdx;

	WDL_Mutex mMutex;

	WDL_FastString mEffectName, mProductName, mMfrName;
	int mUniqueID, mMfrID, mVersion; // Version stored as 0xVVVVRRMM: V = version, R = revision, M = minor revision.

	int mHost, mHostVersion; // Version stored as 0xVVVVRRMM: V = version, R = revision, M = minor revision.

	int mPlugFlags; // See EPlugDoes, EPlugInit, EPlugFlags.
	double WDL_FIXALIGN mSampleRate;
	int mBlockSize, mLatency;

	IGraphics* mGraphics;

	WDL_TypedBuf<const double*> mInData;
	WDL_TypedBuf<double*> mOutData;

	struct InChannel
	{
		bool mConnected;
		const double** mSrc; // Points into mInData.
		WDL_TypedBuf<double> mScratchBuf;

		InChannel(const double** pSrc);
		void SetConnection(bool connected);
		void AttachInputBuffer(const double* const*& ppData);
		double* ResizeScratchBuffer(int size);
		double* AttachScratchBuffer();
	};

	struct OutChannel
	{
		bool mConnected;
		double** mDest; // Points into mOutData.
		float* mFDest;
		WDL_TypedBuf<double> mScratchBuf;

		OutChannel(double** pDest);
		void SetConnection(bool connected);
		void AttachOutputBuffer(double* const*& ppData);
		double* ResizeScratchBuffer(int size);
		void AttachScratchBuffer(float* const*& ppData);
	};

	WDL_PtrList_DeleteOnDestroy<InChannel> mInChannels;
	WDL_PtrList_DeleteOnDestroy<OutChannel> mOutChannels;

	int mPresetChunkSize;
}
WDL_FIXALIGN;
