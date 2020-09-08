#include "IPlugVST2.h"
#include "IGraphics.h"
#include "Hosts.h"

#ifdef __APPLE__
	#include "IGraphicsMac.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"
#include "WDL/wdltypes.h"

#define vst_strncpy(y, x, n) lstrcpyn_safe(y, x, (n) + 1)

const int VST_VERSION = 2400;

static int VSTSpkrArrType(const int nchan)
{
	const int type = nchan - 1;
	return (unsigned int)nchan <= 2 ? type : kSpeakerArrUserDefined;
}

IPlugVST2::IPlugVST2(
	void* const instanceInfo,
	const int nParams,
	const char* const channelIOStr,
	const int nPresets,
	const char* const effectName,
	const char* const productName,
	const char* const mfrName,
	const int vendorVersion,
	const int uniqueID,
	const int mfrID,
	const int latency,
	const int plugDoes
):
IPlugBase(
	nParams,
	channelIOStr,
	nPresets,
	effectName,
	productName,
	mfrName,
	vendorVersion,
	uniqueID,
	mfrID,
	latency,
	plugDoes)
{
	const int nInputs = NInChannels(), nOutputs = NOutChannels();

	memset(&mEditRect, 0, sizeof(ERect));
	mHostCallback = (audioMasterCallback)instanceInfo;

	memset(&mInputSpkrArr, 0, sizeof(VstSpeakerArrangement));
	mInputSpkrArr.type = VSTSpkrArrType(nInputs);
	mInputSpkrArr.numChannels = nInputs;

	memset(&mOutputSpkrArr, 0, sizeof(VstSpeakerArrangement));
	mOutputSpkrArr.type = VSTSpkrArrType(nOutputs);
	mOutputSpkrArr.numChannels = nOutputs;

	mHostSpecificInitDone = false;
	mHasVSTExtensions = VSTEXT_NONE;

	memset(&mAEffect, 0, sizeof(AEffect));
	mAEffect.magic = kEffectMagic;
	mAEffect.dispatcher = VSTDispatcher;
	mAEffect.DECLARE_VST_DEPRECATED(process) = VSTProcess;
	mAEffect.setParameter = VSTSetParameter;
	mAEffect.getParameter = VSTGetParameter;
	mAEffect.numPrograms = nPresets;
	mAEffect.numParams = nParams;
	mAEffect.numInputs = nInputs;
	mAEffect.numOutputs = nOutputs;

	mAEffect.flags = effFlagsCanReplacing | effFlagsCanDoubleReplacing
	#ifndef IPLUG_NO_STATE_CHUNKS
	| effFlagsProgramChunks
	#endif
	;
	if (LegalIO(1, -1)) mAEffect.flags |= DECLARE_VST_DEPRECATED(effFlagsCanMono);
	if (plugDoes & kPlugIsInst) mAEffect.flags |= effFlagsIsSynth;

	mAEffect.initialDelay = latency;
	mAEffect.DECLARE_VST_DEPRECATED(ioRatio) = 1.0f;
	mAEffect.object = this;
	mAEffect.uniqueID = uniqueID;
	mAEffect.version = GetEffectVersion(true);
	mAEffect.processReplacing = VSTProcessReplacing;
	mAEffect.processDoubleReplacing = VSTProcessDoubleReplacing;

	// Default everything to connected, then disconnect pins if the host says to.
	SetInputChannelConnections(0, nInputs, true);
	SetOutputChannelConnections(0, nOutputs, true);

	SetBlockSize(kDefaultBlockSize);
}

// See InitializeVSTChunk().
static const int kInitializeVSTChunkSize = 2 * (int)sizeof(int);

bool IPlugVST2::AllocStateChunk(int chunkSize)
{
	if (chunkSize < 0)
	{
		chunkSize = GetParamsChunkSize(0, NParams());
	}
	chunkSize += kInitializeVSTChunkSize;
	mState.Alloc(chunkSize);
	return mState.Size() == chunkSize;
}

