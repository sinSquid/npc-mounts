$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$node='D:\developer\nodejs\node.exe'
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
foreach($name in @('config','dismount-timing','dismount','input','recovery','restore','seat','seat-binding','sync','transition','restraint-hooks','leash-lifecycle','binding-release','actor-load','queued-actions','movement-compat','recast','mount-timing','crawl','capture')){
    & $node "tests/verify-$name.cjs"
    if($LASTEXITCODE){throw "Generator failed: $name"}
    $testArgs=if($name -in @('seat','seat-binding')){'build\test-Dismount.nmp'}elseif($name -eq 'crawl'){'build\stage-0.4.0\SKSE\Plugins\NPCMounts\Crawl.nmc'}else{''}
    & cmd /c "`"$vc`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc build\$name-regression.cpp /Fe:build\$name-regression.exe /Fo:build\$name-regression.obj && build\$name-regression.exe $testArgs"
    if($LASTEXITCODE){throw "Regression failed: $name"}
}
