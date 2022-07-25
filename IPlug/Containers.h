#pragma once

#include <assert.h>
#include <string.h>

#include "WDL/heapbuf.h"
#include "WDL/wdlstring.h"
#include "WDL/wdltypes.h"

#if defined(__APPLE__) && __BIG_ENDIAN__
	#include "WDL/wdlendian.h"
#endif

class ByteChunk
{
public:
	ByteChunk(): mSize(0) {}
	~ByteChunk() {}

	int PutBytes(const void* const pBuf, const int size)
	{
		#ifndef NDEBUG
		AssertSize(size);
		#endif

		const int oldSize = mSize, newSize = oldSize + size;
		const int delta = newSize > mBytes.GetSize() ? 0 : size;

		if (delta)
		{
			mSize = newSize;
			memcpy((char*)mBytes.GetFast() + oldSize, pBuf, size);
		}

		return delta;
	}

	int GetBytes(void* const pBuf, const int size, const int startPos) const
	{
		int endPos = startPos + size;
		if (startPos >= 0 && endPos <= mBytes.GetSize())
			memcpy(pBuf, (const char*)mBytes.GetFast() + startPos, size);
		else
			endPos = -1;

		return endPos;
	}

	template <class T> inline int Put(const T* const pVal)
	{
		return PutBytes(pVal, (int)sizeof(T));
	}

	template <class T> inline int Get(T* const pVal, const int startPos) const
	{
		return GetBytes(pVal, (int)sizeof(T), startPos);
	}

	// Handle endian conversion for integer and floating point data types.
	// Data is always stored in the chunk in little endian format, so nothing needs
	// changing on Intel x86 or Apple arm64 platforms.

	inline int Put(const bool* const pVal) { return PutByte(*pVal); }
	inline int Put(const unsigned char* const pVal) { return PutByte(*pVal); }
	inline int Put(const unsigned short* const pVal) { return PutInt((short)*pVal); }
	inline int Put(const unsigned int* const pVal) { return PutInt((int)*pVal); }
	inline int Put(const WDL_UINT64* const pVal) { return PutInt((WDL_INT64)*pVal); }

	inline int Get(bool* const pVal, const int startPos) const { return GetBool(pVal, startPos); }
	inline int Get(unsigned char* const pVal, const int startPos) const { return GetByte((char*)pVal, startPos); }
	inline int Get(unsigned short* const pVal, const int startPos) const { return GetInt((short*)pVal, startPos); }
	inline int Get(unsigned int* const pVal, const int startPos) const { return GetInt((int*)pVal, startPos); }
	inline int Get(WDL_UINT64* const pVal, const int startPos) const { return GetInt((WDL_INT64*)pVal, startPos); }

	// Signed

	inline int Put(const signed char* const pVal) { return PutByte(*pVal); }
	inline int Put(const short* const pVal) { return PutInt(*pVal); }
	inline int Put(const int* const pVal) { return PutInt(*pVal); }
	inline int Put(const WDL_INT64* const pVal) { return PutInt(*pVal); }

	inline int Get(signed char* const pVal, const int startPos) const { return GetByte((char*)pVal, startPos); }
	inline int Get(short* const pVal, const int startPos) const { return GetInt(pVal, startPos); }
	inline int Get(int* const pVal, const int startPos) const { return GetInt(pVal, startPos); }
	inline int Get(WDL_INT64* const pVal, const int startPos) const { return GetInt(pVal, startPos); }

	// Floats

	inline int Put(const float* const pVal) { return PutFloat(*pVal); }
	inline int Put(const double* const pVal) { return PutFloat(*pVal); }

	inline int Get(float* const pVal, const int startPos) const { return GetFloat(pVal, startPos); }
	inline int Get(double* const pVal, const int startPos) const { return GetFloat(pVal, startPos); }

	int PutStr(const char* const str, const int slen)
	{
		int delta = (int)sizeof(int) + slen;

		#ifndef NDEBUG
		AssertSize(delta);
		#endif

		const int oldSize = mSize, newSize = oldSize + delta;
		delta = newSize > mBytes.GetSize() ? 0 : delta;

		if (delta)
		{
			mSize = newSize;
			char* const pBytes = (char*)mBytes.GetFast() + oldSize;

			PutStrLen(pBytes, slen);
			memcpy(pBytes + sizeof(int), str, slen);
		}

		return delta;
	}

	inline int PutStr(const char* const str) { return PutStr(str, (int)strlen(str)); }
	inline int PutStr(const WDL_String* const str) { return PutStr(str->Get(), str->GetLength()); }
	inline int PutStr(const WDL_FastString* const str) { return PutStr(str->Get(), str->GetLength()); }

