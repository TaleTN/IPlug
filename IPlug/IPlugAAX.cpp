#include "IPlugAAX.h"
#include "IGraphics.h"

#include "aax-sdk/Interfaces/AAX_CBinaryDisplayDelegate.h"
#include "aax-sdk/Interfaces/AAX_CBinaryTaperDelegate.h"
#include "aax-sdk/Interfaces/AAX_IComponentDescriptor.h"
#include "aax-sdk/Interfaces/AAX_IMIDINode.h"
#include "aax-sdk/Interfaces/AAX_IPropertyMap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"

static const int IPLUG_VERSION_MAGIC = 'pfft';

// TN: In default AAX_CParameter implementation SetNormalizedValue() calls
// SetValue(), which converts to real and then back to normalized. This
// rounds down stepped values in the process, which doesn't seem to work
// well with IPlug.
template <typename T> class IPlugAAX_CParam: public AAX_CParameter<T>
{
public:
	IPlugAAX_CParam(
		AAX_CParamID const id,
		const AAX_IString& name,
		const T defaultVal,
		const AAX_ITaperDelegate<T>& taper,
		const AAX_IDisplayDelegate<T>& display,
		const bool automatable = false
	):
		AAX_CParameter<T>(id, name, defaultVal, taper, display, automatable)
	{}

	void SetNormalizedValue(const double normalizedValue) AAX_OVERRIDE
	{
		if (this->mAutomationDelegate)
		{
			this->Touch();
			this->mAutomationDelegate->PostSetValueRequest(this->Identifier(), normalizedValue);
			this->Release();
		}
		else
		{
			this->mNeedNotify = true;
			this->UpdateNormalizedValue(normalizedValue);
		}
	}
};

class IPlugAAX_EffectParams: public AAX_CEffectParameters
{
public:
	IPlugAAX_EffectParams(IPlugAAX* const pPlug):
		AAX_CEffectParameters(),
		mPlug(pPlug)
	{}

	~IPlugAAX_EffectParams() AAX_OVERRIDE
	{
		delete mPlug;
	}

	AAX_Result NotificationReceived(const AAX_CTypeID type, const void* const pData, const uint32_t size) AAX_OVERRIDE
	{
		mPlug->AAXNotificationReceived(type, pData, size);
		return AAX_CEffectParameters::NotificationReceived(type, pData, size);
	}

	AAX_Result UpdateParameterNormalizedValue(AAX_CParamID const id, const double value, const AAX_EUpdateSource src) AAX_OVERRIDE
	{
		mPlug->AAXUpdateParam(id, value, src);
		return AAX_CEffectParameters::UpdateParameterNormalizedValue(id, value, src);
	}

	AAX_Result ResetFieldData(const AAX_CFieldIndex idx, void* const pData, uint32_t /* size */) const AAX_OVERRIDE
	{
		AAX_Result err = AAX_SUCCESS;

		if (idx == 0)
			memcpy(pData, &mPlug, sizeof(IPlugAAX*));
		else
			err = AAX_ERROR_INVALID_FIELD_INDEX;

		return err;
	}

	AAX_Result GetNumberOfChunks(int32_t* const pNumChunks) const AAX_OVERRIDE
	{
		*pNumChunks = 1;
		return AAX_SUCCESS;
	}

	AAX_Result GetChunkIDFromIndex(const int32_t idx, AAX_CTypeID* const pID) const AAX_OVERRIDE
	{
		AAX_CTypeID id = IPLUG_VERSION_MAGIC;
		AAX_Result err = AAX_SUCCESS;

		if (idx != 0)
		{
			id = 0;
			err = AAX_ERROR_INVALID_CHUNK_INDEX;
		}

		*pID = id;
		return err;
	}

	AAX_Result GetChunkSize(const AAX_CTypeID id, uint32_t* const pSize) const AAX_OVERRIDE
	{
		return mPlug->AAXGetChunkSize(id, pSize);
	}