bool IPlugVST2::AllocBankChunk(int chunkSize)
{
	if (chunkSize < 0)
	{
		if (mPresetChunkSize < 0) AllocPresetChunk();
		chunkSize = GetBankChunkSize(NPresets(), mPresetChunkSize);
	}
	chunkSize += kInitializeVSTChunkSize;
	mBankState.Alloc(chunkSize);
	return mBankState.Size() == chunkSize;
}

void IPlugVST2::BeginInformHostOfParamChange(const int idx)
{
	EndDelayedInformHostOfParamChange();
	mHostCallback(&mAEffect, audioMasterBeginEdit, idx, 0, NULL, 0.0f);
}

void IPlugVST2::InformHostOfParamChange(const int idx, const double normalizedValue)
{
	mHostCallback(&mAEffect, audioMasterAutomate, idx, 0, NULL, (float)normalizedValue);
}

void IPlugVST2::EndInformHostOfParamChange(const int idx)
{
	mHostCallback(&mAEffect, audioMasterEndEdit, idx, 0, NULL, 0.0f);
}

void IPlugVST2::InformHostOfProgramChange()
{
	mHostCallback(&mAEffect, audioMasterUpdateDisplay, 0, 0, NULL, 0.0f);
}

static const VstTimeInfo* GetTimeInfo(const audioMasterCallback hostCallback, AEffect* const pAEffect, const VstIntPtr filter = 0)
{ 
	return (const VstTimeInfo*)hostCallback(pAEffect, audioMasterGetTime, 0, filter, NULL, 0.0f);
}

double IPlugVST2::GetSamplePos()
{ 
	const VstTimeInfo* const pTI = GetTimeInfo(mHostCallback, &mAEffect);
	return pTI ? wdl_max(pTI->samplePos, 0.0) : 0.0;
}

double IPlugVST2::GetTempo()
{
	const VstTimeInfo* const pTI = GetTimeInfo(mHostCallback, &mAEffect, kVstTempoValid);
	return pTI && (pTI->flags & kVstTempoValid) ? wdl_max(pTI->tempo, 0.0) : 0.0;
}

void IPlugVST2::GetTimeSig(int* const pNum, int* const pDenom)
{
	const VstTimeInfo* const pTI = GetTimeInfo(mHostCallback, &mAEffect, kVstTimeSigValid);

	int num, denom;
	if (pTI && (pTI->flags & kVstTimeSigValid) && pTI->timeSigNumerator >= 0 && pTI->timeSigDenominator >= 0)
	{
		num = pTI->timeSigNumerator;
		denom = pTI->timeSigDenominator;
	}
	else
	{
		denom = num = 0;
	}

	*pNum = num;
	*pDenom = denom;
}

int IPlugVST2::GetHost()
{
	int host = IPlugBase::GetHost();
	if (host == kHostUninit)
	{
		char productStr[wdl_max(256, kVstMaxProductStrLen)];
		productStr[0] = 0;
		int version = 0;
		mHostCallback(&mAEffect, audioMasterGetProductString, 0, 0, productStr, 0.0f);
		if (productStr[0])
		{
			int decVer = (int)mHostCallback(&mAEffect, audioMasterGetVendorVersion, 0, 0, NULL, 0.0f);
			const int ver = decVer / 10000;
			const int rmaj = (decVer -= 10000 * ver) / 100;
			const int rmin = decVer - 100 * rmaj;
			version = (((ver << 8) | rmaj) << 8) | rmin;
		}
		SetHost(productStr, version);
		host = IPlugBase::GetHost();
	}
	return host;
}

void IPlugVST2::AttachGraphics(IGraphics* const pGraphics)
{
	if (pGraphics)
	{
		IPlugBase::AttachGraphics(pGraphics);
		mAEffect.flags |= effFlagsHasEditor;

		#ifdef __APPLE__
		static const int scale = IGraphicsMac::kScaleOS;
		#else
		const int scale = pGraphics->Scale();
		#endif

		mEditRect.left = mEditRect.top = 0;
		mEditRect.right = pGraphics->Width() >> scale;
		mEditRect.bottom = pGraphics->Height() >> scale;
	}
}

