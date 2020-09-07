#pragma once

#include "Containers.h"

#ifndef MAX_PARAM_NAME_LEN
const int MAX_PARAM_NAME_LEN = 32;
#endif

class IParam
{
public:
	enum EParamType { kTypeNone = 0, kTypeBool, kTypeInt, kTypeEnum, kTypeDouble };

	IParam(
		const int type
	):
		mType(type),
		mNegateDisplay(false)
	{}

	virtual ~IParam() {}

	inline int Type() const { return mType; }

	// Call this if your param is (x, y) but you want to always display (-x, -y).
	void NegateDisplay() { mNegateDisplay = true; }
	bool DisplayIsNegated() const { return mNegateDisplay; }

	virtual void SetNormalized(double normalizedValue) = 0;
	virtual double GetNormalized() const = 0;
	virtual double GetNormalized(double nonNormalizedValue) const = 0;
	virtual char* GetDisplayForHost(char* buf, int bufSize = 128) = 0;
	virtual char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128) = 0;
	const char* GetNameForHost();
	virtual const char* GetLabelForHost() const { return ""; }

	virtual int GetNDisplayTexts() const { return 0; }

	// Reverse map back to value.
	virtual bool MapDisplayText(const char* /* str */, double* /* pNormalizedValue */) const
	{
		return false;
	}

protected:
	char mType, mDisplayPrecision;
	char mName[MAX_PARAM_NAME_LEN];
	bool mNegateDisplay;
};
