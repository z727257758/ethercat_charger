# EtherCAT Charger

## 1. 概述

本目录用于 HPM5E31 LuckyCAT EtherCAT Charger 从站，包含 SSC 生成代码、CoE/PDO 对象字典、FoE OTA 通道和 HPM EtherCAT port。

hpm_apps仓库：
  github: https://github.com/hpmicro/hpm_apps
  gitee: https://gitee.com/hpmicro/hpm_apps

## 2. 准备

  请参照ECAT_IO的README

## 3. 工程设置

  请参照ECAT_IO的README

## 4. 生成从站协议栈代码

由于许可问题, HPM_SDK不提供EtherCAT从站协议栈代码(SSC), 用户须从倍福官网下载SSC Tool并生成从站协议栈代码

### 4.1. 下载SSC Tool

  请参照ECAT_IO的README

### 4.2 SSC Tool中导入配置文件
  配置文件路径为: SSC/Config/HPM_ECAT_CHARGER_Config.xml

### 4.3 SSC Tool中创建新的工程
  应用文件路径为：SSC/ethercat_charger_objects.xlsx

### 4.4 生成协议栈代码
  协议栈代码输出路径为: SSC/Src

### 4.5 重新生成后的必要修改

SSC Tool 会覆盖 `SSC/Src/ethercat_charger.c`。每次重新生成后，需要确认以下内容仍然存在：

- `APPL_InputMapping()`：将 `charger_txpdo_t` 写入对象 `0x6000` 和 14 字节 TxPDO。
- `APPL_OutputMapping()`：将 2 字节 RxPDO 控制字写入 `charger_rxpdo_t`。
- `APPL_Application()`：同步对象 `0x7000:01` 的控制字。
- RxPDO 和 TxPDO 线缆结构的静态断言分别为 2 字节和 14 字节。
- `ecat_sources.cmake` 编译 `SSC/Src/ethercat_charger.c`。

重新生成后应执行完整构建：

```powershell
.\tools\build.ps1
```

## 5. PDO 与 Modbus 协议

当前设备修订号为 `0x00000002`。ESI、CoE 对象 `0x1018:03` 和
`SSC/Src/eeprom.h` 中的 Revision Number 必须保持一致。

### 5.1 RxPDO（主站到从站）

RxPDO 固定为 2 字节，仅包含 `UINT16 control_word`：

| 位 | Modbus 地址 | 功能 |
|---|---|---|
| bit0 | `0x5000` | 充电模式 |
| bit1 | `0x5001` | 开关小板上电 |
| bit2 | `0x5002` | 存储模式 |
| bit3 | `0x5003` | 内阻测试 |
| bit4 | `0x5004` | 飞机发射 |
| bit5 | `0x5005` | 飞机起飞 |
| bit6 | `0x5006` | BMS 开启 |
| bit7-bit15 | - | 保留 |

控制位通过 Modbus 功能码 `05` 写入，置位发送 `FF00`，清零发送 `0000`。

### 5.2 TxPDO（从站到主站）

TxPDO 固定为 14 字节，所有字段均为 `UINT16`，顺序如下：

| 对象 | 字段 | Modbus 来源 |
|---|---|---|
| `0x6000:01` | `status_word` | 功能码 `02`，地址 `0x0000-0x000F` |
| `0x6000:02` | `battery_level_x100` | `0x4002` |
| `0x6000:03` | `sys_input_voltage_mv` | `0x4003` |
| `0x6000:04` | `battery_voltage_mv` | `0x4004` |
| `0x6000:05` | `charge_current_ma` | `0x4005` |
| `0x6000:06` | `discharge_current_ma` | `0x4006` |
| `0x6000:07` | `internal_resistance_mohm` | `0x4007` |

状态字 bit0-bit12 对应 Modbus 状态地址 `0x0000-0x000C`，bit13-bit15 保留。
Modbus 从机地址为 `0x15`，运行参数不进行二次换算。

## 6. TwinCAT工程设置
  请参照ECAT_IO的README

### 6.1. 添加ESI文件
  ESI文件名称: ethercat_charger.xml

### 6.2 创建工程
  请参照ECAT_IO的README

### 6.3 软件配置
  请参照ECAT_IO的README

### 6.4 扫描设备
  请参照ECAT_IO的README

### 6.5 更新EEPROM
  请选择 **ethercat_charger** 设备描述文件
  ![](doc/twincat_eeprom_update.png)

当 PDO 布局或设备修订号变化时，需要重新导入 ESI 并更新 EEPROM。固件启动时会比较
Product Code 和 Revision Number；内置 EEPROM 镜像的修订号较新时会触发更新。

### 6.6 FOE操作
  1. 设置MailBox timeout时间(当文件比较大时， 需要调整timeout时间)
  ![](doc/twincat_device_timeout.png)
  2. 选择从站， 进入Bootstrap模式
  ![](doc/twincat_device_bootstrap.png)
  3. 进入Bootstrap模式后， 下载文件到从站
    点击Download
    ![](doc/twincat_foe_download_1.png)
    选择要下载的文件，注意：此文件为脚本签名之后的文件(update_sign.bin)
    ![](doc/twincat_foe_download_2.png)
    编辑文件名称和密码， 文件名称是：**app**; 密码是：**87654321**.
    ![](doc/twincat_foe_download_3.png)
    等待写进度条完成
  4. 进入Bootstrap模式后，从从站读取文件
    点击Uplaod
    ![](doc/twincat_foe_read_1.png)
    选择文件保存文件和名称
    ![](doc/twincat_foe_read_2.png)
    编辑文件名称和密码， 文件名称是：**app**; 密码是：**87654321**. (注意:文件名称和密码是固定的)
    ![](doc/twincat_foe_download_3.png)
    等待读进度条完成
    (注意: 下载完成后，并不会立即重启，需退出Bootstrap模式才可以会重启跳转到新的固件中。)
  4. 退出Bootstrap模式

## 7. 运行现象

当工程正确运行后, 串口终端会输出如下信息：
当EEPROM未被初始化时，输出如下信息提示需要初始化EEPROM内容。
```console
EtherCAT FOE sample
Write or Read file from flash by FOE
EEPROM loading with checksum error.
EtherCAT communication is possible even if the EEPROM is blank(checksum error),
but PDI not operational, please update eeprom  context.
```
当EEPROM被正确初始化后， 输出如下信息， 在Twincat中可以进行文件写读操作，对比写下去与读回来的文件保持一致。
```console
EtherCAT IO sample
Write or Read file from flash by FOE
EEPROM loading successful, no checksum error.
```

固件下载中
```console
EEPROM loading successful, no checksum error.
Write file start
ota0, device:0x0048504D, length:85416, version:1728558561, hash_type:0x00000004
ota0 data download...
complete checksum and reset!

ota success!

Write file finish
```

退出Bootstrap模式，重启跳转新固件运行
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
