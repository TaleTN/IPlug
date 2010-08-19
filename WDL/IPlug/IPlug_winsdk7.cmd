@echo off
setlocal

rem  IPlug & LICE makefiles for Microsoft Windows SDK v7.0/v7.1
rem  (c) Theo Niessink 2009, 2010
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

rem  Usage: IPlug_winsdk7.cmd [/x86 | /x64]
rem
rem  Builds the LICE and IPlug libs in various configurations (Debug,
rem  Release) and for various platforms (x86, x64, x86 without SSE2).
rem
rem  /x86 or /x64 only builds the specified platform, so you can e.g. do the
rem  x86 builds using the Windows SDK v7.0, and the x64 build using v7.1.
rem
rem  Note: To build the IPlug lib without JPEG support, remove (or comment
rem  out) the following line in IGraphicsWin.cpp:
rem
rem    if (!stricmp(ext, "jpg") || !stricmp(ext, "jpeg")) return _LICE::LICE_LoadJPGFromResource(mHInstance, ID, 0);

:Usage
if "%1"=="" goto SaveCurEnv
if /i "%1"=="/x86" goto SaveCurEnv
if /i "%1"=="/x64" goto SaveCurEnv
echo "Usage: IPlug_winsdk7.cmd [/x86 | /x64]"
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

:LiceDebug
if /i "%1"=="/x64" goto LiceDebug64
call Setenv /Debug /x86 /xp

title "LICE - Debug|Win32"
NMAKE /f lice_winsdk7.mak clean
NMAKE /f lice_winsdk7.mak all

rem title "LICE - Debug|Win32_noSSE2"
rem NMAKE /f lice_winsdk7.mak NOSSE2=1 clean
rem NMAKE /f lice_winsdk7.mak NOSSE2=1 all

if /i "%1"=="/x86" goto LiceRelease
:LiceDebug64
call Setenv /Debug /x64 /xp

title "LICE - Debug|X64"
NMAKE /f lice_winsdk7.mak clean
NMAKE /f lice_winsdk7.mak all

:LiceRelease
if /i "%1"=="/x64" goto LiceRelease64
call Setenv /Release /x86 /xp

title "LICE - Release|Win32"
NMAKE /f lice_winsdk7.mak clean
NMAKE /f lice_winsdk7.mak lib

title "LICE - Release|Win32_noSSE2"
NMAKE /f lice_winsdk7.mak NOSSE2=1 clean
NMAKE /f lice_winsdk7.mak NOSSE2=1 lib

if /i "%1"=="/x86" goto BuildIPlug
:LiceRelease64
call Setenv /Release /x64 /xp

title "LICE - Release|X64"
NMAKE /f lice_winsdk7.mak clean
NMAKE /f lice_winsdk7.mak lib

:BuildIPlug
popd

:IPlugDebug
if /i "%1"=="/x64" goto IPlugDebug64
call Setenv /Debug /x86 /xp

title "IPlug - Debug|Win32"
NMAKE /f IPlug_winsdk7.mak clean
NMAKE /f IPlug_winsdk7.mak all

rem title "IPlug - Debug|Win32_noSSE2"
rem NMAKE /f IPlug_winsdk7.mak NOSSE2=1 clean
rem NMAKE /f IPlug_winsdk7.mak NOSSE2=1 all

if /i "%1"=="/x86" goto IPlugRelease
:IPlugDebug64
call Setenv /Debug /x64 /xp

title "IPlug - Debug|X64"
NMAKE /f IPlug_winsdk7.mak clean
NMAKE /f IPlug_winsdk7.mak all

:IPlugRelease
if /i "%1"=="/x64" goto IPlugRelease64
call Setenv /Release /x86 /xp

title "IPlug - Release|Win32"
NMAKE /f IPlug_winsdk7.mak clean
NMAKE /f IPlug_winsdk7.mak lib

title "IPlug - Release|Win32_noSSE2"
NMAKE /f IPlug_winsdk7.mak NOSSE2=1 clean
NMAKE /f IPlug_winsdk7.mak NOSSE2=1 lib

if /i "%1"=="/x86" goto IPlugTracer
:IPlugRelease64
call Setenv /Release /x64 /xp

title "IPlug - Release|X64"
NMAKE /f IPlug_winsdk7.mak clean
NMAKE /f IPlug_winsdk7.mak lib

:IPlugTracer
if /i "%1"=="/x64" goto IPlugTracer64
rem call Setenv /Release /x86 /xp

rem title "IPlug - Tracer|Win32"
rem NMAKE /f IPlug_winsdk7.mak CONFIGURATION=Tracer clean
rem NMAKE /f IPlug_winsdk7.mak CONFIGURATION=Tracer lib

rem title "IPlug - Tracer|Win32_noSSE2"
rem NMAKE /f IPlug_winsdk7.mak CONFIGURATION=Tracer NOSSE2=1 clean
rem NMAKE /f IPlug_winsdk7.mak CONFIGURATION=Tracer NOSSE2=1 lib

if /i "%1"=="/x86" goto RestoreEnv
:IPlugTracer64
rem call Setenv /Release /x64 /xp

rem title "IPlug - Tracer|X64"
rem NMAKE /f IPlug_winsdk7.mak CONFIGURATION=Tracer clean
rem NMAKE /f IPlug_winsdk7.mak CONFIGURATION=Tracer lib

:RestoreEnv
call %setenv%
