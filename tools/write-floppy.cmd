@echo off
rem =============================================================================
rem tools\write-floppy.cmd
rem EX-OS - Extensible Operating System
rem
rem Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
rem SPDX-License-Identifier: GPL-2.0-or-later
rem =============================================================================
rem
rem Scrive l'immagine floppy di EX-OS su un drive fisico.
rem
rem USO:
rem   tools\write-floppy.cmd                 scrive ..\dist\floppy.img su A:
rem   tools\write-floppy.cmd B:              altra unita'
rem   tools\write-floppy.cmd A: D:\altra.img altra immagine
rem
rem Si puo' anche fare doppio clic su questo file da Esplora risorse.
rem
rem Non serve una console come Amministratore: lo script PowerShell che viene
rem lanciato si rieleva da solo (comparira' la richiesta UAC) e apre una
rem finestra che resta aperta per mostrare l'esito.
rem
rem Perche' un .cmd e non solo il .ps1: da cmd.exe un .ps1 non e' eseguibile
rem direttamente, e con la ExecutionPolicy predefinita di Windows verrebbe
rem comunque bloccato. Questo file fornisce l'invocazione corretta una volta
rem per tutte.
rem =============================================================================

setlocal

set "SCRIPT_DIR=%~dp0"
set "PS1=%SCRIPT_DIR%write_floppy.ps1"

if not exist "%PS1%" (
    echo [ERRORE] non trovo write_floppy.ps1 accanto a questo file:
    echo          %PS1%
    echo.
    pause
    exit /b 1
)

rem Primo argomento: unita' (default A:). Secondo: immagine (default ..\dist\floppy.img).
set "DRIVE=%~1"
if "%DRIVE%"=="" set "DRIVE=A:"

set "IMAGE=%~2"

echo Immagine : %IMAGE%
echo Unita'   : %DRIVE%
echo.

if "%IMAGE%"=="" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Drive "%DRIVE%"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Drive "%DRIVE%" -Image "%IMAGE%"
)

set RC=%ERRORLEVEL%

if not "%RC%"=="0" (
    echo.
    echo [ERRORE] operazione non riuscita ^(codice %RC%^).
)

rem pause serve al doppio clic: senza, la finestra si chiuderebbe subito e
rem non si leggerebbe nulla.
echo.
pause
exit /b %RC%
