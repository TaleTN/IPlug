#ifndef __IPLUGVERB_H__
#define __IPLUGVERB_H__

/*

IPlug verbengine example
(c) Theo Niessink 2010
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


A simple IPlug plug-in effect that shows how to use WDL's verbengine.

*/


#include "../../IPlug_include_in_plug_hdr.h"
#include "../../../verbengine.h"


class IPlugVerb: public IPlug
{
public:
	IPlugVerb(IPlugInstanceInfo instanceInfo);
	~IPlugVerb() {}

	void OnParamChange(int paramIdx);
	void Reset();
	void ProcessDoubleReplacing(double** inputs, double** outputs, int nFrames);

private:
	WDL_ReverbEngine mEngine;

	double mDry, mWet;
};


#endif // __IPLUGVERB_H__