	AAX_Result GetChunk(const AAX_CTypeID id, AAX_SPlugInChunk* const pChunk) const AAX_OVERRIDE
	{
		return mPlug->AAXGetChunk(id, pChunk);
	}

	AAX_Result SetChunk(const AAX_CTypeID id, const AAX_SPlugInChunk* const pChunk) AAX_OVERRIDE
	{
		const AAX_Result err = mPlug->AAXSetChunk(id, pChunk);

		if (err == AAX_SUCCESS)
		{
			char buf[16];
			memcpy(buf, "Param\0\0", 8);

			const int n = mPlug->NParams();
			for (int i = 0; i < n;)
			{
				const IParam* const pParam = mPlug->GetParam(i++);

				#ifdef _MSC_VER
				_itoa(i, &buf[5], 10);
				#else
				snprintf(&buf[5], sizeof(buf) - 5, "%d", i);
				#endif

				SetParameterNormalizedValue(buf, pParam->GetNormalized());
			}
		}

		return err;
	}

	// TN: Binary compare doesn't work (tested in Pro Tools 2023.3.0),
	// making (generic) IPlug implementation unfeasible.
	AAX_Result CompareActiveChunk(const AAX_SPlugInChunk* const pChunk, AAX_CBoolean* const pIsEqual) const AAX_OVERRIDE
	{
		const bool ok = pChunk->fChunkID == IPLUG_VERSION_MAGIC;
		*pIsEqual = ok;
		return ok ? AAX_SUCCESS : AAX_ERROR_INVALID_CHUNK_ID;
	}

	inline IPlugAAX* GetPlug() const { return mPlug; }

protected:
	AAX_Result EffectInit() AAX_OVERRIDE
	{
		return mPlug->AAXEffectInit(&mParameterManager, Controller());
	}

private:
	IPlugAAX* const mPlug;
};

template <typename T, typename PARAMTYPE>
class IPlugAAX_TaperDelegate: public AAX_ITaperDelegate<T>
{
public:
	IPlugAAX_TaperDelegate(const PARAMTYPE* const pParam)
	: AAX_ITaperDelegate<T>(), mParam(pParam) {}

	IPlugAAX_TaperDelegate<T, PARAMTYPE>* Clone() const AAX_OVERRIDE
	{
		return new IPlugAAX_TaperDelegate(*this);
	}

	T GetMinimumValue() const AAX_OVERRIDE { return mParam->Min(); }
	T GetMaximumValue() const AAX_OVERRIDE { return mParam->Max(); }

	T ConstrainRealValue(const T value) const AAX_OVERRIDE
	{
		return mParam->Bounded(value);
	}

	T NormalizedToReal(const double normalizedValue) const AAX_OVERRIDE
	{
		return (T)mParam->GetNonNormalized(normalizedValue);
	}

	double RealToNormalized(const T realValue) const AAX_OVERRIDE
	{
		return mParam->GetNormalized(realValue);
	}

protected:
	const PARAMTYPE* const mParam;
};

template <typename T, typename PARAMTYPE>
class IPlugAAX_DisplayDelegate: public AAX_IDisplayDelegate<T>
{
public:
	IPlugAAX_DisplayDelegate(PARAMTYPE* const pParam)
	: AAX_IDisplayDelegate<T>(), mParam(pParam) {}

	IPlugAAX_DisplayDelegate<T, PARAMTYPE>* Clone() const AAX_OVERRIDE
	{
		return new IPlugAAX_DisplayDelegate(*this);
	}

	bool ValueToString(const T value, AAX_CString* const valueString) const AAX_OVERRIDE
	{
		static const int bufSize = 128;
		char buf[bufSize];

		mParam->GetDisplayForHost(mParam->GetNormalized((double)value), buf, bufSize);
		const char* const label = mParam->GetLabelForHost();

		if (label && *label)
		{
			lstrcatn(buf, " ", bufSize);
			lstrcatn(buf, label, bufSize);
		}

		valueString->Set(buf);
		return true;
	}