	int GetStr(char* const pBuf, const int bufSize, const int startPos) const
	{
		int endPos = -1;

		const int strStartPos = startPos + (int)sizeof(int);
		if (startPos >= 0 && strStartPos <= mBytes.GetSize())
		{
			const char* const pBytes = (const char*)mBytes.GetFast();
			const int len = GetStrLen(pBytes + startPos);

			const int strEndPos = strStartPos + len;
			if (strEndPos <= mBytes.GetSize() && len < bufSize)
			{
				memcpy(pBuf, pBytes + strStartPos, len);
				pBuf[len] = 0;

				endPos = strEndPos;
			}
		}

		return endPos;
	}

	int GetStr(WDL_String* const pStr, const int startPos) const
	{
		int endPos = -1;

		const int strStartPos = startPos + (int)sizeof(int);
		if (startPos >= 0 && strStartPos <= mBytes.GetSize())
		{
			const char* const pBytes = (const char*)mBytes.GetFast();
			const int len = GetStrLen(pBytes + startPos);

			const int strEndPos = strStartPos + len;
			if (strEndPos <= mBytes.GetSize() && pStr->SetLen(len))
			{
				memcpy(pStr->Get(), pBytes + strStartPos, len);
				endPos = strEndPos;
			}
		}

		return endPos;
	}

	int GetStr(WDL_FastString* const pStr, const int startPos) const
	{
		int endPos = -1;

		const int strStartPos = startPos + (int)sizeof(int);
		if (startPos >= 0 && strStartPos <= mBytes.GetSize())
		{
			const char* const pBytes = (const char*)mBytes.GetFast();
			const int len = GetStrLen(pBytes + startPos);

			const int strEndPos = strStartPos + len;
			if (strEndPos <= mBytes.GetSize())
			{
				pStr->SetRaw(pBytes + strStartPos, len);
				endPos = pStr->GetLength() == len ? strEndPos : endPos;
			}
		}

		return endPos;
	}

	inline int PutBool(const bool b)
	{
		return PutByte(b);
	}

	int PutByte(const int byte)
	{
		#ifndef NDEBUG
		AssertSize(1);
		#endif

		const int delta = mSize < mBytes.GetSize();
		if (delta) *((char*)mBytes.GetFast() + mSize++) = (char)byte;

		return delta;
	}

	template <class T> int PutInt(T n)
	{
		int delta = (int)sizeof(T);

		#ifndef NDEBUG
		AssertSize(delta);
		#endif

		const int oldSize = mSize, newSize = oldSize + delta;
		delta = newSize > mBytes.GetSize() ? 0 : delta;

		if (delta)
		{
			mSize = newSize;

			#ifdef WDL_BIG_ENDIAN
			n = WDL_bswap_if_be(n);
			#endif

			memcpy((char*)mBytes.GetFast() + oldSize, &n, sizeof(T));
		}

		return delta;
	}

	inline int PutInt16(const int n) { return PutInt((short)n); }
	inline int PutInt32(const int n) { return PutInt(n); }
	inline int PutInt64(const WDL_INT64 n) { return PutInt(n); }

	#ifndef WDL_BIG_ENDIAN

	template <class T> int PutFloat(const T x)
	{
		int delta = (int)sizeof(T);

		#ifndef NDEBUG
		AssertSize(delta);
		#endif

		const int oldSize = mSize, newSize = oldSize + delta;
		delta = newSize > mBytes.GetSize() ? 0 : delta;

		if (delta)
		{
			mSize = newSize;
			memcpy((char*)mBytes.GetFast() + oldSize, &x, sizeof(T));
		}

		return delta;
	}

	#else

	int PutFloat(const float x)
	{
		int n;
		memcpy(&n, &x, sizeof(int));
		return PutInt(n);
	}

	int PutFloat(const double x)
	{
		WDL_INT64 n;
		memcpy(&n, &x, sizeof(WDL_INT64));
		return PutInt(n);
	}

	#endif

	inline int PutDouble(const double x) { return PutFloat(x); }

	int GetBool(bool* const pB, const int startPos) const
	{
		int endPos = startPos + 1;
		if (startPos >= 0 && startPos < mBytes.GetSize())
			*pB = !!*((const char*)mBytes.GetFast() + startPos);
		else
			endPos = -1;

		return endPos;
	}

	int GetByte(char* const pVal, const int startPos) const
	{
		int endPos = startPos + 1;
		if (startPos >= 0 && startPos < mBytes.GetSize())
			*pVal = *((const char*)mBytes.GetFast() + startPos);
		else
			endPos = -1;

		return endPos;
	}

