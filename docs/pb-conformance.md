# PB Conformance

> 本文定义相同 Runtime Core 在不同硬件上的最小验收流程。`NOT RUN` 是显式状态，
> 不能按 `PASS` 处理。

## 固定测试 App

测试使用一个不包含 ONE 语义的公开 App Binding：

1. 安装后显示数字 `7`。
2. `PRIMARY_ACTION` 启动 90 秒 Timer。
3. 再次触发可暂停和恢复。
4. 显示高优先级短时 Overlay。
5. Overlay 到期后显示最新 Timer，而不是旧快照。
6. Timer 完成后发出生命周期事件并回到基础 View。

第二个测试 Binding 只上报 `PRIMARY_ACTION`，测试服务返回新的数字 View。两个 Binding
来回切换时不得重新烧录设备，也不得把旧 Binding 的离线事件交给新 Binding。

## 设备矩阵

| 能力 | LP-001 | LP-003 | 第二种 VFD |
| --- | --- | --- | --- |
| Profile 与接线文档 | PASS | PARTIAL | NOT RUN |
| Driver Registry | PASS | PASS | NOT RUN |
| 数字 View | PASS | PASS | NOT RUN |
| Duration View | PASS | PARTIAL | NOT RUN |
| 可控 LED | PASS | PASS | NOT RUN |
| 全部物理键 | PASS | PASS | NOT RUN |
| Profile PRIMARY | PASS (KEY_6 / OK) | NOT RUN (KEY_3 / OK) | NOT RUN |
| Runtime + Cloud State | PASS | PASS | NOT RUN |
| 固定测试 App 全流程 | NOT RUN | NOT RUN | NOT RUN |
| 断网继续交互 | NOT RUN | NOT RUN | NOT RUN |
| 离线重启恢复 Binding | NOT RUN | NOT RUN | NOT RUN |
| 运行中断电不伪造完成 | NOT RUN | NOT RUN | NOT RUN |
| 离线事件按原 Binding 补发 | NOT RUN | NOT RUN | NOT RUN |
| Cloud / Device 身份切换不串发事件 | NOT RUN | NOT RUN | NOT RUN |

Runtime 0.3 已实现上述离线重启与事件归属所需的软件路径，但在完成断网、断电和 App
切换实机流程前，矩阵状态继续保持 `NOT RUN`。

LP-003 的详细证据见 `profiles/LP-003/validation.md`。第二种 VFD 在芯片、接线和能力完成
逆向前保持 `NOT RUN`，不预设其显示和输入能力。

## 通过标准

跨硬件验证通过必须同时满足：

- 使用同一 Runtime Core 源码和同一测试 App 定义。
- 允许不同 Profile 配置和不同固件产物，不允许复制 Runtime 业务分支。
- Required Capability 不满足时明确拒绝，Optional Capability 降级可观察。
- 网络不可用时，本地输入和 Timer 不被 HTTP 超时阻塞。
- 重启、切换 Binding 和事件补发均不改变历史事件的原始上下文。

两块相同 ESP32 只证明跨面板与跨 Driver，不证明跨 MCU。当前计划不把跨 MCU 作为
PB Runtime 0.3 的退出条件。
