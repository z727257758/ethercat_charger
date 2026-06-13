# EtherCAT Charger

## 1. Overview

This directory contains the HPM5E31 LuckyCAT EtherCAT Charger slave stack integration, including SSC generated code, CoE/PDO object dictionary, FoE OTA channel, and the HPM EtherCAT port.

hpm_apps repo：
  github: https://github.com/hpmicro/hpm_apps
  gitee: https://gitee.com/hpmicro/hpm_apps

## 2. Prepare

  Please refer to the README of ECAT_IO sample

## 3. Project Setting

  Please refer to the README of ECAT_IO sample

## 4. Generate EtherCAT slave stack code

Due to licensing issues, HPMSDK does not provide EtherCAT slave protocol stack code (SSC). Users have download the SSC Tool from Beckoff's official website and generate the slave stack code according to the steps.

### 4.1. Download SSC Tool

  Please refer to the README of ECAT_IO sample

### 4.2 SSC Tool import configuration files
  configuration file path: SSC/Config/HPM_ECAT_CHARGER_Config.xml

### 4.3 SSC Tool create new project
  application file path: SSC/ethercat_charger_objects.xlsx

### 4.4 Create slave stack files
  stack code output path: SSC/Src

### 4.5 Required changes after regeneration

SSC Tool overwrites `SSC/Src/ethercat_charger.c`. After each regeneration, verify that:

- `APPL_InputMapping()` copies `charger_txpdo_t` into object `0x6000` and the 14-byte TxPDO.
- `APPL_OutputMapping()` copies the 2-byte RxPDO control word into `charger_rxpdo_t`.
- `APPL_Application()` synchronizes the control word from object `0x7000:01`.
- Static assertions require 2-byte RxPDO and 14-byte TxPDO wire structures.
- `ecat_sources.cmake` compiles `SSC/Src/ethercat_charger.c`.

Run a complete build after regeneration:

```powershell
.\tools\build.ps1
```

## 5. PDO and Modbus protocol

The current device revision is `0x00000002`. The ESI, CoE object `0x1018:03`,
and Revision Number in `SSC/Src/eeprom.h` must match.

### 5.1 RxPDO (master to slave)

RxPDO is fixed at 2 bytes and contains only `UINT16 control_word`:

| Bit | Modbus address | Function |
|---|---|---|
| bit0 | `0x5000` | Charging mode |
| bit1 | `0x5001` | Switch board power |
| bit2 | `0x5002` | Storage mode |
| bit3 | `0x5003` | Internal resistance test |
| bit4 | `0x5004` | Aircraft launch |
| bit5 | `0x5005` | Aircraft takeoff |
| bit6 | `0x5006` | BMS enable |
| bit7-bit15 | - | Reserved |

Control bits use Modbus function code `05`: `FF00` sets a bit and `0000` clears it.

### 5.2 TxPDO (slave to master)

TxPDO is fixed at 14 bytes. All fields are `UINT16` in this order:

| Object | Field | Modbus source |
|---|---|---|
| `0x6000:01` | `status_word` | Function code `02`, addresses `0x0000-0x000F` |
| `0x6000:02` | `battery_level_x100` | `0x4002` |
| `0x6000:03` | `sys_input_voltage_mv` | `0x4003` |
| `0x6000:04` | `battery_voltage_mv` | `0x4004` |
| `0x6000:05` | `charge_current_ma` | `0x4005` |
| `0x6000:06` | `discharge_current_ma` | `0x4006` |
| `0x6000:07` | `internal_resistance_mohm` | `0x4007` |

Status bits 0-12 map to Modbus status addresses `0x0000-0x000C`; bits 13-15 are
reserved. The Modbus slave address is `0x15`, and register values are forwarded
without additional scaling.

## 6. TwinCAT Project setting
  Please refer to the README of ECAT_IO sample

### 6.1. Add ESI file
  ESI file name: ethercat_charger.xml

