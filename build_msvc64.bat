@echo off
rem Builds ShroMagicMenu.dll (x64) with MSVC. Same approach as the FFX build_msvc.bat,
rem but with vcvars64. Run from this folder.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe not found. Install Visual Studio Community with "Desktop development with C++".
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo No Visual Studio with the C++ x64 tools found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist build mkdir build
cl /nologo /LD /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS src\shro_magic_menu.c /Fobuild\ /Febuild\ShroMagicMenu.dll /link /DLL kernel32.lib || exit /b 1

echo.
echo Checking the DLL is 64-bit (expect "8664 machine (x64)"):
dumpbin /headers build\ShroMagicMenu.dll | findstr /i "machine"
copy /y build\ShroMagicMenu.dll mod\ShroMagicMenu.dll >nul && echo Copied to mod\ShroMagicMenu.dll
endlocal
