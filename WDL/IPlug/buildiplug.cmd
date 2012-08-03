@echo off
setlocal

rem  IPlug & LICE makefiles for Microsoft Windows SDK v7.0/v7.1
rem  (c) Theo Niessink 2009-2012
rem  <http://www.taletn.com/>
rem
rem  This file is provided 'as-is', without any express or implied warranty.
rem  In no event will the authors be held liable for any damages arising
rem  from the use of this software.
rem
rem  Permission is granted to anyone to use this software for any purpose,
rem  including commercial applications, and to alter it and redistribute it
rem  freely, subject to the following restrictions:
rem
rem  1. The origin of this software must not be misrepresented; you must not
rem     claim that you wrote the original software. If you use this software
rem     in a product, an acknowledgment in the product documentation would
rem     be appreciated but is not required.
rem  2. Altered source versions must be plainly marked as such, and must not
rem     be misrepresented as being the original software.
rem  3. This notice may not be removed or altered from any source
rem     distribution.

rem  Usage: buildiplug.cmd [/Release | /Debug | /Tracer] [/x86 | /x64]
rem
rem  Builds the LICE and IPlug libs, by default for various configurations
rem  (Debug, Release) and architectures (x86, x86 without SSE2, x64).
rem
rem  If you specify /Release, /Debug or /Tracer, then the libs are built
rem  _only_ for the specified configuration. If you specify /x86 or x64,
rem  then the libs are built _only_ for the specified architecture.

:Usage
if "%1"=="" goto SaveCurEnv
if /i "%1"=="/x86" (
  set arch=x86
  shift
  goto Usage
)
if /i "%1"=="/x64" (
  set arch=x64
  shift
  goto Usage
)
if /i "%1"=="/Release" (
  set config=Release
  shift
  goto Usage
)
if /i "%1"=="/Debug" (
  set config=Debug
  shift
  goto Usage
)
if /i "%1"=="/Tracer" (
  set config=Tracer
  shift
  goto Usage
)
echo Usage: buildiplug.cmd [/x86 ^| /x64] [/Release ^| /Debug ^| Tracer]
goto :eof

:SaveCurEnv
set setenv=Setenv
if not "%TARGET_CPU%"=="" goto SaveCurEnv71

:SaveCurEnv70
if "%NODEBUG%"=="" set setenv=%setenv% /Debug
if "%NODEBUG%"=="1" set setenv=%setenv% /Release
if "%CPU%"=="i386" set setenv=%setenv% /x86
if "%CPU%"=="AMD64" set setenv=%setenv% /x64
if "%CPU%"=="IA64" set setenv=%setenv% /ia64
if "%APPVER%"=="5.01" set setenv=%setenv% /xp
if "%APPVER%"=="6.0" set setenv=%setenv% /vista
if "%APPVER%"=="5.02" set setenv=%setenv% /2003
if "%APPVER%"=="6.1" set setenv=%setenv% /win7
goto BuildLice

:SaveCurEnv71
if "%CONFIGURATION%"=="Debug" set setenv=%setenv% /Debug
if not "%CONFIGURATION%"=="Debug" set setenv=%setenv% /Release
if "%TARGET_CPU%"=="x86" set setenv=%setenv% /x86
if "%TARGET_CPU%"=="x64" set setenv=%setenv% /x64
if "%TARGET_CPU%"=="IA64" set setenv=%setenv% /ia64
if "%TARGET_PLATFORM%"=="XP" set setenv=%setenv% /xp
if "%TARGET_PLATFORM%"=="LH" set setenv=%setenv% /vista
if "%TARGET_PLATFORM%"=="SRV" set setenv=%setenv% /2003
if "%TARGET_PLATFORM%"=="LHS" set setenv=%setenv% /2008
if "%TARGET_PLATFORM%"=="WIN7" set setenv=%setenv% /win7

:BuildLice
pushd ..\lice

if "%config%"=="Release" goto LiceRelease
if "%config%"=="Tracer" goto LiceRelease
:LiceDebug
if "%arch%"=="x64" goto LiceDebug64
call Setenv /Debug /x86 /xp

title LICE - Debug^|Win32
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc all

rem title LICE - Debug^|Win32_noSSE2
rem nmake /f Makefile.msc NOSSE2=1 clean
rem nmake /f Makefile.msc NOSSE2=1 all

if "%arch%"=="x86" goto LiceRelease
:LiceDebug64
call Setenv /Debug /x64 /xp

title LICE - Debug^|X64
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc all

if "%config%"=="Debug" goto BuildIPlug
:LiceRelease
if "%arch%"=="x64" goto LiceRelease64
call Setenv /Release /x86 /xp

title LICE - Release^|Win32
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc lib

title LICE - Release^|Win32_noSSE2
NMAKE /f Makefile.msc NOSSE2=1 clean
NMAKE /f Makefile.msc NOSSE2=1 lib

if "%arch%"=="x86" goto BuildIPlug
:LiceRelease64
call Setenv /Release /x64 /xp

