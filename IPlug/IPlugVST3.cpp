#include "IPlugVST3.h"
#include "VST3_SDK/public.sdk/source/vst/vstaudioprocessoralgo.h"

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
}

tresult IPlugVST3::VSTInitialize(FUnknown* /* context */)
{
	mMutex.Enter();

	HostSpecificInit();
	OnParamReset();

	mMutex.Leave();
	return kResultOk;
}
