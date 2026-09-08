# LP-001 验证记录

> 本文记录 LP-001 在真实硬件上的可复现实机证据。未执行的项目不会按通过处理。

## PB Runtime Conformance

2026-09-08 使用公开 PB Conformance Mock Cloud 完成测试：

- Hardware Profile：`LP-001`
- 串口：COM5
- PB Runtime commit：`8b1d040`
- 固件大小：`0xe37f0` bytes
- 显示能力：3 位七段显示
- PRIMARY：KEY_6 / 原面板 `OK`
- 测试 Device：`PB-CONFORMANCE-001`

### 启动与 State

- LP-001 定向构建通过，生成配置包含 `CONFIG_PB_PANEL_PROFILE="LP-001"` 和
  `CONFIG_PB_PRIMARY_KEY_INDEX=6`
- bootloader、partition table 和 App 烧录到 COM5，三个镜像均通过 Hash 校验
- Runtime 报告 3 位显示、6 个物理按键和 1 个可控 LED
- 用户确认启动时短暂显示 `888`，随后稳定显示 Timer Binding 的 `007`

### Timer 与 Overlay

- 短按 KEY_6 / OK 后启动 90 秒 Timer，Runtime 记录 `started 90/90`
- 用户确认本地显示切换到倒计时；再次短按暂停并闪烁，再次短按恢复
- started、paused、resumed 和自然结束的 finished 事件均成功提交，队列回到 0 pending
- Timer 运行中接收 revision 10 的 `666` 闪烁 Overlay，用户确认 Overlay 到期后恢复
  Timer View

### 离线事件补发

- Mock Cloud Event API 关闭后，started 与 paused 仍正常改变本地显示
- 两个事件按产生顺序保存在 NVS FIFO，Runtime 日志显示 2 pending，HTTP 503 重试期间
  未删除队首事件
- Event API 恢复后按 started、paused 顺序补发，两条事件均保留 revision 10
- 队列回到 0 pending，服务端事件数从 6 增至 8，重复事件数为 0
- 第二轮在 Timer revision 13 产生 started 后保持 Event API 关闭，并切换到 Action
  revision 14；Timer 自然结束产生的 finished 与 started 一同留在队列
- 恢复 Event API 后，两条历史事件仍按 started、finished 顺序补发，payload 均保持
  `pb.conformance.timer@13`；当前 Binding 保持 `pb.conformance.action@14`
- 服务端事件数增至 11，重复事件数仍为 0

### Binding 与 Revision

- 不重烧固件切换至 `pb.conformance.action` revision 11，用户确认显示 `008`
- 短按 PRIMARY 后 Action Event 成功送达，服务端生成 revision 12，用户确认显示 `009`
- 注入 stale revision 11，Runtime 记录 `Ignoring stale state` 并保持 revision 12
- 注入同 revision conflict，Runtime 返回 `ESP_ERR_INVALID_RESPONSE` 并保持 `009`

### NVS 恢复

- Mock Cloud State API 关闭时执行 ESP32 软件复位
- Runtime 从 NVS 加载 0 个待发事件和 `pb.conformance.action` revision 12
- 用户确认面板先显示 `888`，随后在 State API 持续返回 503 时恢复为 `009`
- 测试期间服务端事件数保持 9、重复数保持 0；State API 恢复后正常拉取 revision 12

### 测试隔离说明

LP-001 与 LP-003 若同时使用 `PB-CONFORMANCE-001`，会被 Mock Cloud 视为同一 Device，
任一设备都可能先消费 `stale_once` 或 `conflict_once`。本轮第一次 stale 注入被仍在线的
LP-003 消费；拔掉 LP-003、只保留 COM5 后重新执行，LP-001 明确拒绝 stale State。
同时测试多台设备时必须为每台固件配置唯一 Device Serial。

## 当前未执行

- 真正断开 Wi-Fi 后的本地交互
- 运行中物理断电及恢复
- Device Serial 切换后的事件隔离
