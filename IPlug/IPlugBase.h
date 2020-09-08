#pragma once

#include "Containers.h"
#include "IPlugStructs.h"
#include "IParam.h"
#include "Hosts.h"

#include <assert.h>

#include "WDL/mutex.h"
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

	// Call GetGUI()->SetScale(wantScale) to set scale, then load bitmaps
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
	virtual void OnBypass(bool bypassed) {}
    
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
	virtual int UnserializePreset(const ByteChunk* pChunk, int startPos);

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
	IParam* GetParam(int idx) { return mParams.Get(idx); }
	IGraphics* GetGUI() { return mGraphics; }
  
  const char* GetEffectName() { return mEffectName; }
  int GetEffectVersion(bool decimal);   // Decimal = VVVVRRMM, otherwise 0xVVVVRRMM.
  void GetEffectVersionStr(char* str);
  const char* GetMfrName() { return mMfrName; }
  const char* GetProductName() { return mProductName; }
  
  int GetUniqueID() { return mUniqueID; }
  int GetMfrID() { return mMfrID; }

	void SetParameterFromGUI(int idx, double normalizedValue);
  // If a parameter change comes from the GUI, midi, or external input,
  // the host needs to be informed in case the changes are being automated.
  virtual void BeginInformHostOfParamChange(int idx) = 0;
  virtual void BeginDelayedInformHostOfParamChange(int idx) = 0;
  virtual void InformHostOfParamChange(int idx, double normalizedValue) = 0;
  virtual void EndInformHostOfParamChange(int idx) = 0;
  void DelayEndInformHostOfParamChange(int idx);
  void EndDelayedInformHostOfParamChange();

	virtual void InformHostOfProgramChange() = 0;
  // ----------------------------------------
  // Useful stuff for your plugin class or an outsider to call, 
  // most of which is implemented by the API class.

  double GetSampleRate() { return mSampleRate; }
	int GetBlockSize() { return mBlockSize; }
  int GetLatency() { return mLatency; }
  
  // In ProcessDoubleReplacing you are always guaranteed to get valid pointers 
  // to all the channels the plugin requested.  If the host hasn't connected all the pins,
  // the unconnected channels will be full of zeros.
  int NInChannels() { return mInChannels.GetSize(); }
  int NOutChannels() { return mOutChannels.GetSize(); }
  bool IsInChannelConnected(int chIdx);
  bool IsOutChannelConnected(int chIdx);
      
	virtual int GetSamplePos() = 0;   // Samples since start of project.
	virtual double GetTempo() = 0;
	double GetSamplesPerBeat();
	virtual void GetTimeSig(int* pNum, int* pDenom) = 0;
  
	virtual EHost GetHost() { return mHost; }
	int GetHostVersion(bool decimal); // Decimal = VVVVRRMM, otherwise 0xVVVVRRMM.
  void GetHostVersionStr(char* str);
  
	// Tell the host that the graphics resized.
	// Should be called only by the graphics object when it resizes itself.
	virtual void ResizeGraphics(int w, int h) = 0;

	// Not fully supported. A call back from the host saying the user has resized the window.
	// If the plugin supports different sizes, it may wish to resize.
	// virtual void UserResizedWindow(const IRECT* pR) {}

  void EnsureDefaultPreset();
  
protected:

  // ----------------------------------------
  // Useful stuff for your plugin class to call, implemented here or in the API class, or partly in both.

  struct ChannelIO 
  { 
    int mIn, mOut; 
    ChannelIO(int nIn, int nOut) : mIn(nIn), mOut(nOut) {}
  };
  WDL_PtrList<ChannelIO> mChannelIO;
  bool LegalIO(int nIn, int nOut);    // -1 for either means check the other value only.
  void LimitToStereoIO();
  
  void SetHost(const char* host, int version);   // Version = 0xVVVVRRMM.
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
  
  void SetSampleRate(double sampleRate);
  virtual void SetBlockSize(int blockSize); 
  // If latency changes after initialization (often not supported by the host).
  virtual void SetLatency(int samples);

	virtual bool SendMidiMsg(const IMidiMsg* pMsg) = 0;
	virtual bool SendMidiMsgs(const IMidiMsg* pMsgs, int n);
	virtual bool SendSysEx(const ISysEx* pSysEx) = 0;
	inline bool IsInst() const { return !!(mPlugFlags & kPlugIsInst); }

	inline bool DoesMIDI(const int plugDoes = kPlugDoesMidi) const
	{
		assert(plugDoes == (plugDoes & kPlugDoesMidi));
		return !!(mPlugFlags & plugDoes);
	}

	bool MakeDefaultPreset(const char* name = NULL, int nPresets = 1);
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

	// Will append if the chunk is already started.
	bool SerializeParams(int fromIdx, int toIdx /* up to but *not* including */, ByteChunk* pChunk) const;
	// Returns the new chunk position (endPos).
	int UnserializeParams(int fromIdx, int toIdx, const ByteChunk* pChunk, int startPos);

	void RedrawParamControls(); // Called after restoring state.

  // ----------------------------------------
  // Internal IPlug stuff (but API classes need to get at it).
  
  void OnParamReset();	// Calls OnParamChange(each param) + Reset().

  int NPresets() { return mPresets.GetSize(); }
  int GetCurrentPresetIdx() { return mCurrentPresetIdx; }
  void PruneUninitializedPresets();
  bool RestorePreset(int idx);
  bool RestorePreset(const char* name);
  const char* GetPresetName(int idx);
  void ModifyCurrentPreset(const char* name = 0);     // Sets the currently active preset to whatever current params are.

	bool SerializePresets(int fromIdx, int toIdx /* up to not *not* including */, ByteChunk* pChunk) const;
	// Returns the new chunk position (endPos).
	int UnserializePresets(int fromIdx, int toIdx, const ByteChunk* pChunk, int startPos);

  // Dump the current state as source code for a call to MakePresetFromNamedParams.
  void DumpPresetSrcCode(const char* filename, const char* paramEnumNames[]);

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

 	WDL_PtrList<IParam> mParams;

  WDL_PtrList<IPreset> mPresets;
  int mCurrentPresetIdx;

  int mParamChangeIdx;

	WDL_Mutex mMutex;

private:

  char mEffectName[MAX_EFFECT_NAME_LEN], mProductName[MAX_PRODUCT_NAME_LEN], mMfrName[MAX_MFR_NAME_LEN];
  int mUniqueID, mMfrID, mVersion;   //  Version stored as 0xVVVVRRMM: V = version, R = revision, M = minor revision.
  
  EHost mHost;
  int mHostVersion;   //  Version stored as 0xVVVVRRMM: V = version, R = revision, M = minor revision.

	int mPlugFlags; // See EPlugDoes, EPlugInit, EPlugFlags.
  double WDL_FIXALIGN mSampleRate;
  int mBlockSize, mLatency;

	IGraphics* mGraphics;

  WDL_TypedBuf<double*> mInData, mOutData;
  struct InChannel {
    bool mConnected;
    double** mSrc;   // Points into mInData.
    WDL_TypedBuf<double> mScratchBuf;
  };
  struct OutChannel {
    bool mConnected;
    double** mDest;  // Points into mOutData.
    float* mFDest;
    WDL_TypedBuf<double> mScratchBuf;
  };
  WDL_PtrList<InChannel> mInChannels;
  WDL_PtrList<OutChannel> mOutChannels;

	int mPresetChunkSize;
}
WDL_FIXALIGN;
