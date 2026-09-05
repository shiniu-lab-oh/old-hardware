# PB App Protocol v1

> 本文档记录已冻结的 v1 协议。新实现请使用 `pb-app-protocol-v2.md`。

PB Runtime 通过本协议渲染与具体应用无关的 View，并上报与具体应用无关的
Action。设备只把 App ID 当作不透明标识；由 PB Cloud 决定当前 App、生成
View，并解释 Action 的业务含义。

公开 C 类型定义位于
`sdk/pb-app-protocol/include/pb_app_protocol.h`，协议版本宏为
`PB_APP_PROTOCOL_VERSION`。

## 设备状态

```http
GET /api/pb/v1/devices/{serial}/state
Authorization: Bearer {device_token}
X-PB-Firmware: pb-runtime/0.1.0
```

```json
{
  "revision": 7,
  "app": "example.counter",
  "view": {
    "value": 42,
    "leading_zeroes": true,
    "brightness": 100,
    "blink": false,
    "leds": [false]
  }
}
```

- `app`：1 至 32 字节的不透明 App ID。PB Runtime 不解释其中的业务语义。
- `revision`：设备 View 的非负、单调递增版本号。
- `view.value`：面板显示的整数。
- `leading_zeroes`：是否显示前导零。
- `brightness`：亮度百分比，范围为 0 至 100。
- `blink`：是否闪烁。
- `leds`：LED 状态数组，最多包含 4 项。

首次联网且没有本地缓存时，Runtime 接受 revision `0`。已有本地状态后，只有
更高的 revision 会触发渲染和 NVS 写入；相同 revision 被去重，更低的
revision 被视为过期响应并忽略。

具体 App 负责把自己的领域状态映射为 PB View。

## 设备动作

```http
POST /api/pb/v1/devices/{serial}/events
Authorization: Bearer {device_token}
Content-Type: application/json
```

```json
{
  "type": "action",
  "action": "primary"
}
```

```json
{
  "ok": true,
  "revision": 8
}
```

`primary` 表示与应用无关的主要操作意图。当前 App 决定该 Action 是否改变
状态，以及随后返回什么 View。

## Runtime 配置

进入 `apps/pb-runtime`，将 `sdkconfig.secrets.example` 复制为
`sdkconfig.secrets`，填写 Wi-Fi、PB Cloud URL、设备序列号和 Device Token：

```powershell
idf.py build
idf.py -p COM4 flash monitor
```

`sdkconfig.secrets` 和生成的 `sdkconfig` 文件已被 Git 忽略。生产凭据、Wi-Fi
密码和 Device Token 不得提交到公开仓库。
