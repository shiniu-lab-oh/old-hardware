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
X-PB-Firmware: pb-runtime/0.2.0
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

- `view` 是可持久化的持续状态。
- `timer` 可省略；`enabled: false` 会停止本地 Timer 并恢复 View。
- `overlay` 可省略，只在新 revision 到达时播放，不写入 Last Known View。
- Runtime 只在 revision 增加时渲染并写入 NVS；相同 revision 去重，较低
  revision 作为过期响应忽略。

Timer 运行在设备本地。网络断开不会暂停或重置倒计时。v0.2 不持久化运行中的
Timer，设备重启后会回到 Last Known View，等待云端状态。

## 通用动作

```json
{
  "event_id": "550e8400-e29b-41d4-a716-446655440000",
  "occurred_at": 1788541200,
  "type": "action",
  "action": "primary"
}
```

```json
{
  "event_id": "550e8400-e29b-41d4-a716-446655440001",
  "occurred_at": 1788541210,
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
- `occurred_at` 是可选的 Unix 秒时间戳。设备尚未完成校时时可以省略，Cloud
  此时使用接收时间。
- Runtime 在发送前把事件写入 NVS FIFO。断网或请求失败时保留事件，恢复连接
  后按原顺序补发；只有收到成功响应后才删除。
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
