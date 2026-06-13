# EtherCAT Charger

Standalone HPM5E31 LuckyCAT project with EtherCAT FoE OTA, HPM OTA middleware, FreeRTOS, and a charger application scaffold.

## Environment

Set these variables before building:

```powershell
$env:HPM_SDK_BASE = "D:\Workspace\toolchain\sdk_env_v1.11.0\hpm_sdk"
$env:HPM_APP_BASE = "D:\Workspace\toolchain\hpm_apps"
```

The project uses the local board package under `boards/hpm5e31_LuckyCAT` and imports OTA from `HPM_APP_BASE\middleware\hpm_ota`.

The EtherCAT device currently uses revision `0x00000002`, a 2-byte RxPDO control
word, and a 14-byte TxPDO containing charger status and core measurements. See
[`user_app/ecat/README_en.md`](user_app/ecat/README_en.md) or
[`user_app/ecat/README_zh.md`](user_app/ecat/README_zh.md) for the complete PDO,
Modbus, ESI/EEPROM, and SSC regeneration notes.

## Build

```powershell
.\tools\build.ps1
```

This builds:

- `build\ninja\bootuser\output\ethercat_charger_bootuser.bin`
- `build\ninja\user_app\output\ethercat_charger_user_app.bin`

## Flash Layout

The 1MB QSPI NOR layout is defined in `common/hpm_flashmap.h`.

- `0x00000-0x03000`: boot header / NOR config
- `0x03000-0x43000`: BootUser
- `0x43000-0x99000`: APP1
- `0x99000-0xEF000`: APP2
- `0xEF000-0xFF000`: EtherCAT EEPROM emulation
- `0xFF000-0x100000`: user/reserved

## OTA

The FoE OTA channel is provided by `hpm_apps\middleware\hpm_ota\port\ecat`.

TwinCAT FoE settings:

- File name: `app`
- Password: `87654321`
- Download file: `update_sign.bin`

After FoE download, leave Bootstrap mode to reset and boot the updated image.
