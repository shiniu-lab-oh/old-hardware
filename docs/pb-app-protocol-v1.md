# PB App Protocol v0.1

PB Runtime renders app-independent views and emits app-independent actions. An
app identifier is opaque to the device: only the Cloud implementation decides
which app produces a view or handles an action.

The public C types are defined in
`sdk/pb-app-protocol/include/pb_app_protocol.h`.

## Device state

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

`app` is a non-empty opaque identifier with at most 32 characters. PB Runtime
must not interpret it as a business state. `revision` is a non-negative,
monotonically increasing view version for the device.

`view.value` is the integer shown on the panel. `leading_zeroes`, `brightness`,
`blink`, and `leds` describe presentation only. Apps own the mapping from their
domain state to this view.

## Device action

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

`primary` is an app-independent intent. The active app decides whether it
changes state and what the next view should be.

## Runtime configuration

From `apps/pb-runtime`, copy `sdkconfig.secrets.example` to
`sdkconfig.secrets`, fill in Wi-Fi, PB Cloud URL, device serial, and device
token, then run:

```powershell
idf.py build
idf.py -p COM4 flash monitor
```

`sdkconfig.secrets` and generated `sdkconfig` files are ignored by Git. Never
commit production credentials or device tokens.
