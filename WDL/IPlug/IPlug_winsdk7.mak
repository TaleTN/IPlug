# IPlug makefile for Microsoft Windows SDK v7.0/v7.1
# (c) Theo Niessink 2009, 2010
# <http://www.taletn.com/>
#
# This file is provided 'as-is', without any express or implied warranty. In
# no event will the authors be held liable for any damages arising from the
# use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software in
#    a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.


# Usage:
#   Setenv [/Debug | /Release] [/x86 | /x64] [/xp | /vista | /2003 | /2008 | /win7]
#   NMAKE /f IPlug_winsdk7.mak [CONFIGURATION=Tracer] [NOSSE2=1] [all | lib | iplug]
#
# /Debug                Debug build
# /Release              Release build
# /x86                  32-bit x86 code
# /x64                  64-bit x64 code
# /xp                   Windows XP SP2 (recommended)
# /vista                Windows Vista
# /2003                 Windows Server 2003
# /2008                 Windows Server 2008, Windows Vista SP1
# /win7                 Windows 7
#
# CONFIGURATION=Tracer  Tracer build
# NOSSE2=1              disables the use of SSE2 instructions
# all                   builds the .lib file, keeping all intermediate files
# lib                   builds the .lib file, and then deletes all
#                       intermediate files
# iplug                 only compiles IPlug


!IFNDEF CONFIGURATION
!	IFDEF NODEBUG
CONFIGURATION = Release
!	ELSE
CONFIGURATION = Debug
!	ENDIF
!ENDIF

!IFNDEF TARGET_CPU
!	IF "$(CPU)" == "i386"
TARGET_CPU = x86
!	ELSE IF "$(CPU)" == "AMD64"
TARGET_CPU = x64
!	ENDIF
!ENDIF


!IF "$(TARGET_CPU)" == "x86"
!	IFNDEF NOSSE2
PLATFORM = Win32
CPPFLAGS = $(CPPFLAGS) /arch:SSE2
!	ELSE
PLATFORM = Win32_noSSE2
!	ENDIF
CPPFLAGS = $(CPPFLAGS) /D "WIN32" /D "_CRT_SECURE_NO_WARNINGS"
!ELSE IF "$(TARGET_CPU)" == "x64"
PLATFORM = X64
!	IFDEF NOSSE2
!		MESSAGE Warning: NOSSE2 has no effect on x64 code
!		MESSAGE
!	ENDIF
CPPFLAGS = $(CPPFLAGS) /favor:blend /wd4267 /wd4800
!ELSE
!	ERROR Unsupported target CPU "$(TARGET_CPU)"
!ENDIF

OUTDIR = $(PLATFORM)/$(CONFIGURATION)
INTDIR = $(OUTDIR)

!MESSAGE IPlug - $(CONFIGURATION)|$(PLATFORM)
!MESSAGE


CPPFLAGS = $(CPPFLAGS) /EHsc /GS- /GR- /D "_LIB" /D "_MBCS" /Oi /Ot /fp:fast /MT /c /Fo"$(INTDIR)/" /Fd"$(INTDIR)/" /W3 /WX /wd4996 /wd4244 /nologo

!IF "$(CONFIGURATION)" == "Debug"
CPPFLAGS = $(CPPFLAGS) /D "_DEBUG" /RTCsu
!	IF "$(PLATFORM)" == "X64"
CPPFLAGS = $(CPPFLAGS) /Zi
!	ELSE
CPPFLAGS = $(CPPFLAGS) /ZI
!	ENDIF
!ELSE
CPPFLAGS = $(CPPFLAGS) /D "NDEBUG" /O2 /Ob2
!	IF "$(CONFIGURATION)" == "Tracer"
CPPFLAGS = $(CPPFLAGS) /D "TRACER_BUILD"
!	ENDIF
!ENDIF

!MESSAGE $(CPP) $(CPPFLAGS)
!MESSAGE


all : "$(OUTDIR)/IPlug.lib"

"$(OUTDIR)" :
	@if not exist "$(OUTDIR)/" mkdir "$(OUTDIR)"

!IF "$(INTDIR)" != "$(OUTDIR)"
"$(INTDIR)" :
	@if not exist "$(INTDIR)/" mkdir "$(INTDIR)"
!ENDIF


IPLUG = \
"$(INTDIR)/Hosts.obj" \
"$(INTDIR)/IParam.obj" \
"$(INTDIR)/IPlugStructs.obj" \
"$(INTDIR)/Log.obj" \
"$(INTDIR)/IGraphics.obj" \
"$(INTDIR)/IGraphicsLice.obj" \
"$(INTDIR)/IGraphicsWin.obj" \
"$(INTDIR)/IControl.obj" \
"$(INTDIR)/IPlugBase.obj" \
"$(INTDIR)/IPlugVST.obj"

iplug : $(IPLUG)

.cpp{$(INTDIR)}.obj :
	@$(CPP) $(CPPFLAGS) "$<"


LIB_FLAGS = /out:"$(OUTDIR)/IPlug.lib" /nologo wininet.lib

"$(OUTDIR)/IPlug.lib" : "$(OUTDIR)" "$(INTDIR)" $(IPLUG)
	@echo.
	@echo lib $(LIB_FLAGS)
	@lib $(LIB_FLAGS) $(IPLUG)

lib : "$(OUTDIR)/IPlug.lib" clear

clear :
	@if exist "$(INTDIR)/*.obj" erase "$(INTDIR:/=\)\*.obj"
	@if exist "$(INTDIR)/vc*.*" erase "$(INTDIR:/=\)\vc*.*"

clean : clear
	@if exist "$(OUTDIR)/IPlug.lib" erase "$(OUTDIR:/=\)\IPlug.lib"
