#include "IPlugAU.h"
#include "IGraphicsMac.h"
#include "Hosts.h"

#include "dfx/dfx-au-utilities.h"

#include <AudioUnit/AudioUnit.h>

#ifndef IPLUG_NO_CARBON_SUPPORT
	#include <AudioUnit/AudioUnitCarbonView.h>
#endif

#define kAudioUnitRemovePropertyListenerWithUserDataSelect 0x0012

#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"
#include "WDL/wdltypes.h"

static CFStringRef MakeCFString(const char* const cStr)
{
	return CFStringCreateWithCString(NULL, cStr, kCFStringEncodingUTF8);
}

struct CFStrLocal
{
	CFStringRef const mCFStr;
	CFStrLocal(const char* const cStr): mCFStr(MakeCFString(cStr)) {}
	~CFStrLocal() { CFRelease(mCFStr); }
};

struct CStrLocal
{
	char* mCStr;
	CStrLocal(CFStringRef const cfStr)
	{
		const int n = CFStringGetLength(cfStr) + 1;
		mCStr = (char*)malloc(n);
		CFStringGetCString(cfStr, mCStr, n, kCFStringEncodingUTF8);
	}
	~CStrLocal()
	{
		free(mCStr);
	}
};

typedef AudioStreamBasicDescription STREAM_DESC;

static void MakeDefaultASBD(STREAM_DESC* const pASBD, const double sampleRate, const int nChannels, const bool interleaved)
{
	memset(pASBD, 0, sizeof(STREAM_DESC));
	pASBD->mSampleRate = sampleRate;
	pASBD->mFormatID = kAudioFormatLinearPCM;
	pASBD->mFormatFlags = kAudioFormatFlagsCanonical;
	pASBD->mBitsPerChannel = 8 * sizeof(AudioSampleType);
	pASBD->mChannelsPerFrame = nChannels;
	pASBD->mFramesPerPacket = 1;
	int nBytes = (int)sizeof(AudioSampleType);
	if (interleaved)
	{
		nBytes *= nChannels;
	}
	else
	{
		pASBD->mFormatFlags |= kAudioFormatFlagIsNonInterleaved;
	}
	pASBD->mBytesPerPacket = pASBD->mBytesPerFrame = nBytes;
}

template <class C>
static int PtrListAddFromStack(WDL_PtrList<C>* const pList, const C* const pStackInstance)
{
	C* const pNew = new C;
	memcpy(pNew, pStackInstance, sizeof(C));
	pList->Add(pNew);
	return pList->GetSize() - 1;
}

template <class C>
static int PtrListInitialize(WDL_PtrList<C>* const pList, int const size)
{
	for (int i = 0; i < size; ++i)
	{
		C* const pNew = new C;
		memset(pNew, 0, sizeof(C));
		pList->Add(pNew);
	}
	return size;
}

template <class C>
static inline void SetCompParam(long* const pParam, const C value)
{
	*(C*)pParam = value;
}

#define INIT_COMP_PARAMS(WHAT, NUM) \
	union { long alloc[1 + NUM + 1]; ComponentParameters params; }; \
	params.what = WHAT

#ifdef __LP64__
	#define GET_COMP_PARAM(TYPE, IDX, NUM) *((TYPE*)&(params->params[NUM - IDX]))
	#define SET_COMP_PARAM(VALUE, IDX, NUM) SetCompParam(&params.params[NUM - IDX], VALUE)
#else
	#define GET_COMP_PARAM(TYPE, IDX, NUM) *((TYPE*)&(params->params[IDX]))
	#define SET_COMP_PARAM(VALUE, IDX, NUM) SetCompParam(&params.params[IDX], VALUE)
#endif

#define BASE_LOOKUP(method) case kAudioUnit##method##Select: return (AudioComponentMethod)AUMethod##method
#define MIDI_LOOKUP(method) case kMusicDevice##method##Select: return (AudioComponentMethod)AUMethod##method

struct IPlugAUInstance
{
	AudioComponentPlugInInterface mInterface;
	IPlugAU* mPlug;
};

static inline void* GetPlug(void* const pInstance)
{
	return ((IPlugAUInstance*)pInstance)->mPlug;
}

// static
void* IPlugAU::IPlugAUFactory(const AudioComponentDescription* const pDesc)
{
	const int type = pDesc->componentType;

	IPlugAUInstance* const pInstance = new IPlugAUInstance;
	pInstance->mInterface.Open = AP_Open;
	pInstance->mInterface.Close = AP_Close;
	pInstance->mInterface.Lookup = type == kAudioUnitType_MusicDevice || type == kAudioUnitType_MusicEffect ? AUMIDILookup : AUBaseLookup;
	pInstance->mInterface.reserved = NULL;
	pInstance->mPlug = NULL;
	return pInstance;
}

// static
OSStatus IPlugAU::AP_Open(void* const pInstance, AudioComponentInstance const compInstance)
{
	IPlugAU* const _this = MakeIPlugAU();
	_this->HostSpecificInit();
	_this->PruneUninitializedPresets();
	_this->mCI = compInstance;
	_this->mFactory = true;
	((IPlugAUInstance*)pInstance)->mPlug = _this;
	return noErr;
}

// static
OSStatus IPlugAU::AP_Close(void* const pInstance)
{
	IPlugAU* const _this = (IPlugAU*)GetPlug(pInstance);
	_this->ClearConnections();
	delete _this;
	delete (IPlugAUInstance*)pInstance;
	return noErr;
}

// static
AudioComponentMethod IPlugAU::AUBaseLookup(const SInt16 selector)
{
	switch (selector)
	{
		BASE_LOOKUP(Initialize);
		BASE_LOOKUP(Uninitialize);
		BASE_LOOKUP(GetPropertyInfo);
		BASE_LOOKUP(GetProperty);
		BASE_LOOKUP(SetProperty);
		BASE_LOOKUP(GetParameter);
		BASE_LOOKUP(SetParameter);
		BASE_LOOKUP(Reset);
		BASE_LOOKUP(AddPropertyListener);
		BASE_LOOKUP(RemovePropertyListener);
		BASE_LOOKUP(Render);
		BASE_LOOKUP(AddRenderNotify);
		BASE_LOOKUP(RemoveRenderNotify);
		BASE_LOOKUP(ScheduleParameters);
		BASE_LOOKUP(RemovePropertyListenerWithUserData);
	}
	return NULL;
}

// static
AudioComponentMethod IPlugAU::AUMIDILookup(const SInt16 selector)
{
	switch (selector)
	{
		MIDI_LOOKUP(MIDIEvent);
		MIDI_LOOKUP(SysEx);
	}
	return AUBaseLookup(selector);
}

