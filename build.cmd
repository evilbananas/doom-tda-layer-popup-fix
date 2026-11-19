@echo off
rem Builds LayerPopupFix as a proxy for each DLL name given, or for all supported names.
rem   build.cmd              -> out\dwmapi.dll, out\userenv.dll, ...
rem   build.cmd dwmapi       -> out\dwmapi.dll only (a trailing .dll is fine too)
rem wtsapi32 is left out on purpose: the launcher's crash-dump code uses it, and as a
rem proxy it froze the game at startup.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"

set "NAMES=%*"
if "%~1"=="" set "NAMES=dwmapi userenv dwrite authz netapi32"

if not exist out mkdir out
for %%N in (%NAMES%) do (
    call :build %%~nN || exit /b 1
)
echo Built into %~dp0out
exit /b 0

:build
set "NAME=%~1"
if not exist build\%NAME% mkdir build\%NAME%
if exist out\%NAME%.dll del /q out\%NAME%.dll
powershell -NoProfile -ExecutionPolicy Bypass -File gen_exports.ps1 -Dll %NAME% -Out build\%NAME%\proxy_exports.h || exit /b 1
rem The forwarder header is force-included (/FI) so each DLL gets exactly its own export list.
cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /LD /FIbuild\%NAME%\proxy_exports.h LayerPopupFix.cpp /Fobuild\%NAME%\ /Fe:out\%NAME%.dll /link /NOLOGO /IMPLIB:build\%NAME%\%NAME%.lib kernel32.lib user32.lib > build\%NAME%\cl.log 2>&1
set "CLERR=%ERRORLEVEL%"
rem LNK4104 just says DllCanUnloadNow/DllGetClassObject are normally PRIVATE; forwarding them is fine.
findstr /v /c:"LNK4104" /c:"Creating library" build\%NAME%\cl.log | findstr /v /x /c:"LayerPopupFix.cpp"
if not "%CLERR%"=="0" (echo %NAME%.dll: build failed & exit /b 1)
if not exist out\%NAME%.dll (echo %NAME%.dll: no output & exit /b 1)
exit /b 0
