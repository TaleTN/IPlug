#include "IParam.h"
#include <stdio.h>

#define MAX_PARAM_DISPLAY_PRECISION 6

IParam::IParam()
:	mType(kTypeNone), mNegateDisplay(false)
{
    memset(mName, 0, MAX_PARAM_NAME_LEN * sizeof(char));
}

IParam::~IParam()
{
}

void IParam::SetNormalized(double normalizedValue)
{
  mValue = FromNormalizedParam(normalizedValue, mMin, mMax, mShape);
	if (mType != kTypeDouble) {
		mValue = floor(0.5 + mValue / mStep) * mStep;
	}
	mValue = mMin > mMax ? MAX(mValue, mMax) : MIN(mValue, mMax);
}

double IParam::GetNormalized()
{
	return GetNormalized(mValue);
}

double IParam::GetNormalized(double nonNormalizedValue)
{
  nonNormalizedValue = BOUNDED(nonNormalizedValue, mMin, mMax);
  return ToNormalizedParam(nonNormalizedValue, mMin, mMax, mShape);
}

void IParam::GetDisplayForHost(double value, bool normalized, char* rDisplay)
{
  if (normalized) {
    value = FromNormalizedParam(value, mMin, mMax, mShape);
  }

  const char* displayText = GetDisplayText((int) value);
  if (CSTR_NOT_EMPTY(displayText)) {
    strcpy(rDisplay, displayText);
    return;
  }

	double displayValue = value;
	if (mNegateDisplay) {
		displayValue = -displayValue;
	}

	if (displayValue == 0.0) {
		strcpy(rDisplay, "0");
	}
	else
	if (mDisplayPrecision == 0) {
		sprintf(rDisplay, "%d", int(displayValue));
	} 
	else {
		sprintf(rDisplay, "%.*f", mDisplayPrecision, displayValue);
	}
}

const char* IParam::GetNameForHost()
{
  return mName;
}

const char* IParam::GetLabelForHost()
{
  return mLabel;
}

int IParam::GetNDisplayTexts()
{
  return mDisplayTexts.GetSize();
}

bool IParam::MapDisplayText(char* str, int* pValue)
{
  int n = mDisplayTexts.GetSize();
  if (n) {
    DisplayText* pDT = mDisplayTexts.Get();
    for (int i = 0; i < n; ++i, ++pDT) {
      if (!strcmp(str, pDT->mText)) {
        *pValue = pDT->mValue;
        return true;
      }
    }
  }
  return false;
}
