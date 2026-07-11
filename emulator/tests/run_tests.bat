@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"

set FAIL=0

echo === VIA 6522 ===
cl /nologo /std:c++17 /EHsc /I "..\project" "..\project\via6522.cpp" "via6522_tests.cpp" /Fe:"via6522_tests.exe" 1>build_via.log 2>&1
if errorlevel 1 ( echo BUILD FAILED & type build_via.log & set FAIL=1 ) else ( "%~dp0via6522_tests.exe" & if errorlevel 1 set FAIL=1 )

echo === host_view ===
cl /nologo /std:c++17 /EHsc /I "..\system" "test_host_view.cpp" "..\system\host_view.cpp" /Fe:"host_view_tests.exe" 1>build_hostview.log 2>&1
if errorlevel 1 ( echo BUILD FAILED & type build_hostview.log & set FAIL=1 ) else ( "%~dp0host_view_tests.exe" & if errorlevel 1 set FAIL=1 )

echo.
if "%FAIL%"=="0" ( echo ALL TESTS PASSED ) else ( echo SOME TESTS FAILED )
popd
exit /b %FAIL%
