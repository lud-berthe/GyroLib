param([string]$BuildRoot,[string]$Config='Release',[switch]$Desktop)
$ErrorActionPreference='Stop'
$testRoot=Join-Path ([IO.Path]::GetFullPath($BuildRoot)) ('package-tests\'+[guid]::NewGuid().ToString())
$stage=Join-Path $testRoot 'jeu avec espaces é'
$cache=Join-Path $testRoot 'cache utilisateur é'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildRoot "$Config\gyrolib_runtime_host.exe") -Destination (Join-Path $stage 'le_jeu.exe')
Copy-Item -LiteralPath (Join-Path $BuildRoot "$Config\gyrolib_runtime_mod.dll") -Destination (Join-Path $stage 'le_mod.dll')
Copy-Item -LiteralPath (Join-Path $BuildRoot "$Config\gyrolib.dll") -Destination $stage
$env:GYROLIB_RUNTIME_CACHE=$cache
function Run-Test([string]$Mode){
    & (Join-Path $stage 'le_jeu.exe') $Mode
    if($LASTEXITCODE){throw "Bundled mod failed: $Mode ($LASTEXITCODE)"}
}
if($Desktop){Run-Test 'desktop';exit 0}
Run-Test 'lazy'
Run-Test 'normal'
# Loading a host's different SDL first must not redirect GyroLib's imports or
# alter the host SDL instance's subsystem state/hints.
$hostSdl=Join-Path $testRoot 'host-owned-sdl'
New-Item -ItemType Directory -Path $hostSdl | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildRoot "$Config\SDL3.dll") -Destination $hostSdl
& (Join-Path $stage 'le_jeu.exe') 'coexist' (Join-Path $hostSdl 'SDL3.dll')
if($LASTEXITCODE){throw "Host SDL coexistence failed ($LASTEXITCODE)"}
# Content equality checks repair both dependencies before any execution.
$version=Get-ChildItem -LiteralPath $cache -Directory | Select-Object -First 1
[IO.File]::WriteAllText((Join-Path $version.FullName 'SDL3.dll'),'damaged SDL')
[IO.File]::WriteAllText((Join-Path $version.FullName 'gyrolib_sensor_worker.exe'),'damaged reader')
Run-Test 'normal'
# Start several games from a fresh shared cache, testing publication and leases.
$env:GYROLIB_RUNTIME_CACHE=Join-Path $testRoot 'concurrent-cache'
$processes=1..4 | ForEach-Object {Start-Process -FilePath (Join-Path $stage 'le_jeu.exe') -ArgumentList 'hold' -WindowStyle Hidden -PassThru}
foreach($process in $processes){
    if(!$process.WaitForExit(15000)){throw 'Concurrent runtime startup timed out'}
    if($process.ExitCode){throw "Concurrent startup failed: $($process.ExitCode)"}
}
# Never silently fall back to a DLL in the current directory on cache errors.
$blocked=Join-Path $testRoot 'not-a-directory'
[IO.File]::WriteAllText($blocked,'block')
$env:GYROLIB_RUNTIME_CACHE=$blocked
Run-Test 'failure'
$env:GYROLIB_RUNTIME_CACHE='relative-cache'
Run-Test 'failure'
# A directory junction must not redirect extraction into an unrelated directory.
$target=Join-Path $testRoot 'redirect-target'
$junction=Join-Path $testRoot 'redirect-cache'
New-Item -ItemType Directory -Path $target | Out-Null
New-Item -ItemType Junction -Path $junction -Target $target | Out-Null
$env:GYROLIB_RUNTIME_CACHE=$junction
Run-Test 'failure'
if((Get-ChildItem -LiteralPath $target -Force).Count){throw 'Runtime wrote through a cache junction'}
# Installation remains exactly the game, mod and runtime DLL. All writes went
# into the explicit test cache, never to the game directory or a global service.
if((Get-ChildItem -LiteralPath $stage -File).Count -ne 3){throw 'Unexpected files beside the game'}
# Settings are deliberately beside the runtime DLL, independent of this shell's cwd/cache.
Run-Test 'settings'
if(!(Test-Path -LiteralPath (Join-Path $stage 'gyrolib.ini')) -or (Get-ChildItem -LiteralPath $stage -File).Count -ne 4){throw 'Settings file location mismatch'}
Write-Output 'Distribution passed: three binaries, local gyrolib.ini, lazy reader, recovery, concurrency, failed-cache isolation.'
