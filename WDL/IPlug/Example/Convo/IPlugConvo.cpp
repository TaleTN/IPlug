/*

IPlug convoengine example
(c) Theo Niessink 2010-2012
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


A simple IPlug plug-in effect that shows how to use WDL's fast convolution
engine.

*/


#include "IPlugConvo.h"
#include "../../IPlug_include_in_plug_src.h"

#include "../../../pcmfmtcvt.h"


enum EParams
{
	kDry,
	kWet,
	kNumParams
};


IPlugConvo::IPlugConvo(IPlugInstanceInfo instanceInfo):
	IPLUG_CTOR(kNumParams, 0, instanceInfo),
	mSampleRate(0)
{
	TRACE;

	GetParam(kDry)->InitDouble("Dry", 0., 0., 1., 0.001);
	GetParam(kWet)->InitDouble("Wet", 1., 0., 1., 0.001);
}


void IPlugConvo::OnParamChange(int paramIdx)
{
	IMutexLock lock(this);

	switch (paramIdx)
	{
		case kDry:
			mDry = GetParam(kDry)->Value();
			break;

		case kWet:
			mWet = GetParam(kWet)->Value();
			break;
	}
}


void IPlugConvo::Reset()
{
	TRACE; IMutexLock lock(this);

	// Detect a change in sample rate.
	int sr = int(GetSampleRate());
	if (sr != mSampleRate)
	{
		mSampleRate = sr;

		float* irSrc = (float*)mIR;
		const int irRate = 44100;
		const int mono = 1;

		mImpulse.SetNumChannels(mono);

		int len = sizeof(mIR) / sizeof(float);
		if (mSampleRate != irRate)
		{
			// Resample the impulse response.
			len = int((double)mSampleRate / (double)irRate * (double)len + 0.5);
			len = mImpulse.SetLength(len);
			if (len > 0)
			{
				double state = 0.;
				WDL_FFT_REAL* p = mImpulse.impulses[0].Get();
				#if WDL_FFT_REALSIZE == 8
					float* tmp = (float*)malloc(len * sizeof(float));
					if (tmp)
					{
						memset(tmp, 0, len * sizeof(float));
						mixFloats(irSrc, irRate, mono, tmp, mSampleRate, mono, len, 1.f, 0.f, &state);
						// Convert to impulse response to double precision.
						for (int i = 0; i < len; ++i) *p++ = tmp[i];
						free(tmp);
					}
					else
					{
						memset(p, 0, len * sizeof(WDL_FFT_REAL));
					}
				#else
					memset(p, 0, len * sizeof(WDL_FFT_REAL));
					mixFloats(irSrc, irRate, mono, p, mSampleRate, mono, len, 1.f, 0.f, &state);
				#endif
			}
		}
		else
		{
			len = mImpulse.SetLength(len);
			if (len > 0)
			{
				WDL_FFT_REAL* p = mImpulse.impulses[0].Get();
				for (int i = 0; i < len; ++i) *p++ = *irSrc++;
			}
		}

		// Tie the impulse response to the convolution engine.
		mEngine.SetImpulse(&mImpulse, 0);
	}
}


void IPlugConvo::ProcessDoubleReplacing(double** inputs, double** outputs, int nFrames)
{
	// Send input samples to the convolution engine.
	#if WDL_FFT_REALSIZE == 8
		mEngine.Add(inputs, nFrames, 1);
	#else
	{
		// Convert the input samples from doubles to WDL_FFT_REALs.
		double* in = inputs[0];
		// Use outputs[0] as a temporary buffer.
		WDL_FFT_REAL* tmp = (WDL_FFT_REAL*)outputs[0];
		for (int i = 0; i < nFrames; ++i) *tmp++ = (WDL_FFT_REAL)*in++;
		mEngine.Add((WDL_FFT_REAL**)outputs, nFrames, 1);
	}
	#endif

	double* in = inputs[0];
	double *out_l = outputs[0];
	double *out_r = outputs[1];

	int nAvail = MIN(mEngine.Avail(nFrames), nFrames);

	// If not enough samples are available yet, then only output the dry
	// signal.
	for (int i = 0; i < nFrames - nAvail; ++i) *out_l++ = mDry * *in++;

	// Output samples from the convolution engine.
	if (nAvail > 0)
	{
		// Apply the dry/wet mix (and convert from WDL_FFT_REALs back to
		// doubles).
		WDL_FFT_REAL* convo = mEngine.Get()[0];
		for (int i = 0; i < nAvail; ++i) *out_l++ = *out_r++ = mDry * *in++ + mWet * *convo++;

		// Remove the sample block from the convolution engine's buffer.
		mEngine.Advance(nAvail);
	}
}


// Impulse response (extracted from ir.wav using the convertor code below)

const float IPlugConvo::mIR[] =
{
	#include "ir.h"
};

/*

#include <stdio.h>
#include "../../../wdlendian.h"

int main(int argc, char* argv[])
{
	FILE* f = fopen(argv[1], "rb");
	fseek(f, 12, SEEK_SET);

	unsigned int i, n;
	for (;;)
	{
		fread(&i, 1, 4, f);
		fread(&n, 1, 4, f);
		n = WDL_bswap32_if_be(n);
		if (i == WDL_bswap32_if_le('data')) break;
		fseek(f, n, SEEK_CUR);
	}

	while (n > 0)
	{
		n -= fread(&i, 1, 4, f);
		printf("%.8ef,\n", (float)WDL_bswapf_if_be(i));
	}

	fclose(f);
	return 0;
}

*/