	bool ValueToString(const T value, int32_t /* maxNumChars */, AAX_CString* const valueString) const AAX_OVERRIDE
	{
		return this->ValueToString(value, valueString);
	}

	bool StringToValue(const AAX_CString& valueString, T* const value) const AAX_OVERRIDE
	{
		const char* const cStr = valueString.Get();

		double v;
		const bool mapped = mParam->MapDisplayText(cStr, &v);

		if (mapped)
		{
			v = mParam->GetNonNormalized(v);
		}
		else
		{
			v = strtod(cStr, NULL);
			if (mParam->DisplayIsNegated()) v = -v;
			v = (double)mParam->Bounded((T)v);
		}

		*value = (T)v;
		return true;
	}

protected:
	PARAMTYPE* const mParam;
};

class IPlugAAX_EffectGUI: public AAX_CEffectGUI
{
public:
	IPlugAAX_EffectGUI(): AAX_CEffectGUI(), mGraphics(NULL) {}

	AAX_Result GetViewSize(AAX_Point* const pViewSize) const AAX_OVERRIDE
	{
		if (mGraphics)
		{
			const IPlugAAX* const pPlug = (const IPlugAAX*)mGraphics->GetPlug();
			*pViewSize = *pPlug->AAXGetViewSize();
		}
		return AAX_SUCCESS;
	}

	static AAX_IEffectGUI* AAX_CALLBACK Create() { return new IPlugAAX_EffectGUI(); }

protected:
	void CreateViewContents() AAX_OVERRIDE
	{
		const IPlugAAX_EffectParams* const pEffectParams = (const IPlugAAX_EffectParams*)GetEffectParameters();
		mGraphics = pEffectParams->GetPlug()->GetGUI();
	}

	void CreateViewContainer() AAX_OVERRIDE
	{
		if (mGraphics && mGraphics->OpenWindow(GetViewContainerPtr()))
		{
			mGraphics->GetPlug()->OnGUIOpen();
		}
	}

	void DeleteViewContainer() AAX_OVERRIDE
	{
		if (mGraphics)
		{
			mGraphics->GetPlug()->OnGUIClose();
			mGraphics->CloseWindow();
		}
	}

private:
	IGraphics* mGraphics;
};

IPlugAAX::IPlugAAX(
	void* /* instanceInfo */,
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

	mEffectParams = NULL;

	mSamplePos = 0;
	mTempo = 0.0;
	memset(mTimeSig, 0, sizeof(mTimeSig));

	mSysExMode = false;
}

bool IPlugAAX::AllocStateChunk(int chunkSize)
{
	if (chunkSize < 0) chunkSize = GetParamsChunkSize(0, NParams());
	return mState.Alloc(chunkSize) == chunkSize;
}

bool IPlugAAX::AllocBankChunk(const int chunkSize)
{
	if (chunkSize < 0 && mPresetChunkSize < 0) AllocPresetChunk();
	return true;
}

static void ParamIdxToID(int idx, char buf[16])
{
	memcpy(buf, "Param\0\0", 8);
	idx++;

	#ifdef _MSC_VER
	_itoa(idx, &buf[5], 10);
	#else
	snprintf(&buf[5], 16 - 5, "%d", idx);
	#endif
}

void IPlugAAX::BeginInformHostOfParamChange(const int idx, const bool lockMutex)
{
	char id[16];
	ParamIdxToID(idx, id);

	EndDelayedInformHostOfParamChange(lockMutex);
	mEffectParams->TouchParameter(id);
}

void IPlugAAX::InformHostOfParamChange(const int idx, const double normalizedValue, bool /* lockMutex */)
{
	char id[16];
	ParamIdxToID(idx, id);

	mEffectParams->SetParameterNormalizedValue(id, normalizedValue);
}

void IPlugAAX::EndInformHostOfParamChange(const int idx, bool /* lockMutex */)
{
	char id[16];
	ParamIdxToID(idx, id);

	mEffectParams->ReleaseParameter(id);
}

double IPlugAAX::GetSamplePos()
{
	return (double)mSamplePos;
}

