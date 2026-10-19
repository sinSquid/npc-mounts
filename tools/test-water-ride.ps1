$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$taskVC='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
& cmd /c "`"$taskVC`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc tests\water-ride.cpp /Fe:build\water-ride.exe /Fo:build\water-ride.obj && build\water-ride.exe"
if($LASTEXITCODE){throw 'Water ride regression failed'}
