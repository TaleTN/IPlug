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

	virtual bool Serialize(ByteChunk* pChunk) const = 0;
	virtual int Unserialize(const ByteChunk* pChunk, int startPos) = 0;
	virtual int Size() const = 0;

protected:
	char mType, mDisplayPrecision;
	bool mBoolVal;

	unsigned int mNegateDisplay:1, mGlobalParam:1, _unused:30;

	WDL_FastString mName;
};

class IBoolParam: public IParam
{
public:
	IBoolParam(
		const char* name,
		bool defaultVal = false,
		const char* off = NULL,
		const char* on = NULL
	);

	inline void Set(const bool boolVal) { mBoolVal = boolVal; }
	void SetDisplayText(bool boolVal, const char* text);

	inline bool Bool() const { return mBoolVal; }

	void SetNormalized(double normalizedValue);
	double GetNormalized() const { return (double)mBoolVal; }
	double GetNormalized(double nonNormalizedValue) const;
	char* GetDisplayForHost(char* buf, int bufSize = 128);
	char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128);

	char* ToString(bool boolVal, char* buf, int bufSize = 128) const;

	int GetNDisplayTexts() const { return 2; }
	const char* GetDisplayText(bool boolVal) const;
	bool MapDisplayText(const char* str, double* pNormalizedValue) const;

	bool Serialize(ByteChunk* pChunk) const;
	int Unserialize(const ByteChunk* pChunk, int startPos);
	int Size() const { return (int)sizeof(char); }

protected:
	WDL_FastString mDisplayTexts[2];
};
