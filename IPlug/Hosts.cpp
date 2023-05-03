#include "Hosts.h"

#include <assert.h>
#include <string.h>

#define ANY_OS(idx, str) { idx, str },

#ifdef _WIN32
	#define WIN_ONLY(idx, str) { idx, str },
	#define MAC_ONLY(idx, str)
#elif defined(__APPLE__)
	#define MAC_ONLY(idx, str) { idx, str },
	#define WIN_ONLY(idx, str)
#endif

struct LookUpTbl
{
	unsigned char mIdx;
	char mStr[11];
};

static const LookUpTbl sLookUpTbl[] =
{
	// C4 is version >= 8.2
	ANY_OS(kHostCubase, "cubase")

	ANY_OS(kHostReaper, "reaper")
	ANY_OS(kHostNuendo, "nuendo")
	ANY_OS(kHostSonar, "cakewalk")
	WIN_ONLY(kHostSamplitude, "samplitude")
	ANY_OS(kHostFL, "fruity")
	ANY_OS(kHostAbletonLive, "live")
	ANY_OS(kHostMelodyneStudio, "melodyne")

	// TN: Ancient (last update 2008). Remove?
	WIN_ONLY(kHostVSTScanner, "vstmanlib")

	MAC_ONLY(kHostAULab, "aulab")
	ANY_OS(kHostForte, "forte")
	ANY_OS(kHostChainer, "chainer")
	ANY_OS(kHostAudition, "audition")

	// TN: Reportedly discontinued in 2016. Remove?
	WIN_ONLY(kHostOrion, "orion")

	WIN_ONLY(kHostSAWStudio, "sawstudio")
	ANY_OS(kHostLogic, "logic")
	ANY_OS(kHostDigitalPerformer, "digital")
	ANY_OS(kHostAudioMulch, "audiomulch")
	ANY_OS(kHostStudioOne, "presonus")
	WIN_ONLY(kHostMixcraft, "acoustica")
	MAC_ONLY(kHostGarageBand, "garageband")
	ANY_OS(kHostArdour, "ardour")
	ANY_OS(kHostBitwig, "bitwig")
	ANY_OS(kHostAudacity, "audacity")
	WIN_ONLY(kHostSAVIHost, "savihost")
	WIN_ONLY(kHostVSTHost, "vsthost")
	ANY_OS(kHostProTools, "pro tools")
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
