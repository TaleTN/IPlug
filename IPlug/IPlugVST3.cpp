#include "IPlugVST3.h"
#include "IGraphics.h"
#include "IControl.h"

#include "VST3_SDK/base/source/fstreamer.h"
#include "VST3_SDK/pluginterfaces/base/iplugincompatibility.h"
#include "VST3_SDK/public.sdk/source/vst/vstaudioprocessoralgo.h"

#ifndef IPLUG_NO_MIDI_CC_PARAMS
	#include "VST3_SDK/pluginterfaces/vst/ivstmidicontrollers.h"
#endif

#ifndef IPLUG_NO_VST3_VST2_COMPAT
	#include "VST3_SDK/public.sdk/source/vst/utility/vst2persistence.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"
#include "WDL/wdlutf8.h"

using namespace Steinberg;

static const size_t kVstString128Count = sizeof(Vst::String128) / sizeof(Vst::TChar);

static const Vst::ParamID kMidiCtrlParamID = 0x40000000;
static const int kNumMidiCtrlParams = 16*256; // >= 16*(128 + 3)

static const Vst::ParamID kBypassParamID = kMidiCtrlParamID - 1;

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

static void Str16ToMBStr(char* const dest, const Vst::TChar* const src, const int destSize)
{
	for (int i = 0, j = 0;;)
	{
		const int c = src[i++];
		const int n = wdl_utf8_makechar(c, &dest[j], destSize - j);

		if (!c) break;

		if (n > 0)
		{
			const int k = j + n;
			if (k < destSize)
			{
				j = k;
				continue;
			}
		}
		else if (n < 0)
		{
			continue;
		}

		dest[j] = 0;
		break;
	}
}

#ifndef IPLUG_NO_VST3_VST2_COMPAT

static const int IPLUG_VERSION_MAGIC = 'pfft';

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

#endif // IPLUG_NO_VST3_VST2_COMPAT

static int GetMouseCapture(const IGraphics* const pGraphics)
{
	int paramIdx = -1;

	if (pGraphics)
	{
		const int controlIdx = pGraphics->GetMouseCapture();
		if (controlIdx >= 0)
		{
			paramIdx = pGraphics->GetControl(controlIdx)->ParamIdx();
		}
	}

	return paramIdx;
}

class IPlugVST3_View: public Vst::EditorView
{
public:
	IPlugVST3_View(
		IGraphics* const pGraphics,
		Vst::EditController* const controller,
		ViewRect* const size = nullptr
	):
		Vst::EditorView(controller, size),
		mGraphics(pGraphics)
	{}

	tresult PLUGIN_API isPlatformTypeSupported(FIDString const type) SMTG_OVERRIDE
	{
		#ifdef _WIN32
		if (FIDStringsEqual(type, kPlatformTypeHWND)) return kResultTrue;
		#elif defined(__APPLE__)
		if (FIDStringsEqual(type, kPlatformTypeNSView)) return kResultTrue;
		#endif

		return kInvalidArgument;
	}

	tresult PLUGIN_API onWheel(const float distance) SMTG_OVERRIDE
	{
		return mGraphics->ProcessMouseWheel(distance) ? kResultTrue : kResultFalse;
	}

	inline void* getSystemWindow() const { return systemWindow; }

private:
	IGraphics* const mGraphics;
};

