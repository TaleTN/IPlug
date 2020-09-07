#pragma once

#include "Containers.h"
#include <math.h>

#ifndef MAX_PARAM_NAME_LEN
const int MAX_PARAM_NAME_LEN = 32;
#endif

inline double ToNormalizedParam(double nonNormalizedValue, double min, double max, double shape)
{
  return pow((nonNormalizedValue - min) / (max - min), 1.0 / shape);
}

inline double FromNormalizedParam(double normalizedValue, double min, double max, double shape)
{
  return min + pow((double) normalizedValue, shape) * (max - min);
}

class IParam
{
public:

  enum EParamType { kTypeNone, kTypeBool, kTypeInt, kTypeEnum, kTypeDouble };

	IParam();
  ~IParam();

  EParamType Type() { return mType; }
	
	// Call this if your param is (x, y) but you want to always display (-x, -y).
	void NegateDisplay() { mNegateDisplay = true; }
	bool DisplayIsNegated() const { return mNegateDisplay; }

	void SetNormalized(double normalizedValue);
	double GetNormalized();
	double GetNormalized(double nonNormalizedValue);
  void GetDisplayForHost(char* rDisplay) { GetDisplayForHost(mValue, false, rDisplay); }
  void GetDisplayForHost(double value, bool normalized, char* rDisplay);
	const char* GetNameForHost();
	const char* GetLabelForHost();

  int GetNDisplayTexts();
	bool MapDisplayText(char* str, int* pValue);	// Reverse map back to value.

private:

  EParamType mType;
	int mDisplayPrecision;
	char mName[MAX_PARAM_NAME_LEN];
	bool mNegateDisplay;
};
