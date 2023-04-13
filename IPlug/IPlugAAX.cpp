#include "IPlugAAX.h"

#include "aax-sdk/Interfaces/AAX_IComponentDescriptor.h"
#include "aax-sdk/Interfaces/AAX_IPropertyMap.h"

#include <string.h>

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
	AAX_CString hostName;
	if (pHost->GetHostName(&hostName) == AAX_SUCCESS)
	{
		SetHost(hostName.Get(), mHostVersion);
	}

	HostSpecificInit();
	OnParamReset();

	return AAX_SUCCESS;
}

struct SIPlugAAX_Alg_Context
{
	IPlugAAX* const* mPlug;
};

void AAX_CALLBACK IPlugAAX::AAXAlgProcessFunc(void* const instBegin[], const void* const pInstEnd)
{
	const SIPlugAAX_Alg_Context* AAX_RESTRICT instance = (SIPlugAAX_Alg_Context*)instBegin[0];

	for (const SIPlugAAX_Alg_Context* const* walk = (SIPlugAAX_Alg_Context* const*)instBegin; walk < pInstEnd; ++walk)
	{
		instance = *walk;
		IPlugAAX* const pPlug = *instance->mPlug;
	}
}
