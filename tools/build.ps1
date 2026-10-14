param([string]$Dependencies, [string]$SkyrimData, [string]$CMake='cmake', [string]$Node='node')
$ErrorActionPreference='Stop'
$taskRepo=Split-Path -Parent $PSScriptRoot
if(!$Dependencies -or !$SkyrimData){throw 'Specify -Dependencies (CommonLib prefix) and -SkyrimData'}
& $CMake -S $taskRepo -B (Join-Path $taskRepo 'build') -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$Dependencies"
if($LASTEXITCODE){throw 'CMake configure failed'}
& $CMake --build (Join-Path $taskRepo 'build') --config Release --parallel 2
if($LASTEXITCODE){throw 'Native build failed'}
& $Node (Join-Path $PSScriptRoot 'build-plugin.cjs') (Join-Path $SkyrimData 'Skyrim.esm')
if($LASTEXITCODE){throw 'Plugin build failed'}
$taskPlugins=Join-Path $taskRepo 'dist\SKSE\Plugins'
New-Item -ItemType Directory -Path $taskPlugins -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRepo 'build\Release\NPCMounts.dll') -Destination $taskPlugins
Write-Output 'Built to dist. This script never modifies MO2 or the game.'