double IPlugAAX::GetTempo()
{
	assert(mTempo >= 0.0);
	return mTempo;
}

void IPlugAAX::GetTimeSig(int* const pNum, int* const pDenom)
{
	*pNum = mTimeSig[0];
	*pDenom = mTimeSig[1];
}

void IPlugAAX::ResizeGraphics(const int w, const int h)
{
	mViewSize.vert = (float)h;
	mViewSize.horz = (float)w;
}

static void DescribeAlgorithmComponent(AAX_IComponentDescriptor* const pCompDesc, const char* const name,
	const int plugID, const int mfrID, const int latency, const int plugDoes)
{
	AAX_CheckedResult err;

	// See SIPlugAAX_Alg_Context.
	err = pCompDesc->AddPrivateData(0, sizeof(IPlugAAX*), AAX_ePrivateDataOptions_External);

	if (plugDoes & IPlugBase::kPlugDoesMidiIn)
	{
		err = pCompDesc->AddMIDINode(1, AAX_eMIDINodeType_LocalInput, name, 0xFFFF);
	}

	err = pCompDesc->AddAudioBufferLength(2);
	err = pCompDesc->AddAudioIn(3);
	err = pCompDesc->AddAudioOut(4);

	AAX_IPropertyMap* const pPropMap = pCompDesc->NewPropertyMap();
	if (!pPropMap) err = AAX_ERROR_NULL_OBJECT;

	err = pPropMap->AddProperty(AAX_eProperty_ManufacturerID, mfrID);
	err = pPropMap->AddProperty(AAX_eProperty_ProductID, plugID);

	err = pPropMap->AddProperty(AAX_eProperty_PlugInID_Native, 'nate');

	if (!(plugDoes & IPlugBase::kPlugIsInst))
	{
		err = pPropMap->AddProperty(AAX_eProperty_PlugInID_AudioSuite, 'suit');
	}

	// To-do: Add support for other stem formats.
	err = pPropMap->AddProperty(AAX_eProperty_InputStemFormat, AAX_eStemFormat_Stereo);
	err = pPropMap->AddProperty(AAX_eProperty_OutputStemFormat, AAX_eStemFormat_Stereo);

	if (latency)
	{
		assert(latency <= 16383); // @ 44.1/48 kHz
		err = pPropMap->AddProperty(AAX_eProperty_LatencyContribution, latency);
	}

	err = pPropMap->AddProperty(AAX_eProperty_CanBypass, true);

	#ifdef IPLUG_USE_CLIENT_GUI
	err = pPropMap->AddProperty(AAX_eProperty_UsesClientGUI, true);
	#endif

	err = pCompDesc->AddProcessProc_Native(IPlugAAX::AAXAlgProcessFunc, pPropMap);
}

AAX_Result IPlugAAX::AAXDescribeEffect(AAX_IEffectDescriptor* const pPlugDesc, const char* const name, const char* const shortName,
	const int uniqueID, const int mfrID, const int latency, const int plugDoes, void* const createProc)
{
	AAX_CheckedResult err;

	AAX_IComponentDescriptor* const pCompDesc = pPlugDesc->NewComponentDescriptor();
	if (!pCompDesc) err = AAX_ERROR_NULL_OBJECT;

	err = pPlugDesc->AddName(name);
	if (shortName) err = pPlugDesc->AddName(shortName);

	const AAX_EPlugInCategory category = plugDoes & kPlugIsInst ? AAX_ePlugInCategory_SWGenerators : AAX_EPlugInCategory_Effect;
	err = pPlugDesc->AddCategory(category);

	err = pCompDesc->Clear();
	DescribeAlgorithmComponent(pCompDesc, name, uniqueID, mfrID, latency, plugDoes);
	err = pPlugDesc->AddComponent(pCompDesc);

	err = pPlugDesc->AddProcPtr(createProc, kAAX_ProcPtrID_Create_EffectParameters);
	err = pPlugDesc->AddProcPtr((void*)IPlugAAX_EffectGUI::Create, kAAX_ProcPtrID_Create_EffectGUI);

	AAX_IPropertyMap* const pPropMap = pPlugDesc->NewPropertyMap();
	if (!pPropMap) err = AAX_ERROR_NULL_OBJECT;

	err = pPropMap->AddProperty(AAX_eProperty_UsesTransport, true);
	err = pPlugDesc->SetProperties(pPropMap);

	return err;
}

