@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%~dp0"
cl /nologo /std:c++17 /EHsc /DGLEW_STATIC /DWIN32 /D_CRT_SECURE_NO_WARNINGS /I ..\aae_video /I ..\sys_graphics /I ..\math /I ..\system /I ..\3rdparty beam_draw_tests.cpp ..\aae_video\vector_draw.cpp ..\aae_video\emu_vector_draw.cpp ..\3rdparty\glew.c /Fe:beam_draw_tests.exe /link opengl32.lib user32.lib gdi32.lib
if errorlevel 1 goto fail
.\beam_draw_tests.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