class IPlugVST3_Effect:
	public Vst::SingleComponentEffect

	#ifndef IPLUG_NO_MIDI_CC_PARAMS
	, public Vst::IMidiMapping
	#endif
{
public:
	IPlugVST3_Effect():
		mPlug(NULL),
		mView(NULL)
	{
		processContextRequirements.flags |= kNeedTempo | kNeedTimeSignature;
	}

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
			#endif
		}

		addPlugParams();
		addBypassParam();

		#ifndef IPLUG_NO_MIDI_CC_PARAMS
		if (doesMidiIn) addMidiCCParams();
		#endif

		return mPlug->VSTInitialize(context);
	}

	tresult PLUGIN_API getParamStringByValue(const Vst::ParamID id, const Vst::ParamValue valueNormalized,
		Vst::String128 string) SMTG_OVERRIDE
	{
		return mPlug->VSTGetParamStringByValue(id, valueNormalized, string);
	}

	tresult PLUGIN_API getParamValueByString(const Vst::ParamID id, Vst::TChar* const string,
		Vst::ParamValue& valueNormalized) SMTG_OVERRIDE
	{
		return mPlug->VSTGetParamValueByString(id, string, valueNormalized);
	}

	IPlugView* PLUGIN_API createView(FIDString const name) SMTG_OVERRIDE
	{
		IPlugView* editor = nullptr;

		if (FIDStringsEqual(name, Vst::ViewType::kEditor))
		{
			editor = mPlug->VSTCreateView(name);
			mView = (IPlugVST3_View*)editor;
		}

		return editor;
	}

	void editorAttached(Vst::EditorView* const editor) SMTG_OVERRIDE
	{
		IPlugVST3_View* const pView = (IPlugVST3_View*)editor;
		IGraphics* const pGraphics = mPlug->GetGUI();
		if (pGraphics && pGraphics->OpenWindow(pView->getSystemWindow()))
		{
			mPlug->OnGUIOpen();
		}
	}

	void editorRemoved(Vst::EditorView* const editor) SMTG_OVERRIDE
	{
		IGraphics* const pGraphics = mPlug->GetGUI();
		if (pGraphics)
		{
			mPlug->OnGUIClose();
			pGraphics->CloseWindow();
		}
	}

	tresult PLUGIN_API setActive(const TBool state) SMTG_OVERRIDE
	{
		return mPlug->VSTSetActive(state);
	}

	tresult PLUGIN_API setState(IBStream* const state) SMTG_OVERRIDE
	{
		#ifndef IPLUG_NO_VST3_VST2_COMPAT
		VST3::Optional<VST3::Vst2xState> vst2State = VST3::tryVst2StateLoad(*state);
		if (vst2State)
		{
			return mPlug->VSTSetState(vst2State->chunk.data(), vst2State->chunk.size(), vst2State->currentProgram, vst2State->isBypassed);
		}
		#endif

		return mPlug->VSTSetState(state);
	}

	tresult PLUGIN_API getState(IBStream* const state) SMTG_OVERRIDE
	{
		return mPlug->VSTGetState(state);
	}

	tresult PLUGIN_API canProcessSampleSize(const int32 symbolicSampleSize) SMTG_OVERRIDE
	{
		return (unsigned int)symbolicSampleSize <= Vst::kSample64 ? kResultTrue : kResultFalse;
	}

	uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE
	{
		return mPlug->GetLatency();
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
	inline IPlugVST3_View* getEditor() { return mView; }

	void updateParamNormalized(const Vst::ParamID id, const Vst::ParamValue value)
	{
		getParameterObject(id)->setNormalized(value);
		performEdit(id, value);
	}

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

	#endif // IPLUG_NO_MIDI_CC_PARAMS

	void addBypassParam()
	{
		Vst::ParameterInfo info;
		info.id = kBypassParamID;

		strcpy16(info.title, STR16("Host Bypass"));
		strcpy16(info.shortTitle, STR16("Bypass"));

		info.units[0] = 0;
		info.stepCount = 1;
		info.defaultNormalizedValue = 0.0;
		info.unitId = Vst::kRootUnitId;
		info.flags = Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass;

		parameters.addParameter(info);
	}

	void addPlugParams()
	{
		Vst::ParameterInfo info;

		const int n = mPlug->NParams();
		for (int i = 0; i < n; ++i)
		{
			const IParam* const pParam = mPlug->GetParam(i);

			info.id = i;
			static const int32 maxLen = (int32)kVstString128Count - 1;

			str8ToStr16(info.title, pParam->GetNameForHost(), maxLen);
			str8ToStr16(info.shortTitle, pParam->GetNameForHost(8), maxLen);
			str8ToStr16(info.units, pParam->GetLabelForHost(), maxLen);

			int32 flags = Vst::ParameterInfo::kCanAutomate;

			switch (pParam->Type())
			{
				case IParam::kTypeBool:
				{
					info.stepCount = 1;
					break;
				}

				case IParam::kTypeEnum:
				{
					const IEnumParam* const pEnumParam = (const IEnumParam*)pParam;
					info.stepCount = pEnumParam->NEnums() - 1;
					flags |= info.stepCount > 1 ? Vst::ParameterInfo::kIsList : 0;
					break;
				}

				default:
				{
					info.stepCount = 0;
					break;
				}
			}

			info.defaultNormalizedValue = pParam->GetNormalized();
			info.unitId = Vst::kRootUnitId;
			info.flags = flags;

			parameters.addParameter(info);
		}
	}

	#ifndef IPLUG_NO_MIDI_CC_PARAMS

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
	IPlugVST3_View* mView;
};

