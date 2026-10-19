param([string]$CrawlBank='build\test-Crawl.nmc')
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$node='D:\developer\nodejs\node.exe'
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
& "$PSScriptRoot\test-hook-install.ps1"
& "$PSScriptRoot\test-pose-root.ps1"
& "$PSScriptRoot\test-water-ride.ps1"
& $node 'tests/verify-native-abi.cjs'
if($LASTEXITCODE){throw 'Installed engine ABI evidence differs; do not deploy collision hook'}
& $node 'tools/inspect-ride-entrypoints.cjs'
if($LASTEXITCODE){throw 'Installed movement/camera entry differs; do not deploy riding hooks'}
& $node 'tools/inspect-jump.cjs'
if($LASTEXITCODE){throw 'Installed native jump instructions differ; do not deploy jump request'}
foreach($name in @('ride-hooks','ride-drive','ride-jump','ride-gait','config','spell-migration','dismount-timing','dismount','input','recovery','restore','seat','seat-binding','sync','transition','restraint-hooks','anchor-update','leash-lifecycle','binding-release','actor-load','queued-actions','movement-compat','recast','mount-timing','crawl','capture','travel','wardrobe','ray','ride-collision')){
    & $node "tests/verify-$name.cjs"
    if($LASTEXITCODE){throw "Generator failed: $name"}
    $testArgs=if($name -eq 'seat'){'build\test-Dismount.nmp build\test-Rider.nmr'}elseif($name -eq 'seat-binding'){'build\test-Dismount.nmp'}elseif($name -eq 'crawl'){$CrawlBank}else{''}
    & cmd /c "`"$vc`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /Itests /FIdiagnostics-double.h build\$name-regression.cpp /Fe:build\$name-regression.exe /Fo:build\$name-regression.obj && build\$name-regression.exe $testArgs"
    if($LASTEXITCODE){throw "Regression failed: $name"}
}
& cmd /c "`"$vc`" >nul && ml64 /nologo /c /Fobuild\collision-abi.obj tests\collision-abi.asm && cl /nologo /std:c++20 /utf-8 /EHsc /DABI_BRIDGE build\ride-collision-regression.cpp build\collision-abi.obj /Fe:build\collision-abi.exe /Fo:build\collision-abi-test.obj && build\collision-abi.exe"
if($LASTEXITCODE){throw 'Independent native ABI bridge failed'}
