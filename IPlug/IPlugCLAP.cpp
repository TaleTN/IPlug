#include "IPlugCLAP.h"
#include "IGraphics.h"

#ifdef __APPLE__
	#include "IGraphicsMac.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"

extern "C" {

// See IPlug_include_in_plug_src.h.
uint32_t CLAP_ABI ClapFactoryGetPluginCount(const clap_plugin_factory* pFactory);
const clap_plugin_descriptor* CLAP_ABI ClapFactoryGetPluginDescriptor(const clap_plugin_factory* pFactory, uint32_t index);
const clap_plugin* CLAP_ABI ClapFactoryCreatePlugin(const clap_plugin_factory* pFactory, const clap_host* pHost, const char* id);

} // extern "C"

static const char* const sClapWindowAPI =
#ifdef _WIN32
	CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
	CLAP_WINDOW_API_COCOA;
#endif

static double GetParamValue(const IParam* const pParam)
{
	switch (pParam->Type())
	{
		case IParam::kTypeBool:
			return (double)((const IBoolParam*)pParam)->Bool();
		case IParam::kTypeEnum:
			return (double)((const IEnumParam*)pParam)->Int();
		default:
			break;
	}

	return pParam->GetNormalized();
}

static int NInOutChannels(const IPlugCLAP* const pPlug, const bool isInput)
{
	return isInput ? pPlug->NInChannels() : pPlug->NOutChannels();
}

static void GetInOutName(const bool isInput, char* const buf, const int bufSize)
{
	lstrcpyn_safe(buf, isInput ? "Input" : "Output", bufSize);
}

bool IPlugCLAP::DoesMIDIInOut(const IPlugCLAP* const pPlug, const bool isInput)
{
	return pPlug->DoesMIDI(isInput ? kPlugDoesMidiIn : kPlugDoesMidiOut);
}

IPlugCLAP::IPlugCLAP(
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

	SetInputChannelConnections(0, nInputs, true);
	SetOutputChannelConnections(0, nOutputs, true);

	mTransportFlags = 0;
	mSongPos = 0;
	mTempo = 0.0;
	memset(mTimeSig, 0, sizeof(mTimeSig));

	mPushIt = false;

	mClapPlug.desc = ClapFactoryGetPluginDescriptor(NULL, 0);
	mClapPlug.plugin_data = this;
	mClapPlug.init = ClapInit;
	mClapPlug.destroy = ClapDestroy;
	mClapPlug.activate = ClapActivate;
	mClapPlug.deactivate = ClapDeactivate;
	mClapPlug.start_processing = ClapStartProcessing;
	mClapPlug.stop_processing = ClapStopProcessing;
	mClapPlug.reset = ClapReset;
	mClapPlug.process = ClapProcess;
	mClapPlug.get_extension = ClapGetExtension;
	mClapPlug.on_main_thread = ClapOnMainThread;

	// The plugin is not allowed to use the host callbacks in the create method.
	mClapHost = NULL;
	mRequestFlush = NULL;
	mRequestResize = NULL;

	mGUIParent = NULL;
	mGUIWidth = mGUIHeight = 0;

	mNoteNameCount = 0;

	SetBlockSize(kDefaultBlockSize);
	mClapHost = (const clap_host*)instanceInfo;
}

bool IPlugCLAP::AllocStateChunk(int chunkSize)
{
	if (chunkSize < 0) chunkSize = GetParamsChunkSize(0, NParams());
	return mState.Alloc(chunkSize) == chunkSize;
}

bool IPlugCLAP::AllocBankChunk(const int chunkSize)
{
	if (chunkSize < 0 && mPresetChunkSize < 0) AllocPresetChunk();
	return true;
}

void IPlugCLAP::BeginInformHostOfParamChange(const int idx, const bool lockMutex)
{
	if (lockMutex) mMutex.Enter();

	EndDelayedInformHostOfParamChange(false);
	AddParamChange(kParamChangeBegin, idx);

	if (lockMutex) mMutex.Leave();
}

void IPlugCLAP::InformHostOfParamChange(const int idx, double /* normalizedValue */, const bool lockMutex)
{
	if (lockMutex) mMutex.Enter();

	AddParamChange(kParamChangeValue, idx);

	if (lockMutex) mMutex.Leave();
}

void IPlugCLAP::EndInformHostOfParamChange(const int idx, const bool lockMutex)
{
	if (lockMutex) mMutex.Enter();

	AddParamChange(kParamChangeEnd, idx);

	if (lockMutex) mMutex.Leave();
}

