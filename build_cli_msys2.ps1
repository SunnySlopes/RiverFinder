param(
    [string]$OutDir = "",
    [string]$ObjDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = Join-Path $Root "build\cli" }
if (-not $ObjDir) { $ObjDir = Join-Path $Root "build\cli-obj" }

$MsysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$MsysBin = Join-Path $MsysRoot "ucrt64\bin"
$Gcc = Join-Path $MsysBin "gcc.exe"
$Gxx = Join-Path $MsysBin "g++.exe"

if (-not (Test-Path $Gcc) -or -not (Test-Path $Gxx)) {
    throw "MSYS2 gcc/g++ not found under $MsysBin"
}

# cubiomes is a nested submodule of RiverFinderBase. The include root is the
# submodule root itself, because the sources include "cubiomes/...".
$Includes = @(
    "-I$Root"
)

$CubiomesRoot = Join-Path $Root "cubiomes"
if (-not (Test-Path (Join-Path $CubiomesRoot "generator.c"))) {
    throw "cubiomes submodule not found at $CubiomesRoot. Run 'git submodule update --init --recursive' in RiverFinderBase."
}

New-Item -ItemType Directory -Force -Path $OutDir, $ObjDir | Out-Null
$env:PATH = "$MsysBin;$(Join-Path $MsysRoot 'usr\bin');$env:PATH"

$CSources = @(
    "cubiomes\generator.c",
    "cubiomes\layers.c",
    "cubiomes\biomenoise.c",
    "cubiomes\biomes.c",
    "cubiomes\noise.c",
    "cubiomes\util.c",
    "cubiomes\quadbase.c"
)

# riverFinderLib.cpp includes river_finder.cpp; do not compile river_finder.cpp
# separately or every engine symbol will be duplicated.
$CppSources = @(
    "riverFinderLib.cpp",
    "river_cli.cpp"
)

$Objs = @()
foreach ($src in $CSources) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $obj = Join-Path $ObjDir "$name.o"
    Write-Host "CC  $src"
    & $Gcc -O3 -DNDEBUG -D_WIN32 -std=c11 -ffp-contract=off @Includes -c (Join-Path $Root $src) -o $obj
    if ($LASTEXITCODE -ne 0) { throw "gcc failed on $src" }
    $Objs += $obj
}

foreach ($src in $CppSources) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $obj = Join-Path $ObjDir "$name.o"
    Write-Host "CXX $src"
    & $Gxx -O3 -DNDEBUG -D_WIN32 -DRIVERFINDER_STATIC -std=c++20 -ffp-contract=off @Includes -c (Join-Path $Root $src) -o $obj
    if ($LASTEXITCODE -ne 0) { throw "g++ failed on $src" }
    $Objs += $obj
}

$Exe = Join-Path $OutDir "riverfinder-cli.exe"
Write-Host "LINK $Exe"
& $Gxx -static -o $Exe @Objs -static-libgcc -static-libstdc++
if ($LASTEXITCODE -ne 0) { throw "link failed" }
Write-Host "Built $Exe"
Get-Item $Exe | Format-List FullName, Length, LastWriteTime
