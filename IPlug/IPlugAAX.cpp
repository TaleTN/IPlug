#include "IPlugAAX.h"

#include "aax-sdk/Interfaces/AAX_IComponentDescriptor.h"
#include "aax-sdk/Interfaces/AAX_IPropertyMap.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "WDL/wdlcstring.h"

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

	AAX_Result ResetFieldData(const AAX_CFieldIndex idx, void* const pData, uint32_t /* size */) const AAX_OVERRIDE
	{
		AAX_Result err = AAX_SUCCESS;

		if (idx == 0)
			memcpy(pData, &mPlug, sizeof(IPlugAAX*));
		else
			err = AAX_ERROR_INVALID_FIELD_INDEX;

		return err;
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
}

static void DescribeAlgorithmComponent(AAX_IComponentDescriptor* const pCompDesc,
	const int plugID, const int mfrID, const int plugDoes)
{
	AAX_CheckedResult err;

	// See SIPlugAAX_Alg_Context.
	err = pCompDesc->AddPrivateData(0, sizeof(IPlugAAX*), AAX_ePrivateDataOptions_External);
	err = pCompDesc->AddAudioBufferLength(1);
	err = pCompDesc->AddAudioIn(2);
	err = pCompDesc->AddAudioOut(3);

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

	err = pPropMap->AddProperty(AAX_eProperty_CanBypass, false);
	err = pPropMap->AddProperty(AAX_eProperty_UsesClientGUI, true);

	err = pCompDesc->AddProcessProc_Native(IPlugAAX::AAXAlgProcessFunc, pPropMap);
}

AAX_Result IPlugAAX::AAXDescribeEffect(AAX_IEffectDescriptor* const pPlugDesc, const char* const name, const char* const shortName,
	const int uniqueID, const int mfrID, const int plugDoes, void* const createProc)
{
	AAX_CheckedResult err;

	AAX_IComponentDescriptor* const pCompDesc = pPlugDesc->NewComponentDescriptor();
	if (!pCompDesc) err = AAX_ERROR_NULL_OBJECT;

	err = pPlugDesc->AddName(name);
	if (shortName) err = pPlugDesc->AddName(shortName);

	const AAX_EPlugInCategory category = plugDoes & kPlugIsInst ? AAX_ePlugInCategory_SWGenerators : AAX_EPlugInCategory_Effect;
	err = pPlugDesc->AddCategory(category);

	err = pCompDesc->Clear();
	DescribeAlgorithmComponent(pCompDesc, uniqueID, mfrID, plugDoes);
	err = pPlugDesc->AddComponent(pCompDesc);

	err = pPlugDesc->AddProcPtr(createProc, kAAX_ProcPtrID_Create_EffectParameters);

	return err;
}

AAX_CEffectParameters* AAX_CALLBACK IPlugAAX::AAXCreateParams(IPlugAAX* const pPlug)
{
	return mEffectParams = new IPlugAAX_EffectParams(pPlug);
}

AAX_Result IPlugAAX::AAXEffectInit(AAX_CParameterManager* const pParamMgr, const AAX_IController* const pHost)
{
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

				pAAXParam = new AAX_CParameter<bool>(id, name, pBoolParam->Bool(),
					IPlugAAX_TaperDelegate<bool, IBoolParam>(pBoolParam),
					IPlugAAX_DisplayDelegate<bool, IBoolParam>(pBoolParam),
					true);
				break;
			}

			case IParam::kTypeInt:
			{
				IIntParam* const pIntParam = (IIntParam*)pParam;

				pAAXParam = new AAX_CParameter<int32_t>(id, name, pIntParam->Int(),
					IPlugAAX_TaperDelegate<int32_t, IIntParam>(pIntParam),
					IPlugAAX_DisplayDelegate<int32_t, IIntParam>(pIntParam),
					true);
				break;
			}

			case IParam::kTypeEnum:
			{
				IEnumParam* const pEnumParam = (IEnumParam*)pParam;
				nSteps = pEnumParam->NEnums();

				pAAXParam = new AAX_CParameter<int32_t>(id, name, pEnumParam->Int(),
					IPlugAAX_TaperDelegate<int32_t, IEnumParam>(pEnumParam),
					IPlugAAX_DisplayDelegate<int32_t, IEnumParam>(pEnumParam),
					true);
				break;
			}

			case IParam::kTypeDouble:
			{
				IDoubleParam* const pDoubleParam = (IDoubleParam*)pParam;

				pAAXParam = new AAX_CParameter<double>(id, name, pDoubleParam->Value(),
					IPlugAAX_TaperDelegate<double, IDoubleParam>(pDoubleParam),
					IPlugAAX_DisplayDelegate<double, IDoubleParam>(pDoubleParam),
					true);
				break;
			}

			case IParam::kTypeNormalized:
			{
				INormalizedParam* const pNormalizedParam = (INormalizedParam*)pParam;

				pAAXParam = new AAX_CParameter<double>(id, name, pNormalizedParam->Value(),
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

		pPlug->mMutex.Enter();

		if (!pPlug->IsActive()) pPlug->OnActivate(true);
		const int32_t nFrames = *instance->mBufferSize;

		pPlug->AttachInputBuffers(0, pPlug->NInChannels(), instance->mInputs, nFrames);
		pPlug->AttachOutputBuffers(0, pPlug->NOutChannels(), instance->mOutputs);

		pPlug->ProcessBuffers((float)0.0f, nFrames);

		pPlug->mMutex.Leave();
	}
}