class IPlugVST3_Compatibility: public FObject, public IPluginCompatibility
{
public:
	OBJ_METHODS(IPlugVST3_Compatibility, FObject)

	DEFINE_INTERFACES
		DEF_INTERFACE(IPluginCompatibility)
	END_DEFINE_INTERFACES(FObject)

	REFCOUNT_METHODS(FObject)

	tresult PLUGIN_API getCompatibilityJSON(IBStream* const stream) override
	{
		TUID uid;
		const char* pOld;

		const int nOld = GetIPlugVST3CompatGUIDs(uid, &pOld);
		if (!nOld) return kResultTrue;

		char buf[51];
		lstrcpyn_safe(buf, "[{\"New\":\"", sizeof(buf));

		const FUID plugUID = FUID::fromTUID(uid);
		plugUID.toString(&buf[9]);

		lstrcpyn_safe(&buf[41], "\",\"Old\":[", sizeof(buf) - 41);
		tresult result = stream->write(buf, 50);

		buf[33] = buf[0] = '"';
		buf[34] = ',';

		for (int i = 0; i < nOld && result == kResultTrue;)
		{
			memcpy(&buf[1], pOld, 32);
			pOld += 32;

			result = stream->write(buf, (++i < nOld) + 34);
		}

		memcpy(buf, "]}]", 4);
		if (result == kResultTrue) result = stream->write(buf, 3);

		return result;
	}
};

FUnknown* IPlugVST3::VSTCreateInstance(void* /* context */)
{
	IPlugVST3_Effect* const pEffect = new IPlugVST3_Effect();
	pEffect->setIPlugVST3(MakeIPlugVST3(pEffect));
	return (Vst::IAudioProcessor*)pEffect;
}

FUnknown* IPlugVST3::VSTCreateCompatibilityInstance(void* /* context */)
{
	return (IPluginCompatibility*)new IPlugVST3_Compatibility;
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

	mProcessContextState = 0;
	mSamplePos = 0;
	mTempo = 0.0;
	memset(mTimeSig, 0, sizeof(mTimeSig));

	mGUIWidth = mGUIHeight = 0;

	SetBlockSize(kDefaultBlockSize);
}

bool IPlugVST3::AllocStateChunk(int chunkSize)
{
	if (chunkSize < 0) chunkSize = GetParamsChunkSize(0, NParams());
	chunkSize += sizeof(WDL_UINT64); // Bypass
	return mState.Alloc(chunkSize) == chunkSize;
}

bool IPlugVST3::AllocBankChunk(const int chunkSize)
{
	if (chunkSize < 0 && mPresetChunkSize < 0) AllocPresetChunk();
	return true;
}

void IPlugVST3::OnParamReset()
{
	IPlugBase::OnParamReset();

	int n = NParams();
	IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;

	const int m = pEffect->getParameterCount();
	n = wdl_min(n, m);

	for (int i = 0; i < n; ++i)
	{
		pEffect->getParameterObject(i)->setNormalized(GetParam(i)->GetNormalized());
	}

	Vst::IComponentHandler* const componentHandler = pEffect->getComponentHandler();
	if (componentHandler) componentHandler->restartComponent(Vst::kParamValuesChanged);
}

void IPlugVST3::BeginInformHostOfParamChange(const int idx, const bool lockMutex)
{
	if (lockMutex) mMutex.Enter();

	EndDelayedInformHostOfParamChange(false);

	IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;
	pEffect->beginEdit(idx);

	if (lockMutex) mMutex.Leave();
}

void IPlugVST3::InformHostOfParamChange(const int idx, double normalizedValue, const bool lockMutex)
{
	const int mouseCap = GetMouseCapture(GetGUI());

	if (lockMutex) mMutex.Enter();

	// TN: While dragging GUI control use clamped parameter value rather
	// than continuous GUI value; fixes automation for stepped values.
	if (idx == mouseCap) normalizedValue = GetParam(idx)->GetNormalized();

	IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;
	pEffect->updateParamNormalized(idx, normalizedValue);

	if (lockMutex) mMutex.Leave();
}

void IPlugVST3::EndInformHostOfParamChange(const int idx, const bool lockMutex)
{
	if (lockMutex) mMutex.Enter();

	IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;
	pEffect->endEdit(idx);

	if (lockMutex) mMutex.Leave();
}

double IPlugVST3::GetSamplePos()
{
	return (double)wdl_max(mSamplePos, 0);
}

