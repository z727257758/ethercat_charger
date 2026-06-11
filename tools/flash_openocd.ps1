param(
    [ValidateSet("all", "bootuser", "user_app")]
    [string]$Target = "all",
    [string]$Probe = "cmsis_dap",
    [string]$Soc = "hpm5e00",
    [string]$BootElf = "",
    [string]$UserElf = "",
    [switch]$NoVerify,
    [switch]$NoResetRun
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

if (-not $env:HPM_SDK_BASE) {
    $env:HPM_SDK_BASE = "D:\Workspace\toolchain\sdk_env_v1.11.0\hpm_sdk"
}

$openocd = "D:\Workspace\toolchain\sdk_env_v1.11.0\tools\openocd\openocd.exe"
if (-not (Test-Path $openocd)) {
    $openocd = "openocd"
}

$env:OPENOCD_SCRIPTS = Join-Path $env:HPM_SDK_BASE "boards\openocd"

if ($BootElf -eq "") {
    $BootElf = Join-Path $root "build\ninja\bootuser\output\demo.elf"
}
if ($UserElf -eq "") {
    $UserElf = Join-Path $root "build\ninja\user_app\output\demo.elf"
}

$boardCfg = Join-Path $root "boards\hpm5e31_LuckyCAT\hpm5e31_LuckyCAT.cfg"
$probeCfg = "probes\$Probe.cfg"
$socCfg = "soc\$Soc.cfg"
$verifyArg = if ($NoVerify) { "" } else { " verify" }

if (($Target -eq "all" -or $Target -eq "bootuser") -and -not (Test-Path $BootElf)) {
    throw "BootUser ELF not found: $BootElf. Run .\tools\build.ps1 first."
}
if (($Target -eq "all" -or $Target -eq "user_app") -and -not (Test-Path $UserElf)) {
    throw "User APP ELF not found: $UserElf. Run .\tools\build.ps1 first."
}

$commands = @(
    "init",
    "reset halt"
)

if ($Target -eq "all" -or $Target -eq "bootuser") {
    $commands += "program $($BootElf.Replace('\', '/'))$verifyArg"
}
if ($Target -eq "all" -or $Target -eq "user_app") {
    $commands += "program $($UserElf.Replace('\', '/'))$verifyArg"
}
if (-not $NoResetRun) {
    $commands += "reset run"
}
$commands += "shutdown"

$args = @(
    "-f", $probeCfg,
    "-f", $socCfg,
    "-f", $boardCfg
)

foreach ($cmd in $commands) {
    $args += @("-c", $cmd)
}

Write-Host "OpenOCD: $openocd"
Write-Host "Target : $Target"
Write-Host "Probe  : $Probe"
Write-Host "SOC    : $Soc"
Write-Host "Board  : $boardCfg"

& $openocd @args