	template <class T> int GetInt(T* const pVal, const int startPos) const
	{
		int endPos = startPos + (int)sizeof(T);
		if (startPos >= 0 && endPos <= mBytes.GetSize())
		{
			T n;
			memcpy(&n, (const char*)mBytes.GetFast() + startPos, sizeof(T));

			#ifdef WDL_BIG_ENDIAN
			n = WDL_bswap_if_be(n);
			#endif

			*pVal = n;
		}
		else
		{
			endPos = -1;
		}

		return endPos;
	}

	inline int GetInt16(short* const pVal, const int startPos) const { return GetInt(pVal, startPos); }
	inline int GetInt32(int* const pVal, const int startPos) const { return GetInt(pVal, startPos); }
	inline int GetInt64(WDL_INT64* const pVal, const int startPos) const { return GetInt(pVal, startPos); }

	#ifndef WDL_BIG_ENDIAN

	template <class T> int GetFloat(T* const pVal, const int startPos) const
	{
		int endPos = startPos + (int)sizeof(T);
		if (startPos >= 0 && endPos <= mBytes.GetSize())
			memcpy(pVal, (const char*)mBytes.GetFast() + startPos, sizeof(T));
		else
			endPos = -1;

		return endPos;
	}

	#else

	int GetFloat(float* const pVal, int pos) const
	{
		int n;
		pos = GetInt(&n, pos);
		if (pos >= 0) memcpy(pVal, &n, sizeof(float));
		return pos;
	}

	int GetFloat(double* const pVal, int pos) const
	{
		WDL_INT64 n;
		pos = GetInt(&n, pos);
		if (pos >= 0) memcpy(pVal, &n, sizeof(double));
		return pos;
	}

	#endif

	inline int GetDouble(double* const pVal, const int startPos) const { return GetFloat(pVal, startPos); }

	inline int PutChunk(const ByteChunk* const pRHS)
	{
		return PutBytes(pRHS->GetBytes(), pRHS->Size());
	}

	// Optimal default size = 4095 - 96, see WDL_HeapBuf.
	static const int kDefaultSize = 3999;

	int Alloc(const int size = kDefaultSize, const bool resizeDown = true)
	{
		mBytes.Resize(size, resizeDown);
		Clear();
		return mBytes.GetSize();
	}

	inline int AllocSize() const
	{
		return mBytes.GetSize();
	}

	inline void Clear()
	{
		mSize = 0;
	}

	inline int Size() const
	{
		return mSize;
	}

	int Resize(int newSize, const bool resizeDown = false)
	{
		int allocSize = newSize;
		if (!mBytes.GetSize() && !resizeDown)
		{
			allocSize = allocSize < kDefaultSize ? kDefaultSize : allocSize;
		}

		if (allocSize > mBytes.GetSize() || resizeDown)
		{
			mBytes.Resize(allocSize, resizeDown);
			allocSize = mBytes.GetSize();
			newSize = newSize > allocSize ? allocSize : newSize;
		}

		const int oldSize = mSize;
		mSize = newSize;

		return oldSize;
	}

	inline void* GetBytes()
	{
		return mBytes.Get();
	}

	inline const void* GetBytes() const
	{
		return mBytes.Get();
	}

	bool IsEqual(const ByteChunk* const pRHS) const
	{
		if (!pRHS) return false;

		const void* const ptr1 = mBytes.Get();
		const void* const ptr2 = pRHS->GetBytes();

		if (!((INT_PTR)ptr1 & (INT_PTR)ptr2) || (pRHS->Size() != mSize)) return false;

		return !memcmp(ptr1, ptr2, mSize);
	}

protected:
	static void PutStrLen(char* const pBytes, int len)
	{
		#ifdef WDL_BIG_ENDIAN
		len = WDL_bswap32(len);
		#endif

		memcpy(pBytes, &len, sizeof(int));
	}

	static int GetStrLen(const char* const pBytes)
	{
		int len;
		memcpy(&len, pBytes, sizeof(int));

		#ifdef WDL_BIG_ENDIAN
		len = WDL_bswap32(len);
		#endif

		return len;
	}

	#ifndef NDEBUG
	void AssertSize(const int size) const
	{
		// Oops, should have allocated larger size.
		const bool notEnoughAllocated = mSize + size <= mBytes.GetSize();
		assert(notEnoughAllocated);
	}
	#endif

private:
	WDL_HeapBuf mBytes;
	int mSize;
};
