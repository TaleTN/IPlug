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

  inline int PutBool(bool b)
  {
    int n = mBytes.GetSize();
    mBytes.Resize(n + 1);
    *(mBytes.Get() + n) = (BYTE) (b ? 1 : 0);
    return mBytes.GetSize();
  }

  inline int GetBool(bool* pB, int startPos)
  {
    int endPos = startPos + 1;
    if (startPos >= 0 && endPos <= mBytes.GetSize()) {
      BYTE byt = *(mBytes.Get() + startPos);
      *pB = (byt);
      return endPos;
    }
    return -1;
  }

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