double IPlugCLAP::GetSamplePos()
{
	double samplePos = 0.0;

	if (mTransportFlags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE)
	{
		static const double secTimeFactor = 1.0 / (double)CLAP_SECTIME_FACTOR;
		samplePos = (double)mSongPos * secTimeFactor * mSampleRate;
	}

	return samplePos;
}

double IPlugCLAP::GetTempo()
{
	double tempo = 0.0;

	if (mTransportFlags & CLAP_TRANSPORT_HAS_TEMPO)
	{
		tempo = wdl_max(mTempo, 0.0);
	}

	return tempo;
}

void IPlugCLAP::GetTimeSig(int* const pNum, int* const pDenom)
{
	int num, denom;

	if (mTransportFlags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE)
	{
		num = mTimeSig[0];
		denom = mTimeSig[1];
	}
	else
	{
		denom = num = 0;
	}

	*pNum = num;
	*pDenom = denom;
}

void IPlugCLAP::ResizeGraphics(const int w, const int h)
{
	if (mRequestResize && mRequestResize(mClapHost, w, h))
	{
		mGUIWidth = w;
		mGUIHeight = h;
	}
}

bool IPlugCLAP::SendMidiMsg(const IMidiMsg* const pMsg)
{
	mMutex.Enter();
	bool ret = false;

	if (mMidiOut.Add(*pMsg))
	{
		mPushIt = ret = true;
		mClapHost->request_process(mClapHost);
	}

	mMutex.Leave();
	return ret;
}

bool IPlugCLAP::SendSysEx(const ISysEx* const pSysEx)
{
	mMutex.Enter();
	bool ret = false;

	static const int isSysEx = 0x80000000;

	const int ofs = pSysEx->mOffset, size = pSysEx->mSize;
	const IMidiMsg msg(ofs | isSysEx, (const void*)&size);

	if (mMidiOut.Add(msg))
	{
		const int bufSize = mSysExBuf.GetSize();
		void* const buf = mSysExBuf.ResizeOK(bufSize + size, false);

		if (buf)
		{
			memcpy((unsigned char*)buf + bufSize, pSysEx->mData, size);

			mPushIt = ret = true;
			mClapHost->request_process(mClapHost);
		}
		else
		{
			mMidiOut.Resize(mMidiOut.GetSize() - 1, false);
		}
	}

	mMutex.Leave();
	return ret;
}