// static
OSStatus IPlugAU::AUMethodInitialize(void* const pInstance)
{
	INIT_COMP_PARAMS(kAudioUnitInitializeSelect, 0);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodUninitialize(void* const pInstance)
{
	INIT_COMP_PARAMS(kAudioUnitUninitializeSelect, 0);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodGetPropertyInfo(void* const pInstance, const AudioUnitPropertyID propID, const AudioUnitScope scope,
	const AudioUnitElement element, UInt32* const pDataSize, Boolean* const pWriteable)
{
	INIT_COMP_PARAMS(kAudioUnitGetPropertyInfoSelect, 5);
	SET_COMP_PARAM(propID, 4, 5);
	SET_COMP_PARAM(scope, 3, 5);
	SET_COMP_PARAM(element, 2, 5);
	SET_COMP_PARAM(pDataSize, 1, 5);
	SET_COMP_PARAM(pWriteable, 0, 5);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodGetProperty(void* const pInstance, const AudioUnitPropertyID propID, const AudioUnitScope scope,
	const AudioUnitElement element, void* const pData, UInt32* const pDataSize)
{
	INIT_COMP_PARAMS(kAudioUnitGetPropertySelect, 5);
	SET_COMP_PARAM(propID, 4, 5);
	SET_COMP_PARAM(scope, 3, 5);
	SET_COMP_PARAM(element, 2, 5);
	SET_COMP_PARAM((INT_PTR)pData, 1, 5);
	SET_COMP_PARAM((INT_PTR)pDataSize, 0, 5);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodSetProperty(void* const pInstance, const AudioUnitPropertyID propID, const AudioUnitScope scope,
	const AudioUnitElement element, const void* const pData, const UInt32 pDataSize)
{
	INIT_COMP_PARAMS(kAudioUnitSetPropertySelect, 5);
	SET_COMP_PARAM(propID, 4, 5);
	SET_COMP_PARAM(scope, 3, 5);
	SET_COMP_PARAM(element, 2, 5);
	SET_COMP_PARAM(pData, 1, 5);
	SET_COMP_PARAM(pDataSize, 0, 5);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodGetParameter(void* const pInstance, const AudioUnitParameterID paramID, const AudioUnitScope scope,
	const AudioUnitElement element, AudioUnitParameterValue* const pValue)
{
	INIT_COMP_PARAMS(kAudioUnitGetParameterSelect, 4);
	SET_COMP_PARAM(paramID, 3, 4);
	SET_COMP_PARAM(scope, 2, 4);
	SET_COMP_PARAM(element, 1, 4);
	SET_COMP_PARAM(pValue, 0, 4);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodSetParameter(void* const pInstance, const AudioUnitParameterID paramID, const AudioUnitScope scope,
	const AudioUnitElement element, const AudioUnitParameterValue value, const UInt32 offset)
{
	INIT_COMP_PARAMS(kAudioUnitSetParameterSelect, 5);
	SET_COMP_PARAM(paramID, 4, 5);
	SET_COMP_PARAM(scope, 3, 5);
	SET_COMP_PARAM(element, 2, 5);
	SET_COMP_PARAM(value, 1, 5);
	SET_COMP_PARAM(offset, 0, 5);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodReset(void* const pInstance, AudioUnitScope /* scope */, AudioUnitElement /* element */)
{
	INIT_COMP_PARAMS(kAudioUnitResetSelect, /* 2 */ 0);
	/* SET_COMP_PARAM(scope, 1, 2);
	SET_COMP_PARAM(element, 0, 2); */
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodAddPropertyListener(void* const pInstance, const AudioUnitPropertyID propID,
	AudioUnitPropertyListenerProc const listenerProc, void* const pProcArgs)
{
	INIT_COMP_PARAMS(kAudioUnitAddPropertyListenerSelect, 3);
	SET_COMP_PARAM(propID, 2, 3);
	SET_COMP_PARAM(listenerProc, 1, 3);
	SET_COMP_PARAM(pProcArgs, 0, 3);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodRemovePropertyListener(void* const pInstance, const AudioUnitPropertyID propID,
	AudioUnitPropertyListenerProc const listenerProc)
{
	INIT_COMP_PARAMS(kAudioUnitRemovePropertyListenerSelect, 2);
	SET_COMP_PARAM(propID, 1, 2);
	SET_COMP_PARAM(listenerProc, 0, 2);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodRender(void* const pInstance, AudioUnitRenderActionFlags* const pFlags, const AudioTimeStamp* const pTimestamp,
	const UInt32 outputBusIdx, const UInt32 nFrames, AudioBufferList* const pBufferList)
{
	INIT_COMP_PARAMS(kAudioUnitRenderSelect, 5);
	SET_COMP_PARAM(pFlags, 4, 5);
	SET_COMP_PARAM(pTimestamp, 3, 5);
	SET_COMP_PARAM(outputBusIdx, 2, 5);
	SET_COMP_PARAM(nFrames, 1, 5);
	SET_COMP_PARAM(pBufferList, 0, 5);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodAddRenderNotify(void* const pInstance, AURenderCallback const renderProc, void* const pRefCon)
{
	INIT_COMP_PARAMS(kAudioUnitAddRenderNotifySelect, 2);
	SET_COMP_PARAM(renderProc, 1, 2);
	SET_COMP_PARAM(pRefCon, 0, 2);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodRemoveRenderNotify(void* const pInstance, AURenderCallback const renderProc, void* const pRefCon)
{
	INIT_COMP_PARAMS(kAudioUnitRemoveRenderNotifySelect, 2);
	SET_COMP_PARAM(renderProc, 1, 2);
	SET_COMP_PARAM(pRefCon, 0, 2);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodScheduleParameters(void* const pInstance, const AudioUnitParameterEvent* const pEvent, const UInt32 nEvents)
{
	INIT_COMP_PARAMS(kAudioUnitScheduleParametersSelect, 2);
	SET_COMP_PARAM(pEvent, 1, 2);
	SET_COMP_PARAM(nEvents, 0, 2);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodRemovePropertyListenerWithUserData(void* const pInstance, const AudioUnitPropertyID propID,
	AudioUnitPropertyListenerProc const listenerProc, void* const pProcArgs)
{
	INIT_COMP_PARAMS(kAudioUnitRemovePropertyListenerWithUserDataSelect, 3);
	SET_COMP_PARAM(propID, 2, 3);
	SET_COMP_PARAM(listenerProc, 1, 3);
	SET_COMP_PARAM(pProcArgs, 0, 3);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodMIDIEvent(void* const pInstance, const UInt32 status, const UInt32 data1, const UInt32 data2, const UInt32 offset)
{
	INIT_COMP_PARAMS(kMusicDeviceMIDIEventSelect, 4);
	SET_COMP_PARAM(status, 3, 4);
	SET_COMP_PARAM(data1, 2, 4);
	SET_COMP_PARAM(data2, 1, 4);
	SET_COMP_PARAM(offset, 0, 4);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
OSStatus IPlugAU::AUMethodSysEx(void* pInstance, const UInt8* pData, const UInt32 size)
{
	INIT_COMP_PARAMS(kMusicDeviceSysExSelect, 2);
	SET_COMP_PARAM(pData, 1, 2);
	SET_COMP_PARAM(size, 0, 2);
	return IPlugAUEntry(&params, GetPlug(pInstance));
}

// static
ComponentResult IPlugAU::IPlugAUEntry(ComponentParameters* const params, void* const pPlug)
{
	const int select = params->what;
	ComponentResult ret = noErr;

	if (select == kComponentOpenSelect)
	{
		IPlugAU* const _this = MakeIPlugAU();
		_this->HostSpecificInit();
		_this->PruneUninitializedPresets();
		_this->mCI = GET_COMP_PARAM(ComponentInstance, 0, 1);
		SetComponentInstanceStorage(_this->mCI, (Handle)_this);
		return ret;
	}

	IPlugAU* const _this = (IPlugAU*)pPlug;

	if (select == kComponentCloseSelect)
	{
		_this->ClearConnections();
		delete _this;
		return ret;
	}

	_this->mMutex.Enter();

	switch (select)
	{
		case kComponentVersionSelect:
		{
			ret = _this->GetEffectVersion(false);
			break;
		}

		case kAudioUnitInitializeSelect:
		{
			if (!(_this->CheckLegalIO()))
			{
				ret = badComponentSelector;
				break;
			}
			if (!_this->IsActive())
			{
				_this->OnParamReset();
				_this->mPlugFlags |= kPlugFlagsActive;
				_this->OnActivate(true);
			}
			break;
		}

		case kAudioUnitUninitializeSelect:
		{
			if (_this->IsActive())
			{
				_this->mPlugFlags &= ~kPlugFlagsActive;
				_this->OnActivate(false);
			}
			break;
		}

		case kAudioUnitGetPropertyInfoSelect:
		{
			const AudioUnitPropertyID propID = GET_COMP_PARAM(AudioUnitPropertyID, 4, 5);
			const AudioUnitScope scope = GET_COMP_PARAM(AudioUnitScope, 3, 5);
			const AudioUnitElement element = GET_COMP_PARAM(AudioUnitElement, 2, 5);
			UInt32* pDataSize = GET_COMP_PARAM(UInt32*, 1, 5);
			Boolean* pWriteable = GET_COMP_PARAM(Boolean*, 0, 5);

			UInt32 dataSize = 0;
			if (!pDataSize) pDataSize = &dataSize;
			Boolean writeable;
			if (!pWriteable) pWriteable = &writeable;
			*pWriteable = false;
			ret = _this->GetProperty(propID, scope, element, pDataSize, pWriteable, NULL);
			break;
		}

		case kAudioUnitGetPropertySelect:
		{
			const AudioUnitPropertyID propID = GET_COMP_PARAM(AudioUnitPropertyID, 4, 5);
			const AudioUnitScope scope = GET_COMP_PARAM(AudioUnitScope, 3, 5);
			const AudioUnitElement element = GET_COMP_PARAM(AudioUnitElement, 2, 5);
			void* const pData = GET_COMP_PARAM(void*, 1, 5);
			UInt32* pDataSize = GET_COMP_PARAM(UInt32*, 0, 5);

			UInt32 dataSize = 0;
			if (!pDataSize) pDataSize = &dataSize;
			Boolean writeable = false;
			ret = _this->GetProperty(propID, scope, element, pDataSize, &writeable, pData);
			break;
		}

		case kAudioUnitSetPropertySelect:
		{
			const AudioUnitPropertyID propID = GET_COMP_PARAM(AudioUnitPropertyID, 4, 5);
			const AudioUnitScope scope = GET_COMP_PARAM(AudioUnitScope, 3, 5);
			const AudioUnitElement element = GET_COMP_PARAM(AudioUnitElement, 2, 5);
			const void* const pData = GET_COMP_PARAM(const void*, 1, 5);
			UInt32* const pDataSize = GET_COMP_PARAM(UInt32*, 0, 5);
			ret = _this->SetProperty(propID, scope, element, pDataSize, pData);
			break;
		}

		case kAudioUnitAddPropertyListenerSelect:
		{
			PropertyListener listener;
			listener.mPropID = GET_COMP_PARAM(AudioUnitPropertyID, 2, 3);
			listener.mListenerProc = GET_COMP_PARAM(AudioUnitPropertyListenerProc, 1, 3);
			listener.mProcArgs = GET_COMP_PARAM(void*, 0, 3);
			bool found = false;
			const int n = _this->mPropertyListeners.GetSize();
			for (int i = 0; i < n; ++i)
			{
				const PropertyListener* const pListener = _this->mPropertyListeners.Get(i);
				if (listener.mPropID == pListener->mPropID && listener.mListenerProc == pListener->mListenerProc)
				{
					found = true;
					break;
				}
			}
			if (!found) PtrListAddFromStack(&_this->mPropertyListeners, &listener);
			break;
		}

		case kAudioUnitRemovePropertyListenerSelect:
		{
			PropertyListener listener;
			listener.mPropID = GET_COMP_PARAM(AudioUnitPropertyID, 1, 2);
			listener.mListenerProc = GET_COMP_PARAM(AudioUnitPropertyListenerProc, 0, 2);
			const int n = _this->mPropertyListeners.GetSize();
			for (int i = 0; i < n; ++i)
			{
				const PropertyListener* const pListener = _this->mPropertyListeners.Get(i);
				if (listener.mPropID == pListener->mPropID && listener.mListenerProc == pListener->mListenerProc)
				{
					_this->mPropertyListeners.Delete(i, true);
					break;
				}
			}
			break;
		}

		case kAudioUnitRemovePropertyListenerWithUserDataSelect:
		{
			PropertyListener listener;
			listener.mPropID = GET_COMP_PARAM(AudioUnitPropertyID, 2, 3);
			listener.mListenerProc = GET_COMP_PARAM(AudioUnitPropertyListenerProc, 1, 3);
			listener.mProcArgs = GET_COMP_PARAM(void*, 0, 3);
			const int n = _this->mPropertyListeners.GetSize();
			for (int i = 0; i < n; ++i)
			{
				PropertyListener* pListener = _this->mPropertyListeners.Get(i);
				if (listener.mPropID == pListener->mPropID &&
					listener.mListenerProc == pListener->mListenerProc && listener.mProcArgs == pListener->mProcArgs)
				{
					_this->mPropertyListeners.Delete(i, true);
					break;
				}
			}
			break;
		}

		case kAudioUnitAddRenderNotifySelect:
		{
			AURenderCallbackStruct acs;
			acs.inputProc = GET_COMP_PARAM(AURenderCallback, 1, 2);
			acs.inputProcRefCon = GET_COMP_PARAM(void*, 0, 2);
			PtrListAddFromStack(&_this->mRenderNotify, &acs);
			break;
		}

		case kAudioUnitRemoveRenderNotifySelect:
		{
			AURenderCallbackStruct acs;
			acs.inputProc = GET_COMP_PARAM(AURenderCallback, 1, 2);
			acs.inputProcRefCon = GET_COMP_PARAM(void*, 0, 2);
			const int n = _this->mRenderNotify.GetSize();
			for (int i = 0; i < n; ++i)
			{
				const AURenderCallbackStruct* const pACS = _this->mRenderNotify.Get(i);
				if (acs.inputProc == pACS->inputProc)
				{
					_this->mRenderNotify.Delete(i, true);
					break;
				}
			}
			break;
		}

		case kAudioUnitGetParameterSelect:
		{
			const AudioUnitParameterID paramID = GET_COMP_PARAM(AudioUnitParameterID, 3, 4);
			const AudioUnitScope scope = GET_COMP_PARAM(AudioUnitScope, 2, 4);
			const AudioUnitElement element = GET_COMP_PARAM(AudioUnitElement, 1, 4);
			AudioUnitParameterValue* const pValue = GET_COMP_PARAM(AudioUnitParameterValue*, 0, 4);
			ret = GetParamProc(pPlug, paramID, scope, element, pValue);
			break;
		}

		case kAudioUnitSetParameterSelect:
		{
			const AudioUnitParameterID paramID = GET_COMP_PARAM(AudioUnitParameterID, 4, 5);
			const AudioUnitScope scope = GET_COMP_PARAM(AudioUnitScope, 3, 5);
			const AudioUnitElement element = GET_COMP_PARAM(AudioUnitElement, 2, 5);
			const AudioUnitParameterValue value = GET_COMP_PARAM(AudioUnitParameterValue, 1, 5);
			const UInt32 offset = GET_COMP_PARAM(UInt32, 0, 5);
			ret = SetParamProc(pPlug, paramID, scope, element, value, offset);
			break;
		}

		case kAudioUnitScheduleParametersSelect:
		{
			AudioUnitParameterEvent* pEvent = GET_COMP_PARAM(AudioUnitParameterEvent*, 1, 2);
			const UInt32 nEvents = GET_COMP_PARAM(UInt32, 0, 2);
			for (UInt32 i = 0; i < nEvents; ++i, ++pEvent)
			{
				if (pEvent->eventType == kParameterEvent_Immediate)
				{
					const ComponentResult r = SetParamProc(pPlug, pEvent->parameter, pEvent->scope, pEvent->element,
						pEvent->eventValues.immediate.value, pEvent->eventValues.immediate.bufferOffset);
					if (r != noErr)
					{
						ret = r;
						break;
					}
				}
			}
			break;
		}

		case kAudioUnitRenderSelect:
		{
			AudioUnitRenderActionFlags* const pFlags = GET_COMP_PARAM(AudioUnitRenderActionFlags*, 4, 5);
			const AudioTimeStamp* const pTimestamp = GET_COMP_PARAM(AudioTimeStamp*, 3, 5);
			const UInt32 outputBusIdx = GET_COMP_PARAM(UInt32, 2, 5);
			const UInt32 nFrames = GET_COMP_PARAM(UInt32, 1, 5);
			AudioBufferList* const pBufferList = GET_COMP_PARAM(AudioBufferList*, 0, 5);
			ret = RenderProc(_this, pFlags, pTimestamp, outputBusIdx, nFrames, pBufferList);
			break;
		}

		case kAudioUnitResetSelect:
		{
			// TN: GarageBand seems to call this when toggling bypass, both
			// on and off, without letting plug-in know which.
			_this->Reset();
			break;
		}

		case kMusicDeviceMIDIEventSelect:
		{
			if (!_this->DoesMIDI(kPlugDoesMidiIn))
			{
				ret = badComponentSelector;
				break;
			}
			const UInt32 status = GET_COMP_PARAM(UInt32, 3, 4);
			const UInt32 data1 = GET_COMP_PARAM(UInt32, 2, 4);
			const UInt32 data2 = GET_COMP_PARAM(UInt32, 1, 4);
			const UInt32 offset = GET_COMP_PARAM(UInt32, 0, 4);
			const IMidiMsg msg(offset, status, data1, data2);
			_this->ProcessMidiMsg(&msg);
			break;
		}

		case kMusicDeviceSysExSelect:
		{
			if (!_this->DoesMIDI(kPlugDoesMidiIn))
			{
				ret = badComponentSelector;
				break;
			}
			const UInt8* const pData = GET_COMP_PARAM(UInt8*, 1, 2);
			const UInt32 size = GET_COMP_PARAM(UInt32, 0, 2);
			const ISysEx sysex(0, pData, size);
			_this->ProcessSysEx(&sysex);
			break;
		}

		case kMusicDevicePrepareInstrumentSelect:
		case kMusicDeviceReleaseInstrumentSelect:
		{
			break;
		}

		case kMusicDeviceStartNoteSelect:
		{
			// const MusicDeviceInstrumentID deviceID = GET_COMP_PARAM(MusicDeviceInstrumentID, 4, 5);
			// const MusicDeviceGroupID groupID = GET_COMP_PARAM(MusicDeviceGroupID, 3, 5);
			NoteInstanceID* const pNoteID = GET_COMP_PARAM(NoteInstanceID*, 2, 5);
			// const UInt32 offset = GET_COMP_PARAM(UInt32, 1, 5);
			MusicDeviceNoteParams* const pNoteParams = GET_COMP_PARAM(MusicDeviceNoteParams*, 0, 5);
			const int note = (int)pNoteParams->mPitch;
			*pNoteID = note;
			// const IMidiMsg msg(offset, IMidiMsg::kNoteOn << 4, note, (int)pNoteParams->mVelocity);
			break;
		}

		case kMusicDeviceStopNoteSelect:
		{
			// const MusicDeviceGroupID groupID = GET_COMP_PARAM(MusicDeviceGroupID, 2, 3);
			// const NoteInstanceID noteID = GET_COMP_PARAM(NoteInstanceID, 1, 3);
			// const UInt32 offset = GET_COMP_PARAM(UInt32, 0, 3);
			// noteID is supposed to be some incremented unique ID, but we're just storing note number in it.
			// const IMidiMsg msg(offset, IMidiMsg::kNoteOff << 4, noteID, 64);
			break;
		}

		case kComponentCanDoSelect:
		{
			switch (params->params[0])
			{
				case kAudioUnitInitializeSelect:
				case kAudioUnitUninitializeSelect:
				case kAudioUnitGetPropertyInfoSelect:
				case kAudioUnitGetPropertySelect:
				case kAudioUnitSetPropertySelect:
				case kAudioUnitAddPropertyListenerSelect:
				case kAudioUnitRemovePropertyListenerSelect:
				case kAudioUnitGetParameterSelect:
				case kAudioUnitSetParameterSelect:
				case kAudioUnitResetSelect:
				case kAudioUnitRenderSelect:
				case kAudioUnitAddRenderNotifySelect:
				case kAudioUnitRemoveRenderNotifySelect:
				case kAudioUnitScheduleParametersSelect:
					ret = 1; break;
				default:
					ret = 0; break;
			}
			break;
		}

		default: ret = badComponentSelector;
	}

	_this->mMutex.Leave();
	return ret;
}

#ifndef IPLUG_NO_CARBON_SUPPORT

struct AudioUnitCarbonViewCreateGluePB
{
	unsigned char componentFlags;
	unsigned char componentParamSize;
	short componentWhat;
	ControlRef* outControl;
	const Float32Point* inSize;
	const Float32Point* inLocation;
	ControlRef inParentControl;
	WindowRef inWindow;
	AudioUnit inAudioUnit;
	AudioUnitCarbonView inView;
};

struct CarbonViewInstance
{
	ComponentInstance mCI;
	IPlugAU* mPlug;
};

// static
ComponentResult IPlugAU::IPlugAUCarbonViewEntry(ComponentParameters* const params, void* const pView)
{
	const int select = params->what;

	if (select == kComponentOpenSelect)
	{
		CarbonViewInstance* const pCVI = new CarbonViewInstance;
		pCVI->mCI = GET_COMP_PARAM(ComponentInstance, 0, 1);
		pCVI->mPlug = NULL;
		SetComponentInstanceStorage(pCVI->mCI, (Handle)pCVI);
		return noErr;
	}

    CarbonViewInstance* const pCVI = (CarbonViewInstance*)pView;

	switch (select)
	{
		case kComponentCloseSelect:
		{
			IPlugAU* const _this = pCVI->mPlug;
			IGraphics* pGraphics;
			if (_this && (pGraphics = _this->GetGUI()))
			{
				_this->OnGUIClose();
				pGraphics->CloseWindow();
			}
			delete pCVI;
			return noErr;
		}

		case kAudioUnitCarbonViewCreateSelect:
		{
			AudioUnitCarbonViewCreateGluePB* const pb = (AudioUnitCarbonViewCreateGluePB*)params;
			IPlugAU* const _this = (IPlugAU*)GetComponentInstanceStorage(pb->inAudioUnit);
			pCVI->mPlug = _this;
			IGraphicsMac* pGraphics;
			if (_this && (pGraphics = (IGraphicsMac*)_this->GetGUI()))
			{
				*pb->outControl = (ControlRef)pGraphics->OpenCarbonWindow(pb->inWindow, pb->inParentControl);
				_this->OnGUIOpen();
				return noErr;
			}
			return badComponentSelector;
		}
	}

	return badComponentSelector;
}

#endif // IPLUG_NO_CARBON_SUPPORT

#define ASSERT_SCOPE(reqScope) if (scope != reqScope) return kAudioUnitErr_InvalidProperty
#define ASSERT_ELEMENT_NPARAMS if (!NParams(element)) return kAudioUnitErr_InvalidElement
#define ASSERT_INPUT_OR_GLOBAL_SCOPE \
	if (scope != kAudioUnitScope_Input && scope != kAudioUnitScope_Global) \
	{ \
		return kAudioUnitErr_InvalidProperty; \
	}

#define NO_OP(propID) case propID: return kAudioUnitErr_InvalidProperty

// pData == NULL means return property info only.
ComponentResult IPlugAU::GetProperty(const AudioUnitPropertyID propID, const AudioUnitScope scope, const AudioUnitElement element,
	UInt32* const pDataSize, Boolean* const pWriteable, void* const pData)
{
	// Writeable defaults to false, we only need to set it if true.

	switch (propID)
	{
		case kAudioUnitProperty_ClassInfo:                      // 0,
		{
			*pDataSize = sizeof(CFDictionaryRef);
			*pWriteable = true;
			if (pData)
			{
				CFDictionaryRef* const pDict = (CFDictionaryRef*)pData;
				return GetState(pDict);
			}
			return noErr;
		}

		case kAudioUnitProperty_MakeConnection:                 // 1,
		{
			ASSERT_INPUT_OR_GLOBAL_SCOPE;
			*pDataSize = sizeof(AudioUnitConnection);
			*pWriteable = true;
			return noErr;
		}

		case kAudioUnitProperty_SampleRate:                     // 2,
		{
			*pDataSize = sizeof(Float64);
			*pWriteable = true;
			if (pData) *(Float64*)pData = GetSampleRate();
			return noErr;
		}

		case kAudioUnitProperty_ParameterList:                  // 3, listenable
		{
			const int n = scope == kAudioUnitScope_Global ? NParams() : 0;
			*pDataSize = n * sizeof(AudioUnitParameterID);
			if (pData && n)
			{
				AudioUnitParameterID* const pParamID = (AudioUnitParameterID*)pData;
				for (int i = 0; i < n; ++i)
				{
					pParamID[i] = (AudioUnitParameterID)i;
				}
			}
			return noErr;
		}

		case kAudioUnitProperty_ParameterInfo:                  // 4, listenable
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			ASSERT_ELEMENT_NPARAMS;
			*pDataSize = sizeof(AudioUnitParameterInfo);
			if (pData)
			{
				AudioUnitParameterInfo* const pInfo = (AudioUnitParameterInfo*)pData;
				memset(pInfo, 0, sizeof(AudioUnitParameterInfo));

				const IParam* const pParam = GetParam(element);
				const int type = pParam->Type();

				pInfo->flags = kAudioUnitParameterFlag_CFNameRelease |
					kAudioUnitParameterFlag_HasCFNameString |
					kAudioUnitParameterFlag_IsReadable |
					kAudioUnitParameterFlag_IsWritable;

				// Define IPLUG_NO_HIRES_PARAM for legacy plugin that needs
				// high resolution flag disabled.
				#ifndef IPLUG_NO_HIRES_PARAM
				if (type != IParam::kTypeBool)
				{
					// TN: Reportedly without this flag Logic will quantize
					// automation to 1/128 step, but with this flag any
					// automation previously recorded without flag will
					// break.
					pInfo->flags |= kAudioUnitParameterFlag_IsHighResolution;
				}
				#endif

				const char* const paramName = pParam->GetNameForHost();
				pInfo->cfNameString = MakeCFString(pParam->GetNameForHost());
				lstrcpyn_safe(pInfo->name, paramName, sizeof(pInfo->name)); // Max 52.

				switch (type)
				{
					case IParam::kTypeBool:
					{
						const IBoolParam* const pBool = (const IBoolParam*)pParam;
						pInfo->defaultValue = (AudioUnitParameterValue)pBool->Bool();
						// pInfo->minValue = 0.0f;
						pInfo->maxValue = 1.0f;
						pInfo->unit = kAudioUnitParameterUnit_Indexed;
						break;
					}
					case IParam::kTypeInt:
					{
						const IIntParam* const pInt = (const IIntParam*)pParam;
						pInfo->defaultValue = (AudioUnitParameterValue)pInt->Int();
						pInfo->minValue = (AudioUnitParameterValue)pInt->Min();
						pInfo->maxValue = (AudioUnitParameterValue)pInt->Max();
						break;
					}
					case IParam::kTypeEnum:
					{
						const IEnumParam* const pEnum = (const IEnumParam*)pParam;
						pInfo->defaultValue = (AudioUnitParameterValue)pEnum->Int();
						// pInfo->minValue = 0.0f;
						pInfo->maxValue = (AudioUnitParameterValue)(pEnum->NEnums() - 1);
						pInfo->unit = kAudioUnitParameterUnit_Indexed;
						break;
					}
					case IParam::kTypeDouble:
					{
						const IDoubleParam* const pDouble = (const IDoubleParam*)pParam;
						pInfo->defaultValue = (AudioUnitParameterValue)pDouble->Value();
						pInfo->minValue = (AudioUnitParameterValue)pDouble->Min();
						pInfo->maxValue = (AudioUnitParameterValue)pDouble->Max();
						break;
					}
					case IParam::kTypeNormalized:
					{
						const INormalizedParam* const pNormalized = (INormalizedParam*)pParam;
						pInfo->defaultValue = (AudioUnitParameterValue)pNormalized->Value();
						// pInfo->minValue = 0.0f;
						pInfo->maxValue = 1.0f;
						// pInfo->unit = kAudioUnitParameterUnit_Generic;
						break;
					}
					default:
					{
						pInfo->defaultValue = (AudioUnitParameterValue)pParam->GetNormalized();
						// pInfo->minValue = 0.0f;
						pInfo->maxValue = 1.0f;
						// pInfo->unit = kAudioUnitParameterUnit_Generic;
						break;
					}
				}

				if (!pInfo->unit)
				{
					const char* const label = pParam->GetLabelForHost();
					if (label && *label)
					{
						pInfo->unit = kAudioUnitParameterUnit_CustomUnit;
						pInfo->unitName = MakeCFString(label);
					}
					// else
					// {
						// pInfo->unit = kAudioUnitParameterUnit_Generic;
					// }
				}
			}
			return noErr;
		}

		case kAudioUnitProperty_FastDispatch:                   // 5,
		{
			if (mFactory) return kAudioUnitErr_InvalidProperty;
			return GetProc(element, pDataSize, pData);
		}

		NO_OP(kAudioUnitProperty_CPULoad);                      // 6,

		case kAudioUnitProperty_StreamFormat:                   // 8,
		{
			const BusChannels* const pBus = GetBus(scope, element);
			if (!pBus) return kAudioUnitErr_InvalidProperty;
			*pDataSize = sizeof(STREAM_DESC);
			*pWriteable = true;
			if (pData)
			{
				int nChannels = pBus->mNHostChannels; // Report how many channels the host has connected.
				if (nChannels < 0) // Unless the host hasn't connected any yet, in which case report the default.
				{
					nChannels = pBus->mNPlugChannels;
				}
				STREAM_DESC* const pASBD = (STREAM_DESC*)pData;
				MakeDefaultASBD(pASBD, GetSampleRate(), nChannels, false);
			}
			return noErr;
		}

		case kAudioUnitProperty_ElementCount:                   // 11,
		{
			*pDataSize = sizeof(UInt32);
			if (pData)
			{
				int n = 0;
				switch (scope)
				{
					case kAudioUnitScope_Global: n = 1; break;
					case kAudioUnitScope_Input: n = mInBuses.GetSize(); break;
					case kAudioUnitScope_Output: n = mOutBuses.GetSize(); break;
				}
				*(UInt32*)pData = n;
			}
			return noErr;
		}

		case kAudioUnitProperty_Latency:                        // 12, listenable
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			*pDataSize = sizeof(Float64);
			if (pData)
			{
				*(Float64*)pData = (double)GetLatency() / GetSampleRate();
			}
			return noErr;
		}

		case kAudioUnitProperty_SupportedNumChannels:           // 13,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			const int n = mChannelIO.GetSize();
			*pDataSize = n * sizeof(AUChannelInfo);
			if (pData)
			{
				AUChannelInfo* const pChInfo = (AUChannelInfo*)pData;
				const ChannelIO* const pIO = mChannelIO.Get();
				for (int i = 0; i < n; ++i)
				{
					pChInfo[i].inChannels = pIO[i].mIn;
					pChInfo[i].outChannels = pIO[i].mOut;
				}
			}
			return noErr;
		}

		case kAudioUnitProperty_MaximumFramesPerSlice:          // 14,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			*pDataSize = sizeof(UInt32);
			*pWriteable = true;
			if (pData) *(UInt32*)pData = GetBlockSize();
			return noErr;
		}

		NO_OP(kAudioUnitProperty_SetExternalBuffer);            // 15,

		case kAudioUnitProperty_ParameterValueStrings:          // 16,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			ASSERT_ELEMENT_NPARAMS;
			IParam* const pParam = GetParam(element);
			const int type = pParam->Type(), n = pParam->GetNDisplayTexts();
			if (!(n && (type == IParam::kTypeEnum || type == IParam::kTypeBool)))
			{
				*pDataSize = 0;
				return kAudioUnitErr_InvalidProperty;
			}
			*pDataSize = sizeof(CFArrayRef);
			if (pData)
			{
				CFMutableArrayRef nameArray = CFArrayCreateMutable(kCFAllocatorDefault, n, &kCFTypeArrayCallBacks);
				for (int i = 0; i < n; ++i)
				{
					const CFStrLocal cfStr(type == IParam::kTypeEnum ?
						((IEnumParam*)pParam)->GetDisplayText(i) :
						((IBoolParam*)pParam)->GetDisplayText(i));
					CFArrayAppendValue(nameArray, cfStr.mCFStr);
				}
				*(CFArrayRef*)pData = nameArray;
			}
			return noErr;
		}

		case kAudioUnitProperty_GetUIComponentList:             // 18,
		{
			#ifndef IPLUG_NO_CARBON_SUPPORT
			if (GetGUI())
			{
				*pDataSize = sizeof(ComponentDescription);
				if (pData)
				{
					ComponentDescription* const pDesc = (ComponentDescription*)pData;
					pDesc->componentType = kAudioUnitCarbonViewComponentType;
					pDesc->componentSubType = GetUniqueID();
					pDesc->componentManufacturer = GetMfrID();
					pDesc->componentFlags = 0;
					pDesc->componentFlagsMask = 0;
				}
				return noErr;
			}
			#endif
			return kAudioUnitErr_InvalidProperty;
		}

		NO_OP(kAudioUnitProperty_AudioChannelLayout);           // 19,

		case kAudioUnitProperty_TailTime:                       // 20, listenable
		{
			return GetProperty(kAudioUnitProperty_Latency, scope, element, pDataSize, pWriteable, pData);
		}

		case kAudioUnitProperty_BypassEffect:                   // 21,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			*pWriteable = true;
			*pDataSize = sizeof(UInt32);
			if (pData) *(UInt32*)pData = IsBypassed();
			return noErr;
		}

		case kAudioUnitProperty_LastRenderError:                // 22,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			*pDataSize = sizeof(OSStatus);
			if (pData) *(OSStatus*)pData = noErr;
			return noErr;
		}

		case kAudioUnitProperty_SetRenderCallback:              // 23,
		{
			ASSERT_INPUT_OR_GLOBAL_SCOPE;
			if (element >= mInBuses.GetSize())
			{
				return kAudioUnitErr_InvalidProperty;
			}
			*pDataSize = sizeof(AURenderCallbackStruct);
			*pWriteable = true;
			return noErr;
		}

		case kAudioUnitProperty_FactoryPresets:                 // 24, listenable
		{
			*pDataSize = sizeof(CFArrayRef);
			if (pData)
			{
				const int n = NPresets();
				CFMutableArrayRef const presetArray = CFArrayCreateMutable(kCFAllocatorDefault, n, &kCFAUPresetArrayCallBacks);
				for (int i = 0; i < n; ++i)
				{
					const CFStrLocal presetName(GetPresetName(i));
					CFAUPresetRef const newPreset = CFAUPresetCreate(kCFAllocatorDefault, i, presetName.mCFStr);
					CFArrayAppendValue(presetArray, newPreset);
					CFAUPresetRelease(newPreset);
				}
				*(CFMutableArrayRef*)pData = presetArray;
			}
			return noErr;
		}

		NO_OP(kAudioUnitProperty_ContextName);                  // 25,
		NO_OP(kAudioUnitProperty_RenderQuality);                // 26,

		case kAudioUnitProperty_HostCallbacks:                  // 27,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			*pDataSize = sizeof(HostCallbackInfo);
			*pWriteable = true;
			return noErr;
		}

		NO_OP(kAudioUnitProperty_InPlaceProcessing);            // 29,
		NO_OP(kAudioUnitProperty_ElementName);                  // 30,

		case kAudioUnitProperty_CocoaUI:                        // 31,
		{
			if (GetGUI()
			#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_5
			&& IGraphicsMac::GetUserFoundationVersion() >= 677.00 // OS X v10.5
			#endif
			)
			{
				*pDataSize = sizeof(AudioUnitCocoaViewInfo); // Just one view.
				if (pData)
				{
					AudioUnitCocoaViewInfo* const pViewInfo = (AudioUnitCocoaViewInfo*)pData;
					const CFStrLocal bundleID(mOSXBundleID.Get());
					CFBundleRef const pBundle = CFBundleGetBundleWithIdentifier(bundleID.mCFStr);
					CFURLRef const url = CFBundleCopyBundleURL(pBundle);
					pViewInfo->mCocoaAUViewBundleLocation = url;
					pViewInfo->mCocoaAUViewClass[0] = MakeCFString(mCocoaViewFactoryClassName.Get());
				}
				return noErr;
			}
			return kAudioUnitErr_InvalidProperty;
		}

		NO_OP(kAudioUnitProperty_SupportedChannelLayoutTags);   // 32,

		case kAudioUnitProperty_ParameterIDName:                // 34,
		{
			*pDataSize = sizeof(AudioUnitParameterIDName);
			if (pData && scope == kAudioUnitScope_Global)
			{
				AudioUnitParameterIDName* const pIDName = (AudioUnitParameterIDName*)pData;
				const IParam* const pParam = GetParam(pIDName->inID);
				char cStr[128];
				const int len = pIDName->inDesiredLength;
				lstrcpyn_safe(cStr, pParam->GetNameForHost(len + 1), sizeof(cStr));
				if (len != kAudioUnitParameterName_Full)
				{
					const int n = wdl_min(sizeof(cStr) - 1, len);
					cStr[n] = '\0';
				}
				pIDName->outName = MakeCFString(cStr);
			}
			return noErr;
		}

		NO_OP(kAudioUnitProperty_ParameterClumpName);           // 35,

		case kAudioUnitProperty_CurrentPreset:                  // 28,
		case kAudioUnitProperty_PresentPreset:                  // 36, listenable
		{
			*pDataSize = sizeof(AUPreset);
			*pWriteable = true;
			if (pData)
			{
				AUPreset* const pAUPreset = (AUPreset*)pData;
				pAUPreset->presetNumber = GetCurrentPresetIdx();
				const char* name = GetPresetName(pAUPreset->presetNumber);
				pAUPreset->presetName = MakeCFString(name);
			}
			return noErr;
		}

		NO_OP(kAudioUnitProperty_OfflineRender);                // 37,

		case kAudioUnitProperty_ParameterStringFromValue:       // 33,
		{
			*pDataSize = sizeof(AudioUnitParameterStringFromValue);
			if (pData && scope == kAudioUnitScope_Global)
			{
				AudioUnitParameterStringFromValue* const pSFV = (AudioUnitParameterStringFromValue*)pData;
				IParam* const pParam = GetParam(pSFV->inParamID);
				const double v = pSFV->inValue ? pParam->GetNormalized(*pSFV->inValue) : pParam->GetNormalized();
				char str[128];
				pParam->GetDisplayForHost(v, str, sizeof(str));
				pSFV->outString = MakeCFString(str);
			}
			return noErr;
		}

		case kAudioUnitProperty_ParameterValueFromString:       // 38,
		{
			*pDataSize = sizeof(AudioUnitParameterValueFromString);
			if (pData)
			{
				AudioUnitParameterValueFromString* const pVFS = (AudioUnitParameterValueFromString*)pData;
				if (scope == kAudioUnitScope_Global)
				{
					const CStrLocal cStr(pVFS->inString);
					const IParam* const pParam = GetParam(pVFS->inParamID);
					double v;
					const bool mapped = pParam->MapDisplayText(cStr.mCStr, &v);
					if (!mapped)
					{
						v = strtod(cStr.mCStr, NULL);
						if (pParam->DisplayIsNegated()) v = -v;
						v = pParam->GetNormalized(v);
					}
					pVFS->outValue = (AudioUnitParameterValue)v;
				}
			}
			return noErr;
		}

		NO_OP(kAudioUnitProperty_IconLocation);                 // 39,
		NO_OP(kAudioUnitProperty_PresentationLatency);          // 40,
		NO_OP(kAudioUnitProperty_DependentParameters);          // 45,

		#if MAC_OS_X_VERSION_MAX_ALLOWED > MAC_OS_X_VERSION_10_4

		NO_OP(kAudioUnitProperty_AUHostIdentifier);             // 46,

/*		case kAudioUnitProperty_MIDIOutputCallbackInfo:         // 47,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			if (!DoesMIDI(kPlugDoesMidiOut))
			{
				return kAudioUnitErr_InvalidProperty;
			}
			*pDataSize = sizeof(CFArrayRef);
			if (pData)
			{
				CFMutableArrayRef nameArray = CFArrayCreateMutable(kCFAllocatorDefault, 1, &kCFTypeArrayCallBacks);
				const CFStrLocal cfStr("MIDI Out");
				CFArrayAppendValue(nameArray, cfStr.mCFStr);
				*(CFArrayRef*)pData = nameArray;
			}
			return noErr;
		}

		case kAudioUnitProperty_MIDIOutputCallback:             // 48
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			*pDataSize = sizeof(AUMIDIOutputCallbackStruct);
			*pWriteable = true;
			return noErr;
		}
*/
		NO_OP(kAudioUnitProperty_MIDIOutputCallbackInfo);       // 47,
		NO_OP(kAudioUnitProperty_MIDIOutputCallback);           // 48,

		NO_OP(kAudioUnitProperty_InputSamplesInOutput);         // 49,
		NO_OP(kAudioUnitProperty_ClassInfoFromDocument);        // 50,

		#endif

		case kMusicDeviceProperty_InstrumentCount:              // 1000,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			if (IsInst())
			{
				*pDataSize = sizeof(UInt32);
				if (pData) *(UInt32*)pData = /* IsBypassed() */ 1;
				return noErr;
			}
			return kAudioUnitErr_InvalidProperty;
		}

		case IGraphicsMac::kAudioUnitProperty_PlugInObject:     // 0x1a45ffe9
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			*pDataSize = 2 * sizeof(void*);
			if (pData)
			{
				void* const ptrs[2] = { this, NULL };
				memcpy(pData, ptrs, sizeof(ptrs));
			}
			return noErr;
		}
	}

	return kAudioUnitErr_InvalidProperty;
}

ComponentResult IPlugAU::SetProperty(const AudioUnitPropertyID propID, const AudioUnitScope scope, const AudioUnitElement element,
	UInt32* /* pDataSize */, const void* const pData)
{
	InformListeners(propID, scope);

	switch (propID)
	{
		case kAudioUnitProperty_ClassInfo:                      // 0,
		{
			return SetState(*(CFDictionaryRef*)pData);
		}

		case kAudioUnitProperty_MakeConnection:                 // 1,
		{
			ASSERT_INPUT_OR_GLOBAL_SCOPE;
			const AudioUnitConnection* const pAUC = (AudioUnitConnection*)pData;
			if (pAUC->destInputNumber >= mInBusConnections.GetSize())
			{
				return kAudioUnitErr_InvalidProperty;
			}
			InputBusConnection* const pInBusConn = mInBusConnections.Get(pAUC->destInputNumber);
			memset(pInBusConn, 0, sizeof(InputBusConnection));
			bool negotiatedOK = true;
			if (pAUC->sourceAudioUnit) // Opening connection.
			{
				AudioStreamBasicDescription srcASBD;
				UInt32 size = sizeof(AudioStreamBasicDescription);
				negotiatedOK = // Ask whoever is sending us audio what the format is.
					AudioUnitGetProperty(pAUC->sourceAudioUnit, kAudioUnitProperty_StreamFormat,
						kAudioUnitScope_Output, pAUC->sourceOutputNumber, &srcASBD, &size) == noErr;
				negotiatedOK &= // Try to set our own format to match.
					SetProperty(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
						pAUC->destInputNumber, &size, &srcASBD) == noErr;
				if (negotiatedOK) // Connection terms successfully negotiated.
				{
					pInBusConn->mUpstreamUnit = pAUC->sourceAudioUnit;
					pInBusConn->mUpstreamBusIdx = pAUC->sourceOutputNumber;
					// Will the upstream unit give us a fast render proc for input?
					AudioUnitRenderProc srcRenderProc;
					size = sizeof(AudioUnitRenderProc);
					if (AudioUnitGetProperty(pAUC->sourceAudioUnit, kAudioUnitProperty_FastDispatch, kAudioUnitScope_Global, kAudioUnitRenderSelect,
						&srcRenderProc, &size) == noErr)
					{
						// Yes, we got a fast render proc, and we also need to store the pointer to the upstream audio unit object.
						pInBusConn->mUpstreamRenderProc = srcRenderProc;
						pInBusConn->mUpstreamObj = GetComponentInstanceStorage(pAUC->sourceAudioUnit);
					}
					// Else no fast render proc, so leave the input bus connection struct's upstream render proc and upstream object empty,
					// and we will need to make a component call through the component manager to get input data.
				}
				// Else this is a call to close the connection, which we effectively did by clearing the InputBusConnection struct,
				// which counts as a successful negotiation.
			}
			AssessInputConnections();
			return negotiatedOK ? noErr : (ComponentResult)kAudioUnitErr_InvalidProperty;
		}

		case kAudioUnitProperty_SampleRate:                     // 2,
		{
			UpdateSampleRate(*(Float64*)pData);
			return noErr;
		}

		NO_OP(kAudioUnitProperty_ParameterList);                // 3,
		NO_OP(kAudioUnitProperty_ParameterInfo);                // 4,
		NO_OP(kAudioUnitProperty_FastDispatch);                 // 5,
		NO_OP(kAudioUnitProperty_CPULoad);                      // 6,

		case kAudioUnitProperty_StreamFormat:                   // 8,
		{
			AudioStreamBasicDescription* pASBD = (AudioStreamBasicDescription*)pData;
			const int nHostChannels = pASBD->mChannelsPerFrame;
			BusChannels* const pBus = GetBus(scope, element);
			if (!pBus) return kAudioUnitErr_InvalidProperty;
			// The connection is OK if the plugin expects the same number of channels as the host is attempting to connect,
			// or if the plugin supports mono channels (meaning it's flexible about how many inputs to expect)
			// and the plugin supports at least as many channels as the host is attempting to connect.
			bool connectionOK = nHostChannels > 0 && nHostChannels <= pBus->mNPlugChannels;
			connectionOK &= pASBD->mFormatID == kAudioFormatLinearPCM && (pASBD->mFormatFlags & kAudioFormatFlagsCanonical);

			// const bool interleaved = !(pASBD->mFormatFlags & kAudioFormatFlagIsNonInterleaved);
			if (connectionOK)
			{
				pBus->mNHostChannels = nHostChannels;
				if (pASBD->mSampleRate > 0.0)
				{
					UpdateSampleRate(pASBD->mSampleRate);
				}
				AssessInputConnections();
				return noErr;
			}
			return (ComponentResult)kAudioUnitErr_InvalidProperty;
		}

		NO_OP(kAudioUnitProperty_ElementCount);                 // 11,
		NO_OP(kAudioUnitProperty_Latency);                      // 12,
		NO_OP(kAudioUnitProperty_SupportedNumChannels);         // 13,

		case kAudioUnitProperty_MaximumFramesPerSlice:          // 14,
		{
			UpdateBlockSize(*(UInt32*)pData);
			return noErr;
		}

		NO_OP(kAudioUnitProperty_SetExternalBuffer);            // 15,
		NO_OP(kAudioUnitProperty_ParameterValueStrings);        // 16,
		NO_OP(kAudioUnitProperty_GetUIComponentList);           // 18,
		NO_OP(kAudioUnitProperty_AudioChannelLayout);           // 19,
		NO_OP(kAudioUnitProperty_TailTime);                     // 20,

		case kAudioUnitProperty_BypassEffect:                   // 21,
		{
			// TN: GarageBand doesn't seem to set this propery, but instead
			// seems to call IPlugAUEntry(kAudioUnitResetSelect).
			const bool bypass = !!*(UInt32*)pData;
			if (IsBypassed() != bypass)
			{
				mPlugFlags ^= kPlugFlagsBypass;
				OnBypass(bypass);
			}
			return noErr;
		}

		NO_OP(kAudioUnitProperty_LastRenderError);             // 22,

		case kAudioUnitProperty_SetRenderCallback:             // 23,
		{
			ASSERT_SCOPE(kAudioUnitScope_Input); // If global scope, set all.
			if (element >= mInBusConnections.GetSize())
			{
				return kAudioUnitErr_InvalidProperty;
			}
			InputBusConnection* const pInBusConn = mInBusConnections.Get(element);
			memset(pInBusConn, 0, sizeof(InputBusConnection));
			const AURenderCallbackStruct* const pCS = (AURenderCallbackStruct*)pData;
			if (pCS->inputProc != NULL)
			{
				pInBusConn->mUpstreamRenderCallback = *pCS;
			}
			AssessInputConnections();
			return noErr;
		}

		NO_OP(kAudioUnitProperty_FactoryPresets);               // 24,
		NO_OP(kAudioUnitProperty_ContextName);                  // 25,
		NO_OP(kAudioUnitProperty_RenderQuality);                // 26,

		case kAudioUnitProperty_HostCallbacks:                  // 27,
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			memcpy(&mHostCallbacks, pData, sizeof(HostCallbackInfo));
			return noErr;
		}

		NO_OP(kAudioUnitProperty_InPlaceProcessing);            // 29,
		NO_OP(kAudioUnitProperty_ElementName);                  // 30,
		NO_OP(kAudioUnitProperty_CocoaUI);                      // 31,
		NO_OP(kAudioUnitProperty_SupportedChannelLayoutTags);   // 32,
		NO_OP(kAudioUnitProperty_ParameterIDName);              // 34,
		NO_OP(kAudioUnitProperty_ParameterClumpName);           // 35,

		case kAudioUnitProperty_CurrentPreset:                  // 28,
		case kAudioUnitProperty_PresentPreset:                  // 36,
		{
			const int presetIdx = ((AUPreset*)pData)->presetNumber;
			RestorePreset(presetIdx);
			return noErr;
		}

		case kAudioUnitProperty_OfflineRender:                  // 37,
		{
			const bool offline = !!*(UInt32*)pData;
			if (IsOffline() != offline) mPlugFlags ^= kPlugFlagsOffline;
			return noErr;
		}

		NO_OP(kAudioUnitProperty_ParameterStringFromValue);     // 33,
		NO_OP(kAudioUnitProperty_ParameterValueFromString);     // 38,
		NO_OP(kAudioUnitProperty_IconLocation);                 // 39,
		NO_OP(kAudioUnitProperty_PresentationLatency);          // 40,
		NO_OP(kAudioUnitProperty_DependentParameters);          // 45,

		#if MAC_OS_X_VERSION_MAX_ALLOWED > MAC_OS_X_VERSION_10_4

		case kAudioUnitProperty_AUHostIdentifier:               // 46,
		{
			const AUHostIdentifier* const pHostID = (AUHostIdentifier*)pData;
			CStrLocal hostStr(pHostID->hostName);
			const int hostVer = (pHostID->hostVersion.majorRev << 16) | (pHostID->hostVersion.minorAndBugRev << 8);
			SetHost(hostStr.mCStr, hostVer);
			return noErr;
		}

		NO_OP(kAudioUnitProperty_MIDIOutputCallbackInfo);       // 47,
		NO_OP(kAudioUnitProperty_MIDIOutputCallback);           // 48,

/*		case kAudioUnitProperty_MIDIOutputCallback:             // 48
		{
			ASSERT_SCOPE(kAudioUnitScope_Global);
			memcpy(&mMidiCallback, pData, sizeof(AUMIDIOutputCallbackStruct));
			return noErr;
		}
*/
		NO_OP(kAudioUnitProperty_InputSamplesInOutput);         // 49,
		NO_OP(kAudioUnitProperty_ClassInfoFromDocument);        // 50

		#endif
	}

	return kAudioUnitErr_InvalidProperty;
}

#undef NO_OP

/* static const char* AUInputTypeStr(int type)
{
	static const char* const tbl[] =
	{
		"NotConnected",
		"DirectFastProc",
		"DirectNoFastProc",
		"RenderCallback"
	};
	return tbl[(unsigned int)type >= sizeof(tbl) / sizeof(tbl[0]) ? 0 : type] ;
} */

int IPlugAU::NHostChannelsConnected(const WDL_PtrList<BusChannels>* const pBuses)
{
	int nCh = -1;
	const int n = pBuses->GetSize();
	for (int i = 0; i < n; ++i)
	{
		const int nHostChannels = pBuses->Get(i)->mNHostChannels;
		if (nHostChannels >= 0) nCh = wdl_max(nCh, 0) + nHostChannels;
	}
	return nCh;
}

bool IPlugAU::CheckLegalIO() const
{
	const int nIn = NHostChannelsConnected(&mInBuses);
	const int nOut = NHostChannelsConnected(&mOutBuses);
	return (!nIn && !nOut) || LegalIO(nIn, nOut);
}

void IPlugAU::AssessInputConnections()
{
	mMutex.Enter();

	SetInputChannelConnections(0, NInChannels(), false);

	const int nIn = mInBuses.GetSize();
	for (int i = 0; i < nIn; ++i)
	{
		BusChannels* const pInBus = mInBuses.Get(i);
		InputBusConnection* const pInBusConn = mInBusConnections.Get(i);

		// AU supports 3 ways to get input from the host (or whoever is upstream).
		if (pInBusConn->mUpstreamRenderProc && pInBusConn->mUpstreamObj)
		{
			// 1: Direct input connection with fast render proc (and buffers) supplied by the upstream unit.
			pInBusConn->mInputType = eDirectFastProc;
		}
		else if (pInBusConn->mUpstreamUnit)
		{
			// 2: Direct input connection with no render proc, buffers supplied by the upstream unit.
			pInBusConn->mInputType = eDirectNoFastProc;
		}
		else if (pInBusConn->mUpstreamRenderCallback.inputProc)
		{
			// 3: No direct connection, render callback, buffers supplied by us.
			pInBusConn->mInputType = eRenderCallback;
		}
		else
		{
			pInBusConn->mInputType = eNotConnected;
		}
		pInBus->mConnected = pInBusConn->mInputType != eNotConnected;

		const int startChannelIdx = pInBus->mPlugChannelStartIdx;
		if (pInBus->mConnected)
		{
			// There's an input connection, so we need to tell the plug to expect however many channels
			// are in the negotiated host stream format.
			if (pInBus->mNHostChannels < 0)
			{
				// The host set up a connection without specifying how many channels in the stream.
				// Assume the host will send all the channels the plugin asks for, and hope for the best.
				pInBus->mNHostChannels = pInBus->mNPlugChannels;
			}
			const int nConnected = pInBus->mNHostChannels;
			int nUnconnected = pInBus->mNPlugChannels - nConnected;
			nUnconnected = wdl_max(nUnconnected, 0);
			SetInputChannelConnections(startChannelIdx, nConnected, true);
			SetInputChannelConnections(startChannelIdx + nConnected, nUnconnected, false);
		}
	}

	mMutex.Leave();
}

void IPlugAU::UpdateSampleRate(const double sampleRate)
{
	const int flags = mPlugFlags;
	if (sampleRate != GetSampleRate() || !(flags & kPlugInitSampleRate))
	{
		SetSampleRate(sampleRate);
		mPlugFlags = flags | kPlugInitSampleRate;
		if (flags & kPlugInitBlockSize) Reset();
	}
}

void IPlugAU::UpdateBlockSize(const int blockSize)
{
	const int flags = mPlugFlags;
	if (blockSize != GetBlockSize() || !(flags & kPlugInitBlockSize))
	{
		SetBlockSize(blockSize);
		mPlugFlags = flags | kPlugInitBlockSize;
		if (flags & kPlugInitSampleRate) Reset();
	}
}

static void PutNumberInDict(CFMutableDictionaryRef const pDict, const char* const key, const void* const pNumber, const CFNumberType type)
{
	const CFStrLocal cfKey(key);
	CFNumberRef const pValue = CFNumberCreate(NULL, type, pNumber);
	CFDictionarySetValue(pDict, cfKey.mCFStr, pValue);
	CFRelease(pValue);
}

static void PutStrInDict(CFMutableDictionaryRef const pDict, const char* const key, const char* const value)
{
	const CFStrLocal cfKey(key);
	const CFStrLocal cfValue(value);
	CFDictionarySetValue(pDict, cfKey.mCFStr, cfValue.mCFStr);
}

static void PutDataInDict(CFMutableDictionaryRef const pDict, const char* const key, const ByteChunk* const pChunk)
{
	const CFStrLocal cfKey(key);
	CFDataRef const pData = CFDataCreate(NULL, (const UInt8*)pChunk->GetBytes(), pChunk->Size());
	CFDictionarySetValue(pDict, cfKey.mCFStr, pData);
	CFRelease(pData);
}

static bool GetNumberFromDict(CFDictionaryRef const pDict, const char* const key, void* const pNumber, const CFNumberType type)
{
	const CFStrLocal cfKey(key);
	CFNumberRef const pValue = (CFNumberRef)CFDictionaryGetValue(pDict, cfKey.mCFStr);
	if (pValue)
	{
		CFNumberGetValue(pValue, type, pNumber);
		return true;
	}
	return false;
}

static bool GetStrFromDict(CFDictionaryRef const pDict, const char* const key, char* const buf, const int bufSize = 128)
{
	assert(bufSize > 0);
	const CFStrLocal cfKey(key);
	CFStringRef const pValue = (CFStringRef)CFDictionaryGetValue(pDict, cfKey.mCFStr);
	if (pValue)
	{
		const CStrLocal cStr(pValue);
		lstrcpyn_safe(buf, cStr.mCStr, bufSize);
		return true;
	}
	*buf = 0;
	return false;
}

static bool GetDataFromDict(CFDictionaryRef const pDict, const char* const key, ByteChunk* const pChunk)
{
	const CFStrLocal cfKey(key);
	CFDataRef const pData = (CFDataRef)CFDictionaryGetValue(pDict, cfKey.mCFStr);
	if (pData)
	{
		const int n = CFDataGetLength(pData);
		if (pChunk->Size() != n)
		{
			pChunk->Resize(n);
			if (pChunk->Size() != n) return false;
		}
		memcpy(pChunk->GetBytes(), CFDataGetBytePtr(pData), n);
		return true;
	}
	return false;
}

ComponentResult IPlugAU::GetState(CFDictionaryRef* const ppDict)
{
	const int subtype = GetUniqueID(), mfr = GetMfrID();
	const int version = GetEffectVersion(false);

	CFMutableDictionaryRef const pDict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	PutNumberInDict(pDict, kAUPresetVersionKey, &version, kCFNumberSInt32Type);
	PutNumberInDict(pDict, kAUPresetTypeKey, &mComponentType, kCFNumberSInt32Type);
	PutNumberInDict(pDict, kAUPresetSubtypeKey, &subtype, kCFNumberSInt32Type);
	PutNumberInDict(pDict, kAUPresetManufacturerKey, &mfr, kCFNumberSInt32Type);
	PutStrInDict(pDict, kAUPresetNameKey, GetPresetName(GetCurrentPresetIdx()));

	if (!mState.AllocSize()) AllocStateChunk();
	mState.Clear();

	#ifdef IPLUG_NO_STATE_CHUNKS
	if (SerializeParams(0, NParams(), &mState))
	#else
	if (SerializeState(&mState))
	#endif
	{
		PutDataInDict(pDict, kAUPresetDataKey, &mState);
	}

	*ppDict = pDict;
	return noErr;
}

ComponentResult IPlugAU::SetState(CFDictionaryRef const pDict)
{
	int version, type, subtype, mfr;
	char presetName[64];
	if (!GetNumberFromDict(pDict, kAUPresetVersionKey, &version, kCFNumberSInt32Type) ||
		!GetNumberFromDict(pDict, kAUPresetTypeKey, &type, kCFNumberSInt32Type) ||
		!GetNumberFromDict(pDict, kAUPresetSubtypeKey, &subtype, kCFNumberSInt32Type) ||
		!GetNumberFromDict(pDict, kAUPresetManufacturerKey, &mfr, kCFNumberSInt32Type) ||
		!GetStrFromDict(pDict, kAUPresetNameKey, presetName, sizeof(presetName)) ||
		// version != GetEffectVersion(false) ||
		type != mComponentType ||
		subtype != GetUniqueID() ||
		mfr != GetMfrID())
	{
		return kAudioUnitErr_InvalidPropertyValue;
	}
	RestorePreset(presetName);

	if (!GetDataFromDict(pDict, kAUPresetDataKey, &mState))
	{
		return kAudioUnitErr_InvalidPropertyValue;
	}

	const int pos =
	#ifdef IPLUG_NO_STATE_CHUNKS
	UnserializeParams(0, NParams(), &mState, 0);
	#else
	UnserializeState(&mState, 0);
	#endif

	OnParamReset();
	if (pos < 0) return kAudioUnitErr_InvalidPropertyValue;

	RedrawParamControls();
	return noErr;
}

// pData == NULL means return property info only.
ComponentResult IPlugAU::GetProc(const AudioUnitElement element, UInt32* const pDataSize, void* const pData) const
{
	switch (element)
	{
		case kAudioUnitGetParameterSelect:
		{
			*pDataSize = sizeof(AudioUnitGetParameterProc);
			if (pData)
			{
				*(AudioUnitGetParameterProc*)pData = (AudioUnitGetParameterProc)IPlugAU::GetParamProc;
			}
			return noErr;
		}

		case kAudioUnitSetParameterSelect:
		{
			*pDataSize = sizeof(AudioUnitSetParameterProc);
			if (pData)
			{
				*(AudioUnitSetParameterProc*)pData = (AudioUnitSetParameterProc)IPlugAU::SetParamProc;
			}
			return noErr;
		}

		case kAudioUnitRenderSelect:
		{
			*pDataSize = sizeof(AudioUnitRenderProc);
			if (pData)
			{
				*(AudioUnitRenderProc*)pData = (AudioUnitRenderProc)IPlugAU::RenderProc;
			}
			return noErr;
		}
	}

	return kAudioUnitErr_InvalidElement;
}

// static
ComponentResult IPlugAU::GetParamProc(void* const pPlug, const AudioUnitParameterID paramID, const AudioUnitScope scope, const AudioUnitElement element,
	AudioUnitParameterValue* const pValue)
{
	ASSERT_SCOPE(kAudioUnitScope_Global);
	IPlugAU* const _this = (IPlugAU*)pPlug;
	_this->mMutex.Enter();

	const IParam* const pParam = _this->GetParam(paramID);
	AudioUnitParameterValue v;

	switch (pParam->Type())
	{
		case IParam::kTypeBool:
			v = (AudioUnitParameterValue)((IBoolParam*)pParam)->Bool();
			break;
		case IParam::kTypeInt:
			v = (AudioUnitParameterValue)((IIntParam*)pParam)->Int();
			break;
		case IParam::kTypeEnum:
			v = (AudioUnitParameterValue)((IEnumParam*)pParam)->Int();
			break;
		case IParam::kTypeDouble:
			v = (AudioUnitParameterValue)((IDoubleParam*)pParam)->Value();
			break;
		case IParam::kTypeNormalized:
			v = (AudioUnitParameterValue)((INormalizedParam*)pParam)->Value();
			break;
		default:
			v = (AudioUnitParameterValue)pParam->GetNormalized();
			break;
	}
	*pValue = v;

	_this->mMutex.Leave();
	return noErr;
}

// static
ComponentResult IPlugAU::SetParamProc(void* const pPlug, const AudioUnitParameterID paramID, const AudioUnitScope scope, const AudioUnitElement element,
	const AudioUnitParameterValue value, UInt32 /* offsetFrames */)
{
	// In the SDK, offset frames is only looked at in group scope.
	ASSERT_SCOPE(kAudioUnitScope_Global);
	IPlugAU* const _this = (IPlugAU*)pPlug;
	_this->mMutex.Enter();

	// TN: Why is order in IPlugVST2::VSTSetParameter() different?
	IParam* const pParam = _this->GetParam(paramID);
	const double v = pParam->GetNormalized(value);
	pParam->SetNormalized(v);

	IGraphics* const pGraphics = _this->GetGUI();
	if (pGraphics) pGraphics->SetParameterFromPlug(paramID, v, true);
	_this->OnParamChange(paramID);

	_this->mMutex.Leave();
	return noErr;
}

static ComponentResult RenderCallback(const AURenderCallbackStruct* const pCB, AudioUnitRenderActionFlags* const pFlags, const AudioTimeStamp* const pTimestamp,
	const UInt32 inputBusIdx, const UInt32 nFrames, AudioBufferList* const pOutBufList)
{
	return pCB->inputProc(pCB->inputProcRefCon, pFlags, pTimestamp, inputBusIdx, nFrames, pOutBufList);
}

// static
ComponentResult IPlugAU::RenderProc(void* const pPlug, AudioUnitRenderActionFlags* /* pFlags */, const AudioTimeStamp* const pTimestamp,
	const UInt32 outputBusIdx, const UInt32 nFrames, AudioBufferList* const pOutBufList)
{
	IPlugAU* const _this = (IPlugAU*)pPlug;

	if (!(pTimestamp->mFlags & kAudioTimeStampSampleTimeValid) ||
		outputBusIdx >= _this->mOutBuses.GetSize() ||
		nFrames > _this->GetBlockSize())
	{
		return kAudioUnitErr_InvalidPropertyValue;
	}

	const int nRenderNotify = _this->mRenderNotify.GetSize();
	for (int i = 0; i < nRenderNotify; ++i)
	{
		const AURenderCallbackStruct* const pRN = _this->mRenderNotify.Get(i);
		AudioUnitRenderActionFlags flags = kAudioUnitRenderAction_PreRender;
		RenderCallback(pRN, &flags, pTimestamp, outputBusIdx, nFrames, pOutBufList);
	}

	const double renderTimestamp = pTimestamp->mSampleTime;
	if (renderTimestamp != _this->mRenderTimestamp) // Pull input buffers.
	{
		AudioBufferList* const pInBufList = (AudioBufferList*)_this->mBufList.Get();

		const int nIn = _this->mInBuses.GetSize();
		for (int i = 0; i < nIn; ++i)
		{
			const BusChannels* const pInBus = _this->mInBuses.Get(i);
			const InputBusConnection* const pInBusConn = _this->mInBusConnections.Get(i);

			if (pInBus->mConnected)
			{
				pInBufList->mNumberBuffers = pInBus->mNHostChannels;
				for (int b = 0; b < pInBufList->mNumberBuffers; ++b)
				{
					AudioBuffer* const pBuffer = &pInBufList->mBuffers[b];
					pBuffer->mNumberChannels = 1;
					pBuffer->mDataByteSize = nFrames * sizeof(AudioSampleType);
					pBuffer->mData = NULL;
				}

				AudioUnitRenderActionFlags flags = 0;
				ComponentResult r;
				switch (pInBusConn->mInputType)
				{
					case eDirectFastProc:
					{
						r = pInBusConn->mUpstreamRenderProc(pInBusConn->mUpstreamObj, &flags, pTimestamp, pInBusConn->mUpstreamBusIdx, nFrames, pInBufList);
						break;
					}
					case eDirectNoFastProc:
					{
						r = AudioUnitRender(pInBusConn->mUpstreamUnit, &flags, pTimestamp, pInBusConn->mUpstreamBusIdx, nFrames, pInBufList);
						break;
					}
					case eRenderCallback:
					{
						AudioSampleType* pScratchInput = _this->mInScratchBuf.Get() + pInBus->mPlugChannelStartIdx * nFrames;
						for (int b = 0; b < pInBufList->mNumberBuffers; ++b, pScratchInput += nFrames)
						{
							pInBufList->mBuffers[b].mData = pScratchInput;
						}
						r = RenderCallback(&pInBusConn->mUpstreamRenderCallback, &flags, pTimestamp, i /* 0 */, nFrames, pInBufList);
						break;
					}
					default:
					{
						static const bool inputBusAssessed = false;
						assert(inputBusAssessed); // InputBus.mConnected should be false, we didn't correctly assess the input connections.
						r = kAudioUnitErr_NoConnection;
					}
				}
				if (r != noErr) return r; // Something went wrong upstream.

				for (int j = 0, chIdx = pInBus->mPlugChannelStartIdx; j < pInBus->mNHostChannels; ++j)
				{
					_this->AttachInputBuffers(chIdx + j, 1, (AudioSampleType**)&pInBufList->mBuffers[j].mData, nFrames);
				}
			}
		}
		_this->mRenderTimestamp = renderTimestamp;
	}

	BusChannels* const pOutBus = _this->mOutBuses.Get(outputBusIdx);
	if (!pOutBus->mConnected || pOutBus->mNHostChannels != pOutBufList->mNumberBuffers)
	{
		const int startChannelIdx = pOutBus->mPlugChannelStartIdx;
		const int nConnected = wdl_min(pOutBus->mNHostChannels, pOutBufList->mNumberBuffers);
		int nUnconnected = pOutBus->mNPlugChannels - nConnected;
		nUnconnected = wdl_max(nUnconnected, 0);
		_this->SetOutputChannelConnections(startChannelIdx, nConnected, true);
		_this->SetOutputChannelConnections(startChannelIdx + nConnected, nUnconnected, false);
		pOutBus->mConnected = true;
  }

	for (int i = 0, chIdx = pOutBus->mPlugChannelStartIdx; i < pOutBufList->mNumberBuffers; ++i)
	{
		if (!(pOutBufList->mBuffers[i].mData)) // Grr. Downstream unit didn't give us buffers.
		{
			pOutBufList->mBuffers[i].mData = _this->mOutScratchBuf.Get() + (chIdx + i) * nFrames;
		}
		_this->AttachOutputBuffers(chIdx + i, 1, (AudioSampleType**)&pOutBufList->mBuffers[i].mData);
	}

	if (_this->IsBypassed())
	{
		_this->PassThroughBuffers((AudioSampleType)0, nFrames);
	}
	else
	{
		_this->ProcessBuffers((AudioSampleType)0, nFrames);
	}

	for (int i = 0; i < nRenderNotify; ++i)
	{
		const AURenderCallbackStruct* const pRN = _this->mRenderNotify.Get(i);
		AudioUnitRenderActionFlags flags = kAudioUnitRenderAction_PostRender;
		RenderCallback(pRN, &flags, pTimestamp, outputBusIdx, nFrames, pOutBufList);
	}

	return noErr;
}

IPlugAU::BusChannels* IPlugAU::GetBus(const AudioUnitScope scope, const AudioUnitElement busIdx) const
{
	switch (scope)
	{
		// Global bus is an alias for output bus zero.
		case kAudioUnitScope_Global:
		{
			if (mOutBuses.GetSize())
			{
				return mOutBuses.Get(busIdx);
			}
			break;
		}

		case kAudioUnitScope_Input:
		{
			if (busIdx < (unsigned int)mInBuses.GetSize())
			{
				return mInBuses.Get(busIdx);
			}
			break;
		}

		case kAudioUnitScope_Output:
		{
			if (busIdx < (unsigned int)mOutBuses.GetSize())
			{
				return mOutBuses.Get(busIdx);
			}
			break;
		}
	}

	return NULL;
}

void IPlugAU::ClearConnections()
{
	const int nInBuses = mInBuses.GetSize();
	for (int i = 0; i < nInBuses; ++i)
	{
		BusChannels* const pInBus = mInBuses.Get(i);
		pInBus->mConnected = false;
		pInBus->mNHostChannels = -1;
		InputBusConnection* const pInBusConn = mInBusConnections.Get(i);
		memset(pInBusConn, 0, sizeof(InputBusConnection));
	}
	const int nOutBuses = mOutBuses.GetSize();
	for (int i = 0; i < nOutBuses; ++i)
	{
		BusChannels* const pOutBus = mOutBuses.Get(i);
		pOutBus->mConnected = false;
		pOutBus->mNHostChannels = -1;
	}
}

// GarageBand doesn't always report tempo when the transport is stopped, so we need it to persist in the class.
static const double DEFAULT_TEMPO = 120.0;

IPlugAU::IPlugAU(
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
	plugDoes),

	mCI(NULL),
	mRenderTimestamp(-1.0),
	mTempo(DEFAULT_TEMPO)
{
	memset(&mHostCallbacks, 0, sizeof(HostCallbackInfo));
	// memset(&mMidiCallback, 0, sizeof(AUMIDIOutputCallbackStruct));

	if (IsInst())
		mComponentType = kAudioUnitType_MusicDevice;
	else if (DoesMIDI())
		mComponentType = kAudioUnitType_MusicEffect;
	else
		mComponentType = kAudioUnitType_Effect;

	mFactory = false;

	const char* const* const pID = (const char* const*)instanceInfo;
	mOSXBundleID.Set(pID[0]);
	mCocoaViewFactoryClassName.Set(pID[1]);

	// Every channel pair requested on input or output is a separate bus.
	const int nInputs = NInChannels(), nOutputs = NOutChannels();
	const int nInBuses = nInputs / 2 + (nInputs & 1);
	const int nOutBuses = nOutputs / 2 + (nOutputs & 1);

	if (nInBuses > 0)
	{
		mBufList.Resize(sizeof(AudioBufferList) + (nInBuses - 1) * sizeof(AudioBuffer));
	}

	PtrListInitialize(&mInBusConnections, nInBuses);
	PtrListInitialize(&mInBuses, nInBuses);
	PtrListInitialize(&mOutBuses, nOutBuses);

	for (int i = 0; i < nInBuses; ++i)
	{
		const int startCh = i * 2;
		BusChannels* const pInBus = mInBuses.Get(i);
		pInBus->mNHostChannels = -1;
		pInBus->mPlugChannelStartIdx = startCh;
		const int n = nInputs - startCh;
		pInBus->mNPlugChannels = wdl_min(n, 2);
	}
	for (int i = 0; i < nOutBuses; ++i)
	{
		const int startCh = i * 2;
		BusChannels* const pOutBus = mOutBuses.Get(i);
		pOutBus->mNHostChannels = -1;
		pOutBus->mPlugChannelStartIdx = startCh;
		const int n = nOutputs - startCh;
		pOutBus->mNPlugChannels = wdl_min(n, 2);
	}

	AssessInputConnections();

	SetBlockSize(kDefaultBlockSize);
}

bool IPlugAU::AllocStateChunk(int chunkSize)
{
	if (chunkSize < 0) chunkSize = GetParamsChunkSize(0, NParams());
	return mState.Alloc(chunkSize) == chunkSize;
}

bool IPlugAU::AllocBankChunk(const int chunkSize)
{
	if (chunkSize < 0 && mPresetChunkSize < 0) AllocPresetChunk();
	return true;
}

static void SendAUEvent(const AudioUnitEventType type, ComponentInstance const ci, const int idx)
{
	AudioUnitEvent auEvent;
	memset(&auEvent, 0, sizeof(AudioUnitEvent));
	auEvent.mEventType = type;
	auEvent.mArgument.mParameter.mAudioUnit = ci;
	auEvent.mArgument.mParameter.mParameterID = idx;
	auEvent.mArgument.mParameter.mScope = kAudioUnitScope_Global;
	auEvent.mArgument.mParameter.mElement = 0;
	AUEventListenerNotify(NULL, NULL, &auEvent);
}

void IPlugAU::BeginInformHostOfParamChange(const int idx, const bool lockMutex)
{
	EndDelayedInformHostOfParamChange(lockMutex);
	SendAUEvent(kAudioUnitEvent_BeginParameterChangeGesture, mCI, idx);
}

void IPlugAU::InformHostOfParamChange(const int idx, double /* normalizedValue */, bool /* lockMutex */)
{
	SendAUEvent(kAudioUnitEvent_ParameterValueChange, mCI, idx);
}

void IPlugAU::EndInformHostOfParamChange(const int idx, bool /* lockMutex */)
{
	SendAUEvent(kAudioUnitEvent_EndParameterChangeGesture, mCI, idx);
}

// void IPlugAU::InformHostOfProgramChange()
// {
	// InformListeners(kAudioUnitProperty_CurrentPreset, kAudioUnitScope_Global);
	// InformListeners(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global);
// }

// Samples since start of project.
double IPlugAU::GetSamplePos()
{
	if (mHostCallbacks.transportStateProc)
	{
		Float64 samplePos = 0.0, loopStartBeat, loopEndBeat;
		Boolean playing, changed, looping;
		mHostCallbacks.transportStateProc(mHostCallbacks.hostUserData, &playing, &changed, &samplePos,
			&looping, &loopStartBeat, &loopEndBeat);
		return samplePos;
	}
	return 0.0;
}

double IPlugAU::GetTempo()
{
	if (mHostCallbacks.beatAndTempoProc)
	{
		Float64 currentBeat = 0.0, tempo = 0.0;
		mHostCallbacks.beatAndTempoProc(mHostCallbacks.hostUserData, &currentBeat, &tempo);
		if (tempo > 0.0) mTempo = tempo;
    }
	return mTempo;
}

void IPlugAU::GetTimeSig(int* const pNum, int* const pDenom)
{
	Float32 tsNum = 0.0f;
	UInt32 tsDenom = 0;
	if (mHostCallbacks.musicalTimeLocationProc)
	{
		Float64 currentMeasureDownBeat = 0.0;
		UInt32 sampleOffsetToNextBeat = 0;
		mHostCallbacks.musicalTimeLocationProc(mHostCallbacks.hostUserData, &sampleOffsetToNextBeat,
			&tsNum, &tsDenom, &currentMeasureDownBeat);
	}
	*pNum = (int)tsNum;
	*pDenom = tsDenom;
}

int IPlugAU::GetHost()
{
	int host = IPlugBase::GetHost();
	if (host == kHostUninit)
	{
		CFBundleRef const mainBundle = CFBundleGetMainBundle();
		if (mainBundle)
		{
			CFStringRef const id = CFBundleGetIdentifier(mainBundle);
			if (id)
			{
				const CStrLocal str(id);
				SetHost(str.mCStr, 0);
				host = IPlugBase::GetHost();
			}
		}
		if (host == kHostUninit)
		{
			SetHost(NULL, 0);
			host = IPlugBase::GetHost();
		}
	}
	return host;
}

void IPlugAU::SetBlockSize(int blockSize)
{
	IPlugBase::SetBlockSize(blockSize);
	blockSize = mBlockSize;

	const int nIn = NInChannels() * blockSize;
	AudioSampleType* const pScratchInput = mInScratchBuf.ResizeOK(nIn);

	if (pScratchInput)
		memset(pScratchInput, 0, nIn * sizeof(AudioSampleType));
	else if (nIn)
		blockSize = 0;

	const int nOut = NOutChannels() * blockSize;
	AudioSampleType* const pScratchOutput = mOutScratchBuf.ResizeOK(nOut);

	if (pScratchOutput)
		memset(pScratchOutput, 0, nOut * sizeof(AudioSampleType));
	else if (nOut)
		blockSize = 0;

	mBlockSize = blockSize;
}

void IPlugAU::InformListeners(const AudioUnitPropertyID propID, const AudioUnitScope scope)
{
	const int n = mPropertyListeners.GetSize();
	for (int i = 0; i < n; ++i)
	{
		PropertyListener* const pListener = mPropertyListeners.Get(i);
		if (pListener->mPropID == propID)
		{
			pListener->mListenerProc(pListener->mProcArgs, mCI, propID, scope, 0);
		}
	}
}

void IPlugAU::SetLatency(const int samples)
{
	const int n = mPropertyListeners.GetSize();
	for (int i = 0; i < n; ++i)
	{
		PropertyListener* const pListener = mPropertyListeners.Get(i);
		if (pListener->mPropID == kAudioUnitProperty_Latency)
		{
			pListener->mListenerProc(pListener->mProcArgs, mCI, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
		}
	}
	IPlugBase::SetLatency(samples);
}

/* bool IPlugAU::SendMidiMsg(const IMidiMsg* const pMsg)
{
	// I believe AU passes MIDI messages through automatically.
	// For the case where we're generating midi messages, we'll use AUMIDIOutputCallback.
	// See AudioUnitProperties.h.
	if (mMidiCallback.midiOutputCallback)
	{
		AudioTimeStamp timeStamp;
		memset(&timeStamp, 0, sizeof(AudioTimeStamp));
		timeStamp.mFlags = kAudioTimeStampSampleTimeValid;
		timeStamp.mSampleTime = wdl_max(mRenderTimestamp, 0.0);

		MIDIPacketList packetList;
		packetList.numPackets = 1;
		packetList.packet[0].data[0] = pMsg->mStatus;
		packetList.packet[0].data[1] = pMsg->mData1;
		packetList.packet[0].data[2] = pMsg->mData2;
		packetList.packet[0].length = pMsg->Size();
		packetList.packet[0].timeStamp = pMsg->mOffset;

		return mMidiCallback.midiOutputCallback(NULL, &timeStamp, 1, &packetList) == noErr;
	}
	return false;
}

bool IPlugAU::SendSysEx(const ISysEx* const pSysEx)
{
	if (mMidiCallback.midiOutputCallback)
	{
		AudioTimeStamp timeStamp;
		memset(&timeStamp, 0, sizeof(AudioTimeStamp));
		timeStamp.mFlags = kAudioTimeStampSampleTimeValid;
		timeStamp.mSampleTime = wdl_max(mRenderTimestamp, 0.0);

		MIDIPacketList packetList;
		packetList.numPackets = 1;
		packetList.packet[0].timeStamp = pSysEx->mOffset;

		int size = pSysEx->mSize;
		while (size > 0)
		{
			size -= packetList.packet[0].length = wdl_min(size, sizeof(packetList.packet[0].data));
			size = mMidiCallback.midiOutputCallback(NULL, &timeStamp, 1, &packetList) == noErr ? size : -1;
		}
		return size >= 0;
	}
	return false;
} */
