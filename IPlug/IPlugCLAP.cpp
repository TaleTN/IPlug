#include "IPlugCLAP.h"

#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"

extern "C" {

// See IPlug_include_in_plug_src.h.
uint32_t CLAP_ABI ClapFactoryGetPluginCount(const clap_plugin_factory* pFactory);
const clap_plugin_descriptor* CLAP_ABI ClapFactoryGetPluginDescriptor(const clap_plugin_factory* pFactory, uint32_t index);
const clap_plugin* CLAP_ABI ClapFactoryCreatePlugin(const clap_plugin_factory* pFactory, const clap_host* pHost, const char* id);

} // extern "C"

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

	SetBlockSize(kDefaultBlockSize);
	mClapHost = (const clap_host*)instanceInfo;
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
			case CLAP_EVENT_PARAM_VALUE:
			{
				ProcessParamEvent((const clap_event_param_value*)pEvent);
				break;
			}
		}
	}
}

void IPlugCLAP::ProcessParamEvent(const clap_event_param_value* const pEvent)
{
	const int idx = pEvent->param_id;
	if (!NParams(idx)) return;

	IParam* const pParam = GetParam(idx);
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

	OnParamChange(idx);
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

	if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
	{
		static const clap_plugin_audio_ports audioPorts =
		{
			ClapAudioPortsCount,
			ClapAudioPortsGet
		};

		return &audioPorts;
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
	pInfo->cookie = NULL;

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

void CLAP_ABI IPlugCLAP::ClapParamsFlush(const clap_plugin* const pPlug, const clap_input_events* const pInEvents, const clap_output_events* /* pOutEvents */)
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

	_this->mMutex.Leave();
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