void IPlugVST2::ResizeGraphics(const int w, const int h)
{
	if (GetGUI())
	{
		mEditRect.left = mEditRect.top = 0;
		mEditRect.right = w;
		mEditRect.bottom = h;
	}
}

bool IPlugVST2::IsRenderingOffline()
{
	const bool offline = mHostCallback(&mAEffect, audioMasterGetCurrentProcessLevel, 0, 0, NULL, 0.0f) == kVstProcessLevelOffline;
	if (IsOffline() != offline) mPlugFlags ^= kPlugFlagsOffline;
	return offline;
}

void IPlugVST2::SetLatency(const int samples)
{
    mAEffect.initialDelay = samples;
    IPlugBase::SetLatency(samples);
}

bool IPlugVST2::SendVSTEvent(VstEvent* const pEvent)
{ 
	// It would be more efficient to bundle these and send at the end of a processed block,
	// but that would require writing OnBlockEnd and making sure it always gets called,
	// and who cares anyway, MIDI events aren't that dense.
	VstEvents events = { 1, 0, pEvent, NULL };
	return mHostCallback(&mAEffect, audioMasterProcessEvents, 0, 0, &events, 0.0f) == 1;
}

bool IPlugVST2::SendMidiMsg(const IMidiMsg* const pMsg)
{
	VstMidiEvent midiEvent;

	#if defined(_WIN64) || (defined(__LP64__) && __LITTLE_ENDIAN__)
	{
		assert(pMsg->_padding == 0);

		*(WDL_UINT64*)&midiEvent.type = ((WDL_UINT64)sizeof(VstMidiEvent) << 32) | kVstMidiType;
		*(WDL_UINT64*)&midiEvent.deltaFrames = (unsigned int)pMsg->mOffset;
		*(WDL_UINT64*)&midiEvent.noteLength = 0;
		*(WDL_UINT64*)&midiEvent.midiData = *(const unsigned int*)&pMsg->mStatus;
	}
	#else
	{
		memset(&midiEvent, 0, sizeof(VstMidiEvent));

		midiEvent.type = kVstMidiType;
		midiEvent.byteSize = (int)sizeof(VstMidiEvent);
		midiEvent.deltaFrames = pMsg->mOffset;
		midiEvent.midiData[0] = pMsg->mStatus;
		midiEvent.midiData[1] = pMsg->mData1;
		midiEvent.midiData[2] = pMsg->mData2;
	}
	#endif

	return SendVSTEvent((VstEvent*)&midiEvent);
}

bool IPlugVST2::SendSysEx(const ISysEx* const pSysEx)
{ 
	VstMidiSysexEvent sysexEvent;
	memset(&sysexEvent, 0, sizeof(VstMidiSysexEvent));

	sysexEvent.type = kVstSysExType;
	sysexEvent.byteSize = (int)sizeof(VstMidiSysexEvent);
	sysexEvent.deltaFrames = pSysEx->mOffset;
	sysexEvent.dumpBytes = pSysEx->mSize;
	sysexEvent.sysexDump = (char*)pSysEx->mData;

	return SendVSTEvent((VstEvent*)&sysexEvent);
}

void IPlugVST2::HostSpecificInit()
{
	if (!mHostSpecificInitDone)
	{
		mHostSpecificInitDone = true;
		switch (GetHost())
		{
			case kHostForte:
			case kHostAudition:
			case kHostOrion:
			case kHostSAWStudio:
				LimitToStereoIO();
				break;
		}

		// This won't always solve a picky host problem -- for example Forte
		// looks at mAEffect IO count before identifying itself.
		mAEffect.numInputs = mInputSpkrArr.numChannels = NInChannels();
		mAEffect.numOutputs = mOutputSpkrArr.numChannels = NOutChannels();
	}
}