AAX_CEffectParameters* AAX_CALLBACK IPlugAAX::AAXCreateParams(IPlugAAX* const pPlug)
{
	return mEffectParams = new IPlugAAX_EffectParams(pPlug);
}

AAX_Result IPlugAAX::AAXEffectInit(AAX_CParameterManager* const pParamMgr, const AAX_IController* const pHost)
{
	AAX_IParameter* const pBypass = new AAX_CParameter<bool>(cDefaultMasterBypassID, AAX_CString("Master Bypass"), false,
		AAX_CBinaryTaperDelegate<bool>(),
		AAX_CBinaryDisplayDelegate<bool>("bypass", "on"),
		true);

	pBypass->SetNumberOfSteps(2);
	pBypass->SetType(AAX_eParameterType_Discrete);
	pBypass->AddShortenedName(AAX_CString("Bypass"));
	pParamMgr->AddParameter(pBypass);

	char id[16];
	memcpy(id, "Param\0\0", 8);

	const int n = NParams();
	for (int i = 0; i < n;)
	{
		IParam* const pParam = GetParam(i++);
		const int type = pParam->Type();
		const AAX_CString name(pParam->GetNameForHost());

		AAX_IParameter* pAAXParam = NULL;
		int nSteps = 0;

		#ifdef _MSC_VER
		_itoa(i, &id[5], 10);
		#else
		snprintf(&id[5], sizeof(id) - 5, "%d", i);
		#endif

		switch (type)
		{
			case IParam::kTypeBool:
			{
				IBoolParam* const pBoolParam = (IBoolParam*)pParam;
				nSteps = 2;

				pAAXParam = new IPlugAAX_CParam<bool>(id, name, pBoolParam->Bool(),
					IPlugAAX_TaperDelegate<bool, IBoolParam>(pBoolParam),
					IPlugAAX_DisplayDelegate<bool, IBoolParam>(pBoolParam),
					true);
				break;
			}

			case IParam::kTypeInt:
			{
				IIntParam* const pIntParam = (IIntParam*)pParam;

				pAAXParam = new IPlugAAX_CParam<int32_t>(id, name, pIntParam->Int(),
					IPlugAAX_TaperDelegate<int32_t, IIntParam>(pIntParam),
					IPlugAAX_DisplayDelegate<int32_t, IIntParam>(pIntParam),
					true);
				break;
			}

			case IParam::kTypeEnum:
			{
				IEnumParam* const pEnumParam = (IEnumParam*)pParam;
				nSteps = pEnumParam->NEnums();

				pAAXParam = new IPlugAAX_CParam<int32_t>(id, name, pEnumParam->Int(),
					IPlugAAX_TaperDelegate<int32_t, IEnumParam>(pEnumParam),
					IPlugAAX_DisplayDelegate<int32_t, IEnumParam>(pEnumParam),
					true);
				break;
			}

			case IParam::kTypeDouble:
			{
				IDoubleParam* const pDoubleParam = (IDoubleParam*)pParam;

				pAAXParam = new IPlugAAX_CParam<double>(id, name, pDoubleParam->Value(),
					IPlugAAX_TaperDelegate<double, IDoubleParam>(pDoubleParam),
					IPlugAAX_DisplayDelegate<double, IDoubleParam>(pDoubleParam),
					true);
				break;
			}

			case IParam::kTypeNormalized:
			{
				INormalizedParam* const pNormalizedParam = (INormalizedParam*)pParam;

				pAAXParam = new IPlugAAX_CParam<double>(id, name, pNormalizedParam->Value(),
					IPlugAAX_TaperDelegate<double, INormalizedParam>(pNormalizedParam),
					IPlugAAX_DisplayDelegate<double, INormalizedParam>(pNormalizedParam),
					true);
				break;
			}

			default:
			{
				static const bool unknownParamType = false;
				assert(unknownParamType);
				break;
			}
		}

		if (pAAXParam)
		{
			pAAXParam->SetNumberOfSteps(nSteps ? nSteps : 128); // TN: Sensible default?
			pAAXParam->SetType(nSteps ? AAX_eParameterType_Discrete : AAX_eParameterType_Continuous);

			pParamMgr->AddParameter(pAAXParam);
		}
	}

	AAX_CString hostName;
	if (pHost->GetHostName(&hostName) == AAX_SUCCESS)
	{
		SetHost(hostName.Get(), mHostVersion);
	}

	HostSpecificInit();
	OnParamReset();

	int flags = mPlugFlags;
	AAX_CSampleRate sampleRate;

	if (pHost->GetSampleRate(&sampleRate) == AAX_SUCCESS)
	{
		SetSampleRate(sampleRate);
		flags |= kPlugInitSampleRate;
	}

	SetBlockSize(1 << AAX_eAudioBufferLength_Max);
	mPlugFlags = flags | kPlugInitBlockSize;

	if (flags & kPlugInitSampleRate) Reset();

	return AAX_SUCCESS;
}