double IPlugVST3::GetTempo()
{
	double tempo = 0.0;

	if (mProcessContextState & Vst::ProcessContext::kTempoValid)
	{
		tempo = wdl_max(mTempo, 0.0);
	}

	return tempo;
}

void IPlugVST3::GetTimeSig(int* const pNum, int* const pDenom)
{
	int num = 0, denom = 0;

	if (mProcessContextState & Vst::ProcessContext::kTimeSigValid)
	{
		const int a = mTimeSig[0], b = mTimeSig[1];
		if ((a | b) >= 0)
		{
			num = a;
			denom = b;
		}
	}

	*pNum = num;
	*pDenom = denom;
}

void IPlugVST3::ResizeGraphics(const int w, const int h)
{
	IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;

	// BD: "I used to care, but..."
	const bool thingsHaveChanged = mGUIWidth != w || mGUIHeight != h;

	mGUIWidth = w;
	mGUIHeight = h;

	if (thingsHaveChanged && GetGUI())
	{
		ViewRect newSize(0, 0, w, h);
		pEffect->getEditor()->onSize(&newSize);
	}
}

tresult IPlugVST3::VSTInitialize(FUnknown* const context)
{
	Vst::IHostApplication* pHost;
	if (context->queryInterface(Vst::IHostApplication::iid, (void**)&pHost) == kResultOk)
	{
		Vst::String128 name;
		if (pHost->getName(name) == kResultTrue)
		{
			char str8[kVstString128Count];
			Str16ToMBStr(str8, name, (int)kVstString128Count);

			// TN: Vst::IHostApplication doesn't provide host version.
			SetHost(str8, mHostVersion);
		}
	}

	mMutex.Enter();

	HostSpecificInit();
	OnParamReset();

	mMutex.Leave();
	return kResultOk;
}

tresult IPlugVST3::VSTGetParamStringByValue(const Vst::ParamID id, const Vst::ParamValue valueNormalized,
	Vst::String128 string)
{
	char str8[kVstString128Count];

	#ifndef IPLUG_NO_MIDI_CC_PARAMS
	if (GetMidiCtrlParamIdx(id) < kNumMidiCtrlParams)
	{
		static const char* const unipolar = "%d";
		const char* fmt;

		Vst::ParamValue scale;
		int ofs = 0;

		if (GetMidiCtrlNo(id) != Vst::kPitchBend)
		{
			fmt = unipolar;
			scale = 127.0;
		}
		else
		{
			fmt = "%+d";
			scale = 16383.0;
			ofs = -8192;
		}

		const int midiValue = (int)(valueNormalized * scale + 0.5) + ofs;
		fmt = midiValue ? fmt : unipolar;

		snprintf(str8, kVstString128Count, fmt, midiValue);
	}
	else
	#endif // IPLUG_NO_MIDI_CC_PARAMS

	if (NParams(id))
	{
		GetParam(id)->GetDisplayForHost(valueNormalized, str8, (int)kVstString128Count);
	}
	else if (id == kBypassParamID)
	{
		memcpy(string, valueNormalized >= 0.5 ? STR16("On\0") : STR16("Off"), 4 * sizeof(Vst::TChar));
		return kResultTrue;
	}
	else
	{
		return kResultFalse;
	}

	str8ToStr16(string, str8);
	return kResultTrue;
}

tresult IPlugVST3::VSTGetParamValueByString(const Vst::ParamID id, const Vst::TChar* const string,
	Vst::ParamValue& valueNormalized)
{
	char str8[kVstString128Count];
	Str16ToMBStr(str8, string, kVstString128Count);

	if (NParams(id))
	{
		const IParam* const pParam = GetParam(id);
		double v;

		const bool mapped = pParam->MapDisplayText(str8, &v);
		if (!mapped)
		{
			v = strtod(str8, NULL);
			if (pParam->DisplayIsNegated()) v = -v;
			v = pParam->GetNormalized(v);
		}

		valueNormalized = v;
		return kResultTrue;
	}
	else if (id == kBypassParamID)
	{
		const double v = strtod(str8, NULL);
		valueNormalized = (Vst::ParamValue)(v >= 0.5);
		return kResultTrue;
	}

	#ifndef IPLUG_NO_MIDI_CC_PARAMS
	else if (GetMidiCtrlParamIdx(id) < kNumMidiCtrlParams)
	{
		Vst::ParamValue scale;
		int i = atoi(str8), maxVal = 127;

		if (GetMidiCtrlNo(id) != Vst::kPitchBend)
		{
			scale = 0.0078740157480314961; // 1/127
		}
		else
		{
			scale = 6.1038881767686016e-05; // 1/16383
			i += 8192;
			maxVal = 16383;
		}

		i = wdl_min(i, maxVal);
		i = wdl_max(i, 0);

		valueNormalized = (Vst::ParamValue)i * scale;
		return kResultTrue;
	}
	#endif

	return kResultFalse;
}