void IPlugCLAP::ProcessInputEvents(const clap_input_events* const pInEvents, const uint32_t nEvents, const uint32_t nFrames)
{
	for (uint32_t i = 0; i < nEvents; ++i)
	{
		const clap_event_header* const pEvent = pInEvents->get(pInEvents, i);

		const uint32_t ofs = pEvent->time;
		if (ofs >= nFrames) break;

		if (pEvent->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

		switch (pEvent->type)
		{
			case CLAP_EVENT_NOTE_ON:
			{
				const clap_event_note* const pNoteOn = (const clap_event_note*)pEvent;
				if (pNoteOn->port_index != 0) break;

				int velocity = (int)(pNoteOn->velocity * 127.0 + 0.5);
				velocity = wdl_max(velocity, 1);

				const IMidiMsg msg(ofs, 0x90 | pNoteOn->channel, pNoteOn->key, velocity);
				ProcessMidiMsg(&msg);
				break;
			}

			case CLAP_EVENT_NOTE_OFF:
			{
				const clap_event_note* const pNoteOff = (const clap_event_note*)pEvent;
				if (pNoteOff->port_index != 0) break;

				const int velocity = (int)(pNoteOff->velocity * 127.0 + 0.5);

				const IMidiMsg msg(ofs, 0x80 | pNoteOff->channel, pNoteOff->key, velocity);
				ProcessMidiMsg(&msg);
				break;
			}

			case CLAP_EVENT_PARAM_VALUE:
			{
				ProcessParamEvent((const clap_event_param_value*)pEvent);
				break;
			}

			case CLAP_EVENT_MIDI:
			{
				const clap_event_midi* const pMidiEvent = (const clap_event_midi*)pEvent;
				if (pMidiEvent->port_index) break;

				const uint8_t* const data = pMidiEvent->data;

				const IMidiMsg msg(ofs, data[0], data[1], data[2]);
				ProcessMidiMsg(&msg);
				break;
			}

			case CLAP_EVENT_MIDI_SYSEX:
			{
				const clap_event_midi_sysex* const pSysExEvent = (const clap_event_midi_sysex*)pEvent;
				if (pSysExEvent->port_index) break;

				const ISysEx sysex(ofs, pSysExEvent->buffer, pSysExEvent->size);
				if (sysex.mSize >= 0) ProcessSysEx(&sysex);
				break;
			}
		}
	}
}

void IPlugCLAP::ProcessParamEvent(const clap_event_param_value* const pEvent)
{
	const int idx = pEvent->param_id;
	IParam* pParam = (IParam*)pEvent->cookie;

	if (!pParam)
	{
		pParam = GetParam(idx);
		if (!pParam) return;
	}

	double v = pEvent->value;

	// TN: Why is order in IPlugVST2::VSTSetParameter() different?

	switch (pParam->Type())
	{
		case IParam::kTypeBool:
		{
			IBoolParam* const pBool = (IBoolParam*)pParam;
			const int boolVal = (int)v;
			pBool->Set(boolVal);
			v = (double)boolVal;
			break;
		}

		case IParam::kTypeEnum:
		{
			IEnumParam* const pEnum = (IEnumParam*)pParam;
			const int intVal = (int)v;
			pEnum->Set(intVal);
			v = pEnum->ToNormalized(intVal);
			break;
		}

		default:
		{
			pParam->SetNormalized(v);
			break;
		}
	}

	IGraphics* const pGraphics = GetGUI();
	if (pGraphics) pGraphics->SetParameterFromPlug(idx, v, true);
	OnParamChange(idx);
}

void IPlugCLAP::AddParamChange(const int change, const int idx)
{
	mPushIt = true;

	const unsigned int packed = (idx << 2) | change;
	mParamChanges.Add(packed);

	if (mRequestFlush) mRequestFlush(mClapHost);
}

void IPlugCLAP::PushOutputEvents(const clap_output_events* const pOutEvents)
{
	const unsigned int* const pParamChanges = mParamChanges.GetFast();
	const int nChanges = mParamChanges.GetSize();

	if (nChanges)
	{
		PushParamChanges(pOutEvents, pParamChanges, nChanges);
		mParamChanges.Resize(0, false);
	}

	const IMidiMsg* const pMidiOut = mMidiOut.GetFast();
	const int nMidiMsgs = mMidiOut.GetSize();

	if (nMidiMsgs)
	{
		const unsigned char* const pSysExBuf = (const unsigned char*)mSysExBuf.Get();

		PushMidiMsgs(pOutEvents, pMidiOut, nMidiMsgs, pSysExBuf);
		mMidiOut.Resize(0, false);

		if (pSysExBuf) mSysExBuf.Resize(0, false);
	}
}

void IPlugCLAP::PushParamChanges(const clap_output_events* const pOutEvents, const unsigned int* const pParamChanges, const int nChanges) const
{
	clap_event_param_value paramValue =
	{
		sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0,
		0, NULL, -1, -1, -1, -1, 0.0
	};

	clap_event_param_gesture paramGesture =
	{
		sizeof(clap_event_param_gesture), 0, CLAP_CORE_EVENT_SPACE_ID, 0, 0,
		0
	};

	for (int i = 0; i < nChanges; ++i)
	{
		const unsigned int packed = pParamChanges[i];

		const int change = packed & 3;
		const int idx = packed >> 2;

		const clap_event_header* pEvent;

		if (!change)
		{
			paramValue.param_id = idx;
			paramValue.value = GetParamValue(GetParam(idx));
			pEvent = &paramValue.header;
		}
		else
		{
			static const int toType = CLAP_EVENT_PARAM_GESTURE_BEGIN - kParamChangeBegin;

			paramGesture.header.type = change + toType;
			paramGesture.param_id = idx;
			pEvent = &paramGesture.header;
		}

		pOutEvents->try_push(pOutEvents, pEvent);
	}
}

void IPlugCLAP::PushMidiMsgs(const clap_output_events* const pOutEvents, const IMidiMsg* const pMidiOut, const int nMidiMsgs, const unsigned char* pSysExBuf)
{
	clap_event_midi midiEvent =
	{
		sizeof(clap_event_midi), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0,
		0, { 0, 0, 0 }
	};

	clap_event_midi_sysex sysExEvent =
	{
		sizeof(clap_event_midi_sysex), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI_SYSEX, 0,
		0, NULL, 0
	};

	for (int i = 0; i < nMidiMsgs; ++i)
	{
		const IMidiMsg* const pMsg = &pMidiOut[i];

		const int ofs = pMsg->mOffset;
		const int isSysEx = ofs & 0x80000000;

		const clap_event_header* pEvent;

		if (!isSysEx)
		{
			midiEvent.header.time = ofs;
			memcpy(midiEvent.data, &pMsg->mStatus, 3);

			pEvent = &midiEvent.header;
		}
		else
		{
			sysExEvent.header.time = ofs ^ 0x80000000;
			sysExEvent.buffer = pSysExBuf;

			memcpy(&sysExEvent.size, &pMsg->mStatus, sizeof(uint32_t));
			pSysExBuf += sysExEvent.size;

			pEvent = &sysExEvent.header;
		}

		pOutEvents->try_push(pOutEvents, pEvent);
	}
}

const void* CLAP_ABI IPlugCLAP::ClapEntryGetFactory(const char* const id)
{
	static const clap_plugin_factory factory =
	{
		ClapFactoryGetPluginCount,
		ClapFactoryGetPluginDescriptor,
		ClapFactoryCreatePlugin
	};

	return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : NULL;
}

bool CLAP_ABI IPlugCLAP::ClapInit(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const bool doesMidiOut = _this->DoesMIDI(kPlugDoesMidiOut);
	for (int prealloc = 1; prealloc >= 0; --prealloc)
	{
		_this->mParamChanges.Resize(prealloc, false);
		if (!doesMidiOut) continue;

		_this->mMidiOut.Resize(prealloc, false);
		_this->mSysExBuf.Resize(prealloc, false);
	}

	const clap_host* const pHost = _this->mClapHost;
	const clap_host_params* const pHostParams = (const clap_host_params*)pHost->get_extension(pHost, CLAP_EXT_PARAMS);
	_this->mRequestFlush = pHostParams ? pHostParams->request_flush : NULL;

	_this->HostSpecificInit();
	_this->OnParamReset();

	_this->mMutex.Leave();
	return true;
}

void CLAP_ABI IPlugCLAP::ClapDestroy(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	delete _this;
}

bool CLAP_ABI IPlugCLAP::ClapActivate(const clap_plugin* const pPlug, const double sampleRate, uint32_t /* minBufSize */, const uint32_t maxBufSize)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const int flags = _this->mPlugFlags;
	bool reset = false;

	if (sampleRate != _this->GetSampleRate() || !(flags & kPlugInitSampleRate))
	{
		_this->SetSampleRate(sampleRate);
		reset = true;
	}

	if (maxBufSize != _this->GetBlockSize() || !(flags & kPlugInitBlockSize))
	{
		_this->SetBlockSize(maxBufSize);
		reset = true;
	}

	if (reset)
	{
		_this->mPlugFlags = flags | kPlugInit;
		_this->Reset();
	}

	_this->mMutex.Leave();
	return true;
}

