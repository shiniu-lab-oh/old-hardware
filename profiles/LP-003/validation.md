# LP-003 验证记录

## 基线

- 日期：2026-09-07
- Old Hardware commit：`0a99cc4` (`feat: add LP003 driver`)
- ESP-IDF：v6.0.2
- Target：ESP32
- 实测芯片：ESP32-D0WD-V3 revision 3.1
- USB 串口：CH340，实测端口为 COM5
- Profile：LP-003
- Driver：`lp003`

端口号由 Windows 动态分配，不属于 Profile 配置。

## 可复现构建

在已加载 ESP-IDF 环境的 PowerShell 中执行：

```powershell
cd D:\Code\shiniu-lab\old-hardware\apps\pb-runtime
idf.py -B build-lp003 `
  -D SDKCONFIG="$PWD/build-lp003/sdkconfig" `
  -D PB_RUNTIME_PROFILE_DEFAULTS=../../profiles/LP-003/pb-runtime.sdkconfig.defaults `
  set-target esp32
idf.py -B build-lp003 build
```

首次配置日志必须包含：

```text
PB Runtime profile defaults: .../profiles/LP-003/pb-runtime.sdkconfig.defaults
```

并检查生成配置：

```powershell
Select-String build-lp003/sdkconfig -Pattern `
  'CONFIG_PB_PANEL_PROFILE|CONFIG_PB_PRIMARY_KEY_INDEX'
```

预期：

```text
CONFIG_PB_PANEL_PROFILE="LP-003"
CONFIG_PB_PRIMARY_KEY_INDEX=3
```

## 烧录与启动证据

2026-09-08 使用上述独立 Profile 配置完成一次全新 PB Runtime 构建：

- CMake 明确加载 `profiles/LP-003/pb-runtime.sdkconfig.defaults`
- `CONFIG_PB_PANEL_PROFILE="LP-003"`
- `CONFIG_PB_PRIMARY_KEY_INDEX=3`
- `pb_runtime.bin` 大小为 `0xe2190` bytes
- 1 MiB 应用分区剩余 12%

本次仅验证构建，没有烧录，也没有改写现有设备配置。

2026-09-08 随后完成 PB HAL / Runtime Core 组件边界重构后的增量构建：

- ESP-IDF 识别独立组件 `pb-hal` 与 `pb-runtime`
- App `main` 不再直接编译 View、Action、Timer、Overlay 和事件队列
- `pb_runtime.bin` 大小为 `0xe24c0` bytes
- 1 MiB 应用分区仍剩余 12%
- 本轮同样未烧录设备

2026-09-07 已完成 PB Runtime 全量构建和 COM5 烧录，写入校验通过。
启动日志确认：

```text
[LP003]: Konka SDC251 ready (CT1668 compatibility mode)
PB Runtime ready: serial=PB01-0001 profile=LP-003 cached_revision=11
```

设备随后连入 Wi-Fi，并成功读取 Cloud State，日志包含 `app=one revision=11`。
验证记录不保存 Wi-Fi 密码、Device Token 或其他 Secret。

## 已验证矩阵

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| Driver Registry 选择 LP-003 | PASS | Runtime 启动日志 |
| 4 位七段显示映射 | PASS | CT1668 段位扫描记录 |
| 中间冒号 | PASS | `0xC2 bit7` 实测 |
| 7 个单键 | PASS | 主机测试和实机按键记录 |
| 绿灯控制 | PASS | GPIO33 实机测试 |
| Runtime 构建 / 烧录 / 启动 | PASS | ESP-IDF v6.0.2 实机日志 |
| Profile 独立构建配置 | PASS | 2026-09-08 全新构建与生成的 sdkconfig |
| Cloud State 拉取 | PASS | `app=one revision=11` 日志 |
| `OK` 作为 PRIMARY_ACTION | NOT RUN | 本 Profile 已固定为物理按键 3，待实机验收 |
| hello-panel 完整流程 | NOT RUN | 待实机验收 |
| factory-test 完整流程 | NOT RUN | 待实机验收 |
| 断网 Local First 流程 | NOT RUN | Runtime Core 重构阶段验收 |

## 未完成项

- 补充 `photos/lp003-panel.jpg` 与 `photos/lp003-pinout.png`。
- 只按 `pinout.md` 断开并重新接线，升级 Pinout 为 `verified`。
- 跑通 LP-003 的 `hello-panel` 和 `factory-test`。
- 在 Runtime Core / HAL 拆分后重新执行本矩阵。
