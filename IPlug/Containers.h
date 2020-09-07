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
//  changing on Intel x86 platforms.

#ifdef WDL_BIG_ENDIAN

	inline int Put(const unsigned short* pVal)
	{
		unsigned short i = WDL_bswap16_if_be(*pVal);
		return PutBytes(&i, 2);
	}

	inline int Get(unsigned short* pVal, int startPos)
	{
		startPos = GetBytes(pVal, 2, startPos);
		WDL_BSWAP16_IF_BE(*pVal);
		return startPos;
	}

	inline int Put(const unsigned int* pVal)
	{
		unsigned int i = WDL_bswap32_if_be(*pVal);
		return PutBytes(&i, 4);
	}

	inline int Get(unsigned int* pVal, int startPos)
	{
		startPos = GetBytes(pVal, 4, startPos);
		WDL_BSWAP32_IF_BE(*pVal);
		return startPos;
	}

	inline int Put(const WDL_UINT64* pVal)
	{
		WDL_UINT64 i = WDL_bswap64_if_be(*pVal);
		return PutBytes(&i, 8);
	}

	inline int Get(WDL_UINT64* pVal, int startPos)
	{
		startPos = GetBytes(pVal, 8, startPos);
		WDL_BSWAP64_IF_BE(*pVal);
		return startPos;
	}

	// Signed

	inline int Put(const short*     pVal) { return Put((const unsigned short*) pVal); }
	inline int Put(const int*       pVal) { return Put((const unsigned int*)   pVal); }
	inline int Put(const WDL_INT64* pVal) { return Put((const WDL_UINT64*)     pVal); }

	inline int Get(short*     pVal, int startPos) { return Get((unsigned short*) pVal, startPos); }
	inline int Get(int*       pVal, int startPos) { return Get((unsigned int*)   pVal, startPos); }
	inline int Get(WDL_INT64* pVal, int startPos) { return Get((WDL_UINT64*)     pVal, startPos); }

	// Floats

	inline int Put(const float* pVal)
	{
		unsigned int i = WDL_bswapf_if_be(*pVal);
		return PutBytes(&i, 4);
	}

	inline int Get(float* pVal, int startPos)
	{
		unsigned int i;
		startPos = GetBytes(&i, 4, startPos);
		*pVal = WDL_bswapf_if_be(i);
		return startPos;
	}

	inline int Put(const double* pVal)
	{
		WDL_UINT64 i = WDL_bswapf_if_be(*pVal);
		return PutBytes(&i, 8);
	}

	inline int Get(double* pVal, int startPos)
	{
		WDL_UINT64 i;
		startPos = GetBytes(&i, 8, startPos);
		*pVal = WDL_bswapf_if_be(i);
		return startPos;
	}

#endif // WDL_BIG_ENDIAN

	inline int PutStr(const char* str) 
  {
    int slen = strlen(str);
        #ifdef WDL_BIG_ENDIAN
        { const unsigned int i = WDL_bswap32_if_be(slen); Put(&i); }
        #else
		Put(&slen);
		#endif
		return PutBytes(str, slen);
	}

	inline int GetStr(WDL_String* pStr, int startPos)
  {
		int len;
    int strStartPos = Get(&len, startPos);
    if (strStartPos >= 0) {
      WDL_BSWAP32_IF_BE(len);
      int strEndPos = strStartPos + len;
      if (strEndPos <= mBytes.GetSize() && len > 0) {
        pStr->Set((char*) (mBytes.Get() + strStartPos), len);
      }
      return strEndPos;
    }
    return -1;
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

	template <class T> int PutInt(const T n)
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
			*(T*)((char*)mBytes.GetFast() + oldSize) = bswap_if_be(n);
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
			*(T*)((char*)mBytes.GetFast() + oldSize) = x;
		}

		return delta;
	}

	#else

	inline int PutFloat(const float x) { return PutInt(*(const int*)&x); }
	inline int PutFloat(const double x) { return PutInt(*(const WDL_INT64*)&x); }

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
			*pVal = bswap_if_be(*(const T*)((const char*)mBytes.GetFast() + startPos));
		else
			endPos = -1;

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
			*pVal = *(const T*)((const char*)mBytes.GetFast() + startPos);
		else
			endPos = -1;

		return endPos;
	}

	#else

	inline int GetFloat(float* const pVal, const int startPos) const { return GetInt((int*)pVal, startPos); }
	inline int GetFloat(double* const pVal, const int startPos) const { return GetInt((WDL_INT64*)pVal, startPos); }

	#endif

	inline int GetDouble(double* const pVal, const int startPos) const { return GetFloat(pVal, startPos); }

  inline int PutChunk(ByteChunk* pRHS)
  {
    return PutBytes(pRHS->GetBytes(), pRHS->Size());
  }

	// Optimal default size = 4095 - 96, see WDL_HeapBuf.
	static const int kDefaultSize = 3999;

	void Alloc(const int size = kDefaultSize, const bool resizeDown = true)
	{
		mBytes.Resize(size, resizeDown);
		Clear();
	}

	inline void Clear()
	{
		mSize = 0;
	}

	inline int Size() const
	{
		return mSize;
	}

  inline int Resize(int newSize) 
  {
    int n = mBytes.GetSize();
    mBytes.Resize(newSize);
    if (newSize > n) {
      memset(mBytes.Get() + n, 0, (newSize - n));
    }
    return n;
  }

	inline void* GetBytes()
	{
		return mBytes.Get();
	}

	inline const void* GetBytes() const
	{
		return mBytes.Get();
	}

  inline bool IsEqual(ByteChunk* pRHS)
  {
    return (pRHS && pRHS->Size() == Size() && !memcmp(pRHS->GetBytes(), GetBytes(), Size()));
  }

protected:
	template <class T> static inline T bswap_if_be(const T i)
	{
		#ifdef WDL_BIG_ENDIAN
		return WDL_bswap_if_be(i);
		#else
		return i;
		#endif
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
