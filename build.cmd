@echo off
rem Builds dwmapi.dll (LayerPopupFix) with the Visual Studio x64 toolchain.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /LD LayerPopupFix.cpp /Fe:dwmapi.dll /link /NOLOGO kernel32.lib user32.lib || exit /b 1
del /q LayerPopupFix.obj dwmapi.exp 2>nul
echo Built %~dp0dwmapi.dll