bool CLAP_ABI IPlugCLAP::ClapStartProcessing(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const int flags = _this->mPlugFlags;

	if (!(flags & kPlugFlagsActive))
	{
		_this->mPlugFlags = flags | kPlugFlagsActive;
		_this->OnActivate(true);
	}

	_this->mMutex.Leave();
	return true;
}

void CLAP_ABI IPlugCLAP::ClapStopProcessing(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const int flags = _this->mPlugFlags;

	if (flags & kPlugFlagsActive)
	{
		_this->mPlugFlags = flags & ~kPlugFlagsActive;
		_this->OnActivate(false);
	}

	_this->mMutex.Leave();
}

void CLAP_ABI IPlugCLAP::ClapReset(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	_this->Reset();	

	_this->mMutex.Leave();
}

clap_process_status CLAP_ABI IPlugCLAP::ClapProcess(const clap_plugin* const pPlug, const clap_process* const pProcess)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const uint32_t nFrames = pProcess->frames_count;
	const clap_event_transport* const pTransport = pProcess->transport;

	if (pTransport)
	{
		_this->mTransportFlags = pTransport->flags;
		_this->mSongPos = pTransport->song_pos_seconds;

		WDL_UINT64 tempo;
		memcpy(&tempo, &pTransport->tempo, sizeof(double));
		memcpy(&_this->mTempo, &tempo, sizeof(double));

		memcpy(_this->mTimeSig, &pTransport->tsig_num, 2 * sizeof(uint16_t));
	}

	if (_this->mPushIt)
	{
		_this->mPushIt = false;
		_this->PushOutputEvents(pProcess->out_events);
	}

	const clap_input_events* const pInEvents = pProcess->in_events;
	const uint32_t nEvents = pInEvents->size(pInEvents);

	if (nEvents) _this->ProcessInputEvents(pInEvents, nEvents, nFrames);

	const void* const* inputs = NULL;
	void* const* outputs = NULL;

	bool is64bits = true;

	if (pProcess->audio_inputs_count)
	{
		const clap_audio_buffer* const pBuf = &pProcess->audio_inputs[0];

		const void* const* const data32 = (const void* const*)pBuf->data32;
		const void* const* const data64 = (const void* const*)pBuf->data64;

		is64bits = !!data64;
		inputs = is64bits ? data64 : data32;
	}

	if (pProcess->audio_outputs_count)
	{
		const clap_audio_buffer* const pBuf = &pProcess->audio_outputs[0];

		void* const* const data32 = (void* const*)pBuf->data32;
		void* const* const data64 = (void* const*)pBuf->data64;

		is64bits = !!data64;
		outputs = is64bits ? data64 : data32;
	}

	const int nInputs = _this->NInChannels();
	const int nOutputs = _this->NOutChannels();

	if (is64bits)
	{
		_this->AttachInputBuffers(0, nInputs, (const double* const*)inputs, nFrames);
		_this->AttachOutputBuffers(0, nOutputs, (double* const*)outputs);
		_this->ProcessBuffers((double)0.0, nFrames);
	}
	else
	{
		_this->AttachInputBuffers(0, nInputs, (const float* const*)inputs, nFrames);
		_this->AttachOutputBuffers(0, nOutputs, (float* const*)outputs);
		_this->ProcessBuffers((float)0.0f, nFrames);
	}

	_this->mMutex.Leave();
	return CLAP_PROCESS_CONTINUE;
}