IPlugView* IPlugVST3::VSTCreateView(FIDString /* name */)
{
	IGraphics* const pGraphics = GetGUI();
	if (!pGraphics) return NULL;

	IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;
	int w = mGUIWidth, h = mGUIHeight;

	if (!(w & h))
	{
		const int scale = pGraphics->Scale();
		mGUIWidth = w = pGraphics->Width() >> scale;
		mGUIHeight = h = pGraphics->Height() >> scale;
	}

	ViewRect size(0, 0, w, h);
	return new IPlugVST3_View(pGraphics, pEffect, &size);
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

#ifndef IPLUG_NO_VST3_VST2_COMPAT

tresult IPlugVST3::VSTSetState(const int8_t* const data, const size_t size, const int32_t currentProgram, const bool isBypassed)
{
	mMutex.Enter();

	ByteChunk* const pChunk = &mState;
	tresult ok = kResultOk;
	int pos;

	if (pChunk->Size() != size)
	{
		pChunk->Resize((int)size);
		ok = pChunk->Size() == size ? ok : kResultFalse;
	}

	if (ok == kResultOk)
	{
		memcpy(pChunk->GetBytes(), data, size);

		pos = 0;
		const int iplugVer = GetIPlugVerFromChunk(pChunk, &pos);
		ok = iplugVer >= 0x010000 ? ok : kResultFalse;
	}

	if (ok == kResultOk)
	{
		pos = UnserializeBank(pChunk, pos);
		ok = pos >= 0 ? ok : kResultFalse;

		if (IsBypassed() != isBypassed)
		{
			mPlugFlags ^= IPlugBase::kPlugFlagsBypass;
			OnBypass(isBypassed);

			IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;
			pEffect->getParameterObject(kBypassParamID)->setNormalized((Vst::ParamValue)isBypassed);
		}

		RestorePreset(ok == kResultOk ? currentProgram : -1);
	}

	RedrawParamControls();

	mMutex.Leave();
	return ok;

}

#endif // IPLUG_NO_VST3_VST2_COMPAT

tresult IPlugVST3::VSTSetState(IBStream* const state)
{
	mMutex.Enter();

	IBStreamer streamer(state);
	const int64 size = streamer.seek(0, kSeekEnd);
	streamer.seek(0, kSeekSet);

	ByteChunk* const pChunk = &mState;
	tresult ok = kResultOk;

	if (pChunk->Size() != size)
	{
		pChunk->Resize((int)size);
		ok = pChunk->Size() == size ? ok : kResultFalse;
	}

	if (ok == kResultOk)
	{
		const TSize n = streamer.readRaw(pChunk->GetBytes(), size);
		ok = n == size ? ok : kResultFalse;
	}

	if (ok == kResultOk)
	{
		WDL_INT64 bitmask = 1;
		int pos = pChunk->GetInt64(&bitmask, 0);
		const bool bypass = !!(bitmask & 1);

		if (IsBypassed() != bypass)
		{
			mPlugFlags ^= IPlugBase::kPlugFlagsBypass;
			OnBypass(bypass);

			IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;
			pEffect->getParameterObject(kBypassParamID)->setNormalized((Vst::ParamValue)bypass);
		}

		if (pos >= 0)
		{
			pos = UnserializeState(pChunk, pos);
			ok = pos >= 0 ? ok : kResultFalse;
			OnParamReset();
		}
	}

	RedrawParamControls();

	mMutex.Leave();
	return ok;
}

tresult IPlugVST3::VSTGetState(IBStream* const state)
{
	mMutex.Enter();

	IBStreamer streamer(state);
	ByteChunk* const pChunk = &mState;

	tresult ok = pChunk->AllocSize() || AllocStateChunk() ? kResultOk : kResultFalse;
	pChunk->Clear();

	if (ok == kResultOk)
	{
		const WDL_UINT64 bitmask = IsBypassed();

		static const int vst2PrivateChunkID = 0x57747356;
		assert((int)bitmask != vst2PrivateChunkID);

		ok = pChunk->PutInt64(bitmask) ? ok : kResultFalse;
	}

	if (ok == kResultOk)
	{
		ok = SerializeState(pChunk) ? ok : kResultFalse;

		void* const pData = pChunk->GetBytes();
		const int size = pChunk->Size();

		if (ok == kResultOk)
		{
			ok = streamer.writeRaw(pData, size) == size ? ok : kResultFalse;
		}
	}

	mMutex.Leave();
	return ok;
}

tresult IPlugVST3::VSTSetupProcessing(Vst::ProcessSetup& setup)
{
	mMutex.Enter();

	const int offline = setup.processMode == Vst::kOffline ? kPlugFlagsOffline : 0;
	const int flags = (mPlugFlags & ~kPlugFlagsOffline) | offline;

	mPlugFlags = flags;
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

	const Vst::ProcessContext* const pProcessContext = data.processContext;
	mProcessContextState = pProcessContext->state;
	mSamplePos = pProcessContext->projectTimeSamples;

	WDL_UINT64 tempo;
	memcpy(&tempo, &pProcessContext->tempo, sizeof(double));
	memcpy(&mTempo, &tempo, sizeof(double));

	memcpy(mTimeSig, &pProcessContext->timeSigNumerator, 2 * sizeof(int32));

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

	if (nChanges) FlushParamChanges(pParamChanges, nChanges);

	mMutex.Leave();
	return kResultOk;
}

void IPlugVST3::ProcessParamChanges(Vst::IParameterChanges* const pParamChanges, const int32 nChanges)
{
	// TN: While dragging GUI control ignore parameter changes; fixes
	// automation for stepped values.
	const int mouseCap = GetMouseCapture(GetGUI());

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
		else
		#endif // IPLUG_NO_MIDI_CC_PARAMS

		if (id != mouseCap && nPoints > 0 && pParamQueue->getPoint(0, ofs, value) == kResultOk)
		{
			IPlugVST3_Effect* const pEffect = (IPlugVST3_Effect*)mEffect;
			const Vst::ParamValue oldValue = pEffect->getParameterObject(id)->getNormalized();

			value = (value - oldValue) / (Vst::ParamValue)++ofs + oldValue;
			ProcessParamChange(id, value);
		}
	}
}

