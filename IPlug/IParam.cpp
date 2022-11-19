#include "IParam.h"

#include <string.h>

#ifdef _MSC_VER
	#define stricmp _stricmp
#endif

#include "WDL/db2val.h"
#include "WDL/wdlcstring.h"

void IParam::SetShortName(const char* const name)
{
	assert(strlen(name) < 8);
	strcpy(mShortName, name);
}

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

IIntParam::IIntParam(
	const char* const name,
	const int defaultVal,
	const int minVal,
	const int maxVal,
	const char* const label
):
	IParam(kTypeInt, name),
	mIntVal(defaultVal),
	mMin(minVal),
	mMax(maxVal),
	mDisplayTexts(Delete),
	mLabel(label)
{
	#ifndef NDEBUG
	assert(minVal != maxVal);
	AssertInt(defaultVal);
	#endif
}

void IIntParam::SetDisplayText(const int intVal, const char* const text)
{
	#ifndef NDEBUG
	AssertInt(intVal);
	#endif

	WDL_FastString* pDT = mDisplayTexts.Get(intVal);
	if (!pDT) mDisplayTexts.Insert(intVal, pDT = new WDL_FastString);

	pDT->Set(text);
}

void IIntParam::SetNormalized(const double normalizedValue)
{
	mIntVal = FromNormalized(normalizedValue);
}

double IIntParam::GetNormalized() const
{
	return Normalized(mIntVal, mMin, mMax);
}

double IIntParam::GetNormalized(const double nonNormalizedValue) const
{
	const int intVal = (int)floor(nonNormalizedValue + 0.5);
	return Normalized(intVal, mMin, mMax);
}

char* IIntParam::ToString(const int intVal, char* const buf, const int bufSize) const
{
	#ifndef NDEBUG
	AssertInt(intVal);
	#endif

	const char* displayText = NULL;
	char tmp[12];

	if (GetNDisplayTexts())
	{
		const WDL_FastString* const pDT = mDisplayTexts.Get(intVal);
		if (pDT) displayText = pDT->Get();
	}

	if (!displayText)
	{
		displayText = tmp;
		const int displayValue = DisplayIsNegated() ? -intVal : intVal;

		const bool sign = displayValue && (mMin >= 0) != (mMax >= 0);
		snprintf(tmp, sizeof(tmp), sign ? "%+d" : "%d", displayValue);
	}

	lstrcpyn_safe(buf, displayText, bufSize);
	return buf;
}

char* IIntParam::GetDisplayForHost(char* const buf, const int bufSize)
{
	return ToString(mIntVal, buf, bufSize);
}

char* IIntParam::GetDisplayForHost(const double normalizedValue, char* const buf, const int bufSize)
{
	const int intVal = FromNormalized(normalizedValue);
	return ToString(intVal, buf, bufSize);
}

const char* IIntParam::GetLabelForHost() const
{
	return mLabel.Get();
}

const char* IIntParam::GetDisplayText(const int intVal) const
{
	#ifndef NDEBUG
	AssertInt(intVal);
	#endif

	const WDL_FastString* const pDT = mDisplayTexts.Get(intVal);
	return pDT ? pDT->Get() : NULL;
}

bool IIntParam::MapDisplayText(const char* const str, double* const pNormalizedValue) const
{
	const int n = mDisplayTexts.GetSize();

	for (int i = 0; i < n; ++i)
	{
		int key;
		const WDL_FastString* const pDT = mDisplayTexts.Enumerate(i, &key);

		if (!stricmp(str, pDT->Get()))
		{
			*pNormalizedValue = ToNormalized(key);
			return true;
		}
	}

	return false;
}

bool IIntParam::Serialize(ByteChunk* const pChunk) const
{
	return !!pChunk->PutInt32(mIntVal);
}

int IIntParam::Unserialize(const ByteChunk* const pChunk, const int startPos)
{
	return pChunk->GetInt32(&mIntVal, startPos);
}

#ifndef NDEBUG
void IIntParam::AssertInt(const int intVal) const
{
	const int minVal = wdl_min(mMin, mMax);
	const int maxVal = wdl_max(mMin, mMax);

	assert(intVal >= minVal && intVal <= maxVal);
}
#endif

IDoubleParam::IDoubleParam(
	const char* const name,
	const double defaultVal,
	const double minVal,
	const double maxVal,
	const int displayPrecision,
	const char* const label
):
	IParam(kTypeDouble, name),
	mValue(defaultVal),
	mMin(minVal),
	mMax(maxVal),
	mDisplayTexts(Delete),
	mLabel(label)
{
	#ifndef NDEBUG
	assert(minVal != maxVal);
	AssertValue(defaultVal);
	assert(displayPrecision >= 0);
	#endif

	mDisplayPrecision = displayPrecision;
}

