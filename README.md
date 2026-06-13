# EtherCAT Charger

基于 HPM5E31 LuckyCAT 的独立充电器工程，包含 EtherCAT 从站、FoE OTA、
HPM OTA 中间件、FreeRTOS、Modbus RTU 主站及充电器应用。

## 环境配置

构建前设置以下环境变量：

```powershell
$env:HPM_SDK_BASE = "D:\Workspace\toolchain\sdk_env_v1.11.0\hpm_sdk"
$env:HPM_APP_BASE = "D:\Workspace\toolchain\hpm_apps"
```

工程使用 `boards/hpm5e31_LuckyCAT` 下的本地板级支持包，并从
`HPM_APP_BASE\middleware\hpm_ota` 引入 OTA 中间件。

当前 EtherCAT 设备修订号为 `0x00000002`。RxPDO 为 2 字节控制字，TxPDO
为 14 字节充电器状态及核心测量值。SSC 重新生成、TwinCAT 和 FoE 的详细说明
参见 [`user_app/ecat/README_zh.md`](user_app/ecat/README_zh.md)。

## 构建

```powershell
.\tools\build.ps1
```

生成以下文件：

- `build\ninja\bootuser\output\ethercat_charger_bootuser.bin`
- `build\ninja\user_app\output\ethercat_charger_user_app.bin`

## Modbus RTU 协议

控制器作为 Modbus RTU 主站，使用 UART2 的 PC08/PC09 引脚：

- 从机地址：`0x15`
- 串口参数：115200 波特率、8 数据位、无校验、1 停止位
- 轮询周期：100 ms
- 多字节寄存器采用标准 Modbus 大端字节序

### 状态位

状态位为只读离散输入。使用功能码 `02`，从地址 `0x0000` 开始读取 16 位。
状态值为 `1` 表示对应状态有效。

| 地址/位 | 测点描述 | 备注 |
|---|---|---|
| `0x00` / bit0 | 充电开启 | |
| `0x01` / bit1 | 开关小板上电 | |
| `0x02` / bit2 | 电阻放电开启 | |
| `0x03` / bit3 | 充电完成 | |
| `0x04` / bit4 | 电阻放电完成 | |
| `0x05` / bit5 | 飞机发射 | |
| `0x06` / bit6 | 飞机起飞 | |
| `0x07` / bit7 | 与飞机电池连接 | |
| `0x08` / bit8 | BMS 状态 | `1`：开启，`0`：关闭 |
| `0x09` / bit9 | 装订状态 | `1`：成功，`0`：不成功 |
| `0x0A` / bit10 | 与飞机连接 | |
| `0x0B` / bit11 | 充电模式 | |
| `0x0C` / bit12 | 存储模式 | |
| `0x0D` / bit13 | 预留 | |
| `0x0E` / bit14 | 预留 | |
| `0x0F` / bit15 | 预留 | |

读取全部状态位的请求示例：

```text
15 02 00 00 00 10 7A D2
```

### 运行参数寄存器

运行参数为只读保持寄存器，使用功能码 `03`。各寄存器的倍率和单位如下：

| 地址 | 测点描述 | 数据类型 | 倍率/单位 |
|---|---|---|---|
| `0x4000` | 充电器 ID 地址 | `UINT16` | 1 |
| `0x4001` | 飞机 ID 地址 | `UINT16` | 1 |
| `0x4002` | 电池电量 | `UINT16` | 100，`%` |
| `0x4003` | SYS 输入电压 | `UINT16` | 1000，mV |
| `0x4004` | BAT 电池电压 | `UINT16` | 1000，mV |
| `0x4005` | 充电电流 | `UINT16` | 1000，mA |
| `0x4006` | 放电电流 | `UINT16` | 1000，mA |
| `0x4007` | 内阻值 | `UINT16` | 1000，mΩ |
| `0x4008` | 电池 1 电压 | `UINT16` | 1000，mV |
| `0x4009` | 电池 2 电压 | `UINT16` | 1000，mV |
| `0x400A` | 电池 3 电压 | `UINT16` | 1000，mV |
| `0x400B` | 电池 4 电压 | `UINT16` | 1000，mV |
| `0x400C` | 电池 5 电压 | `UINT16` | 1000，mV |
| `0x400D` | 电池 6 电压 | `UINT16` | 1000，mV |
| `0x400E` | 充电时间 | `UINT16` | 分钟 |
| `0x400F` | 放电时间 | `UINT16` | 分钟 |

