param([string]$Dependencies)
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$cache=Get-Content build\CMakeCache.txt
$cmake=($cache | Where-Object {$_ -match '^CMAKE_COMMAND:INTERNAL='}) -replace '^CMAKE_COMMAND:INTERNAL=',''
if(!$Dependencies){$Dependencies=($cache | Where-Object {$_ -match '^CMAKE_PREFIX_PATH:'}) -replace '^CMAKE_PREFIX_PATH:[^=]*=',''}
if(!$cmake -or !$Dependencies){throw 'Configure the main build first, or supply -Dependencies'}
& 'D:\developer\nodejs\node.exe' tests\prepare-hook-install.cjs
if($LASTEXITCODE){throw 'Installer extraction failed'}
& $cmake -S tests\hook-install -B build\hook-install "-DCMAKE_PREFIX_PATH=$Dependencies"
if($LASTEXITCODE){throw 'Installer test configure failed'}
& $cmake --build build\hook-install --config Release
if($LASTEXITCODE){throw 'Installer test build failed'}
& .\build\hook-install\Release\hook-install.exe
if($LASTEXITCODE){throw 'Executable installer regression failed'}
& .\build\hook-install\Release\diagnostics.exe
if($LASTEXITCODE){throw 'Diagnostic logger regression failed'}
