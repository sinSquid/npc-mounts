$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
& 'D:\developer\nodejs\node.exe' tests/verify-pose-entry.cjs
if($LASTEXITCODE){throw 'Native pose entry evidence changed'}
$taskVC='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
& cmd /c "`"$taskVC`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MD /Ithird_party\minhook\include tests\pose-root.cpp build\Release\npcmounts_minhook.lib /Fe:build\pose-root.exe /Fo:build\pose-root.obj && build\pose-root.exe"
if($LASTEXITCODE){throw 'Real MinHook pose entry test failed'}