const void* CLAP_ABI IPlugCLAP::ClapGetExtension(const clap_plugin* const pPlug, const char* const id)
{
	if (!strcmp(id, CLAP_EXT_PARAMS))
	{
		static const clap_plugin_params params =
		{
			ClapParamsCount,
			ClapParamsGetInfo,
			ClapParamsGetValue,
			ClapParamsValueToText,
			ClapParamsTextToValue,
			ClapParamsFlush
		};

		return &params;
	}

	if (!strcmp(id, CLAP_EXT_STATE))
	{
		static const clap_plugin_state state =
		{
			ClapStateSave,
			ClapStateLoad
		};

		return &state;
	}

	if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
	{
		static const clap_plugin_audio_ports audioPorts =
		{
			ClapAudioPortsCount,
			ClapAudioPortsGet
		};

		return &audioPorts;
	}

	if (!strcmp(id, CLAP_EXT_LATENCY))
	{
		static const clap_plugin_latency latency =
		{
			ClapLatencyGet
		};

		return &latency;
	}

	if (!strcmp(id, CLAP_EXT_RENDER))
	{
		static const clap_plugin_render render =
		{
			ClapRenderHasHardRealtimeRequirement,
			ClapRenderSet
		};

		return &render;
	}

	if (!strcmp(id, CLAP_EXT_NOTE_PORTS))
	{
		const IPlugCLAP* const _this = (const IPlugCLAP*)pPlug->plugin_data;
		if (!_this->DoesMIDI()) return NULL;

		static const clap_plugin_note_ports notePorts =
		{
			ClapNotePortsCount,
			ClapNotePortsGet
		};

		return &notePorts;
	}

	if (!strcmp(id, CLAP_EXT_NOTE_NAME))
	{
		static const clap_plugin_note_name noteName =
		{
			ClapNoteNameCount,
			ClapNoteNameGet
		};

		return &noteName;
	}

	if (!strcmp(id, CLAP_EXT_GUI))
	{
		const IPlugCLAP* const _this = (const IPlugCLAP*)pPlug->plugin_data;
		if (!_this->GetGUI()) return NULL;

		static const clap_plugin_gui gui =
		{
			ClapGUIIsAPISupported,
			ClapGUIGetPreferredAPI,
			ClapGUICreate,
			ClapGUIDestroy,
			ClapGUISetScale,
			ClapGUIGetSize,
			ClapGUICanResize,
			ClapGUIGetResizeHints,
			ClapGUIAdjustSize,
			ClapGUISetSize,
			ClapGUISetParent,
			ClapGUISetTransient,
			ClapGUISuggestTitle,
			ClapGUIShow,
			ClapGUIHide
		};

		return &gui;
	}

	return NULL;
}

