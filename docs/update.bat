@echo off
:: ============================================================
:: update.bat — NOVA Engine docs regenerator
:: Double-click this (or run from terminal) to rebuild both
:: the .docx and the .md from ENGINE_PLAN_DATA.js
:: ============================================================

cd /d "%~dp0"

echo.
echo  ============================================================
echo   NOVA ENGINE — Regenerating documentation...
echo  ============================================================
echo.

node generate.js
if errorlevel 1 (
    echo.
    echo  [FAILED] generate.js encountered an error.
    pause
    exit /b 1
)

node generate-md.js
if errorlevel 1 (
    echo.
    echo  [FAILED] generate-md.js encountered an error.
    pause
    exit /b 1
)

echo.
echo  ============================================================
echo   Done! Files updated:
echo     docs\NOVA_ENGINE_PLAN.docx   (Word document)
echo     docs\ENGINE_PLAN.md          (plain text / AI paste)
echo  ============================================================
echo.
pause