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

公开测试服务位于
[`tools/pb-conformance-server`](../tools/pb-conformance-server/README.md)。它提供固定的
Timer / Action Binding、一次性 Overlay、stale/conflict State、接口故障开关和 Event
幂等记录。测试数据仅保存在内存，不依赖私有 Cloud 实现。

## 设备矩阵

| 能力 | LP-001 | LP-003 | 第二种 VFD |
| --- | --- | --- | --- |
| Profile 与接线文档 | PASS | PARTIAL | NOT RUN |
| Driver Registry | PASS | PASS | NOT RUN |
| 数字 View | PASS | PASS | NOT RUN |
| Duration View | PASS | PARTIAL | NOT RUN |
| 可控 LED | PASS | PASS | NOT RUN |
| 全部物理键 | PASS | PASS | NOT RUN |
| Profile PRIMARY | PASS (KEY_6 / OK) | PASS (KEY_3 / OK) | NOT RUN |
| Runtime + Cloud State | PASS | PASS | NOT RUN |
| 固定测试 App 全流程 | NOT RUN | PASS | NOT RUN |
| 断网继续交互 | NOT RUN | PARTIAL (Event API) | NOT RUN |
| 离线重启恢复 Binding | NOT RUN | PASS (State API 故障注入) | NOT RUN |
| 运行中断电不伪造完成 | NOT RUN | NOT RUN | NOT RUN |
| 离线事件按原 Binding 补发 | NOT RUN | PASS | NOT RUN |
| Cloud / Device 身份切换不串发事件 | NOT RUN | NOT RUN | NOT RUN |

Runtime 0.3 已实现上述离线重启与事件归属所需的软件路径。矩阵只将已有实机证据的
项目标为 `PASS` 或 `PARTIAL`，真正断网、断电和身份切换等未执行项目继续保持
`NOT RUN`。

2026-09-08，LP-003 使用公开 Mock Cloud 完成第一轮实机验证：PRIMARY 输入、Timer
生命周期、Overlay、stale/conflict State 拒绝、Binding 切换和 Event API 故障后的 FIFO
补发均通过。用户确认看到 `666` 闪烁并恢复 Timer View。State/Event API 关闭后软重启，
Runtime 从 NVS 恢复 Timer Binding revision 5；用户确认面板稳定显示 `0007`，服务端事件
数未变化。完成 Core 组件拆分后再次烧录相同固件，用户确认 Action Binding 显示
`0008`，PRIMARY 后更新为 `0009`。固定测试 App 流程标为 `PASS`；真正的 Wi-Fi 断开
和运行中断电仍按独立 Local First 项记录，不计入该结论。

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
