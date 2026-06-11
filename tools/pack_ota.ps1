param(
    [string]$Firmware = "..\build\user_app\output\demo.bin"
)

$ErrorActionPreference = "Stop"

if (-not $env:HPM_APP_BASE) {
    $env:HPM_APP_BASE = "D:\Workspace\toolchain\hpm_apps"
}

$packTool = Join-Path $env:HPM_APP_BASE "apps\otav2\tool\ota_pack_tool\pack_ota.py"
python $packTool $Firmware

