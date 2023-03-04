#pragma once

#include "Containers.h"

#include <assert.h>
#include <math.h>

#include "WDL/assocarray.h"
#include "WDL/ptrlist.h"
#include "WDL/wdlstring.h"
#include "WDL/wdltypes.h"

class IParam
{
public:
	enum EParamType { kTypeNone = 0, kTypeBool, kTypeInt, kTypeEnum, kTypeDouble, kTypeNormalized };

	IParam(
		const int type,
		const char* const name
	):
		mType(type),
		mNegateDisplay(0),
		mGlobalParam(0),
		_unused(0),
		mName(name)
	{
		mShortName[0] = 0;
	}

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

	const char* GetNameForHost(const int wantSize) const
	{
		return wantSize <= 8 && mShortName[0] ? mShortName : mName.Get();
	}

	void SetShortName(const char* name);
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

	// Returns IDoublePowParam shape so non-normalized value
	// y*(maxVal - minVal) + minVal corresponds to normalized value x.
	static double GetPowShape(const double y, const double x = 0.5)
	{
		assert(y > 0.0 && y < 1.0);
		assert(x > 0.0 && x < 1.0);
		return log(y) / log(x);
	}

	// Returns IDoubleExpParam shape so non-normalized value
	// y*(maxVal - minVal) + minVal corresponds to normalized value 0.5.
	static double GetExpShape(const double y)
	{
		assert(y > 0.0 && y < 1.0);
		return log((1.0 - y) / y) * 2.0;
	}

protected:
	static double Clamped(double normalizedValue)
	{
		normalizedValue = wdl_max(normalizedValue, 0.0);
		normalizedValue = wdl_min(normalizedValue, 1.0);
		return normalizedValue;
	}

	static void Delete(WDL_FastString* const str) { delete str; }

	char mType, mDisplayPrecision;
	bool mBoolVal;

	unsigned int mNegateDisplay:1, mGlobalParam:1, _unused:30;

	WDL_FastString mName;
	char mShortName[8];
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

class IEnumParam: public IParam
{
public:
	IEnumParam(
		const char* name,
		int defaultVal = 0,
		int nEnums = 2
	);

	inline void Set(const int intVal)
	{
		assert(intVal >= 0 && intVal < mEnums);
		mIntVal = intVal;
	}

	void SetDisplayText(int intVal, const char* text);

	inline int Int() const { return mIntVal; }
	inline int NEnums() const { return mEnums; }

	int FromNormalized(const double normalizedValue) const
	{
		assert(normalizedValue >= 0.0 && normalizedValue <= 1.0);
		return (int)((double)(mEnums - 1) * normalizedValue + 0.5);
	}

	double ToNormalized(const int intVal) const
	{
		assert(intVal >= 0 && intVal < mEnums);
		return (double)intVal / (double)(mEnums - 1);
	}

	int Bounded(int intVal) const
	{
		const int n = mEnums - 1;
		intVal = wdl_max(intVal, 0);
		intVal = wdl_min(intVal, n);
		return intVal;
	}

	void SetNormalized(double normalizedValue);
	double GetNormalized() const;
	double GetNormalized(double nonNormalizedValue) const;
	char* GetDisplayForHost(char* buf, int bufSize = 128);
	char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128);

	char* ToString(int intVal, char* buf, int bufSize = 128) const;

	int GetNDisplayTexts() const { return mDisplayTexts.GetSize(); }
	const char* GetDisplayText(int intVal) const;
	bool MapDisplayText(const char* str, double* pNormalizedValue) const;

	bool Serialize(ByteChunk* pChunk) const;
	int Unserialize(const ByteChunk* pChunk, int startPos);
	int Size() const { return (int)sizeof(int); }

protected:
	int mIntVal;
	const int mEnums;

	WDL_PtrList_DeleteOnDestroy<WDL_FastString> mDisplayTexts;
};

class IIntParam: public IParam
{
public:
	IIntParam(
		const char* name,
		int defaultVal = 0,
		int minVal = 0,
		int maxVal = 1,
		const char* label = NULL
	);

	inline void Set(const int intVal)
	{
		#ifndef NDEBUG
		AssertInt(intVal);
		#endif

		mIntVal = intVal;
	}

	void SetDisplayText(int intVal, const char* text);

	inline int Int() const { return mIntVal; }
	inline int Min() const { return mMin; }
	inline int Max() const { return mMax; }

	int FromNormalized(const double normalizedValue) const
	{
		assert(normalizedValue >= 0.0 && normalizedValue <= 1.0);
		return (int)floor((double)(mMax - mMin) * normalizedValue + (double)mMin + 0.5);
	}

	double ToNormalized(const int intVal) const
	{
		#ifndef NDEBUG
		AssertInt(intVal);
		#endif

		return Normalized(intVal, mMin, mMax);
	}

