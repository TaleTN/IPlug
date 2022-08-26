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
	return NULL;
}