struct SIPlugAAX_Alg_Context
{
	IPlugAAX* const* mPlug;
	AAX_IMIDINode* mMidiInNode;
	const int32_t* mBufferSize;
	const float* const* mInputs;
	float* const* mOutputs;
};

void AAX_CALLBACK IPlugAAX::AAXAlgProcessFunc(void* const instBegin[], const void* const pInstEnd)
{
	const SIPlugAAX_Alg_Context* AAX_RESTRICT instance = (SIPlugAAX_Alg_Context*)instBegin[0];

	for (const SIPlugAAX_Alg_Context* const* walk = (SIPlugAAX_Alg_Context* const*)instBegin; walk < pInstEnd; ++walk)
	{
		instance = *walk;
		IPlugAAX* const pPlug = *instance->mPlug;

		const IPlugAAX_EffectParams* const pEffectParams = (const IPlugAAX_EffectParams*)pPlug->mEffectParams;
		const AAX_ITransport* const pTransport = pEffectParams->Transport();

		double tempo;
		int32_t timeSig[2];
		int64_t pos;
		bool isPlaying;

		const AAX_Result errTempo = pTransport->GetCurrentTempo(&tempo);
		const AAX_Result errMeter = pTransport->GetCurrentMeter(&timeSig[0], &timeSig[1]);

		AAX_Result errPos = pTransport->IsTransportPlaying(&isPlaying);
		errPos = errPos == AAX_SUCCESS && isPlaying ? pTransport->GetCurrentNativeSampleLocation(&pos) : !AAX_SUCCESS;

		pPlug->mMutex.Enter();

		if (!pPlug->IsActive()) pPlug->OnActivate(true);

		if (errPos == AAX_SUCCESS) pPlug->mSamplePos = pos;
		if (errTempo == AAX_SUCCESS) pPlug->mTempo = tempo;

		if (errMeter == AAX_SUCCESS)
		{
			memcpy(pPlug->mTimeSig, timeSig, 2 * sizeof(int32_t));
		}

		if (pPlug->DoesMIDI(kPlugDoesMidiIn))
		{
			AAX_IMIDINode* const pMidiInNode = instance->mMidiInNode;
			AAX_CMidiStream* const pMidiInStream = pMidiInNode->GetNodeBuffer();
			const uint32_t nMidiInPackets = pMidiInStream->mBufferSize;

			if (nMidiInPackets)
			{
				pPlug->ProcessMidiInNode(pMidiInStream->mBuffer, nMidiInPackets);
			}
		}

		const int32_t nFrames = *instance->mBufferSize;

		pPlug->AttachInputBuffers(0, pPlug->NInChannels(), instance->mInputs, nFrames);
		pPlug->AttachOutputBuffers(0, pPlug->NOutChannels(), instance->mOutputs);

		pPlug->ProcessBuffers((float)0.0f, nFrames);

		pPlug->mMutex.Leave();
	}
}

