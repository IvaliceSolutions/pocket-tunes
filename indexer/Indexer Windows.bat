@echo off
rem Pocket Tunes - indexeur (Windows). Double-cliquez ce fichier.
rem
rem Utilise Node s'il est deja installe ; sinon telecharge une version
rem PORTABLE dans indexer\runtime\ (aucune installation, aucun droit admin).
rem Les chemins se reglent dans indexer.config.json (cree au premier
rem lancement), ou automatiquement si ce dossier indexer\ est copie sur la
rem carte SD.
setlocal
cd /d "%~dp0"

set NODE_VERSION=v22.12.0
set PDIR=runtime\node-%NODE_VERSION%-win-x64

where node >nul 2>nul
if %errorlevel%==0 (
  set NODE=node
  set NPM=npm
  goto deps
)
if exist "%PDIR%\node.exe" goto portable

echo Node absent : telechargement d'une version portable (une seule fois)...
if not exist runtime mkdir runtime
powershell -NoProfile -Command "Invoke-WebRequest 'https://nodejs.org/dist/%NODE_VERSION%/node-%NODE_VERSION%-win-x64.zip' -OutFile runtime\node.zip; Expand-Archive runtime\node.zip runtime; Remove-Item runtime\node.zip"

:portable
set NODE=%PDIR%\node.exe
set NPM="%PDIR%\node.exe" "%PDIR%\node_modules\npm\bin\npm-cli.js"

:deps
if not exist node_modules (
  echo Installation des dependances (une seule fois)...
  call %NPM% install --omit=dev --no-audit --no-fund
)

"%NODE%" src\run.js
echo.
pause
