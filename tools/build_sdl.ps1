param([string]$BuildDirectory='build-deps/sdl-build')
$ErrorActionPreference='Stop'
$gyroProject=Split-Path $PSScriptRoot -Parent
$gyroArchive=Join-Path $gyroProject 'build-deps/downloads/SDL3-3.4.16.tar.gz'
$gyroExpected='7322236CD12090C3EB40B9728BE4D49C76F66AD17D04369584D4ECAD5CF77C68'
if(!(Test-Path -LiteralPath $gyroArchive)){
    New-Item -ItemType Directory -Force -Path (Split-Path $gyroArchive -Parent) | Out-Null
    Invoke-WebRequest -Uri 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz' -OutFile $gyroArchive
}
if((Get-FileHash -LiteralPath $gyroArchive -Algorithm SHA256).Hash -ne $gyroExpected){throw 'SDL archive SHA256 mismatch'}
$gyroSourceParent=Join-Path $gyroProject 'build-deps/sdl-source'
$gyroSource=Join-Path $gyroSourceParent 'SDL3-3.4.16'
if(!(Test-Path -LiteralPath (Join-Path $gyroSource 'CMakeLists.txt'))){
    New-Item -ItemType Directory -Force -Path $gyroSourceParent | Out-Null
    tar -xf $gyroArchive -C $gyroSourceParent
    if($LASTEXITCODE){throw 'SDL extraction failed'}
}
$gyroPatches=@('SDL3-steam-touchpads-upstream.patch','SDL_hidapi_steam.c.origins.patch','SDL_hidapi_steam_triton.c.origins.patch')
function Test-GyroPatch([string]$Patch, [switch]$Reverse){
    # Windows PowerShell reports a nonzero native stderr as a terminating error
    # under Stop; a failed probe is expected when the patch is already applied.
    $ErrorActionPreference='Continue'
    if($Reverse){$null=git apply --reverse --check $Patch 2>&1}
    else {$null=git apply --check $Patch 2>&1}
    return $LASTEXITCODE -eq 0
}
Push-Location $gyroSource
try {
    foreach($gyroPatchName in $gyroPatches){
        $gyroPatch=Join-Path $gyroProject ('third_party/patches/'+$gyroPatchName)
        if(Test-GyroPatch $gyroPatch){git apply $gyroPatch;if($LASTEXITCODE){throw "SDL patch failed: $gyroPatchName"}}
        else {
            # Recognize an already-applied patch instead of modifying it twice.
            if(!(Test-GyroPatch $gyroPatch -Reverse)){throw "SDL source differs from the expected patch state: $gyroPatchName"}
        }
    }
    Copy-Item -LiteralPath (Join-Path $gyroProject 'third_party/SDL/gyrolib_controls.h') -Destination 'src/joystick/hidapi/gyrolib_controls.h'
} finally {Pop-Location}
# Normalize duplicate Path/PATH supplied by some Windows launchers.
$gyroBuildPath=$env:PATH
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:PATH=$gyroBuildPath
$gyroBuild=[IO.Path]::GetFullPath((Join-Path $gyroProject $BuildDirectory))
$gyroInstall=Join-Path $gyroProject 'third_party/SDL/SDL3-3.4.16-gyrolib'
cmake -S $gyroSource -B $gyroBuild -A x64 -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF "-DCMAKE_INSTALL_PREFIX=$gyroInstall"
if($LASTEXITCODE){exit $LASTEXITCODE}
cmake --build $gyroBuild --config Release --parallel 6
if($LASTEXITCODE){exit $LASTEXITCODE}
cmake --install $gyroBuild --config Release
exit $LASTEXITCODE