void IPlugAAX::ProcessMidiInNode(const AAX_CMidiPacket* const pBuf, const uint32_t nPackets)
{
	bool sysExMode = mSysExMode;

	for (uint32_t i = 0; i < nPackets; ++i)
	{
		const AAX_CMidiPacket* const pPacket = &pBuf[i];

		const int ofs = pPacket->mTimestamp;
		const int len = pPacket->mLength;
		const unsigned char* const pData = pPacket->mData;

		assert(len > 0);
		if (!len) continue;

		sysExMode |= pData[0] == 0xF0;

		if (!sysExMode)
		{
			// TN: Undefined behavior if host hasn't initialized
			// AAX_CMidiPacket::mData, but should be fine.
			const IMidiMsg msg(ofs, pData);

			ProcessMidiMsg(&msg);
		}
		else
		{
			const ISysEx sysex(ofs, pData, len);
			ProcessSysEx(&sysex);

			sysExMode = pData[len - 1] != 0xF7;
		}
	}

	mSysExMode = sysExMode;
}

void IPlugAAX::AAXUpdateParam(AAX_CParamID const id, const double value, AAX_EUpdateSource src)
{
	mMutex.Enter();

	if (!strncmp(id, "Param", 5) && id[5])
	{
		const unsigned long int idx = strtoul(&id[5], NULL, 10) - 1;

		if (idx < (unsigned long int)NParams())
		{
			IGraphics* const pGraphics = GetGUI();
			if (pGraphics) pGraphics->SetParameterFromPlug(idx, value, true);

			GetParam(idx)->SetNormalized(value);
			OnParamChange(idx);
		}
	}
	else if (!strcmp(id, cDefaultMasterBypassID))
	{
		const bool bypass = value > 0.0;
		if (IsBypassed() != bypass)
		{
			mPlugFlags ^= IPlugBase::kPlugFlagsBypass;
			OnBypass(bypass);
		}
	}

	mMutex.Leave();
}

void IPlugAAX::AAXNotificationReceived(const AAX_CTypeID type, const void* /* pData */, uint32_t/*  size */)
{
	int offline;

	switch (type)
	{
		case AAX_eNotificationEvent_EnteringOfflineMode:
		{
			offline = kPlugFlagsOffline;
			break;
		}

		case AAX_eNotificationEvent_ExitingOfflineMode:
		{
			offline = 0;
			break;
		}

		default: return;
	}

	mMutex.Enter();
	mPlugFlags = (mPlugFlags & ~kPlugFlagsOffline) | offline;
	mMutex.Leave();
}

AAX_Result IPlugAAX::AAXGetChunkSize(const AAX_CTypeID id, uint32_t* const pSize)
{
	int size = 0;
	AAX_Result err;

	if (id == IPLUG_VERSION_MAGIC)
	{
		err = AAX_ERROR_INCORRECT_CHUNK_SIZE;
		mMutex.Enter();

		if (mState.AllocSize() || AllocStateChunk())
		{
			mState.Clear();
			if (SerializeState(&mState))
			{
				size = mState.Size();
				err = AAX_SUCCESS;
			}
		}

		mMutex.Leave();
	}
	else
	{
		err = AAX_ERROR_INVALID_CHUNK_ID;
	}

	*pSize = size;
	return err;
}

