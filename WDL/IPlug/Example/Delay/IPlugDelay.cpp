/*

IPlug delay example
(c) Theo Niessink 2012
<http://www.taletn.com/>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software in a
   product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.


Simple IPlug audio effect that shows how to implement a delay buffer.

*/


#include "IPlugDelay.h"
#include "../../IPlug_include_in_plug_src.h"


IPlugDelay::IPlugDelay(IPlugInstanceInfo instanceInfo):
IPLUG_CTOR(kNumParams, 0, instanceInfo),
mBuffer(NULL), mDelaySamples(0), mSampleRate(0.), mDecay(0.)
{
	TRACE;

	GetParam(kDecay)->InitDouble("Decay", 0.5, 0., 1., 0.001);
}


IPlugDelay::~IPlugDelay()
{
	if (mBuffer) free(mBuffer);
}


void IPlugDelay::Reset()
{
	TRACE; IMutexLock lock(this);

	if (GetSampleRate() != mSampleRate)
	{
		mSampleRate = GetSampleRate();

		const int delayMilliseconds = 500;
		mDelaySamples = int((double)delayMilliseconds * 0.001 * mSampleRate);
		mBuffer = (double*)realloc(mBuffer, mDelaySamples * sizeof(double));
		memset(mBuffer, 0, mDelaySamples * sizeof(double));
	}
}


void IPlugDelay::ProcessDoubleReplacing(double** inputs, double** outputs, int nFrames)
{
	if (!mBuffer)
	{
		IPlug::ProcessDoubleReplacing(inputs, outputs, nFrames);
		return;
	}

	int i, j, n;
	if (nFrames < mDelaySamples)
	{
		memmove(&mBuffer[0], &mBuffer[nFrames], (mDelaySamples - nFrames) * sizeof(double));
		i = 0;
		j = mDelaySamples - nFrames;
		n = nFrames;
	}
	else
	{
		i = nFrames - mDelaySamples;
		j = 0;
		n = mDelaySamples;
	}
	for (; i < n; ++i, ++j)
	{
		mBuffer[j] = 0.5 * (inputs[0][i] + inputs[1][i]);
	}

	if (nFrames < mDelaySamples)
	{
		i = 0;
		// n = nFrames;
	}
	else
	{
		i = nFrames - mDelaySamples;
		n = i * sizeof(double);
		memcpy(outputs[0], inputs[0], n);
		memcpy(outputs[1], inputs[1], n);
		n = mDelaySamples;
	}
	double decay = GetParam(kDecay)->Value();
	for (j = 0; i < n; ++i, ++j)
	{
		outputs[0][i] = inputs[0][i] + mBuffer[j] * decay;
		outputs[1][i] = inputs[1][i] + mBuffer[j] * decay;
	}
}
