# PB App Protocol v2

PB App Protocol v2 在 View 与 Action 之外增加通用 Local Timer 和瞬时
Overlay。PB Runtime 不解释 App ID、显示数值或动作的业务含义。

公开 C 类型定义位于
`sdk/pb-app-protocol/include/pb_app_protocol.h`，版本宏为
`PB_APP_PROTOCOL_VERSION`。

## 获取设备状态

```http
GET /api/pb/v1/devices/{serial}/state
Authorization: Bearer {device_token}
X-PB-Firmware: pb-runtime/0.3.0
```

```json
{
  "revision": 18,
  "app": "example.app",
  "view": {
    "value": 1,
    "leading_zeroes": true,
    "brightness": 100,
    "blink": false,
    "leds": [false]
  },
  "timer": {
    "enabled": true,
    "default_seconds": 1500,
    "presets_seconds": [1500, 3000, 5400]
  },
  "overlay": {
    "value": 666,
    "duration_ms": 2400,
    "blink": true
  }
}
```

- `app`、`revision`、`view` 和 `timer` 共同构成 Runtime 的最小 App Binding 快照。
- `timer` 可省略；v2 State 是完整快照，因此省略或 `enabled: false` 都表示禁用。
- `overlay` 可省略，只在新 revision 到达时播放，不写入 Last Known View。
- Runtime 只在 revision 增加时采纳并写入 NVS；相同 revision 的相同内容去重，
  相同 revision 的冲突内容拒绝，较低 revision 作为过期响应忽略。
- v2 revision 必须是 `0..9007199254740991` 范围内的 JSON 整数，以避免 IEEE-754
  数字精度导致错误排序。
- Cloud 切换 `app` 时必须同时增加设备 revision。Runtime 采纳新 App 后取消旧 App
  的本地 Timer、瞬时 Overlay 和尚未完成的按键手势。

Timer 运行在设备本地。网络断开不会暂停或重置倒计时。Runtime 0.3 持久化 Timer
配置但不持久化运行状态；设备重启后以 ready 状态恢复 Binding，可再次从本地启动。

## 通用动作

```json
{
  "event_id": "550e8400-e29b-41d4-a716-446655440000",
  "occurred_at": 1788541200,
  "app": "example.app",
  "state_revision": 18,
  "type": "action",
  "action": "primary"
}
```

```json
{
  "event_id": "550e8400-e29b-41d4-a716-446655440001",
  "occurred_at": 1788541210,
  "app": "example.app",
  "state_revision": 18,
  "type": "action",
  "action": "primary_long"
}
```

`primary` 是兼容 v1 的短按动作，`primary_long` 是长按动作。当前 App 负责决定
它们是否改变业务状态。

## Timer 事件

```json
{
  "event_id": "550e8400-e29b-41d4-a716-446655440002",
  "occurred_at": 1788541220,
  "app": "example.app",
  "state_revision": 18,
  "type": "timer",
  "event": "started",
  "duration_seconds": 1500,
  "remaining_seconds": 1500
}
```

`event` 可为 `started`、`paused`、`resumed` 或 `finished`。这些名字只描述
通用 Timer 生命周期，云端 App 决定如何记录和解释事件。

- `event_id` 是每次物理事件生成的 UUID。设备重试时必须保持原值；Cloud 以
  `(device, event_id)` 幂等处理。
- `app` 与 `state_revision` 是事件产生时捕获的 Binding 上下文。Cloud 必须按捕获的
  App 分发离线事件，不能在重连后按设备当前 App 重新解释。旧 Runtime 可以省略这两个
  字段，此时 Cloud 使用当前 App 的兼容路由。
- `occurred_at` 是可选的 Unix 秒时间戳。设备尚未完成校时时可以省略，Cloud
  此时使用接收时间。
- Runtime 在发送前把事件写入 NVS FIFO。断网或请求失败时保留事件，恢复连接
  后按原顺序补发；只有收到成功响应后才删除。
- 持久事件队列按 `Cloud Base URL + Device Serial` 隔离。Device Token 轮换不会改变
  队列归属；Cloud 地址或设备序列号变化后，Runtime 只加载新身份自己的队列，不会
  把旧身份的离线事件发送到新端点。
- 从旧版全局队列升级时，Runtime 仅在已有 Binding 或 Last Known View 能证明队列属于
  当前身份时迁移；无法证明归属的旧事件写入持久化隔离标记，后续启动也不自动投递。
- 队列最多保存 24 条事件。队列已满时 Runtime 明确记录错误，不覆盖旧事件。

首次处理和幂等重试使用相同的成功响应：

```json
{"ok":true,"revision":18}
```

## Runtime 系统 Overlay

Runtime 自身还可以显示不会进入协议状态与 NVS 的系统 Overlay：

- `888`：启动自检；
- `404`：已联网后发生网络断开。

Overlay 到期后恢复当前 Timer View 或 App View。代码值本身没有业务状态含义。

## 凭据

设备凭据只应写入被 Git 忽略的 `apps/pb-runtime/sdkconfig.secrets`。公开仓库不得
提交 Wi-Fi 密码、Device Token 或云端 Secret。
