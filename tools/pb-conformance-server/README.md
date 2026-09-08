# PB Conformance Mock Cloud

这是 PB App Protocol v2 的公开、确定性测试服务。它只使用 Python 标准库，不依赖
Laravel ERP，也不包含 ONE、COUNTDOWN 或其他产品业务逻辑。

运行环境要求 Python 3.10 或更高版本。

## 能力

- 提供 PB Runtime 使用的 State 与 Event API。
- 固定 Timer Binding：显示 `007`，本地 Timer 为 90 秒。
- 固定 Action Binding：显示 `008`，每次 `primary` 后数字增加。
- 生成一次性 Overlay、stale State 和同 revision 冲突 State。
- 独立关闭 State 或 Event 接口，测试重试与离线事件队列。
- 按 `event_id` 幂等处理，并验证事件产生时的 App / revision 上下文。
- 不同 Device Serial 使用完全独立的内存状态。

服务重启后测试数据会清空，不写入数据库。

## 启动

只在本机测试：

```powershell
python tools/pb-conformance-server/server.py
```

允许同一局域网的 ESP32 访问：

```powershell
python tools/pb-conformance-server/server.py `
  --host 0.0.0.0 `
  --port 8080 `
  --token pb-conformance-local
```

查出电脑的局域网 IPv4 地址后，在未提交的
`apps/pb-runtime/sdkconfig.secrets` 中填写：

```text
CONFIG_PB_CLOUD_BASE_URL="http://192.168.1.10:8080"
CONFIG_PB_DEVICE_SERIAL="PB-CONFORMANCE-001"
CONFIG_PB_DEVICE_TOKEN="pb-conformance-local"
```

`192.168.1.10` 只是示例，必须替换为运行 Mock Cloud 的电脑地址。Windows 防火墙需要
允许 Python 的 TCP 8080 入站连接。

## 控制测试状态

以下 PowerShell 示例假设服务、命令行和 Runtime 使用相同测试 Token：

```powershell
$base = "http://127.0.0.1:8080"
$serial = "PB-CONFORMANCE-001"
$headers = @{ Authorization = "Bearer pb-conformance-local" }

function Send-PbCommand($command) {
    Invoke-RestMethod `
        -Method Post `
        -Uri "$base/conformance/v1/devices/$serial/commands" `
        -Headers $headers `
        -ContentType "application/json" `
        -Body (@{ command = $command } | ConvertTo-Json)
}
```

可用命令：

| 命令 | 行为 |
| --- | --- |
| `reset` | 清空事件记录，并以更高 revision 恢复 Timer Binding |
| `activate_timer` | 切换到显示 `007` 的 90 秒 Timer Binding |
| `activate_action` | 切换到显示 `008` 的 Action Binding |
| `overlay` | 在当前 Binding 上发布一次 `666` 闪烁 Overlay |
| `stale_once` | 下一次 State GET 返回较低 revision |
| `conflict_once` | 下一次 State GET 返回同 revision、不同 View |
| `state_delivery_off` / `state_delivery_on` | 关闭或恢复 State API |
| `event_delivery_off` / `event_delivery_on` | 关闭或恢复 Event API |

示例：

```powershell
Send-PbCommand "overlay"
Send-PbCommand "activate_action"
Send-PbCommand "event_delivery_off"
Send-PbCommand "event_delivery_on"
```

查看当前 State 和已接收事件：

```powershell
Invoke-RestMethod `
  -Uri "$base/conformance/v1/devices/$serial" `
  -Headers $headers | ConvertTo-Json -Depth 10
```

## 建议验收顺序

1. 启动服务并烧录指向它的 PB Runtime，确认面板显示 `007`。
2. 短按 PRIMARY，确认显示约 2 分钟并开始本地倒计时。
3. 再次短按暂停，确认显示闪烁；再次短按恢复。
4. Timer 运行时执行 `overlay`，确认先显示 `666`，随后恢复 Timer View。
5. 执行 `event_delivery_off`，继续暂停/恢复；恢复接口后确认事件按原顺序补发。
6. 离线期间执行 `activate_action`，恢复 State 接口后确认切换到 `008`。
7. 在 Action Binding 下短按 PRIMARY，确认 Event 成功后 View 变为 `009`。
8. 分别执行 `stale_once` 和 `conflict_once`，确认 Runtime 拒绝异常 State。

真正的 Wi-Fi 断开、断电恢复与 Device Serial 切换仍需按
[`docs/pb-conformance.md`](../../docs/pb-conformance.md) 做实机记录。

## 测试

```powershell
python -m unittest discover -s tools/pb-conformance-server/tests -v
```