	int Bounded(int intVal) const
	{
		const int minVal = wdl_min(mMin, mMax);
		intVal = wdl_max(intVal, minVal);

		const int maxVal = wdl_max(mMin, mMax);
		intVal = wdl_min(intVal, maxVal);

		return intVal;
	}

	void SetNormalized(double normalizedValue);
	double GetNormalized() const;
	double GetNormalized(double nonNormalizedValue) const;
	char* GetDisplayForHost(char* buf, int bufSize = 128);
	char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128);
	const char* GetLabelForHost() const;

	char* ToString(int intVal, char* buf, int bufSize = 128) const;

	int GetNDisplayTexts() const { return mDisplayTexts.GetSize(); }
	const char* GetDisplayText(int intVal) const;
	bool MapDisplayText(const char* str, double* pNormalizedValue) const;

	bool Serialize(ByteChunk* pChunk) const;
	int Unserialize(const ByteChunk* pChunk, int startPos);
	int Size() const { return (int)sizeof(int); }

protected:
	#ifndef NDEBUG
	void AssertInt(int intVal) const;
	#endif

	static double Normalized(const int intVal, const int minVal, const int maxVal)
	{
		return Clamped((double)(intVal - minVal) / (double)(maxVal - minVal));
	}

	int mIntVal, mMin, mMax;

	WDL_IntKeyedArray<WDL_FastString*> mDisplayTexts;
	WDL_FastString mLabel;
};

class IDoubleParam: public IParam
{
public:
	IDoubleParam(
		const char* name,
		double defaultVal = 0.0,
		double minVal = 0.0,
		double maxVal = 1.0,
		int displayPrecision = 6,
		const char* label = NULL
	);

	inline void Set(const double nonNormalizedValue)
	{
		#ifndef NDEBUG
		AssertValue(nonNormalizedValue);
		#endif

		mValue = nonNormalizedValue;
	}

	void SetDisplayText(double normalizedValue, const char* text);

	// These return the readable value, not the normalized [0, 1].
	inline double Value() const { return mValue; }
	inline double Min() const { return mMin; }
	inline double Max() const { return mMax; }
	inline int GetDisplayPrecision() const { return mDisplayPrecision; }
	double DBToAmp() const;

	double FromNormalized(const double normalizedValue) const
	{
		assert(normalizedValue >= 0.0 && normalizedValue <= 1.0);
		return (mMax - mMin) * normalizedValue + mMin;
	}

	double ToNormalized(const double nonNormalizedValue) const
	{
		#ifndef NDEBUG
		AssertValue(nonNormalizedValue);
		#endif

		return Normalize(nonNormalizedValue, mMin, mMax);
	}

	double Bounded(double nonNormalizedValue) const
	{
		const double minVal = wdl_min(mMin, mMax);
		nonNormalizedValue = wdl_max(nonNormalizedValue, minVal);

		const double maxVal = wdl_max(mMin, mMax);
		nonNormalizedValue = wdl_min(nonNormalizedValue, maxVal);

		return nonNormalizedValue;
	}

	void SetNormalized(double normalizedValue);
	double GetNormalized() const;
	double GetNormalized(double nonNormalizedValue) const;
	char* GetDisplayForHost(char* buf, int bufSize = 128);
	char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128);
	const char* GetLabelForHost() const;

	char* ToString(double nonNormalizedValue, char* buf, int bufSize = 128, const double* pNormalizedValue = NULL) const;

	static int ToIntKey(double normalizedValue);
	static double FromIntKey(int key); // To normalized value

	int GetNDisplayTexts() const { return mDisplayTexts.GetSize(); }
	const char* GetDisplayText(double normalizedValue) const;
	bool MapDisplayText(const char* str, double* pNormalizedValue) const;

	void GetBounds(double* const pMin, double* const pMax) const
	{
		*pMin = mMin;
		*pMax = mMax;
	}

	bool Serialize(ByteChunk* pChunk) const;
	int Unserialize(const ByteChunk* pChunk, int startPos);
	int Size() const { return (int)sizeof(double); }

	inline void SetDisplayPrecision(const int displayPrecision)
	{
		mDisplayPrecision = displayPrecision;
	}

protected:
	#ifndef NDEBUG
	void AssertValue(double nonNormalizedValue) const;
	#endif

	static double Normalize(const double nonNormalizedValue, const double minVal, const double maxVal)
	{
		return Clamped((nonNormalizedValue - minVal) / (maxVal - minVal));
	}

	// All we store is the readable values.
	// SetNormalized() and GetNormalized() handle conversion from/to [0, 1].
	double WDL_FIXALIGN mValue, mMin, mMax;

	WDL_IntKeyedArray<WDL_FastString*> mDisplayTexts;
	WDL_FastString mLabel;
}
WDL_FIXALIGN;