uint32_t CLAP_ABI IPlugCLAP::ClapParamsCount(const clap_plugin* const pPlug)
{
	const IPlugCLAP* const _this = (const IPlugCLAP*)pPlug->plugin_data;
	return _this->NParams();
}

bool CLAP_ABI IPlugCLAP::ClapParamsGetInfo(const clap_plugin* const pPlug, const uint32_t idx, clap_param_info* const pInfo)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	if (!_this->NParams(idx)) return false;

	_this->mMutex.Enter();

	const IParam* const pParam = _this->GetParam(idx);
	const int type = pParam->Type();

	pInfo->id = idx;
	pInfo->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
	pInfo->cookie = (void*)pParam;

	lstrcpyn_safe(pInfo->name, pParam->GetNameForHost(), sizeof(pInfo->name));
	pInfo->module[0] = 0;

	pInfo->min_value = 0.0;

	switch (type)
	{
		case IParam::kTypeEnum:
		{
			pInfo->flags |= CLAP_PARAM_IS_STEPPED;

			const IEnumParam* const pEnum = (const IEnumParam*)pParam;
			pInfo->max_value = (double)(pEnum->NEnums() - 1);
			pInfo->default_value = (double)pEnum->Int();
			break;
		}

		case IParam::kTypeBool:
		{
			pInfo->flags |= CLAP_PARAM_IS_STEPPED;
			// [[fallthrough]];
		}
		default:
		{
			pInfo->max_value = 1.0;
			pInfo->default_value = pParam->GetNormalized();
			break;
		}
	}

	_this->mMutex.Leave();
	return true;
}

bool CLAP_ABI IPlugCLAP::ClapParamsGetValue(const clap_plugin* const pPlug, const clap_id idx, double* const pValue)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	if (!_this->NParams(idx)) return false;

	_this->mMutex.Enter();

	*pValue = GetParamValue(_this->GetParam(idx));

	_this->mMutex.Leave();
	return true;
}

bool CLAP_ABI IPlugCLAP::ClapParamsValueToText(const clap_plugin* const pPlug, const clap_id idx, const double value, char* const buf, const uint32_t bufSize)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	if (!_this->NParams(idx)) return false;

	_this->mMutex.Enter();

	IParam* const pParam = _this->GetParam(idx);
	double v = value;

	switch (pParam->Type())
	{
		case IParam::kTypeBool:
		{
			v = (double)(int)v;
			break;
		}

		case IParam::kTypeEnum:
		{
			v = ((IEnumParam*)pParam)->ToNormalized((int)v);
			break;
		}
	}

	pParam->GetDisplayForHost(v, buf, bufSize);
	const char* const label = pParam->GetLabelForHost();

	if (label && *label)
	{
		lstrcatn(buf, " ", bufSize);
		lstrcatn(buf, label, bufSize);
	}

	_this->mMutex.Leave();
	return true;
}

bool CLAP_ABI IPlugCLAP::ClapParamsTextToValue(const clap_plugin* const pPlug, const clap_id idx, const char* const str, double* const pValue)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	if (!_this->NParams(idx)) return false;

	_this->mMutex.Enter();

	const IParam* const pParam = _this->GetParam(idx);
	double v;

	const int type = pParam->Type();
	const bool mapped = pParam->MapDisplayText(str, &v);

	if (!mapped)
	{
		v = strtod(str, NULL);
		if (pParam->DisplayIsNegated()) v = -v;
		v = pParam->GetNormalized(v);
	}

	if (type == IParam::kTypeEnum)
	{
		v = (double)((const IEnumParam*)pParam)->FromNormalized(v);
	}

	*pValue = v;

	_this->mMutex.Leave();
	return true;
}

void CLAP_ABI IPlugCLAP::ClapParamsFlush(const clap_plugin* const pPlug, const clap_input_events* const pInEvents, const clap_output_events* const pOutEvents)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const uint32_t nEvents = pInEvents->size(pInEvents);

	for (uint32_t i = 0; i < nEvents; ++i)
	{
		const clap_event_header* const pEvent = pInEvents->get(pInEvents, i);

		if (pEvent->space_id == CLAP_CORE_EVENT_SPACE_ID && pEvent->type == CLAP_EVENT_PARAM_VALUE)
		{
			_this->ProcessParamEvent((const clap_event_param_value*)pEvent);
		}
	}

	_this->PushOutputEvents(pOutEvents);
	_this->mMutex.Leave();
}

