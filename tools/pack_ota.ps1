param(
    [string]$Firmware = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

if ($Firmware -eq "") {
    $Firmware = Join-Path $root "build\ninja\user_app\output\ethercat_charger_user_app.bin"
}

if (-not $env:HPM_APP_BASE) {
    $env:HPM_APP_BASE = "D:\Workspace\toolchain\hpm_apps"
}

$packTool = Join-Path $env:HPM_APP_BASE "apps\otav2\tool\ota_pack_tool\pack_ota.py"
python $packTool $Firmware
