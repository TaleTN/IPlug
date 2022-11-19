#include "IPlugVST2.h"
#include "IGraphics.h"
#include "Hosts.h"

#ifdef __APPLE__
	#include "IGraphicsMac.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"
#include "WDL/wdltypes.h"

#define vst_strncpy(y, x, n) lstrcpyn_safe(y, x, n)

struct CanDoTbl
{
	unsigned char mIdx;
	char mStr[23];
};

enum EPlugCanDos
{
	kReceiveVstTimeInfo = 0,
	kSendVstEvents,
	kSendVstMidiEvent,
	kReceiveVstEvents,
	kReceiveVstMidiEvent,
	// kMidiProgramNames,
	kCanDoBypass,

	kHasCockosExtensions,
	kHasCockosViewAsConfig
};

static const CanDoTbl sPlugCanDos[] =
{
	{ kReceiveVstTimeInfo, "receiveVstTimeInfo" },
	{ kSendVstEvents, "sendVstEvents" },
	{ kSendVstMidiEvent, "sendVstMidiEvent" },
	{ kReceiveVstEvents, "receiveVstEvents" },
	{ kReceiveVstMidiEvent, "receiveVstMidiEvent" },
	// { kMidiProgramNames, "midiProgramNames" },
	{ kCanDoBypass, "bypass" },

	{ kHasCockosExtensions, "hasCockosExtensions" },
	{ kHasCockosViewAsConfig, "hasCockosViewAsConfig" }
};

enum EPinPropFlags
{
	kPinNotStereo = kVstPinIsActive | kVstPinUseSpeaker,
	kPinIsStereo = kVstPinIsActive | kVstPinIsStereo | kVstPinUseSpeaker
};

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
	return mState.Alloc(chunkSize) == chunkSize;
}

bool IPlugVST2::AllocBankChunk(int chunkSize)
{
	if (chunkSize < 0)
	{
		if (mPresetChunkSize < 0) AllocPresetChunk();
		chunkSize = GetBankChunkSize(NPresets(), mPresetChunkSize);
	}
	chunkSize += kInitializeVSTChunkSize;
	return mBankState.Alloc(chunkSize) == chunkSize;
}

void IPlugVST2::SetPresetName(int idx, const char* const name)
{
	if (idx < 0) idx = GetCurrentPresetIdx();
	IPreset* const pPreset = mPresets.Get(idx);
	if (pPreset) pPreset->SetName(name);
}

void IPlugVST2::BeginInformHostOfParamChange(const int idx, const bool lockMutex)
{
	EndDelayedInformHostOfParamChange(lockMutex);
	mHostCallback(&mAEffect, audioMasterBeginEdit, idx, 0, NULL, 0.0f);
}

void IPlugVST2::InformHostOfParamChange(const int idx, const double normalizedValue, bool /* lockMutex */)
{
	mHostCallback(&mAEffect, audioMasterAutomate, idx, 0, NULL, (float)normalizedValue);
}

void IPlugVST2::EndInformHostOfParamChange(const int idx, bool /* lockMutex */)
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
		char productStr[wdl_max(256, kVstMaxProductStrLen + 1)];
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
		const int oldW = mEditRect.right - mEditRect.left;
		const int oldH = mEditRect.bottom - mEditRect.top;

		if (w == oldW && h == oldH) return;

		mEditRect.left = mEditRect.top = 0;
		mEditRect.right = w;
		mEditRect.bottom = h;

		mHostCallback(&mAEffect, audioMasterSizeWindow, w, h, NULL, 0.0f);
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

static inline void SetMidiEventData(void* const ptr, const WDL_UINT64 data)
{
	memcpy(ptr, &data, sizeof(WDL_UINT64));
}