### 6.2 Create Project
  Please refer to the README of ECAT_IO sample

### 6.3 Software Configuration
  Please refer to the README of ECAT_IO sample

### 6.4 Scan device
  Please refer to the README of ECAT_IO sample

### 6.5 Update EEPROM context
  select **ethercat_charger**
  ![](doc/twincat_eeprom_update.png)

Re-import the ESI and update EEPROM whenever the PDO layout or device revision
changes. At startup, the firmware compares Product Code and Revision Number and
updates EEPROM when the built-in image has a newer revision.

### 6.6 FOE action
  1. Set MailBox timeout time (when the file is large, the timeout time needs to be adjusted)
  ![](doc/twincat_device_timeout.png)
  2. Enter Bootstrap mode
  ![](doc/twincat_device_bootstrap.png)
  3. Download file
    click 'Download'
    ![](doc/twincat_foe_download_1.png)
    select file to download. Note: This file is the file after the script is signed (update_sign.bin)
    ![](doc/twincat_foe_download_2.png)
    edit file name and password， file name：**app**; pass word：**87654321**.
    ![](doc/twincat_foe_download_3.png)
    waiting for completion of writing
  4. Enter Bootstrap mode，uploade file
    click 'Uplaod'
    ![](doc/twincat_foe_read_1.png)
    select file name and path
    ![](doc/twincat_foe_read_2.png)
    edit file name and password， file name：**app**; pass word：**87654321**. (Note: the file name and password are fixed)
    ![](doc/twincat_foe_download_3.png)
    waiting for completion of reading
    (Note: After the download is complete, it will not reboot immediately, you need to exit Bootstrap mode to reboot and jump to the new firmware).
  4. quit Bootstrap mode

## 7. Running the example

After the project is running correctly：
When the EEPROM is not initialized, the following message is output indicating the need to initialize the contents of the EEPROM.
```console
EtherCAT FOE sample
Write or Read file from flash by FOE
EEPROM loading with checksum error.
EtherCAT communication is possible even if the EEPROM is blank(checksum error),
but PDI not operational, please update eeprom  context.
```
After the EEPROM is properly initialized, the following information is output, which can be used for file write and read operations in Twincat, comparing the written and read files to ensure consistency.
```console
EtherCAT IO sample
Write or Read file from flash by FOE
EEPROM loading successful, no checksum error.
```
Firmware download in progress
```console
EEPROM loading successful, no checksum error.
Write file start
ota0, device:0x0048504D, length:85416, version:1728558561, hash_type:0x00000004
ota0 data download...
complete checksum and reset!

ota success!

Write file finish
```

Exit Bootstrap mode and reboot to jump to the new firmware running
```console
system reset...

----------------------------------------------------------------------
$$\   $$\ $$$$$$$\  $$\      $$\ $$\
$$ |  $$ |$$  __$$\ $$$\    $$$ |\__|
$$ |  $$ |$$ |  $$ |$$$$\  $$$$ |$$\  $$$$$$$\  $$$$$$\   $$$$$$\
$$$$$$$$ |$$$$$$$  |$$\$$\$$ $$ |$$ |$$  _____|$$  __$$\ $$  __$$\
$$  __$$ |$$  ____/ $$ \$$$  $$ |$$ |$$ /      $$ |  \__|$$ /  $$ |
$$ |  $$ |$$ |      $$ |\$  /$$ |$$ |$$ |      $$ |      $$ |  $$ |
$$ |  $$ |$$ |      $$ | \_/ $$ |$$ |\$$$$$$$\ $$ |      \$$$$$$  |
\__|  \__|\__|      \__|     \__|\__| \_______|\__|       \______/
----------------------------------------------------------------------
boot user

ver1:1728558561,ver2:1726018801

APP0, verify SUCCESS!

APP index:0
hello world, THIS OTA0
ECAT FOE Funcation
EEPROM loading successful, no checksum error.

```
