void IPlugVST3::ProcessParamChange(const Vst::ParamID id, const Vst::ParamValue value)
{
	if (NParams(id))
	{
		IGraphics* const pGraphics = GetGUI();
		if (pGraphics) pGraphics->SetParameterFromPlug(id, value, true);

		GetParam(id)->SetNormalized(value);
		OnParamChange(id);
	}
	else if (id == kBypassParamID)
	{
		const bool bypass = value >= 0.5;
		if (IsBypassed() != bypass)
		{
			mPlugFlags ^= IPlugBase::kPlugFlagsBypass;
			OnBypass(bypass);
		}
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

			case Vst::Event::kDataEvent:
			{
				const uint32 size = event.data.size, type = event.data.type;

				if (type == Vst::DataEvent::kMidiSysEx)
				{
					const ISysEx sysex(ofs, event.data.bytes, size);
					ProcessSysEx(&sysex);
				}
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

void IPlugVST3::FlushParamChanges(Vst::IParameterChanges* const pParamChanges, const int32 nChanges)
{
	for (int32 i = 0; i < nChanges; ++i)
	{
		Vst::IParamValueQueue* const pParamQueue = pParamChanges->getParameterData(i);
		if (!pParamQueue) continue;

		const Vst::ParamID id = pParamQueue->getParameterId();

		#ifndef IPLUG_NO_MIDI_CC_PARAMS
		if (GetMidiCtrlParamIdx(id) < kNumMidiCtrlParams) continue;
		#endif

		const int32 point = pParamQueue->getPointCount() - 1;

		Vst::ParamValue value;
		int32 ofs;

		if (point >= 0 && pParamQueue->getPoint(point, ofs, value) == kResultOk && ofs > 0)
		{
			ProcessParamChange(id, value);
		}
	}
}

#ifndef NDEBUG

char* IPlugVST3::VST2UniqueIDToGUID(const int uniqueID, const char* const plugName, char* buf, const int bufSize)
{
	assert(bufSize >= 33);

	memcpy(buf, "565354\0", 8); // VST
	snprintf(&buf[6], 9, "%08X", uniqueID);
	buf += 12;

	for (int c = 1, i = 0; i < 9; ++i)
	{
		if (c) c = plugName[i];
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		snprintf(buf += 2, 3, "%02X", c);
	}

	return &buf[-30];
}

#endif // NDEBUG
