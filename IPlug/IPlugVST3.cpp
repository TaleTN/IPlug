#include "IPlugVST3.h"
#include "VST3_SDK/public.sdk/source/vst/vstaudioprocessoralgo.h"

#include <assert.h>

using namespace Steinberg;

class IPlugVST3_Effect:
	public Vst::SingleComponentEffect
{
public:
	IPlugVST3_Effect():
		mPlug(NULL)
	{}

	virtual ~IPlugVST3_Effect()
	{
		delete mPlug;
	}

	OBJ_METHODS(IPlugVST3_Effect, Vst::SingleComponentEffect)
	REFCOUNT_METHODS(Vst::SingleComponentEffect)

	tresult PLUGIN_API initialize(FUnknown* const context) SMTG_OVERRIDE
	{
		const tresult result = Vst::SingleComponentEffect::initialize(context);
		if (result != kResultOk) return result;

		const bool doesMidiIn = mPlug->VSTDoesMidiIn();

		if (mPlug->NInChannels()) addAudioInput(STR16("Stereo Input"), Vst::SpeakerArr::kStereo);
		if (mPlug->NOutChannels()) addAudioOutput(STR16("Stereo Output"), Vst::SpeakerArr::kStereo);

		if (doesMidiIn) addEventInput(STR16("MIDI Input"), 16);

		return mPlug->VSTInitialize(context);
	}

	tresult PLUGIN_API setActive(const TBool state) SMTG_OVERRIDE
	{
		return mPlug->VSTSetActive(state);
	}

	tresult PLUGIN_API canProcessSampleSize(const int32 symbolicSampleSize) SMTG_OVERRIDE
	{
		return (unsigned int)symbolicSampleSize <= Vst::kSample64 ? kResultTrue : kResultFalse;
	}

	tresult PLUGIN_API setupProcessing(Vst::ProcessSetup& setup) SMTG_OVERRIDE
	{
		const tresult result = Vst::SingleComponentEffect::setupProcessing(setup);
		return result == kResultOk ? mPlug->VSTSetupProcessing(setup) : result;
	}

	tresult PLUGIN_API process(Vst::ProcessData& data) SMTG_OVERRIDE
	{
		return mPlug->VSTProcess(data);
	}

	inline void setIPlugVST3(IPlugVST3* const pPlug) { mPlug = pPlug; }

private:
	IPlugVST3* mPlug;
};

FUnknown* IPlugVST3::VSTCreateInstance(void* /* context */)
{
	IPlugVST3_Effect* const pEffect = new IPlugVST3_Effect();
	pEffect->setIPlugVST3(MakeIPlugVST3(pEffect));
	return (Vst::IAudioProcessor*)pEffect;
}

IPlugVST3::IPlugVST3(
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
	mEffect(instanceInfo)
{
	const int nInputs = NInChannels(), nOutputs = NOutChannels();

	// Default everything to connected, then disconnect pins if the host says to.
	SetInputChannelConnections(0, nInputs, true);
	SetOutputChannelConnections(0, nOutputs, true);

	SetBlockSize(kDefaultBlockSize);
}

tresult IPlugVST3::VSTInitialize(FUnknown* /* context */)
{
	mMutex.Enter();

	HostSpecificInit();
	OnParamReset();

	mMutex.Leave();
	return kResultOk;
}

tresult IPlugVST3::VSTSetActive(const TBool state)
{
	mMutex.Enter();

	const bool active = !!state;
	const int flags = mPlugFlags;

	if (!(flags & kPlugFlagsActive) == active)
	{
		mPlugFlags = flags ^ kPlugFlagsActive;
		OnActivate(active);
	}

	mMutex.Leave();
	return kResultOk;
}

tresult IPlugVST3::VSTSetupProcessing(Vst::ProcessSetup& setup)
{
	mMutex.Enter();

	const int flags = mPlugFlags;
	bool reset = false;

	if (setup.sampleRate != GetSampleRate() || !(flags & kPlugInitSampleRate))
	{
		SetSampleRate(setup.sampleRate);
		reset = true;
	}

	if (setup.maxSamplesPerBlock != GetBlockSize() || !(flags & kPlugInitBlockSize))
	{
		SetBlockSize(setup.maxSamplesPerBlock);
		reset = true;
	}

	if (reset)
	{
		mPlugFlags = flags | kPlugInit;
		Reset();
	}

	mMutex.Leave();
	return kResultOk;
}

tresult IPlugVST3::VSTProcess(Vst::ProcessData& data)
{
	mMutex.Enter();

	const bool is64bits = data.symbolicSampleSize != Vst::kSample32;
	const int32 nFrames = data.numSamples;

	static const Vst::AudioBusBuffers emptyBus;
	assert(emptyBus.numChannels == 0);

	const Vst::AudioBusBuffers* const inBus = data.numInputs ? &data.inputs[0] : &emptyBus;
	const Vst::AudioBusBuffers* const outBus = data.numOutputs ? &data.outputs[0] : &emptyBus;

	Vst::IEventList* const pInputEvents = data.inputEvents;
	if (pInputEvents)
	{
		const int32 nEvents = pInputEvents->getEventCount();
		if (nEvents) ProcessInputEvents(pInputEvents, nEvents);
	}

	const int nInputs = NInChannels(), nOutputs = NOutChannels();

	if (inBus->numChannels >= nInputs && outBus->numChannels >= nOutputs)
	{
		if (is64bits)
		{
			AttachInputBuffers(0, nInputs, inBus->channelBuffers64, nFrames);
			AttachOutputBuffers(0, nOutputs, outBus->channelBuffers64);
			ProcessBuffers((double)0.0, nFrames);
		}
		else
		{
			AttachInputBuffers(0, nInputs, inBus->channelBuffers32, nFrames);
			AttachOutputBuffers(0, nOutputs, outBus->channelBuffers32);
			ProcessBuffers((float)0.0f, nFrames);
		}
	}

	mMutex.Leave();
	return kResultOk;
}

void IPlugVST3::ProcessInputEvents(Vst::IEventList* const pInputEvents, const int32 nEvents)
{
	for (int32 i = 0; i < nEvents; ++i)
	{
		Vst::Event event;
		if (pInputEvents->getEvent(i, event) != kResultOk) continue;

		const int32 busIndex = event.busIndex, ofs = event.sampleOffset;
		if (busIndex != 0) continue;

		switch (event.type)
		{
			case Vst::Event::kNoteOnEvent:
			{
				int velocity = (int)(event.noteOn.velocity * 127.0f + 0.5f);
				velocity = wdl_max(velocity, 1);

				const IMidiMsg msg(ofs, 0x90 | event.noteOn.channel, event.noteOn.pitch, velocity);
				ProcessMidiMsg(&msg);
				break;
			}

			case Vst::Event::kNoteOffEvent:
			{
				const int velocity = (int)(event.noteOff.velocity * 127.0f + 0.5f);

				const IMidiMsg msg(ofs, 0x80 | event.noteOff.channel, event.noteOff.pitch, velocity);
				ProcessMidiMsg(&msg);
				break;
			}

			case Vst::Event::kPolyPressureEvent:
			{
				const int pressure = (int)(event.polyPressure.pressure * 127.0f + 0.5f);

				const IMidiMsg msg(ofs, 0xA0 | event.polyPressure.channel, event.polyPressure.pitch, pressure);
				ProcessMidiMsg(&msg);
				break;
			}
		}
	}
}
