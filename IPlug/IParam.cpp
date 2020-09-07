#include "IParam.h"

#include <string.h>

#ifdef _MSC_VER
	#define stricmp _stricmp
#endif

#include "WDL/wdlcstring.h"

IBoolParam::IBoolParam(
	const char* const name,
	const bool defaultVal,
	const char* const off,
	const char* const on
):
	IParam(kTypeBool, name)
{
	mBoolVal = defaultVal;

	mDisplayTexts[0].Set(off ? off : "Off");
	mDisplayTexts[1].Set(on ? on : "On");
}

void IBoolParam::SetDisplayText(const bool boolVal, const char* const text)
{
	mDisplayTexts[(int)boolVal].Set(text);
}

void IBoolParam::SetNormalized(const double normalizedValue)
{
	mBoolVal = normalizedValue >= 0.5;
}

double IBoolParam::GetNormalized(const double nonNormalizedValue) const
{
	return nonNormalizedValue >= 0.5;
}

char* IBoolParam::ToString(const bool boolVal, char* const buf, const int bufSize) const
{
	lstrcpyn_safe(buf, mDisplayTexts[(int)boolVal].Get(), bufSize);
	return buf;
}

char* IBoolParam::GetDisplayForHost(char* const buf, const int bufSize)
{
	return ToString(mBoolVal, buf, bufSize);
}

char* IBoolParam::GetDisplayForHost(const double normalizedValue, char* const buf, const int bufSize)
{
	return ToString(normalizedValue >= 0.5, buf, bufSize);
}

const char* IBoolParam::GetDisplayText(const bool boolVal) const
{
	return mDisplayTexts[(int)boolVal].Get();
}

bool IBoolParam::MapDisplayText(const char* const str, double* const pNormalizedValue) const
{
	bool boolVal;

	if ((boolVal = !!stricmp(str, mDisplayTexts[0].Get())) &&
	   !(boolVal =  !stricmp(str, mDisplayTexts[1].Get())))
	{
		return false;
	}

	*pNormalizedValue = (double)boolVal;
	return true;
}

bool IBoolParam::Serialize(ByteChunk* const pChunk) const
{
	return !!pChunk->PutBool(mBoolVal);
}

int IBoolParam::Unserialize(const ByteChunk* const pChunk, const int startPos)
{
	return pChunk->GetBool(&mBoolVal, startPos);
}

IEnumParam::IEnumParam(
	const char* const name,
	const int defaultVal,
	const int nEnums
):
	IParam(kTypeEnum, name),
	mIntVal(defaultVal),
	mEnums(nEnums)
{
	assert(nEnums >= 2);
	assert(defaultVal >= 0 && defaultVal < nEnums);

	for (int i = 0; i < nEnums; ++i)
	{
		mDisplayTexts.Add(new WDL_FastString);
	}
}

void IEnumParam::SetDisplayText(const int intVal, const char* const text)
{
	assert(intVal >= 0 && intVal < mEnums);
	mDisplayTexts.Get(intVal)->Set(text);
}

void IEnumParam::SetNormalized(const double normalizedValue)
{
	mIntVal = FromNormalized(normalizedValue);
}

double IEnumParam::GetNormalized() const
{
	return ToNormalized(mIntVal);
}

double IEnumParam::GetNormalized(const double nonNormalizedValue) const
{
	const int intVal = (int)(nonNormalizedValue + 0.5);
	return ToNormalized(Bounded(intVal));
}

char* IEnumParam::ToString(const int intVal, char* const buf, const int bufSize) const
{
	lstrcpyn_safe(buf, GetDisplayText(intVal), bufSize);
	return buf;
}

char* IEnumParam::GetDisplayForHost(char* const buf, const int bufSize)
{
	return ToString(mIntVal, buf, bufSize);
}

char* IEnumParam::GetDisplayForHost(const double normalizedValue, char* const buf, const int bufSize)
{
	const int intVal = FromNormalized(normalizedValue);
	return ToString(intVal, buf, bufSize);
}

const char* IEnumParam::GetDisplayText(const int intVal) const
{
	assert(intVal >= 0 && intVal < mEnums);
	return mDisplayTexts.Get(intVal)->Get();
}

bool IEnumParam::MapDisplayText(const char* const str, double* const pNormalizedValue) const
{
	const WDL_FastString* const* const ppDT = mDisplayTexts.GetList();
	const int n = mDisplayTexts.GetSize();

	for (int i = 0; i < n; ++i)
	{
		if (!stricmp(str, ppDT[i]->Get()))
		{
			*pNormalizedValue = ToNormalized(i);
			return true;
		}
	}

	return false;
}

bool IEnumParam::Serialize(ByteChunk* const pChunk) const
{
	return !!pChunk->PutInt32(mIntVal);
}

int IEnumParam::Unserialize(const ByteChunk* const pChunk, const int startPos)
{
	return pChunk->GetInt32(&mIntVal, startPos);
}
