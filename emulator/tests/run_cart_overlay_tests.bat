@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"
cl /nologo /std:c++17 /EHsc /I ..\project cart_overlay_tests.cpp ..\project\cart_overlay.cpp /Fe:cart_overlay_tests.exe
if errorlevel 1 goto fail
.\cart_overlay_tests.exe %*
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
