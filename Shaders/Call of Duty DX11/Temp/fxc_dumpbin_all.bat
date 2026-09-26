@echo off
setlocal enabledelayedexpansion

:: Set the path to fxc.exe if it is not in your System PATH
set "FXC_PATH=fxc.exe"

echo Scanning for .cso files...

for %%f in (*.cso) do (
    set "CSO_FILE=%%f"
    set "ASM_FILE=%%~nf.asm"

    if exist "!ASM_FILE!" (
        echo Skipping "!CSO_FILE!" (Already dumped as !ASM_FILE!^)
    ) else (
        echo Disassembling "!CSO_FILE!"...
        "%FXC_PATH%" /nologo /dumpbin /Fc "!ASM_FILE!" "!CSO_FILE!"
        
        if %errorlevel% equ 0 (
            echo   [OK] Created !ASM_FILE!
        ) else (
            echo   [ERROR] Failed to process "!CSO_FILE!"
        )
    )
)

echo.
echo Process complete.
pause