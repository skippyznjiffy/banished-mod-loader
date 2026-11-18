@echo off
rem builds the loader (dinput8.dll) and every plugin under plugins\ into out\
setlocal
set ROOT=%~dp0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%I"
if not defined VS (
    echo Visual Studio with the C++ tools is not installed
    exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%ROOT%out\obj" mkdir "%ROOT%out\obj"
set FLAGS=/nologo /std:c++17 /O2 /W4 /EHsc /MT /DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /I"%ROOT%include" /I"%ROOT%third_party\imgui" /I"%ROOT%third_party\implot"

cl %FLAGS% /Fo"%ROOT%out\obj\\" "%ROOT%core\proxy.cpp" "%ROOT%core\game.cpp" "%ROOT%core\console.cpp" "%ROOT%core\menu.cpp" "%ROOT%core\settings.cpp" "%ROOT%core\update.cpp" "%ROOT%core\input.cpp" /LD /Fe"%ROOT%out\dinput8.dll" /link /DEF:"%ROOT%core\dinput8.def" user32.lib winhttp.lib dxguid.lib || exit /b 1

for /d %%P in ("%ROOT%plugins\*") do (
    cl %FLAGS% /Fo"%ROOT%out\obj\\" "%%P\*.cpp" /LD /Fe"%ROOT%out\%%~nxP.dll" /link user32.lib d3d11.lib || exit /b 1
)
del "%ROOT%out\*.exp" "%ROOT%out\*.lib" 2>nul
echo built into %ROOT%out
