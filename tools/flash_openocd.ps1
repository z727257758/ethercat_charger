param(
    [ValidateSet("all", "bootuser", "user_app")]
    [string]$Target = "all",
    [string]$Probe = "cmsis_dap",
    [string]$Soc = "hpm5e00",
    [string]$BoardCfg = "",
    [string]$BootElf = "",
    [string]$UserElf = "",
    [ValidateSet("reset_soc", "halt_resume", "reset_run", "reset_run_resume")]
    [string]$ResetMode = "reset_soc",
    [int]$ResetDelayMs = 300,
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

$openocdScripts = Join-Path $env:HPM_SDK_BASE "boards\openocd"
$env:OPENOCD_SCRIPTS = $openocdScripts

if ($BootElf -eq "") {
    $BootElf = Join-Path $root "build\ninja\bootuser\output\ethercat_charger_bootuser.elf"
}
if ($UserElf -eq "") {
    $UserElf = Join-Path $root "build\ninja\user_app\output\ethercat_charger_user_app.elf"
}

if ($BoardCfg -eq "") {
    $BoardCfg = Join-Path $openocdScripts "boards\hpm5e00evk.cfg"
}
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
    if ($ResetMode -eq "reset_soc") {
        $commands += "reset_soc"
    } elseif ($ResetMode -eq "halt_resume") {
        $commands += "reset halt"
        $commands += "sleep 100"
        $commands += "resume"
    } elseif ($ResetMode -eq "reset_run") {
        $commands += "reset run"
    } else {
        $commands += "reset run"
        $commands += "sleep 100"
        $commands += "resume"
    }
    $commands += "sleep $ResetDelayMs"
}
$commands += "exit"

$args = @(
    "-s", $openocdScripts,
    "-f", $probeCfg,
    "-f", $socCfg,
    "-f", $BoardCfg
)

foreach ($cmd in $commands) {
    $args += @("-c", $cmd)
}

Write-Host "OpenOCD: $openocd"
Write-Host "Target : $Target"
Write-Host "Probe  : $Probe"
Write-Host "SOC    : $Soc"
Write-Host "Board  : $BoardCfg"
if (-not $NoResetRun) {
    Write-Host "Reset  : $ResetMode, delay ${ResetDelayMs}ms"
} else {
    Write-Host "Reset  : disabled"
}

& $openocd @args