void IDoubleParam::SetDisplayText(const double normalizedValue, const char* const text)
{
	const int key = ToIntKey(normalizedValue);
	WDL_FastString* pDT = mDisplayTexts.Get(key);

	if (!pDT) mDisplayTexts.Insert(key, pDT = new WDL_FastString);

	pDT->Set(text);
}

double IDoubleParam::DBToAmp() const
{
	return DB2VAL(mValue);
}

void IDoubleParam::SetNormalized(const double normalizedValue)
{
	mValue = FromNormalized(normalizedValue);
}

double IDoubleParam::GetNormalized() const
{
	return Normalize(mValue, mMin, mMax);
}

double IDoubleParam::GetNormalized(const double nonNormalizedValue) const
{
	return Normalize(nonNormalizedValue, mMin, mMax);
}

char* IDoubleParam::ToString(const double nonNormalizedValue, char* const buf, const int bufSize, const double* const pNormalizedValue) const
{
	const char* displayText = NULL;

	// Limits min/max value and precision, but should be fine for anything
	// reasonable.
	char tmp[16];

	if (GetNDisplayTexts())
	{
		const double normalizedValue = pNormalizedValue ? *pNormalizedValue : GetNormalized();
		displayText = GetDisplayText(normalizedValue);
	}

	if (!displayText)
	{
		displayText = tmp;

		double displayValue = nonNormalizedValue;
		const bool nz = displayValue != 0.0;
		if (nz && DisplayIsNegated()) displayValue = -displayValue;

		const bool sign = nz && (mMin >= 0.0) != (mMax >= 0.0);
		snprintf(tmp, sizeof(tmp), sign ? "%+.*f" : "%.*f", (int)mDisplayPrecision, displayValue);
	}

	lstrcpyn_safe(buf, displayText, bufSize);
	return buf;
}

char* IDoubleParam::GetDisplayForHost(char* const buf, const int bufSize)
{
	return ToString(mValue, buf, bufSize);
}

char* IDoubleParam::GetDisplayForHost(const double normalizedValue, char* const buf, const int bufSize)
{
	const double nonNormalizedValue = FromNormalized(normalizedValue);
	return ToString(nonNormalizedValue, buf, bufSize, &normalizedValue);
}

const char* IDoubleParam::GetLabelForHost() const
{
	return mLabel.Get();
}

int IDoubleParam::ToIntKey(const double normalizedValue)
{
	assert(normalizedValue >= 0.0 && normalizedValue <= 1.0);

	const double x = normalizedValue + 1.0;

	WDL_UINT64 i;
	memcpy(&i, &x, sizeof(WDL_UINT64));

	const int y = (int)(i >> 20) + (int)((i >> 19) & 1);
	return (int)(i >> 62) ? 0xFFFFFFFF : y;
}

double IDoubleParam::FromIntKey(const int key)
{
	WDL_UINT64 i = ((WDL_UINT64)(unsigned int)key << 20) | WDL_UINT64_CONST(0x3FF0000000000000);
	i = key == 0xFFFFFFFF ? WDL_UINT64_CONST(0x4000000000000000) : i;

	double x;
	memcpy(&x, &i, sizeof(double));

	const double normalizedValue = x - 1.0;

	return normalizedValue;
}

const char* IDoubleParam::GetDisplayText(const double normalizedValue) const
{
	const int key = ToIntKey(normalizedValue);
	const WDL_FastString* const pDT = mDisplayTexts.Get(key);

	return pDT ? pDT->Get() : NULL;
}

bool IDoubleParam::MapDisplayText(const char* const str, double* const pNormalizedValue) const
{
	const int n = mDisplayTexts.GetSize();

	for (int i = 0; i < n; ++i)
	{
		int key;
		const WDL_FastString* const pDT = mDisplayTexts.Enumerate(i, &key);

		if (!stricmp(str, pDT->Get()))
		{
			*pNormalizedValue = FromIntKey(key);
			return true;
		}
	}

	return false;
}

bool IDoubleParam::Serialize(ByteChunk* const pChunk) const
{
	return !!pChunk->PutDouble(mValue);
}

int IDoubleParam::Unserialize(const ByteChunk* const pChunk, const int startPos)
{
	return pChunk->GetDouble(&mValue, startPos);
}

