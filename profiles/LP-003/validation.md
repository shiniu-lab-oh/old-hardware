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

2026-09-08 完成 Network Worker 接入后的 LP-003 增量构建：

- State 拉取与 Event 提交已从 Core 本地循环迁入独立 Worker
- Event 以不可变副本发送，Core 按 `event_id` 确认后再更新持久队列
- `pb_runtime.bin` 大小为 `0xe2890` bytes
- 1 MiB 应用分区仍剩余 12%
- 本轮仅验证构建，没有烧录设备

2026-09-08 完成 Runtime 0.3 最小 App Binding 持久化后的增量构建：

- NVS 快照包含 App ID、revision、View 与 Timer 配置
- 重启只恢复 ready Timer 配置，不恢复运行中的倒计时
- App 切换会取消旧 Timer、Overlay 与未完成输入手势
- 新事件保存产生时的 App ID 与 State revision；v0.2 队列可无损迁移
- LP-001 与 LP-003 使用同一 Runtime 源码构建通过
- `pb_runtime.bin` 大小为 `0xe3200` bytes
- 1 MiB 应用分区剩余 11%
- 本轮仅验证构建，没有烧录设备

2026-09-08 完成 Local First 持久事件队列身份隔离后的增量构建：

- 事件队列按 Cloud URL 与 Device Serial 分区，Device Token 可独立轮换
- 旧版全局队列仅在现有 Binding 或 Last Known View 能证明来源时迁移
- 来源不明的旧事件持久隔离，不因后续 Binding 更新而被错误认领
- LP-001 与 LP-003 使用同一 Runtime 源码构建通过
- `pb_runtime.bin` 大小为 `0xe3620` bytes
- 1 MiB 应用分区剩余 11%
- 本轮仅验证构建，没有烧录设备

2026-09-07 已完成 PB Runtime 全量构建和 COM5 烧录，写入校验通过。
启动日志确认：

```text
[LP003]: Konka SDC251 ready (CT1668 compatibility mode)
PB Runtime ready: serial=PB01-0001 profile=LP-003 cached_revision=11
```

设备随后连入 Wi-Fi，并成功读取 Cloud State，日志包含 `app=one revision=11`。
验证记录不保存 Wi-Fi 密码、Device Token 或其他 Secret。

2026-09-08 使用公开 PB Conformance Mock Cloud 完成第一轮 Runtime 0.3 实机流程：

- 在 COM3 写入 LP-003 Conformance 固件，Bootloader、分区表和 App 均通过 Hash 校验
- Runtime 识别 `LP-003`、7 个物理键、1 个可控 LED，并成功获取测试 State
- `OK`（物理按键 3）产生通用 PRIMARY 输入，Timer 的 started、paused、resumed、
  finished 事件均成功入队、提交和确认
- 用户确认看到 `666` 闪烁 Overlay，并在约 2.4 秒后恢复 Timer View
- stale revision 被忽略；同 revision、不同 View 的冲突 State 被拒绝
- Timer / Action Binding 在不重新烧录固件的情况下完成切换
- Event API 返回 503 时，本地 Timer 继续运行；started 与 paused 保存在 NVS FIFO，
  接口恢复后按原顺序补发并回到 0 pending
- 补发事件保留 `pb.conformance.timer` 和产生时 revision 5，Mock Cloud 未发现重复事件
- Action Binding 已在协议、Runtime 日志和物理显示层完成 `0008 -> 0009` 更新
- State 与 Event API 同时返回 503 时软重启设备，Runtime 从 NVS 恢复
  `pb.conformance.timer` revision 5，待发事件为 0
- 用户确认重启后面板稳定显示 `0007`，没有闪烁或继续倒计时；Mock Cloud 事件数保持
  13，未产生伪造的 Timer 生命周期事件
- 恢复 State 与 Event API 后，Runtime 继续拉取 revision 5，事件数和重复数均未变化
- 本轮未执行真正的 Wi-Fi 断开、设备断电或 Device Serial 切换

2026-09-08 完成 Core 组装边界拆分后的第二轮实机回归：

- 通用单写入者循环迁入 `sdk/pb-runtime`，App `main.c` 只保留 ESP-IDF 平台初始化和
  Transport 回调装配
- Runtime 版本字符串由 `pb_runtime.h` 统一导出，HTTP Worker 通过通用 Transport
  Result 与 Core 通讯
- LP-001 与 LP-003 使用独立 Profile 构建目录编译通过；两份固件均为 `0xe37f0`
  bytes，1 MiB 应用分区剩余 11%
- LP-003 固件在 COM3 重新烧录成功，三个镜像均通过 Hash 校验
- 启动后恢复 `pb.conformance.timer` revision 6，PRIMARY 生成 `started 90/90`，事件
  成功提交并从 1 pending 回到 0
- 切换到 `pb.conformance.action` 不需要重烧；用户确认面板先显示 `0008`，PRIMARY 后
  显示 `0009`

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
| `OK` 作为 PRIMARY_ACTION | PASS | 物理按键 3 已触发 Timer 与 Action 事件 |
| `666` Overlay 后恢复 Timer | PASS | 用户视觉确认，Runtime revision 2 日志 |
| Event API 故障后 FIFO 补发 | PASS | 2 pending 按 started、paused 顺序清空 |
| stale / conflict State 拒绝 | PASS | Runtime 串口日志 |
| Timer / Action Binding 热切换 | PASS | 用户确认同一固件显示 `0008 -> 0009` |
| State API 故障时重启恢复 Binding | PASS | NVS 恢复 revision 5，用户确认稳定显示 `0007` |
| 重启不伪造 Timer 事件 | PASS | 重启前后 Mock Cloud 事件数保持 13 |
| hello-panel 完整流程 | NOT RUN | 待实机验收 |
| factory-test 完整流程 | NOT RUN | 待实机验收 |
| 断网 Local First 流程 | PARTIAL | Event API 故障通过，真正 Wi-Fi 断开待测 |

## 未完成项

- 补充 `photos/lp003-panel.jpg` 与 `photos/lp003-pinout.png`。
- 只按 `pinout.md` 断开并重新接线，升级 Pinout 为 `verified`。
- 跑通 LP-003 的 `hello-panel` 和 `factory-test`。
- 使用 LP-001 运行相同的 PB Conformance 实机流程。
