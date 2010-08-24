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
# TARGET_CPU = x64
!		ERROR Use Windows SDK v7.1 for x64 build
!	ENDIF
!ELSE
!	IF "$(TARGET_CPU)" == "x86"
!		ERROR Use Windows SDK v7.0 for x86 builds
!	ENDIF
!ENDIF


!IF "$(TARGET_CPU)" == "x86"
!	IFNDEF NOSSE2
PLATFORM = Win32
OUTFILE = IPlugExample.dll
CPPFLAGS = $(CPPFLAGS) /arch:SSE2
!	ELSE
PLATFORM = Win32_noSSE2
OUTFILE = IPlugExample.dll
!	ENDIF
CPPFLAGS = $(CPPFLAGS) /D "WIN32" /D "_WIN32_WINNT=0x0501"
LINKFLAGS = $(LINKFLAGS) /machine:x86

!ELSE IF "$(TARGET_CPU)" == "x64"
!	IFDEF NOSSE2
!		MESSAGE Warning: NOSSE2 has no effect on x64 code
!		MESSAGE
!	ENDIF
PLATFORM = X64
OUTFILE = IPlugExample.dll
CPPFLAGS = $(CPPFLAGS) /favor:INTEL64 /wd4267 /wd4800
LINKFLAGS = $(LINKFLAGS) /machine:x64

!ELSE
!	ERROR Unsupported target CPU "$(TARGET_CPU)"
!ENDIF

OUTDIR = $(PLATFORM)/$(CONFIGURATION)
INTDIR = $(OUTDIR)

!MESSAGE IPlugExample - $(CONFIGURATION)|$(PLATFORM)
!MESSAGE


CPPFLAGS = $(CPPFLAGS) /EHsc /fp:fast /D "VST_API" /D "WINVER=0x0501" /D "_WINDLL" /D "_MBCS" /MT /c /Fo"$(INTDIR)/" /Fd"$(INTDIR)/" /W3 /D "_CRT_SECURE_NO_WARNINGS" /nologo
LINKFLAGS = $(LINKFLAGS) /dll /subsystem:windows /libpath:"../../lice/$(OUTDIR:Tracer=Release)" /libpath:"../$(OUTDIR)" /dynamicbase:no /manifest:no /nologo
RCFLAGS = $(RCFLAGS) /nologo

!IF "$(CONFIGURATION)" == "Debug"
CPPFLAGS = $(CPPFLAGS) /D "_DEBUG" /RTCsu
!	IF "$(PLATFORM)" == "X64"
CPPFLAGS = $(CPPFLAGS) /Zi
!	ELSE
CPPFLAGS = $(CPPFLAGS) /ZI
!	ENDIF
LINKFLAGS = $(LINKFLAGS) /debug /nodefaultlib:libcmt /defaultlib:libcmtd

!ELSE
CPPFLAGS = $(CPPFLAGS) /D "NDEBUG" /O2 /Ob2
!	IF "$(CONFIGURATION)" == "Tracer"
CPPFLAGS = $(CPPFLAGS) /D "TRACER_BUILD"
!	ENDIF
LINKFLAGS = $(LINKFLAGS) /opt:icf /opt:ref /incremental:no /defaultlib:libcmt
!ENDIF

LINKFLAGS = $(LINKFLAGS) lice.lib IPlug.lib shell32.lib user32.lib gdi32.lib comdlg32.lib


all : dll

"$(OUTDIR)" :
	@if not exist "$(OUTDIR)/" mkdir "$(OUTDIR)"

!IF "$(INTDIR)" != "$(OUTDIR)"
"$(INTDIR)" :
	@if not exist "$(INTDIR)/" mkdir "$(INTDIR)"
!ENDIF


"$(INTDIR)/IPlugExample.obj" : IPlugExample.cpp IPlugExample.h resource.h
	$(CPP) $(CPPFLAGS) IPlugExample.cpp

"$(INTDIR)/IPlugExample.res" : IPlugExample.rc resource.h img/toggle-switch.png img/knob_sm.png img/fader-cap_sm.png img/BG_400x200.png img/VU-meter_sm.png
	$(RC) $(RCFLAGS) /fo"$(INTDIR)/IPlugExample.res" IPlugExample.rc

"$(OUTDIR)/$(OUTFILE)" : "$(OUTDIR)" "$(INTDIR)" "$(INTDIR)/IPlugExample.obj" "$(INTDIR)/IPlugExample.res"
	link $(LINKFLAGS) /out:"$(OUTDIR)/$(OUTFILE)" /implib:"$(INTDIR)/IPlugExample.lib" "$(INTDIR)/IPlugExample.obj" "$(INTDIR)/IPlugExample.res"

dll : "$(OUTDIR)/$(OUTFILE)"

clean :
	@if exist "$(OUTDIR)/*.*" erase /Q "$(OUTDIR:/=\)\*.*"
	@if exist "$(OUTDIR)" rd "$(OUTDIR:/=\)"