#ifndef NDEBUG
void IDoubleParam::AssertValue(const double nonNormalizedValue) const
{
	const float value = (float)nonNormalizedValue;

	const float minVal = (float)wdl_min(mMin, mMax);
	const float maxVal = (float)wdl_max(mMin, mMax);

	assert(value >= minVal && value <= maxVal);
}
#endif

IDoublePowParam::IDoublePowParam(
	const double shape,
	const char* const name,
	const double defaultVal,
	const double minVal,
	const double maxVal,
	const int displayPrecision,
	const char* const label
):
	IDoubleParam(name, defaultVal, minVal, maxVal, displayPrecision, label)
{
	SetShape(shape);
}

void IDoublePowParam::SetShape(const double nonNormalizedValue, const double normalizedValue)
{
	SetShape(IParam::GetPowShape(IDoubleParam::ToNormalized(nonNormalizedValue), normalizedValue));
}

void IDoublePowParam::SetNormalized(const double normalizedValue)
{
	mValue = FromNormalized(normalizedValue);
}

double IDoublePowParam::GetNormalized() const
{
	return Normalize(mValue, mMin, mMax, mShape);
}

double IDoublePowParam::GetNormalized(const double nonNormalizedValue) const
{
	return Normalize(nonNormalizedValue, mMin, mMax, mShape);
}

char* IDoublePowParam::GetDisplayForHost(const double normalizedValue, char* const buf, const int bufSize)
{
	const double nonNormalizedValue = FromNormalized(normalizedValue);
	return ToString(nonNormalizedValue, buf, bufSize, &normalizedValue);
}

IDoubleExpParam::IDoubleExpParam(
	const double shape,
	const char* const name,
	const double defaultVal,
	const double minVal,
	const double maxVal,
	const int displayPrecision,
	const char* const label
):
	IDoubleParam(name, defaultVal, minVal, maxVal, displayPrecision, label)
{
	SetShape(shape);
}

void IDoubleExpParam::SetShape(const double nonNormalizedValue, const double normalizedValue)
{
	assert(normalizedValue == 0.5);
	SetShape(IParam::GetExpShape(IDoubleParam::ToNormalized(nonNormalizedValue)));
}

void IDoubleExpParam::SetNormalized(const double normalizedValue)
{
	mValue = FromNormalized(normalizedValue);
}

double IDoubleExpParam::GetNormalized() const
{
	return Normalize(mValue, mMin, mMax, mShape, mExpMin1);
}

double IDoubleExpParam::GetNormalized(const double nonNormalizedValue) const
{
	return Normalize(nonNormalizedValue, mMin, mMax, mShape, mExpMin1);
}

char* IDoubleExpParam::GetDisplayForHost(const double normalizedValue, char* const buf, const int bufSize)
{
	const double nonNormalizedValue = FromNormalized(normalizedValue);
	return ToString(nonNormalizedValue, buf, bufSize, &normalizedValue);
}

INormalizedParam::INormalizedParam(
	const char* const name,
	const double defaultVal
):
	IParam(kTypeNormalized, name),
	mValue(defaultVal)
{
	assert(defaultVal >= 0.0 && defaultVal <= 1.0);
}

void INormalizedParam::SetNormalized(const double normalizedValue)
{
	Set(normalizedValue);
}

double INormalizedParam::GetNormalized(const double nonNormalizedValue) const
{
	return Bounded(nonNormalizedValue);
}

char* INormalizedParam::ToString(const double normalizedValue, char* const buf, const int bufSize) const
{
	assert(normalizedValue >= 0.0 && normalizedValue <= 1.0);

	char tmp[12];
	snprintf(tmp, sizeof(tmp), "%.6g", normalizedValue);

	if (tmp[0] && !tmp[1])
	{
		tmp[1] = '.';
		tmp[2] = '0';
		tmp[3] = 0;
	}

	lstrcpyn_safe(buf, tmp, bufSize);
	return buf;
}

char* INormalizedParam::GetDisplayForHost(char* const buf, const int bufSize)
{
	return ToString(mValue, buf, bufSize);
}

char* INormalizedParam::GetDisplayForHost(const double normalizedValue, char* const buf, const int bufSize)
{
	return ToString(normalizedValue, buf, bufSize);
}

bool INormalizedParam::Serialize(ByteChunk* const pChunk) const
{
	return !!pChunk->PutDouble(mValue);
}

int INormalizedParam::Unserialize(const ByteChunk* const pChunk, const int startPos)
{
	return pChunk->GetDouble(&mValue, startPos);
}
