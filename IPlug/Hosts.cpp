#include "Hosts.h"

#include <assert.h>
#include <string.h>

struct LookUpTbl
{
	unsigned char mIdx;
	char mStr[11];
};

static const LookUpTbl sLookUpTbl[] =
{
	// C4 is version >= 8.2
	{ kHostCubase, "cubase" },
	{ kHostReaper, "reaper" },
	{ kHostNuendo, "nuendo" },
	{ kHostSonar, "cakewalk" },
	{ kHostSamplitude, "samplitude" },
	{ kHostFL, "fruity" },
	{ kHostAbletonLive, "live" },
	{ kHostMelodyneStudio, "melodyne" },
	{ kHostVSTScanner, "vstmanlib" },
	{ kHostAULab, "aulab" },
	{ kHostForte, "forte" },
	{ kHostChainer, "chainer" },
	{ kHostAudition, "audition" },
	{ kHostOrion, "orion" },
	{ kHostSAWStudio, "sawstudio" },
	{ kHostLogic, "logic" },
	{ kHostDigitalPerformer, "digital" },
	{ kHostAudioMulch, "audiomulch" },
	{ kHostStudioOne, "presonus" },
	{ kHostMixcraft, "acoustica" },
	{ kHostGarageBand, "garageband" },
	{ kHostArdour, "ardour" }
};

static size_t ToLower(char* const cDest, const char* const cSrc, const size_t n)
{
	assert(n > 0);

	size_t i;
	char c = 1;

	for (i = 0; i < n && c; ++i)
	{
		c = cSrc[i];

		const unsigned char u = c - 'A';
		const char l = u + 'a';

		cDest[i] = u < 26 ? l : c;
	}
	i--;

	if (c) cDest[i++] = 0;

	return i;
}

int LookUpHost(const char* const inHost)
{
	static const int size = 256;
	char host[size];

	if (ToLower(host, inHost, size) < size)
	{
		const int n = sizeof(sLookUpTbl) / sizeof(sLookUpTbl[0]);
		for (int i = 0; i < n; ++i)
		{
			const int idx = sLookUpTbl[i].mIdx;
			const char* const str = sLookUpTbl[i].mStr;

			if (strstr(host, str)) return idx;
		}
	}

	return kHostUnknown;
}