class IDoublePowParam: public IDoubleParam
{
public:
	IDoublePowParam(
		double shape,
		const char* name,
		double defaultVal = 0.0,
		double minVal = 0.0,
		double maxVal = 1.0,
		int displayPrecision = 6,
		const char* label = NULL
	);

	// The higher the shape, the more resolution around host value zero.
	inline void SetShape(const double shape)
	{
		assert(shape > 0.0);
		mShape = shape;
	}

	// Adjusts the shape so nonNormalizedValue corresponds to normalizedValue.
	void SetShape(double nonNormalizedValue, double normalizedValue);
	inline double GetShape() const { return mShape; }

	double FromNormalized(const double normalizedValue) const
	{
		return IDoubleParam::FromNormalized(pow(normalizedValue, mShape));
	}

	double ToNormalized(const double nonNormalizedValue) const
	{
		#ifndef NDEBUG
		AssertValue(nonNormalizedValue);
		#endif

		return Normalize(nonNormalizedValue, mMin, mMax, mShape);
	}

	void SetNormalized(double normalizedValue);
	double GetNormalized() const;
	double GetNormalized(double nonNormalizedValue) const;
	char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128);

protected:
	static double Normalize(const double nonNormalizedValue, const double minVal, const double maxVal, const double shape)
	{
		double normalizedValue = (nonNormalizedValue - minVal) / (maxVal - minVal);
		normalizedValue = wdl_max(normalizedValue, 0.0);

		normalizedValue = pow(normalizedValue, 1.0 / shape);
		normalizedValue = wdl_min(normalizedValue, 1.0);

		return normalizedValue;
	}

	double WDL_FIXALIGN mShape;
}
WDL_FIXALIGN;

class IDoubleExpParam: public IDoubleParam
{
public:
	IDoubleExpParam(
		double shape,
		const char* name,
		double defaultVal = 0.0,
		double minVal = 0.0,
		double maxVal = 1.0,
		int displayPrecision = 6,
		const char* label = NULL
	);

	// The higher the shape, the more resolution around host value zero.
	void SetShape(const double shape)
	{
		assert(shape != 0.0);

		mShape = shape;
		mExpMin1 = exp(shape) - 1.0;
	}

	// Adjusts the shape so nonNormalizedValue corresponds to normalizedValue = 0.5.
	void SetShape(double nonNormalizedValue, double normalizedValue);
	inline double GetShape() const { return mShape; }

	double FromNormalized(const double normalizedValue) const
	{
		return IDoubleParam::FromNormalized((exp(normalizedValue * mShape) - 1.0) / mExpMin1);
	}

	double ToNormalized(const double nonNormalizedValue) const
	{
		#ifndef NDEBUG
		AssertValue(nonNormalizedValue);
		#endif

		return Normalize(nonNormalizedValue, mMin, mMax, mShape, mExpMin1);
	}

	void SetNormalized(double normalizedValue);
	double GetNormalized() const;
	double GetNormalized(double nonNormalizedValue) const;
	char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128);

protected:
	static double Normalize(const double nonNormalizedValue, const double minVal, const double maxVal, const double shape, const double expMin1)
	{
		double normalizedValue = (nonNormalizedValue - minVal) / (maxVal - minVal);
		normalizedValue = wdl_max(normalizedValue, 0.0);

		normalizedValue = log(normalizedValue * expMin1 + 1.0) / shape;
		normalizedValue = wdl_min(normalizedValue, 1.0);

		return normalizedValue;
	}

	double WDL_FIXALIGN mShape, mExpMin1;
}
WDL_FIXALIGN;

class INormalizedParam: public IParam
{
public:
	INormalizedParam(const char* name, double defaultVal = 0.0);

	inline void Set(const double normalizedValue)
	{
		assert(normalizedValue >= 0.0 && normalizedValue <= 1.0);
		mValue = normalizedValue;
	}

	inline double Value() const { return mValue; }

	static double Bounded(const double normalizedValue)
	{
		return Clamped(normalizedValue);
	}

	void SetNormalized(double normalizedValue);
	double GetNormalized() const { return mValue; }
	double GetNormalized(double nonNormalizedValue) const;
	char* GetDisplayForHost(char* buf, int bufSize = 128);
	char* GetDisplayForHost(double normalizedValue, char* buf, int bufSize = 128);

	char* ToString(double normalizedValue, char* buf, int bufSize = 128) const;

	bool Serialize(ByteChunk* pChunk) const;
	int Unserialize(const ByteChunk* pChunk, int startPos);
	int GetSize() const { return (int)sizeof(double); }

protected:
	double WDL_FIXALIGN mValue;
}
WDL_FIXALIGN;
