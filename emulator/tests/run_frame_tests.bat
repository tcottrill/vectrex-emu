@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"
cl /nologo /std:c++17 /EHsc /I ..\project /I ..\system /I ..\sys_audio frame_cadence_tests.cpp ..\project\ay8910.cpp ..\project\vecx.cpp ..\project\cpu_m6809.cpp ..\project\via6522.cpp ..\project\alg.cpp /Fe:frame_cadence_tests.exe
if errorlevel 1 goto fail
.\frame_cadence_tests.exe %*
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
