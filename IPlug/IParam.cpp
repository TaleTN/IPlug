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