title LICE - Release^|X64
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc lib

:BuildIPlug
popd

if "%config%"=="Release" goto IPlugRelease
if "%config%"=="Tracer" goto IPlugTracer
:IPlugDebug
if "%arch%"=="x64" goto IPlugDebug64
call Setenv /Debug /x86 /xp

title IPlug - Debug^|Win32
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc all

rem title IPlug - Debug^|Win32_noSSE2
rem NMAKE /f Makefile.msc NOSSE2=1 clean
rem NMAKE /f Makefile.msc NOSSE2=1 all

if "%arch%"=="x86" goto IPlugRelease
:IPlugDebug64
call Setenv /Debug /x64 /xp

title IPlug - Debug^|X64
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc all

if "%config%"=="Debug" goto RestoreEnv
:IPlugRelease
if "%arch%"=="x64" goto IPlugRelease64
call Setenv /Release /x86 /xp

title IPlug - Release^|Win32
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc lib

title IPlug - Release^|Win32_noSSE2
NMAKE /f Makefile.msc NOSSE2=1 clean
NMAKE /f Makefile.msc NOSSE2=1 lib

if "%arch%"=="x86" goto RestoreEnv
:IPlugRelease64
call Setenv /Release /x64 /xp

title IPlug - Release^|X64
NMAKE /f Makefile.msc clean
NMAKE /f Makefile.msc lib

goto RestoreEnv
:IPlugTracer
if "%arch%"=="x64" goto IPlugTracer64
call Setenv /Release /x86 /xp

title IPlug - Tracer^|Win32
NMAKE /f Makefile.msc CONFIGURATION=Tracer clean
NMAKE /f Makefile.msc CONFIGURATION=Tracer lib

rem title IPlug - Tracer^|Win32_noSSE2
rem NMAKE /f Makefile.msc CONFIGURATION=Tracer NOSSE2=1 clean
rem NMAKE /f Makefile.msc CONFIGURATION=Tracer NOSSE2=1 lib

if "%arch%"=="x86" goto RestoreEnv
:IPlugTracer64
call Setenv /Release /x64 /xp

title IPlug - Tracer^|X64
NMAKE /f Makefile.msc CONFIGURATION=Tracer clean
NMAKE /f Makefile.msc CONFIGURATION=Tracer lib

:RestoreEnv
call %setenv%

if not "%arch%"=="x64" (
  if not "%config%"=="Release" if not "%config%"=="Tracer" (
    if not exist ..\lice\Win32\Debug\lice.lib echo Error: LICE - Debug^|Win32 failed.
    rem if not exist ..\lice\Win32_noSSE2\Debug\lice.lib echo Error: LICE - Debug^|Win32_noSSE2 failed.
    if not exist Win32\Debug\IPlug.lib echo Error: IPlug - Debug^|Win32 failed.
    rem if not exist Win32_noSSE2\Debug\IPlug.lib echo Error: IPlug - Debug^|Win32_noSSE2 failed.
  )
  if not "%config%"=="Debug" if not "%config%"=="Tracer" (
    if not exist ..\lice\Win32\Release\lice.lib echo Error: LICE - Release^|Win32 failed.
    if not exist ..\lice\Win32_noSSE2\Release\lice.lib echo Error: LICE - Release^|Win32_noSSE2 failed.
    if not exist Win32\Release\IPlug.lib echo Error: IPlug - Release^|Win32 failed.
    if not exist Win32_noSSE2\Release\IPlug.lib echo Error: IPlug - Release^|Win32_noSSE2 failed.
  )
  if "%config%"=="Tracer" (
    if not exist ..\lice\Win32\Release\lice.lib echo Error: LICE - Release^|Win32 failed.
    rem if not exist ..\lice\Win32_noSSE2\Release\lice.lib echo Error: LICE - Release^|Win32_noSSE2 failed.
    if not exist Win32\Tracer\IPlug.lib echo Error: IPlug - Tracer^|Win32 failed.
    rem if not exist Win32_noSSE2\Tracer\IPlug.lib echo Error: IPlug - Tracer^|Win32_noSSE2 failed.
  )
)

if not "%arch%"=="x86" (
  if not "%config%"=="Release" if not "%config%"=="Tracer" (
    if not exist ..\lice\X64\Debug\lice.lib echo Error: LICE - Debug^|X64 failed.
    if not exist X64\Debug\IPlug.lib echo Error: IPlug - Debug^|X64 failed.
  )
  if not "%config%"=="Debug" if not "%config%"=="Tracer" (
    if not exist ..\lice\X64\Release\lice.lib echo Error: LICE - Release^|X64 failed.
    if not exist X64\Release\IPlug.lib echo Error: IPlug - Release^|X64 failed.
  )
  if "%config%"=="Tracer" (
    if not exist ..\lice\X64\Release\lice.lib echo Error: LICE - Release^|X64 failed.
    if not exist X64\Tracer\IPlug.lib echo Error: IPlug - Tracer^|X64 failed.
  )
)
