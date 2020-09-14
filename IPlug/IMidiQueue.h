/*
	IPlug - IMidiQueue.h
	Copyright (C) 2009-2020 Theo Niessink

	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
	   claim that you wrote the original software. If you use this software
	   in a product, an acknowledgment in the product documentation would be
	   appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
	   misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.

*/

/*
	IMidiQueue is a fast, lean & mean MIDI queue for IPlug instruments or
	effects. Here are some code snippets showing how to implement IMidiQueue
	in an IPlug project:

	MyPlug.h:

	#include "IPlug/IMidiQueue.h"

	class MyPlug: public IPlug
	{
	public:
		void ProcessDoubleReplacing(const double* const* inputs, double* const* outputs, int nFrames);
		void ProcessMidiMsg(const IMidiMsg* pMsg);
		void SetBlockSize(int blockSize);

	private:
		IMidiQueue mMidiQueue;
	}

	MyPlug.cpp:

	void MyPlug::ProcessDoubleReplacing(const double* const* const inputs, double* const* const outputs, const int nFrames)
	{
		for (int offset = 0; offset < nFrames; ++offset)
		{
			while (!mMidiQueue.Empty())
			{
				const IMidiMsg* const pMsg = mMidiQueue.Peek();
				if (pMsg->mOffset > offset) break;

				// To-do: Handle the MIDI message

				mMidiQueue.Remove();
			}

			// To-do: Process audio

		}
		mMidiQueue.Flush(nFrames);
	}

	void MyPlug::ProcessMidiMsg(const IMidiMsg* const pMsg)
	{
		mMidiQueue.Add(pMsg);
	}

	void MyPlug::SetBlockSize(const int blockSize)
	{
		IPlug::SetBlockSize(blockSize);
		mMidiQueue.Resize(GetBlockSize(), false);
	}

*/

#pragma once

#include "IPlugBase.h"
#include "IPlugStructs.h"

#include "WDL/heapbuf.h"
#include "WDL/wdltypes.h"

class IMidiQueue
{
public:
	IMidiQueue(const int size = IPlugBase::kDefaultBlockSize)
	{
		mFront = mBack = 0;
		mGrow = size;
		Expand();
	}

	// Adds a MIDI message at the back of the queue. If the queue is full,
	// it will automatically expand itself.
	void Add(const IMidiMsg* const pMsg)
	{
		if (mBack >= GetSize())
		{
			if (mFront > 0)
				Compact();
			else
				if (!Expand()) return;
		}

		IMidiMsg* const buf = mBuf.GetFast();

		#ifndef DONT_SORT_IMIDIQUEUE
		// Insert the MIDI message at the right offset.
		if (mBack > mFront && pMsg->mOffset < buf[mBack - 1].mOffset)
		{
			int i = mBack - 2;
			while (i >= mFront && pMsg->mOffset < buf[i].mOffset) --i;
			i++;
			memmove(&buf[i + 1], &buf[i], (mBack - i) * sizeof(IMidiMsg));
			buf[i] = *pMsg;
		}
		else
		#endif
		{
			buf[mBack] = *pMsg;
		}

		++mBack;
	}

	// Removes a MIDI message from the front of the queue (but does *not*
	// free up its space until Compact() is called).
	inline void Remove() { ++mFront; }

	// Returns true if the queue is empty.
	inline bool Empty() const { return mFront == mBack; }

	// Returns the number of MIDI messages in the queue.
	inline int ToDo() const { return mBack - mFront; }

	// Returns the number of MIDI messages for which memory has already been
	// allocated.
	inline int GetSize() const { return mBuf.GetSize(); }

	// Returns the "next" MIDI message (all the way in the front of the
	// queue), but does *not* remove it from the queue.
	inline const IMidiMsg* Peek() const { return &mBuf.GetFast()[mFront]; }

	// Moves back MIDI messages all the way to the front of the queue, thus
	// freeing up space at the back, and updates the sample offset of the
	// remaining MIDI messages by substracting nFrames.
	void Flush(int const nFrames)
	{
		// Move everything all the way to the front.
		if (mFront > 0) Compact();

		// Update the sample offset.
		IMidiMsg* const buf = mBuf.GetFast();
		for (int i = 0; i < mBack; ++i) buf[i].mOffset -= nFrames;
	}

	// Clears the queue.
	inline void Clear() { mFront = mBack = 0; }

	// Resizes (grows or shrinks) the queue, returns the new size.
	int Resize(int size, const bool resizeDown = true)
	{
		if (mFront > 0) Compact();
		mGrow = size;

		// Don't shrink below the number of currently queued MIDI messages.
		size = wdl_max(size, mBack);

		mBuf.Resize(size, resizeDown);
		return mBuf.GetSize();
	}

protected:
	// Automatically expands the queue.
	bool Expand()
	{
		if (!mGrow) return false;
		const int size = (GetSize() / mGrow + 1) * mGrow;

		mBuf.Resize(size);
		return mBuf.GetSize() == size;
	}

	// Moves everything all the way to the front.
	void Compact()
	{
		mBack -= mFront;
		if (mBack > 0)
		{
			IMidiMsg* const buf = mBuf.GetFast();
			memmove(&buf[0], &buf[mFront], mBack * sizeof(IMidiMsg));
		}
		mFront = 0;
	}

	WDL_TypedBuf<IMidiMsg> mBuf;
	int mFront, mBack, mGrow;
};