bool CLAP_ABI IPlugCLAP::ClapStateSave(const clap_plugin* const pPlug, const clap_ostream* const pStream)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	ByteChunk* const pChunk = &_this->mState;
	bool ok = pChunk->AllocSize() || _this->AllocStateChunk();

	if (ok)
	{
		pChunk->Clear();
		ok = _this->SerializeState(pChunk);

		const void* const pData = pChunk->GetBytes();
		const int64_t size = pChunk->Size();

		if (ok) ok = pStream->write(pStream, pData, size) == size;
	}

	_this->mMutex.Leave();
	return ok;
}

bool CLAP_ABI IPlugCLAP::ClapStateLoad(const clap_plugin* const pPlug, const clap_istream* const pStream)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	ByteChunk* const pChunk = &_this->mState;

	const int maxSize = pChunk->AllocSize();
	bool ok = true;

	if (pChunk->Size() < maxSize)
	{
		pChunk->Resize(maxSize);
		ok = pChunk->Size() == maxSize;
	}

	if (ok)
	{
		const int64_t size = pStream->read(pStream, pChunk->GetBytes(), maxSize);
		ok = size >= 0;

		if (ok)
		{
			pChunk->Resize((int)size);
			ok = pChunk->Size() == size;
		}
	}

	if (ok)
	{
		const int pos = _this->UnserializeState(pChunk, 0);
		ok = pos >= 0;

		_this->OnParamReset();
	}

	if (ok) _this->RedrawParamControls();

	_this->mMutex.Leave();
	return ok;
}

uint32_t CLAP_ABI IPlugCLAP::ClapAudioPortsCount(const clap_plugin* const pPlug, const bool isInput)
{
	const IPlugCLAP* const _this = (const IPlugCLAP*)pPlug->plugin_data;
	return !!NInOutChannels(_this, isInput);
}

bool CLAP_ABI IPlugCLAP::ClapAudioPortsGet(const clap_plugin* const pPlug, const uint32_t idx, const bool isInput, clap_audio_port_info* const pInfo)
{
	const IPlugCLAP* const _this = (const IPlugCLAP*)pPlug->plugin_data;
	const char* type;

	const int nChannels = NInOutChannels(_this, isInput);
	const uint32_t nPorts = !!nChannels;

	if (!(idx < nPorts)) return false;

	switch (nChannels)
	{
		case 1: type = CLAP_PORT_MONO; break;
		case 2: type = CLAP_PORT_STEREO; break;
		default: type = NULL; break;
	}

	pInfo->id = 0;
	GetInOutName(isInput, pInfo->name, sizeof(pInfo->name));

	pInfo->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS | CLAP_AUDIO_PORT_PREFERS_64BITS | CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
	pInfo->channel_count = nChannels;
	pInfo->port_type = type;
	pInfo->in_place_pair = CLAP_INVALID_ID;

	return true;
}

uint32_t CLAP_ABI IPlugCLAP::ClapLatencyGet(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const uint32_t latency = _this->GetLatency();

	_this->mMutex.Leave();
	return latency;
}

bool CLAP_ABI IPlugCLAP::ClapRenderSet(const clap_plugin* const pPlug, const clap_plugin_render_mode mode)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	bool ret = false;

	switch (mode)
	{
		case CLAP_RENDER_REALTIME:
		{
			_this->mPlugFlags &= ~kPlugFlagsOffline;
			ret = true;
			break;
		}

		case CLAP_RENDER_OFFLINE:
		{
			_this->mPlugFlags |= kPlugFlagsOffline;
			ret = true;
			break;
		}
	}

	_this->mMutex.Leave();
	return ret;
}

uint32_t CLAP_ABI IPlugCLAP::ClapNotePortsCount(const clap_plugin* const pPlug, const bool isInput)
{
	const IPlugCLAP* const _this = (const IPlugCLAP*)pPlug->plugin_data;
	return DoesMIDIInOut(_this, isInput);
}

bool CLAP_ABI IPlugCLAP::ClapNotePortsGet(const clap_plugin* const pPlug, const uint32_t idx, const bool isInput, clap_note_port_info* const pInfo)
{
	const IPlugCLAP* const _this = (const IPlugCLAP*)pPlug->plugin_data;

	const uint32_t nPorts = DoesMIDIInOut(_this, isInput);
	if (!(idx < nPorts)) return false;

	pInfo->id = 0;
	pInfo->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
	pInfo->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
	GetInOutName(isInput, pInfo->name, sizeof(pInfo->name));

	return true;
}

