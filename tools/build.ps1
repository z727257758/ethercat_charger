param(
    [string]$BuildType = "flash_xip",
    [string]$Board = "hpm5e31_LuckyCAT"
)

$ErrorActionPreference = "Stop"

if (-not $env:HPM_SDK_BASE) {
    $env:HPM_SDK_BASE = "D:\Workspace\toolchain\sdk_env_v1.11.0\hpm_sdk"
}

if (-not $env:HPM_APP_BASE) {
    $env:HPM_APP_BASE = "D:\Workspace\toolchain\hpm_apps"
}

if (-not $env:GNURISCV_TOOLCHAIN_PATH) {
    $env:GNURISCV_TOOLCHAIN_PATH = "D:\Workspace\toolchain\sdk_env_v1.11.0\toolchains\rv32imac_zicsr_zifencei_multilib_b_ext-win"
}

$sdkPython = "D:\Workspace\toolchain\sdk_env_v1.11.0\tools\python3"
if ((Test-Path $sdkPython) -and (-not $env:PATH.StartsWith($sdkPython))) {
    $env:PATH = "$sdkPython;$env:PATH"
}

$root = Split-Path -Parent $PSScriptRoot
$boardSearchPath = Join-Path $root "boards"

$bootBuild = Join-Path $root "build\ninja\bootuser"
$appBuild = Join-Path $root "build\ninja\user_app"

function Promote-CMakeTempFiles {
    param([string]$BuildDir)

    $pairs = @(
        @{ Dir = $BuildDir; Name = "CMakeCache.txt" },
        @{ Dir = $BuildDir; Name = "build.ninja" },
        @{ Dir = $BuildDir; Name = "cmake_install.cmake" },
        @{ Dir = $BuildDir; Name = "compile_commands.json" },
        @{ Dir = (Join-Path $BuildDir "CMakeFiles"); Name = "rules.ninja" },
        @{ Dir = (Join-Path $BuildDir "CMakeFiles"); Name = "TargetDirectories.txt" }
    )

    foreach ($pair in $pairs) {
        $dir = $pair.Dir
        $name = $pair.Name
        if (-not (Test-Path $dir)) {
            continue
        }
        if (Test-Path (Join-Path $dir $name)) {
            continue
        }

        $tmp = Get-ChildItem -LiteralPath $dir -Filter "$name.tmp*" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($tmp) {
            Copy-Item -Force -LiteralPath $tmp.FullName -Destination (Join-Path $dir $name)
        }
    }
}

cmake -S (Join-Path $root "bootuser") -B $bootBuild -G "Ninja" `
    -DBOARD_SEARCH_PATH="$boardSearchPath" `
    -DBOARD="$Board" `
    -DHPM_BUILD_TYPE="$BuildType" `
    -DCMAKE_BUILD_TYPE=debug
Promote-CMakeTempFiles $bootBuild
cmake --build $bootBuild -j 16

cmake -S (Join-Path $root "user_app") -B $appBuild -G "Ninja" `
    -DBOARD_SEARCH_PATH="$boardSearchPath" `
    -DBOARD="$Board" `
    -DHPM_BUILD_TYPE="$BuildType" `
    -DCMAKE_BUILD_TYPE=debug
Promote-CMakeTempFiles $appBuild
cmake --build $appBuild -j 16
