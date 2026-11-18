@echo off
rem builds out\preview\townstats-preview.exe: the Town Stats overlay without the game
setlocal
set ROOT=%~dp0..\..\
set OUT=%ROOT%out\preview
set PLUGIN=%ROOT%plugins\townstats
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%I"
if not defined VS (
    echo Visual Studio with the C++ tools is not installed
    exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
set FLAGS=/nologo /std:c++17 /O2 /W4 /EHsc /MT /DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /I"%ROOT%include" /I"%ROOT%third_party\imgui" /I"%ROOT%third_party\implot" /I"%PLUGIN%"

rem Dear ImGui and ImPlot only once, they take most of the time
if not exist "%OUT%\imgui.lib" (
    cl %FLAGS% /c /Fo"%OUT%\obj\\" "%PLUGIN%\ts_*.cpp" || exit /b 1
    lib /nologo /out:"%OUT%\imgui.lib" "%OUT%\obj\ts_*.obj" || exit /b 1
)
cl %FLAGS% /Fo"%OUT%\obj\\" "%~dp0preview.cpp" "%PLUGIN%\ui.cpp" /Fe"%OUT%\townstats-preview.exe" /link "%OUT%\imgui.lib" user32.lib d3d11.lib dxgi.lib ole32.lib windowscodecs.lib || exit /b 1
echo built %OUT%\townstats-preview.exe
