// TN: Prevent TARGET_OS_IOS is not defined warnings.
#if defined(__APPLE__) && !defined(TARGET_OS_IOS)
	#define TARGET_OS_IOS 0
#endif

#include "AAX_SDK/Interfaces/AAX_Assert.h"

// TN: Some AAX macros leak absolute (!) source filenames into the binary,
// even with NDEBUG defined. For IPlug this happens only for
// AAX_CParameter.h, so as a workaround/hack this header redefines the
// AAX_ASSERT macro to report only that specific filename.

// (IMHO you shouldn't leak any source code data into a release build...)

#ifdef NDEBUG

#undef AAX_ASSERT

#define AAX_ASSERT(cond) \
{ \
	if (!(cond)) \
	{ \
		AAX_CHostServices::HandleAssertFailure("AAX_CParameter.h", __LINE__, #cond, (int32_t)AAX_eAssertFlags_Log); \
	} \
}

#endif
