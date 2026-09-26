@echo off

:: folder path
set "folder=%~dp0"

echo Scanning %folder% for .cso files...

FOR /f %%G IN ('dir /b /o:n "%folder%\*.cso"') DO (
    CALL :decompile "%%G"
)

echo.
echo Done processing all .cso files.
pause
GOTO :eof

:decompile
SETLOCAL
SET "file=%~1"
SET "csoFile=%folder%\%file%"
SET "hlslFile=%folder%\%file:.cso=.hlsl%"

::echo.
::echo Decompiling %file%

:: Step 3: .cso → .hlsl
IF EXIST "%hlslFile%" (
    ::echo .hlsl %file%, skipping...
    GOTO :eof
)
cmd_Decompiler.exe -D "%csoFile%"

ENDLOCAL
GOTO :eof