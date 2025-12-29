param(
    [string]$Config = $env:CONFIG,
    [string]$Triplet = "x64-windows-static",
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"

if (-not $Config) {
    $Config = "Release"
}

if (-not $VcpkgRoot) {
    throw "VCPKG_ROOT is not set. Set it to your vcpkg installation (e.g. C:\vcpkg) or pass -VcpkgRoot."
}

$toolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"

    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
    Push-Location $scriptDir
    try {
        $buildDir = Join-Path $scriptDir "build"
        $cache = Join-Path $buildDir "CMakeCache.txt"

        if (-not (Test-Path $cache)) {
            $configureArgs = @(
                "-S", $scriptDir,
                "-B", $buildDir,
                "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
                "-DVCPKG_TARGET_TRIPLET=$Triplet",
                "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>"
            )
            cmake @configureArgs
        } else {
            Write-Host "CMake cache found. Skipping configure step." -ForegroundColor Cyan
        }

        cmake --build $buildDir --config $Config

    $compileDb = Join-Path $buildDir "compile_commands.json"
    if (Test-Path $compileDb) {
        Copy-Item -Path $compileDb -Destination (Join-Path $scriptDir "compile_commands.json") -Force
    }
}
finally {
    Pop-Location
}
