#pragma once

#include "Containers.h"
#include "WDL/wdlstring.h"

class IParam
{
public:
	enum EParamType { kTypeNone = 0, kTypeBool, kTypeInt, kTypeEnum, kTypeDouble };

	IParam(
		const int type,
		const char* const name
	):
		mType(type),
		mNegateDisplay(0),
		mGlobalParam(0),
		_unused(0),
		mName(name)
	{}

	virtual ~IParam() {}

	inline int Type() const { return mType; }

	// Call this if your param is [x, y], but you want to always display [-x, -y].
	inline void NegateDisplay(const bool negate = true) { mNegateDisplay = negate; }
	inline bool DisplayIsNegated() const { return mNegateDisplay; }

	inline void SetGlobal(const bool global) { mGlobalParam = global; }
	inline bool IsGlobal() const { return mGlobalParam; }

	virtual void SetNormalized(double normalizedValue) = 0;
	virtual double GetNormalized() const = 0;
	virtual double GetNormalized(double nonNormalizedValue) const = 0;
	virtual char* GetDisplayForHost(char* buf, int bufSize = 128) = 0;
	virtual char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128) = 0;
	const char* GetNameForHost() const { return mName.Get(); }
	virtual const char* GetLabelForHost() const { return ""; }

	virtual int GetNDisplayTexts() const { return 0; }

	// Reverse map back to value.
	virtual bool MapDisplayText(const char* /* str */, double* /* pNormalizedValue */) const
	{
		return false;
	}

protected:
	char mType, mDisplayPrecision;
	unsigned int mNegateDisplay:1, mGlobalParam:1, _unused:30;

	WDL_FastString mName;
};