AAX_Result IPlugAAX::AAXGetChunk(const AAX_CTypeID id, AAX_SPlugInChunk* const pChunk)
{
	if (id != IPLUG_VERSION_MAGIC) return AAX_ERROR_INVALID_CHUNK_ID;

	AAX_Result err = AAX_SUCCESS;
	mMutex.Enter();

	mState.Clear();
	if (SerializeState(&mState))
	{
		const void* const pData = mState.GetBytes();
		const int size = mState.Size();

		pChunk->fSize = size;
		pChunk->fVersion = IPlugBase::kIPlugVersion;

		memset(pChunk->fName, 0, sizeof(pChunk->fName));
		lstrcpyn_safe((char*)pChunk->fName, GetPresetName(), sizeof(pChunk->fName));

		memcpy(pChunk->fData, pData, size);
	}
	else
	{
		err = AAX_ERROR_INCORRECT_CHUNK_SIZE;
	}

	mMutex.Leave();
	return err;
}

AAX_Result IPlugAAX::AAXSetChunk(const AAX_CTypeID id, const AAX_SPlugInChunk* const pChunk)
{
	if (id != IPLUG_VERSION_MAGIC) return AAX_ERROR_INVALID_CHUNK_ID;

	AAX_Result err = AAX_SUCCESS;
	mMutex.Enter();

	const int size = pChunk->fSize;
	if (mState.Size() != size)
	{
		mState.Resize(size);
		if (mState.Size() != size) err = AAX_ERROR_INCORRECT_CHUNK_SIZE;
	}

	if (err == AAX_SUCCESS)
	{
		char name[sizeof(pChunk->fName)];
		lstrcpyn_safe(name, (const char*)pChunk->fName, sizeof(name));

		RestorePreset(name);

		memcpy(mState.GetBytes(), pChunk->fData, size);
		const int pos = UnserializeState(&mState, 0);

		OnParamReset();

		if (pos >= 0)
		{
			OnPresetChange(GetCurrentPresetIdx());
			RedrawParamControls();
		}
		else
		{
			err = AAX_ERROR_INCORRECT_CHUNK_SIZE;
		}
	}

	mMutex.Leave();
	return err;
}

#ifndef NDEBUG

#include "WDL/filename.h"
#include "WDL/wdlendian.h"
#include "WDL/wdlstring.h"

int IPlugAAX::AAXExtractFactoryPresets(const char* const dir)
{
	WDL_String filename;
	int n = 0;

	const int m = NPresets();
	const int idx = GetCurrentPresetIdx();

	for (int i = 0; i < m; ++i)
	{
		if (!RestorePreset(i)) continue;
		const char* const name = GetPresetName();

		if (!(mState.AllocSize() || AllocStateChunk())) continue;

		mState.Clear();
		if (!SerializeState(&mState)) continue;

		filename.Set(dir);
		filename.remove_trailing_dirchars();
		filename.Append(WDL_DIRCHAR_STR);
		filename.AppendFormatted(12, "%d ", i + 1);

		const int ofs = filename.GetLength();
		filename.Append(name);
		WDL_filename_filterstr(filename.Get() + ofs);
		filename.Append(".tfx");

		FILE* const fp = fopen(filename.Get(), "wb");
		if (!fp) continue;

		const int size = mState.Size();
		AAX_SPlugInChunkHeader hdr;

		hdr.fSize = WDL_bswap32_if_le(sizeof(AAX_SPlugInChunkHeader) + size);
		hdr.fVersion = WDL_bswap32_if_le(IPlugBase::kIPlugVersion);
		hdr.fManufacturerID = WDL_bswap32_if_le(GetMfrID());
		hdr.fProductID = WDL_bswap32_if_le(GetUniqueID());
		hdr.fPlugInID = WDL_bswap32_if_le('nate');
		hdr.fChunkID = WDL_bswap32_if_le(IPLUG_VERSION_MAGIC);

		memset(hdr.fName, 0, sizeof(hdr.fName));
		lstrcpyn_safe((char*)hdr.fName, name, sizeof(hdr.fName));

		if (fwrite(&hdr, 1, sizeof(AAX_SPlugInChunkHeader), fp) &&
			fwrite(mState.GetBytes(), 1, size, fp))
		{
			n++;
		}

		fclose(fp);
	}

	RestorePreset(idx);
	return n;
}

#endif // NDEBUG