bool IPlugVST2::SendMidiMsg(const IMidiMsg* const pMsg)
{
	VstMidiEvent midiEvent;

	#if defined(_WIN64) || (defined(__LP64__) && __LITTLE_ENDIAN__)
	{
		const unsigned int ofs = pMsg->mOffset;
		unsigned int data;

		assert(pMsg->_padding == 0);
		memcpy(&data, &pMsg->mStatus, sizeof(data));

		SetMidiEventData(&midiEvent.type, ((WDL_UINT64)sizeof(VstMidiEvent) << 32) | kVstMidiType);
		SetMidiEventData(&midiEvent.deltaFrames, ofs);
		SetMidiEventData(&midiEvent.noteLength, 0);
		SetMidiEventData(&midiEvent.midiData, data);
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

int IPlugVST2::GetMaxParamStrLen() const
{
	// See https://www.reaper.fm/sdk/vst/
	const bool hasCockosExtensions = !!(mHasVSTExtensions & VSTEXT_COCKOS);
	return hasCockosExtensions ? 256 : kVstMaxParamStrLen;
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
				vst_strncpy((char*)ptr, _this->GetParam(idx)->GetLabelForHost(), _this->GetMaxParamStrLen());
			}
			break;
		}

		case effGetParamDisplay:
		{
			if (_this->NParams(idx))
			{
				_this->GetParam(idx)->GetDisplayForHost((char*)ptr, _this->GetMaxParamStrLen());
			}
			break;
		}

		case effGetParamName:
		{
			if (_this->NParams(idx))
			{
				const int len = _this->GetMaxParamStrLen();
				vst_strncpy((char*)ptr, _this->GetParam(idx)->GetNameForHost(len), len);
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
				_this->mPlugFlags = flags | kPlugInitSampleRate;
				if (flags & kPlugInitBlockSize) _this->Reset();
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
				_this->mPlugFlags = flags | kPlugInitBlockSize;
				if (flags & kPlugInitSampleRate) _this->Reset();
			}
			break;
		}

		case effMainsChanged:
		{
			const bool active = !!value;
			if (_this->IsActive() != active)
			{
				_this->mPlugFlags ^= IPlugBase::kPlugFlagsActive;
				_this->OnActivate(active);
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
				if (!pChunk->AllocSize())
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

		case effProcessEvents:
		{
			const VstEvents* const pEvents = (VstEvents*)ptr;
			if (!pEvents) break;

			const int numEvents = pEvents->numEvents;
			if (pEvents->events)
			{
				for (int i = 0; i < numEvents; ++i)
				{
					const VstEvent* const pEvent = pEvents->events[i];
					if (pEvent)
					{
						if (pEvent->type == kVstMidiType)
						{
							const VstMidiEvent* const pME = (const VstMidiEvent*)pEvent;
							const IMidiMsg msg(pME->deltaFrames, pME->midiData);
							_this->ProcessMidiMsg(&msg);
						}
						else if (pEvent->type == kVstSysExType)
						{
							const VstMidiSysexEvent* const pSE = (const VstMidiSysexEvent*)pEvent;
							const ISysEx sysex(pSE->deltaFrames, pSE->sysexDump, pSE->dumpBytes);
							_this->ProcessSysEx(&sysex);
						}
					}
				}
				ret = 1;
			}
			break;
		}

		case effCanBeAutomated:
		{
			/* if (_this->NParams(idx)) */ ret = 1;
			break;
		}

		case effString2Parameter:
		{
			if (_this->NParams(idx))
			{
				if (ptr)
				{
					IParam* const pParam = _this->GetParam(idx);
					const double v = VSTString2Parameter(pParam, (const char*)ptr);
					IGraphics* const pGraphics = _this->GetGUI();
					if (pGraphics)
					{
						pGraphics->SetParameterFromPlug(idx, v, true);
					}
					pParam->SetNormalized(v);
					_this->OnParamChange(idx);
				}
				ret = 1;
			}
			break;
		}

		case effGetProgramNameIndexed:
		{
			char* const buf = (char*)ptr;
			vst_strncpy(buf, _this->GetPresetName(idx), kVstMaxProgNameLen);
			ret = !!*buf;
			break;
		}

		case effGetInputProperties:
		{
			if (ptr && _this->NInChannels(idx))
			{
				VstPinProperties* const pp = (VstPinProperties*)ptr;
				const int i = idx + 1;
				snprintf(pp->label, kVstMaxLabelLen, "Input %d", i);
				pp->flags = ((i & 1) & (i < _this->NInChannels())) ? kPinIsStereo : kPinNotStereo;
				pp->arrangementType = _this->mInputSpkrArr.type;
				snprintf(pp->shortLabel, kVstMaxShortLabelLen, "In%d", i);
				ret = 1;
			}
			break;
		}

		case effGetOutputProperties:
		{
			if (ptr && _this->NOutChannels(idx))
			{
				VstPinProperties* const pp = (VstPinProperties*)ptr;
				const int i = idx + 1;
				snprintf(pp->label, kVstMaxLabelLen, "Output %d", i);
				pp->flags = ((i & 1) & (i < _this->NOutChannels())) ? kPinIsStereo : kPinNotStereo;
				pp->arrangementType = _this->mOutputSpkrArr.type;
				snprintf(pp->shortLabel, kVstMaxShortLabelLen, "Out%d", i);
				ret = 1;
			}
			break;
		}

		case effGetPlugCategory:
		{
			ret = _this->IsInst() ? kPlugCategSynth : kPlugCategEffect;
			break;
		}

		case effSetSpeakerArrangement:
		{
			VstSpeakerArrangement* const pInputArr = (VstSpeakerArrangement*)value;
			VstSpeakerArrangement* const pOutputArr = (VstSpeakerArrangement*)ptr;
			if (pInputArr)
			{
				const int n = pInputArr->numChannels;
				_this->SetInputChannelConnections(0, n, true);
				_this->SetInputChannelConnections(n, _this->NInChannels() - n, false);
			}
			if (pOutputArr)
			{
				const int n = pOutputArr->numChannels;
				_this->SetOutputChannelConnections(0, n, true);
				_this->SetOutputChannelConnections(n, _this->NOutChannels() - n, false);
			}
			ret = 1;
			break;
		}

		case effSetBypass:
		{
			const bool bypass = !!value;
			if (_this->IsBypassed() != bypass)
			{
				_this->mPlugFlags ^= IPlugBase::kPlugFlagsBypass;
				_this->OnBypass(bypass);
			}
			ret = 1;
			break;
		}

		case effGetEffectName:
		{
			if (ptr)
			{
				vst_strncpy((char*)ptr, _this->GetEffectName(), kVstMaxEffectNameLen);
				ret = 1;
			}
			break;
		}

		case effGetVendorString:
		{
			if (ptr)
			{
				vst_strncpy((char*)ptr, _this->GetMfrName(), kVstMaxVendorStrLen);
				ret = 1;
			}
			break;
		}

		case effGetProductString:
		{
			if (ptr)
			{
				vst_strncpy((char*)ptr, _this->GetProductName(), kVstMaxProductStrLen);
				ret = 1;
			}
			break;
		}

		case effGetVendorVersion:
		{
			ret = _this->GetEffectVersion(true);
			break;
		}

		case effVendorSpecific:
		{
			ret = _this->VSTVendorSpecific(idx, value, ptr, opt);
			break;
		}

		case effCanDo:
		{
			if (ptr) ret = _this->VSTPlugCanDo((const char*)ptr);
			break;
		}

		case effGetParameterProperties:
		{
			if (ptr && _this->NParams(idx))
			{
				VstParameterProperties* const pp = (VstParameterProperties*)ptr;
				const IParam* const pParam = _this->GetParam(idx);
				vst_strncpy(pp->label, pParam->GetNameForHost(), kVstMaxLabelLen);
				switch (pParam->Type())
				{
					case IParam::kTypeBool:
					{
						pp->flags = kVstParameterIsSwitch;
						break;
					}
					case IParam::kTypeEnum:
					{
						pp->flags = kVstParameterUsesIntegerMinMax | kVstParameterUsesIntStep;
						pp->minInteger = 0;
						pp->maxInteger = ((const IEnumParam*)pParam)->NEnums() - 1;
						pp->largeStepInteger = pp->stepInteger = 1;
						break;
					}
				}
				static const int len = kVstMaxShortLabelLen;
				vst_strncpy(pp->shortLabel, pParam->GetNameForHost(len), len);
				ret = 1;
			}
			break;
		}

		case effGetVstVersion:
		{
			ret = kVstVersion;
			break;
		}

		case effEditKeyDown:
		case effEditKeyUp:
		{
			IGraphics* const pGraphics = _this->GetGUI();
			if (!(pGraphics && pGraphics->GetKeyboardFocus() >= 0)) break;

			const int flags = (int)opt;
			IMouseMod mod(false, false, !!(flags & MODIFIER_SHIFT), !!(flags & MODIFIER_CONTROL), !!(flags & MODIFIER_ALTERNATE));

			int key = (int)value;
			if (key--)
			{
				static const char vk[16] = { 0, 0, 0, 0, 0, 0, VK_SPACE, 0, VK_END, VK_HOME, VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_PRIOR, VK_NEXT };
				key = (unsigned int)key < 16 ? vk[key] : 0;
			}
			else
			{
				if ((unsigned int)(key - 'a') < 26)
				{
					static const int toUpper = 'a' - 'A';
					key -= toUpper;
					mod.S = 1;
				}
				static const unsigned char ascii[12] = { 0, 0, 0, 0, 0, 0, 0xFF, 0x03, 0xFE, 0xFF, 0xFF, 0x07 };
				key = (unsigned int)key < 96 && !!(ascii[key >> 3] & (1 << (key & 7))) ? key : 0;
			}
			if (!key) break;

			const bool state = opCode == effEditKeyDown;
			ret = pGraphics->ProcessKey(state, mod, key);
			break;
		}

		case effGetMidiKeyName:
		{
			MidiKeyName* const pMKN = (MidiKeyName*)ptr;
			if (pMKN && _this->MidiNoteName(pMKN->thisKeyNumber, pMKN->keyName, kVstMaxNameLen))
			{
				ret = 1;
			}
			break;
		}

		case effGetSpeakerArrangement:
		{
			VstSpeakerArrangement** const ppInputArr = (VstSpeakerArrangement**)value;
			VstSpeakerArrangement** const ppOutputArr = (VstSpeakerArrangement**)ptr;
			if (ppInputArr) *ppInputArr = &_this->mInputSpkrArr;
			if (ppOutputArr) *ppOutputArr = &_this->mOutputSpkrArr;
			ret = 1;
			break;
		}
	}

	_this->mMutex.Leave();
	return ret;
}

VstIntPtr IPlugVST2::VSTVendorSpecific(const VstInt32 idx, const VstIntPtr value, void* const ptr, const float opt)
{
	switch (idx)
	{
		// Support Cockos Extensions to VST SDK: https://www.reaper.fm/sdk/vst/

		case effGetParamDisplay:
		{
			const int paramIdx = (int)value;
			if (NParams(paramIdx) && ptr)
			{
				GetParam(paramIdx)->GetDisplayForHost(opt, (char*)ptr/*, 256 */);
				return 0xbeef;
			}
			break;
		}

		case kVstParameterUsesIntStep:
		{
			const int paramIdx = (int)value;
			if (NParams(paramIdx))
			{
				static const unsigned int typeEnum = IParam::kTypeEnum - IParam::kTypeBool;
				const unsigned int type = GetParam(paramIdx)->Type() - IParam::kTypeBool;
				return type <= typeEnum ? 0xbeef : 0;
			}
			break;
		}

		case effString2Parameter:
		{
			const int paramIdx = (int)value;
			if (NParams(paramIdx) && ptr)
			{
				char* const buf = (char*)ptr;
				if (*buf)
				{
					const double v = VSTString2Parameter(GetParam(paramIdx), buf);
					snprintf(buf, 20, "%.17f", v);
				}
				return 0xbeef;
			}
			break;
		}

		// Mouse wheel, original source:
		// http://asseca.com/vst-24-specs/efVendorSpecific.html

		case 0x73744341: // 'stCA'
		{
			if (value == 0x57686565) // 'Whee'
			{
				IGraphics* const pGraphics = GetGUI();
				if (pGraphics)
				{
					// 1.0 = wheel-up, -1.0 = wheel-down
					return pGraphics->ProcessMouseWheel(opt);
				}
			}
			break;
		}
	}

	return 0;
}

VstIntPtr IPlugVST2::VSTPlugCanDo(const char* const ptr)
{
	int canDo = -1;

	const int n = sizeof(sPlugCanDos) / sizeof(sPlugCanDos[0]);
	for (int i = 0; i < n; ++i)
	{
		const int idx = sPlugCanDos[i].mIdx;
		const char* const str = sPlugCanDos[i].mStr;

		if (!strcmp(ptr, str))
		{
			canDo = idx;
			break;
		}
	}

	switch (canDo)
	{
		case kReceiveVstTimeInfo:
			return 1;

		case kSendVstEvents:
		case kSendVstMidiEvent:
			return DoesMIDI(kPlugDoesMidiOut);

		case kReceiveVstEvents:
		case kReceiveVstMidiEvent:
			return DoesMIDI(kPlugDoesMidiIn);

		// case kMidiProgramNames:
			// return 1;

		case kCanDoBypass:
			return 1;

		// Support Cockos Extensions to VST SDK: https://www.reaper.fm/sdk/vst/

		case kHasCockosExtensions:
			mHasVSTExtensions |= VSTEXT_COCKOS;
			return 0xbeef0000;

		case kHasCockosViewAsConfig:
			mHasVSTExtensions |= VSTEXT_COCOA;
			return 0xbeef0000;
	}

	return 0;
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

float VSTCALLBACK IPlugVST2::VSTGetParameter(AEffect* const pEffect, const VstInt32 idx)
{
	double v = 0.0;

	IPlugVST2* const _this = (IPlugVST2*)pEffect->object;
	_this->mMutex.Enter();

	if (_this->NParams(idx)) v = _this->GetParam(idx)->GetNormalized();

	_this->mMutex.Leave();
	return (float)v;
}

void VSTCALLBACK IPlugVST2::VSTSetParameter(AEffect* const pEffect, const VstInt32 idx, const float value)
{
	const double v = (double)value;

	IPlugVST2* const _this = (IPlugVST2*)pEffect->object;
	_this->mMutex.Enter();

	// TN: Why is order in IPlugAU::SetParamProc() different?
	if (_this->NParams(idx))
	{
		IGraphics* const pGraphics = _this->GetGUI();
		if (pGraphics) pGraphics->SetParameterFromPlug(idx, v, true);

		_this->GetParam(idx)->SetNormalized(v);
		_this->OnParamChange(idx);
	}

	_this->mMutex.Leave();
}
