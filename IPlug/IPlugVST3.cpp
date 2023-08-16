#include "IPlugVST3.h"

#include "VST3_SDK/pluginterfaces/vst/ivstmidicontrollers.h"
#include "VST3_SDK/public.sdk/source/vst/vstaudioprocessoralgo.h"

#include <assert.h>
#include "WDL/wdlcstring.h"

using namespace Steinberg;

static const Vst::ParamID kMidiCtrlParamID = 0x40000000;
static const int kNumMidiCtrlParams = 16*256; // >= 16*(128 + 3)

#ifndef IPLUG_NO_MIDI_CC_PARAMS

static Vst::ParamID GetMidiCtrlParamID(const int ch, const int cc)
{
	return (ch << 8) | cc | kMidiCtrlParamID;
}

static inline int GetMidiCtrlParamIdx(const Vst::ParamID id)
{
	return id ^ kMidiCtrlParamID;
}

static inline int GetMidiCtrlNo(const Vst::ParamID id)
{
	return id & 0xFF;
}

static int GetMidiChName(Vst::TChar* const name)
{
	strcpy16(name, STR16("MIDI Channel "));
	static const int len = 13; // strlen16(name)
	return len;
}

#endif // IPLUG_NO_MIDI_CC_PARAMS

class IPlugVST3_Effect:
	public Vst::SingleComponentEffect

	#ifndef IPLUG_NO_MIDI_CC_PARAMS
	, public Vst::IMidiMapping
	#endif
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

	#ifndef IPLUG_NO_MIDI_CC_PARAMS
	DEFINE_INTERFACES
		DEF_INTERFACE(Vst::IMidiMapping)
	END_DEFINE_INTERFACES(Vst::SingleComponentEffect)
	#endif

	REFCOUNT_METHODS(Vst::SingleComponentEffect)

	tresult PLUGIN_API initialize(FUnknown* const context) SMTG_OVERRIDE
	{
		const tresult result = Vst::SingleComponentEffect::initialize(context);
		if (result != kResultOk) return result;

		const bool doesMidiIn = mPlug->VSTDoesMidiIn();

		if (mPlug->NInChannels()) addAudioInput(STR16("Stereo Input"), Vst::SpeakerArr::kStereo);
		if (mPlug->NOutChannels()) addAudioOutput(STR16("Stereo Output"), Vst::SpeakerArr::kStereo);

		if (doesMidiIn)
		{
			addEventInput(STR16("MIDI Input"), 16);

			#ifndef IPLUG_NO_MIDI_CC_PARAMS
			addMidiChUnits();
			addMidiCCParams();
			#endif
		}

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

	#ifndef IPLUG_NO_MIDI_CC_PARAMS

	tresult PLUGIN_API getMidiControllerAssignment(const int32 busIndex, const int16 channel,
		const Vst::CtrlNumber midiControllerNumber, Vst::ParamID& id) SMTG_OVERRIDE
	{
		tresult result = kResultFalse;

		if (busIndex == 0 && (unsigned int)midiControllerNumber < Vst::kCountCtrlNumber && mPlug->VSTDoesMidiIn())
		{
			id = GetMidiCtrlParamID(channel, midiControllerNumber);
			result = kResultTrue;
		}

		return result;
	}

	int32 PLUGIN_API getProgramListCount() SMTG_OVERRIDE
	{
		return mPlug->VSTDoesMidiIn() ? 16 : 0;
	}

	tresult PLUGIN_API getProgramListInfo(int32 listIndex, Vst::ProgramListInfo& info) SMTG_OVERRIDE
	{
		tresult result = kResultFalse;

		if ((unsigned int)listIndex < 16)
		{
			info.id = ++listIndex;

			const int len = GetMidiChName(info.name);
			char str8[4];

			snprintf(str8, sizeof(str8), "%d", listIndex);
			str8ToStr16(&info.name[len], str8);

			info.programCount = 128;
			result = kResultTrue;
		}

		return result;
	}

	tresult PLUGIN_API getProgramName(const Vst::ProgramListID listId, const int32 programIndex,
		Vst::String128 name) SMTG_OVERRIDE
	{
		tresult result = kResultFalse;

		if ((unsigned int)listId < 16 && (unsigned int)programIndex < 128)
		{
			strcpy16(name, STR16("Program "));
			static const int len = 8; // strlen16("Program ")
			char str8[4];

			snprintf(str8, sizeof(str8), "%d", programIndex + 1);
			str8ToStr16(&name[len], str8);

			result = kResultTrue;
		}

		return result;
	}

	tresult PLUGIN_API getUnitByBus(const Vst::MediaType type, const Vst::BusDirection dir,
		const int32 busIndex, const int32 channel, Vst::UnitID& unitId) SMTG_OVERRIDE
	{
		tresult result = kResultFalse;

		if (type == Vst::kEvent && dir == Vst::kInput && busIndex == 0 && (unsigned int)channel < 16 && mPlug->VSTDoesMidiIn())
		{
			unitId = channel + 1;
			result = kResultTrue;
		}

		return result;
	}

	#endif // IPLUG_NO_MIDI_CC_PARAMS

	inline void setIPlugVST3(IPlugVST3* const pPlug) { mPlug = pPlug; }

private:
	#ifndef IPLUG_NO_MIDI_CC_PARAMS

	void addMidiChUnits()
	{
		Vst::UnitInfo info;
		info.parentUnitId = Vst::kRootUnitId;

		const int len = GetMidiChName(info.name);
		char str8[4];

		for (int ch = 1; ch <= 16; ++ch)
		{
			info.id = ch;

			snprintf(str8, sizeof(str8), "%d", ch);
			str8ToStr16(&info.name[len], str8);

			info.programListId = ch;
			addUnit(new Vst::Unit(info));
		}
	}

	void addMidiCCParams()
	{
		static const Vst::TChar* const chModeStr[8] =
		{
			STR16("All Sound Off"),
			STR16("Reset All Controllers"),
			STR16("Local Control"),
			STR16("All Notes Off"),
			STR16("Omni Mode Off"),
			STR16("Omni Mode On"),
			STR16("Mono Mode On"),
			STR16("Poly Mode On")
		};

		Vst::ParameterInfo info;
		info.units[0] = 0;

		char str8[8];
		Vst::TChar str16[sizeof(str8)];

		for (int ch = 0; ch < 16; ++ch)
		{
			info.stepCount = 0;
			info.defaultNormalizedValue = 0.0;
			info.unitId = Vst::kRootUnitId;
			info.flags = Vst::ParameterInfo::kNoFlags;

			for (int cc = 0; cc <= Vst::kCountCtrlNumber; ++cc)
			{
				info.id = GetMidiCtrlParamID(ch, cc);

				switch (cc)
				{
					case Vst::kAfterTouch:
					{
						strcpy16(info.title, STR16("Aftertouch"));
						strcpy16(info.shortTitle, STR16("AT"));
						break;
					}

					case Vst::kPitchBend:
					{
						strcpy16(info.title, STR16("Pitch Bend"));
						strcpy16(info.shortTitle, STR16("PB"));

						info.defaultNormalizedValue = 0.5;
						break;
					}

					case Vst::kCtrlProgramChange:
					{
						strcpy16(info.title, STR16("Program Change"));
						strcpy16(info.shortTitle, STR16("PC"));

						info.stepCount = 127;
						info.defaultNormalizedValue = 0.0;
						info.unitId = ch + 1;
						info.flags = Vst::ParameterInfo::kIsList | Vst::ParameterInfo::kIsProgramChange;
						break;
					}

					default:
					{
						snprintf(str8, sizeof(str8), "%d", cc);

						strcpy16(info.shortTitle, STR16("CC#"));
						str8ToStr16(&info.shortTitle[3], str8);

						const unsigned int chMode = cc - Vst::kCtrlAllSoundsOff;
						strcpy16(info.title, chMode >= 8 ? info.shortTitle : chModeStr[chMode]);
						break;
					}
				}

				snprintf(str8, sizeof(str8), " [%d]", ch + 1);
				str8ToStr16(str16, str8);

				strcat16(info.title, str16);
				strcat16(info.shortTitle, str16);

				parameters.addParameter(info);
			}
		}
	}

	#endif // IPLUG_NO_MIDI_CC_PARAMS

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

	Vst::IParameterChanges* const pParamChanges = data.inputParameterChanges;
	const int32 nChanges = pParamChanges->getParameterCount();

	if (nChanges) ProcessParamChanges(pParamChanges, nChanges);

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

void IPlugVST3::ProcessParamChanges(Vst::IParameterChanges* const pParamChanges, const int32 nChanges)
{
	for (int32 i = 0; i < nChanges; ++i)
	{
		Vst::IParamValueQueue* const pParamQueue = pParamChanges->getParameterData(i);
		if (!pParamQueue) continue;

		const Vst::ParamID id = pParamQueue->getParameterId();
		const int32 nPoints = pParamQueue->getPointCount();

		Vst::ParamValue value;
		int32 ofs;

		#ifndef IPLUG_NO_MIDI_CC_PARAMS
		const int idx = GetMidiCtrlParamIdx(id);
		if (idx < kNumMidiCtrlParams)
		{
			const int ch = (unsigned int)idx >> 8;
			const int cc = GetMidiCtrlNo(id);

			for (int32 j = 0; j < nPoints; ++j)
			{
				if (pParamQueue->getPoint(j, ofs, value) != kResultOk) continue;

				switch (cc)
				{
					case Vst::kAfterTouch:
					{
						const IMidiMsg msg(ofs, 0xD0 | ch, (int)(value * 127.0 + 0.5));
						ProcessMidiMsg(&msg);
						break;
					}

					case Vst::kPitchBend:
					{
						const int pb = (int)(value * 16383.0 + 0.5);
						const IMidiMsg msg(ofs, 0xE0 | ch, pb & 127, (unsigned int)pb >> 7);
						ProcessMidiMsg(&msg);
						break;
					}

					case Vst::kCtrlProgramChange:
					{
						const IMidiMsg msg(ofs, 0xC0 | ch, (int)(value * 127.0 + 0.5));
						ProcessMidiMsg(&msg);
						break;
					}

					default:
					{
						if (cc > 127) break;

						const IMidiMsg msg(ofs, 0xB0 | ch, cc, (int)(value * 127.0 + 0.5));
						ProcessMidiMsg(&msg);
						break;
					}
				}
			}
		}
		#endif // IPLUG_NO_MIDI_CC_PARAMS
	}
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