uint32_t CLAP_ABI IPlugCLAP::ClapNoteNameCount(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	char* const tbl = _this->mNoteNameTbl;
	int n = 0;

	for (int note = 0; note < 128; ++note)
	{
		char dummy;
		if (_this->MidiNoteName(note, &dummy, 1)) tbl[n++] = note;
	}

	_this->mNoteNameCount = n;
	_this->mMutex.Leave();

	return n;
}

bool CLAP_ABI IPlugCLAP::ClapNoteNameGet(const clap_plugin* const pPlug, const uint32_t idx, clap_note_name* const pName)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	int note;
	bool ret = false;

	if (idx < _this->mNoteNameCount)
	{
		note = _this->mNoteNameTbl[idx];
		ret = _this->MidiNoteName(note, pName->name, CLAP_NAME_SIZE);

		if (ret)
		{
			pName->port = -1;
			pName->key = note;
			pName->channel = -1;
		}
	}

	_this->mMutex.Leave();
	return ret;
}

bool CLAP_ABI IPlugCLAP::ClapGUIIsAPISupported(const clap_plugin* /* pPlug */, const char* const id, const bool isFloating)
{
	return !isFloating && !strcmp(id, sClapWindowAPI);
}

bool CLAP_ABI IPlugCLAP::ClapGUIGetPreferredAPI(const clap_plugin* /* pPlug */, const char** const pID, bool* const pIsFloating)
{
	*pID = sClapWindowAPI;
	*pIsFloating = false;
	return true;
}

bool CLAP_ABI IPlugCLAP::ClapGUICreate(const clap_plugin* const pPlug, const char* const id, const bool isFloating)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	bool ret = false;

	if (_this->GetGUI() && ClapGUIIsAPISupported(pPlug, id, isFloating))
	{
		const clap_host* const pHost = _this->mClapHost;
		const clap_host_gui* const pHostGUI = (const clap_host_gui*)pHost->get_extension(pHost, CLAP_EXT_GUI);
		_this->mRequestResize = pHostGUI ? pHostGUI->request_resize : NULL;
		ret = true;
	}

	return ret;
}

void CLAP_ABI IPlugCLAP::ClapGUIDestroy(const clap_plugin* const pPlug)
{
	ClapGUIHide(pPlug);

	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;

	_this->mGUIParent = NULL;
	_this->mGUIWidth = _this->mGUIHeight = 0;
}

bool CLAP_ABI IPlugCLAP::ClapGUIGetSize(const clap_plugin* const pPlug, uint32_t* const pWidth, uint32_t* const pHeight)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	const IGraphics* const pGraphics = _this->GetGUI();
	bool ret = false;

	if (pGraphics)
	{
		int w = _this->mGUIWidth, h = _this->mGUIHeight;

		if (!(w & h))
		{
			#ifdef __APPLE__
			static const int scale = IGraphicsMac::kScaleOS;
			#else
			const int scale = pGraphics->Scale();
			#endif

			w = pGraphics->Width() >> scale;
			h = pGraphics->Height() >> scale;
		}

		*pWidth = w;
		*pHeight = h;

		ret = true;
	}

	_this->mMutex.Leave();
	return ret;
}

bool CLAP_ABI IPlugCLAP::ClapGUISetParent(const clap_plugin* const pPlug, const clap_window* const pWindow)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mGUIParent = pWindow->ptr;
	return true;
}

bool CLAP_ABI IPlugCLAP::ClapGUIShow(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	IGraphics* const pGraphics = _this->GetGUI();
	bool ret = false;

	if (pGraphics && (pGraphics->WindowIsOpen() || pGraphics->OpenWindow(_this->mGUIParent)))
	{
		_this->OnGUIOpen();
		ret = true;
	}

	_this->mMutex.Leave();
	return ret;
}

bool CLAP_ABI IPlugCLAP::ClapGUIHide(const clap_plugin* const pPlug)
{
	IPlugCLAP* const _this = (IPlugCLAP*)pPlug->plugin_data;
	_this->mMutex.Enter();

	IGraphics* const pGraphics = _this->GetGUI();
	bool ret = false;

	if (pGraphics && pGraphics->WindowIsOpen())
	{
		_this->OnGUIClose();
		pGraphics->CloseWindow();
		ret = true;
	}

	_this->mMutex.Leave();
	return ret;
}