当前固件循环读取 `0x4002~0x4007`，用于 TxPDO 上报，不进行额外数值换算。

### 控制线圈

控制命令使用功能码 `05`。写入 `FF00` 表示开启或触发，写入 `0000` 表示
停止、关闭或清除。固件启动时强制同步全部 7 个控制位，之后仅写入发生变化的位。

| 地址/控制位 | 功能 | 有效值 |
|---|---|---|
| `0x5000` / bit0 | 充电模式 | `FF00`：启动，`0000`：停止 |
| `0x5001` / bit1 | 开关小板上电 | `FF00`：上电，`0000`：断电 |
| `0x5002` / bit2 | 存储模式 | `FF00`：启动，`0000`：停止 |
| `0x5003` / bit3 | 内阻测试 | `FF00`：启动，`0000`：清除 |
| `0x5004` / bit4 | 飞机发射 | `FF00`：触发，`0000`：清除 |
| `0x5005` / bit5 | 飞机起飞 | `FF00`：触发，`0000`：清除 |
| `0x5006` / bit6 | BMS | `FF00`：开启，`0000`：关闭 |

BMS 开启后才能启动充电、放电和内阻测试。BMS 关闭后会停止这些功能，
并允许飞机发射、起飞和连接操作。

## EtherCAT PDO 协议

EtherCAT 设备修订号为 `0x00000002`。

### RxPDO

RxPDO 固定为 2 字节：

| 对象 | 数据类型 | 描述 |
|---|---|---|
| `0x7000:01` | `UINT16` | `control_word`，bit0-bit6 对应线圈 `0x5000-0x5006` |

bit7-bit15 为保留位，固件忽略这些位。

### TxPDO

TxPDO 固定为 14 字节，所有字段均为 `UINT16`：

| 对象 | 字段 | Modbus 来源 |
|---|---|---|
| `0x6000:01` | `status_word` | 功能码 `02`，`0x0000-0x000F` |
| `0x6000:02` | `battery_level_x100` | `0x4002` |
| `0x6000:03` | `sys_input_voltage_mv` | `0x4003` |
| `0x6000:04` | `battery_voltage_mv` | `0x4004` |
| `0x6000:05` | `charge_current_ma` | `0x4005` |
| `0x6000:06` | `discharge_current_ma` | `0x4006` |
| `0x6000:07` | `internal_resistance_mohm` | `0x4007` |

ESI 的 `RevisionNo`、CoE 对象 `0x1018:03` 和
`user_app/ecat/SSC/Src/eeprom.h` 中的修订号必须保持一致。

## 闪存布局

1 MB QSPI NOR 的分区由 `common/hpm_flashmap.h` 定义：

- `0x00000-0x03000`：启动头和 NOR 配置
- `0x03000-0x43000`：BootUser
- `0x43000-0x99000`：APP1
- `0x99000-0xEF000`：APP2
- `0xEF000-0xFF000`：EtherCAT EEPROM 模拟区
- `0xFF000-0x100000`：用户/保留区

## OTA

FoE OTA 通道由 `hpm_apps\middleware\hpm_ota\port\ecat` 提供。

TwinCAT FoE 参数：

- 文件名：`app`
- 密码：`87654321`
- 下载文件：`update_sign.bin`

FoE 下载完成后退出 Bootstrap 模式，设备将复位并启动更新后的固件。