static const int IPLUG_VERSION_MAGIC = 'pfft';

static void InitializeVSTChunk(ByteChunk* const pChunk)
{
	pChunk->Clear();
	pChunk->PutInt32(IPLUG_VERSION_MAGIC);
	pChunk->PutInt32(IPlugBase::kIPlugVersion);
}

static int GetIPlugVerFromChunk(const ByteChunk* const pChunk, int* const pPos)
{
	int magic, ver = 0;
	const int pos = pChunk->GetInt32(&magic, *pPos);
	if (pos > *pPos && magic == IPLUG_VERSION_MAGIC)
	{
		*pPos = pChunk->GetInt32(&ver, pos);
	}
	return ver;
}

static double VSTString2Parameter(const IParam* const pParam, const char* const ptr)
{
	double v;
	const bool mapped = pParam->MapDisplayText(ptr, &v);
	if (!mapped)
	{
		v = strtod(ptr, NULL);
		if (pParam->DisplayIsNegated()) v = -v;
		v = pParam->GetNormalized(v);
	}
	return v;
}

VstIntPtr VSTCALLBACK IPlugVST2::VSTDispatcher(AEffect* const pEffect, const VstInt32 opCode, const VstInt32 idx, const VstIntPtr value, void* const ptr, const float opt)
{
	VstIntPtr ret = 0;

	// VSTDispatcher is an IPlugVST2 class member, we can access anything in IPlugVST2 from here.
	IPlugVST2* const _this = (IPlugVST2*)pEffect->object;
	if (!_this) return ret;

	_this->mMutex.Enter();

	switch (opCode)
	{
		case effOpen:
		{
			_this->HostSpecificInit();
			_this->OnParamReset();
			break;
		}

		case effClose:
		{
			_this->mMutex.Leave();
			delete _this;
			return ret;
		}

		case effSetProgram:
		{
			_this->ModifyCurrentPreset();
			_this->RestorePreset((int)value);
			break;
		}

		case effGetProgram:
		{
			ret = _this->GetCurrentPresetIdx();
			break;
		}

		case effSetProgramName:
		{
			if (ptr) _this->ModifyCurrentPreset((const char*)ptr);
			break;
		}

		case effGetProgramName:
		{
			if (ptr)
			{
				const int i = _this->GetCurrentPresetIdx();
				vst_strncpy((char*)ptr, _this->GetPresetName(i), kVstMaxProgNameLen);
			}
			break;
		}

		case effGetParamLabel:
		{
			if (_this->NParams(idx))
			{
				vst_strncpy((char*)ptr, _this->GetParam(idx)->GetLabelForHost(), kVstMaxParamStrLen);
			}
			break;
		}

		case effGetParamDisplay:
		{
			if (_this->NParams(idx))
			{
				_this->GetParam(idx)->GetDisplayForHost((char*)ptr, kVstMaxParamStrLen);
			}
			break;
		}

		case effGetParamName:
		{
			if (_this->NParams(idx))
			{
				vst_strncpy((char*)ptr, _this->GetParam(idx)->GetNameForHost(), kVstMaxParamStrLen);
			}
			break;
		}

		case effSetSampleRate:
		{
			const double sampleRate = opt;
			const int flags = _this->mPlugFlags;
			if (sampleRate != _this->GetSampleRate() || !(flags & kPlugInitSampleRate))
			{
				_this->SetSampleRate(sampleRate);
				if (flags & kPlugInitBlockSize) _this->Reset();
				_this->mPlugFlags = flags | kPlugInitSampleRate;
			}
			break;
		}

		case effSetBlockSize:
		{
			const int blockSize = (int)value;
			const int flags = _this->mPlugFlags;
			if (blockSize != _this->GetBlockSize() || !(flags & kPlugInitBlockSize))
			{
				_this->SetBlockSize(blockSize);
				if (flags & kPlugInitSampleRate) _this->Reset();
				_this->mPlugFlags = flags | kPlugInitBlockSize;
			}
			break;
		}

		case effMainsChanged:
		{
			const bool active = !!value;
			if (_this->IsActive() != active)
			{
				_this->OnActivate(active);
				_this->mPlugFlags ^= IPlugBase::kPlugFlagsActive;
			}
			break;
		}

		case effEditGetRect:
		{
			if (ptr && _this->GetGUI())
			{
				*(ERect**)ptr = &_this->mEditRect;
				ret = 1;
			}
			break;
		}

		case effEditOpen:
		{
			#if defined(__APPLE__) && !defined(IPLUG_NO_CARBON_SUPPORT)
			IGraphicsMac* pGraphics = (IGraphicsMac*)_this->GetGUI();
			if (pGraphics)
			{
				// OSX, check if we are in a Cocoa VST host.
				const bool iscocoa = !!(_this->mHasVSTExtensions & VSTEXT_COCOA);
				if (iscocoa && !pGraphics->OpenCocoaWindow(ptr)) pGraphics = NULL;
				if (!iscocoa && !pGraphics->OpenCarbonWindow(ptr)) pGraphics = NULL;
				if (pGraphics)
			#else
			IGraphics* const pGraphics = _this->GetGUI();
			if (pGraphics)
			{
				if (pGraphics->OpenWindow(ptr))
				#endif
				{
					_this->OnGUIOpen();
					ret = 1;
				}
			}
			break;
		}

		case effEditClose:
		{
			IGraphics* const pGraphics = _this->GetGUI();
			if (pGraphics)
			{
				_this->OnGUIClose();
				pGraphics->CloseWindow();
				ret = 1;
			}
			break;
		}

		#ifdef IPLUG_USE_IDLE_CALLS
		case effEditIdle:
		case DECLARE_VST_DEPRECATED(effIdle):
		{
			_this->OnIdle();
			break;
		}
		#endif

		case DECLARE_VST_DEPRECATED(effIdentify):
		{
			ret = 'NvEf'; // Random deprecated magic.
			break;
		}

		case effGetChunk:
		{
			void** const ppData = (void**)ptr;
			if (ppData)
			{
				const bool isBank = !idx;
				ByteChunk* const pChunk = isBank ? &_this->mBankState : &_this->mState;
				if (!pChunk->Size())
				{
					const bool allocOK = isBank ? _this->AllocBankChunk() : _this->AllocStateChunk();
					if (!allocOK) break;
				}
				InitializeVSTChunk(pChunk);
				bool savedOK;
				if (isBank)
				{
					_this->ModifyCurrentPreset();
					savedOK = _this->SerializeBank(pChunk);
				}
				else
				{
					savedOK = _this->SerializeState(pChunk);
				}
				void* const pData = pChunk->GetBytes();
				const int size = pChunk->Size();
				if (savedOK && size)
				{
					*ppData = pData;
					ret = size;
				}
			}
			break;
		}

		case effSetChunk:
		{
			if (ptr)
			{
				const bool isBank = !idx;
				ByteChunk* const pChunk = isBank ? &_this->mBankState : &_this->mState;
				const int size = (int)value;
				if (pChunk->Size() != size)
				{
					pChunk->Resize(size);
					if (pChunk->Size() != size) break;
				}
				memcpy(pChunk->GetBytes(), ptr, size);
				int pos = 0;
				const int iplugVer = GetIPlugVerFromChunk(pChunk, &pos);
				if (isBank & (iplugVer >= 0x010000))
				{
					pos = _this->UnserializeBank(pChunk, pos);
					_this->RestorePreset();
				}
				else
				{
					pos = _this->UnserializeState(pChunk, pos);
					_this->OnParamReset();
					_this->ModifyCurrentPreset();
				}
				if (pos >= 0)
				{
					_this->RedrawParamControls();
					ret = 1;
				}
			}
			break;
		}

    case effString2Parameter:
    {
      if (idx >= 0 && idx < _this->NParams())
      {
        if (ptr)
        {
          IParam* pParam = _this->GetParam(idx);
          double v = VSTString2Parameter(pParam, (char*)ptr);
          if (_this->GetGUI()) _this->GetGUI()->SetParameterFromPlug(idx, v, false);
          pParam->Set(v);
          _this->OnParamChange(idx);
        }
        return 1;
      }
      return 0;
    }
    case effProcessEvents: {
	    VstEvents* pEvents = (VstEvents*) ptr;
	    if (pEvents && pEvents->events) {
		    for (int i = 0; i < pEvents->numEvents; ++i) {
          VstEvent* pEvent = pEvents->events[i];
			    if (pEvent) {
				    if (pEvent->type == kVstMidiType) {
					    VstMidiEvent* pME = (VstMidiEvent*) pEvent;
              IMidiMsg msg(pME->deltaFrames, pME->midiData[0], pME->midiData[1], pME->midiData[2]);
              _this->ProcessMidiMsg(&msg);
              //#ifdef TRACER_BUILD
              //  msg.LogMsg();
              //#endif
				    }
				    else if (pEvent->type == kVstSysExType) {
				        VstMidiSysexEvent* pSE = (VstMidiSysexEvent*) pEvent;
				        ISysEx sysex(pSE->deltaFrames, (const BYTE*)pSE->sysexDump, pSE->dumpBytes);
				        _this->ProcessSysEx(&sysex);
				    }
			    }
		    }
		    return 1;
	    }
	    return 0;
    }
	  case effCanBeAutomated: {
	  	return 1;
    }
	  case effGetInputProperties: {
      if (ptr && idx >= 0 && idx < _this->NInChannels()) {
        VstPinProperties* pp = (VstPinProperties*) ptr;
        pp->flags = kVstPinIsActive;
        if (!(idx%2) && idx < _this->NInChannels()-1)
        {
          pp->flags |= kVstPinIsStereo;
        }
        sprintf(pp->label, "Input %d", idx + 1);
        return 1;
      }
      return 0;
    }
    case effGetOutputProperties: {
	    if (ptr && idx >= 0 && idx < _this->NOutChannels()) {
		    VstPinProperties* pp = (VstPinProperties*) ptr;
			  pp->flags = kVstPinIsActive;
        if (!(idx%2) && idx < _this->NOutChannels()-1)
        {
			  	pp->flags |= kVstPinIsStereo;
			  }
		    sprintf(pp->label, "Output %d", idx + 1);
		    return 1;
	    }
	    return 0;
    }
    case effGetPlugCategory: {
      if (_this->IsInst()) return kPlugCategSynth;
	    return kPlugCategEffect;
    }
    case effProcessVarIo: {
	    // VstVariableIo* pIO = (VstVariableIo*) ptr;		// For offline processing (of audio files?)
	    return 0;
    }
    case effSetSpeakerArrangement: {
	    VstSpeakerArrangement* pInputArr = (VstSpeakerArrangement*) value;
	    VstSpeakerArrangement* pOutputArr = (VstSpeakerArrangement*) ptr;
	    if (pInputArr) {
        int n = pInputArr->numChannels;
        _this->SetInputChannelConnections(0, n, true);
        _this->SetInputChannelConnections(n, _this->NInChannels() - n, false);
      }
	    if (pOutputArr) {
        int n = pOutputArr->numChannels;
        _this->SetOutputChannelConnections(0, n, true);
        _this->SetOutputChannelConnections(n, _this->NOutChannels() - n, false);
	    }
	    return 1;
    }
    case effGetSpeakerArrangement: {
	    VstSpeakerArrangement** ppInputArr = (VstSpeakerArrangement**) value;
	    VstSpeakerArrangement** ppOutputArr = (VstSpeakerArrangement**) ptr;
      if (ppInputArr) {
        *ppInputArr = &(_this->mInputSpkrArr);
      }
      if (ppOutputArr) {
        *ppOutputArr = &(_this->mOutputSpkrArr);
      }
      return 1;
    }
    case effGetEffectName: {
	    if (ptr) {
		    strcpy((char*) ptr, _this->GetEffectName());
 		    return 1;
	    }
	    return 0;
    }
    case effGetProductString: {
	    if (ptr) {
		    strcpy((char*) ptr, _this->GetProductName());
		    return 1;
	    }
	    return 0;
    }
    case effGetVendorString: {
	    if (ptr) {
		    strcpy((char*) ptr, _this->GetMfrName());
		    return 1;
	    }
	    return 0;
    }
    case effGetVendorVersion: {
      return _this->GetEffectVersion(true);
    }
    case effCanDo: {
	    if (ptr) {
        Trace(TRACELOC, "VSTCanDo(%s)", (char*) ptr);
        if (!strcmp((char*) ptr, "receiveVstTimeInfo")) {
          return 1;
        }
        if (_this->DoesMIDI()) {
          if (_this->DoesMIDI() & 1) {
            if (!strcmp((char*) ptr, "sendVstEvents") ||
                !strcmp((char*) ptr, "sendVstMidiEvent")) {
              return 1;
            }
          }
          if (_this->DoesMIDI() <= 2) {
            if (!strcmp((char*) ptr, "receiveVstEvents") ||
                !strcmp((char*) ptr, "receiveVstMidiEvent")) {
              return 1;
            }
          }
          //if (!strcmp((char*) ptr, "midiProgramNames")) {
          //  return 1;
          //}
        }
        // Support Reaper VST extensions: http://www.reaper.fm/sdk/vst/
        if (!strcmp((char*) ptr, "hasCockosExtensions"))
        {
          _this->mHasVSTExtensions |= VSTEXT_COCKOS;
          return 0xbeef0000;
        }
        else if (!strcmp((char*) ptr, "hasCockosViewAsConfig")) 
        {
          _this->mHasVSTExtensions |= VSTEXT_COCOA;
          return 0xbeef0000; 
        }
      }
	    return 0;
    }
    case effVendorSpecific: {
      switch (idx) {
        // Mouse wheel
        case 0x73744341: {
          if (value == 0x57686565) {
            IGraphics* pGraphics = _this->GetGUI();
            if (pGraphics) {
              return pGraphics->ProcessMouseWheel(opt);
            }
          }
          break;
        }
        // Support Reaper VST extensions: http://www.reaper.fm/sdk/vst/
        case effGetParamDisplay: {
          if (ptr) {
            if (value >= 0 && value < _this->NParams()) {
              _this->GetParam(value)->GetDisplayForHost((double) opt, true, (char*) ptr);
            }
            return 0xbeef;
          }
          break;
        }
        case effString2Parameter: {
          if (ptr && value >= 0 && value < _this->NParams()) {
            if (*(char*) ptr != '\0') {
              IParam* pParam = _this->GetParam(value);
              sprintf((char*) ptr, "%.17f", pParam->GetNormalized(VSTString2Parameter(pParam, (char*) ptr)));
            }
            return 0xbeef;
          }
          break;
        }
        case kVstParameterUsesIntStep: {
          if (value >= 0 && value < _this->NParams()) {
            IParam* pParam = _this->GetParam(value);
            switch (pParam->Type()) {
              case IParam::kTypeBool: {
                return 0xbeef;
              }
              case IParam::kTypeInt:
              case IParam::kTypeEnum: {
                double min, max;
                pParam->GetBounds(&min, &max);
                if (fabs(max - min) < 1.5) {
                  return 0xbeef;
                }
                break;
              }
            }
          }
          break;
        }
      }
      return 0;
    }
    case effGetProgramNameIndexed: {
      strcpy((char*) ptr, _this->GetPresetName(idx));
      return (CSTR_NOT_EMPTY((char*) ptr) ? 1 : 0);
    }
    case effGetMidiKeyName: {
	    if (ptr) {
		    MidiKeyName* pMKN = (MidiKeyName*) ptr;
		    pMKN->keyName[0] = '\0';
		    if (_this->MidiNoteName(pMKN->thisKeyNumber, pMKN->keyName)) {
			    return 1;
		    }
	    }
	    return 0;
    }
    case effGetVstVersion: {
	    return VST_VERSION;
    }
    case effBeginSetProgram:
    case effEndSetProgram:
    case effGetMidiProgramName: 
    case effHasMidiProgramsChanged:
    case effGetMidiProgramCategory: 
    case effGetCurrentMidiProgram:
    case effSetBypass:
	}

	_this->mMutex.Leave();
	return ret;
}

template <class SAMPLETYPE>
void IPlugVST2::VSTPrepProcess(const SAMPLETYPE* const* const inputs, SAMPLETYPE* const* const outputs, const VstInt32 nFrames)
{
	if (DoesMIDI())
	{
		mHostCallback(&mAEffect, DECLARE_VST_DEPRECATED(audioMasterWantMidi), 0, 0, NULL, 0.0f);
	}
	AttachInputBuffers(0, NInChannels(), inputs, nFrames);
	AttachOutputBuffers(0, NOutChannels(), outputs);
}

// Deprecated.
void VSTCALLBACK IPlugVST2::VSTProcess(AEffect* const pEffect, float** const inputs, float** const outputs, const VstInt32 nFrames)
{ 
	IPlugVST2* const _this = (IPlugVST2*)pEffect->object;
	_this->mMutex.Enter();

	_this->VSTPrepProcess(inputs, outputs, nFrames);
	_this->ProcessBuffersAccumulating((float)0.0f, nFrames);

	_this->mMutex.Leave();
}

void VSTCALLBACK IPlugVST2::VSTProcessReplacing(AEffect* const pEffect, float** const inputs, float** const outputs, const VstInt32 nFrames)
{ 
	IPlugVST2* const _this = (IPlugVST2*)pEffect->object;
	_this->mMutex.Enter();

	_this->VSTPrepProcess(inputs, outputs, nFrames);
	_this->ProcessBuffers((float)0.0f, nFrames);

	_this->mMutex.Leave();
}

void VSTCALLBACK IPlugVST2::VSTProcessDoubleReplacing(AEffect* const pEffect, double** const inputs, double** const outputs, const VstInt32 nFrames)
{  
	IPlugVST2* const _this = (IPlugVST2*)pEffect->object;
	_this->mMutex.Enter();

	_this->VSTPrepProcess(inputs, outputs, nFrames);
	_this->ProcessBuffers((double)0.0, nFrames);

	_this->mMutex.Leave();
}  

float VSTCALLBACK IPlugVST::VSTGetParameter(AEffect *pEffect, VstInt32 idx)
{ 
  Trace(TRACELOC, "%d", idx);
	IPlugVST* _this = (IPlugVST*) pEffect->object;
  IMutexLock lock(_this);
  if (idx >= 0 && idx < _this->NParams()) {
	  return (float) _this->GetParam(idx)->GetNormalized();
  }
  return 0.0f;
}

void VSTCALLBACK IPlugVST::VSTSetParameter(AEffect *pEffect, VstInt32 idx, float value)
{  
  Trace(TRACELOC, "%d:%f", idx, value);
	IPlugVST* _this = (IPlugVST*) pEffect->object;
  IMutexLock lock(_this);
  if (idx >= 0 && idx < _this->NParams()) {
    if (_this->GetGUI()) {
      _this->GetGUI()->SetParameterFromPlug(idx, value, true);
  	}
    _this->GetParam(idx)->SetNormalized(value);
  	_this->OnParamChange(idx);
  }
}
