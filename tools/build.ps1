param([string]$Dependencies, [string]$SkyrimData, [string]$Codec, [string]$Python='python', [string]$CMake='cmake', [string]$Node='node')
$ErrorActionPreference='Stop'
$taskRepo=Split-Path -Parent $PSScriptRoot
if(!$Dependencies -or !$SkyrimData -or !$Codec){throw 'Specify -Dependencies (CommonLib prefix), -SkyrimData and -Codec (PyNifly HKX directory)'}
& $CMake -S $taskRepo -B (Join-Path $taskRepo 'build') -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$Dependencies"
if($LASTEXITCODE){throw 'CMake configure failed'}
& $CMake --build (Join-Path $taskRepo 'build') --config Release --parallel 2
if($LASTEXITCODE){throw 'Native build failed'}
& $Node (Join-Path $PSScriptRoot 'build-plugin.cjs') (Join-Path $SkyrimData 'Skyrim.esm')
if($LASTEXITCODE){throw 'Plugin build failed'}
Copy-Item -Path (Join-Path $taskRepo 'assets\*') -Destination (Join-Path $taskRepo 'dist') -Recurse -Force
$taskPlugins=Join-Path $taskRepo 'dist\SKSE\Plugins'
New-Item -ItemType Directory -Path $taskPlugins -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRepo 'build\Release\NPCMounts.dll') -Destination $taskPlugins
Copy-Item -LiteralPath (Join-Path $taskRepo 'config\NPCMounts.ini') -Destination $taskPlugins
& $Python (Join-Path $PSScriptRoot 'build-seat-motion.py') --codec $Codec --output (Join-Path $taskRepo 'dist')
if($LASTEXITCODE){throw 'Dismount motion build failed; generate base animations first'}
Write-Output 'Built to dist. This script never modifies MO2 or the game.'
