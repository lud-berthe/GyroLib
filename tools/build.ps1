param([string]$BuildDirectory='build',[switch]$Static,[switch]$CoreOnly,[switch]$Modular)
$ErrorActionPreference='Stop'
# Some Windows launchers supply both Path and PATH; MSBuild rejects duplicates.
$gyroBuildPath=$env:PATH
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:PATH=$gyroBuildPath
$projectRoot=Split-Path $PSScriptRoot -Parent
$arguments=@('-S',$projectRoot,'-B',$BuildDirectory,'-A','x64')
# Explicit values make switching modes in the same build directory reliable;
# an earlier -CoreOnly/-Static invocation must not silently change the next one.
$shared=if($Static){'OFF'}else{'ON'}
$extras=if($CoreOnly){'OFF'}else{'ON'}
$single=if($Static -or $CoreOnly -or $Modular){'OFF'}else{'ON'}
$arguments+=@("-DBUILD_SHARED_LIBS=$shared","-DGL_SINGLE_DLL=$single",
    "-DGL_BUILD_OVERLAY=$(if($IsWindows){$extras}else{'OFF'})",
    "-DGL_BUILD_SDL=$extras","-DGL_BUILD_PANEL=$extras","-DGL_BUILD_EXAMPLES=$extras",'-DGL_BUILD_TESTS=ON')
& cmake @arguments
if($LASTEXITCODE){exit $LASTEXITCODE}
& cmake --build $BuildDirectory --config Release --parallel 4
if($LASTEXITCODE){exit $LASTEXITCODE}
& ctest --test-dir $BuildDirectory -C Release --output-on-failure
exit $LASTEXITCODE
