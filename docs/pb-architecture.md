# PB 架构

> 状态：架构方向已确认，正在渐进落地。当前基线为 PB Runtime 0.2 / PB App
> Protocol v2；本文描述下一阶段约束，不表示所有模块已经完成拆分。

## 1. PB 的定义

PB 是面向受限物理设备的 Local First 交互运行时。它把 PB App 提交的呈现意图、
通用动作和生命周期配置映射到不同旧硬件前面板。

当前只承诺以下范围：

- 数字或时长显示
- 物理按键输入
- 可控状态 LED
- 亮度
- 单计时器
- 短时系统 Overlay

当前不承诺电机、任意外设编排、通用脚本或设备端业务插件系统。

## 2. 项目边界

| 路线 | 责任 | 开放边界 |
| --- | --- | --- |
| Old Hardware / Old Panel | Driver、Profile、逆向资料、测试工具 | 公开 |
| PB Runtime / PB App Protocol | 通用设备运行时与公开协议 | 公开 |
| ONE | Task、ACTIVE / COMPLETED、AI、业务规则与 View 生成 | 私有 Cloud App |

PB Runtime 不理解 ONE、COUNTDOWN、GOAL 等 App 的业务状态，也不针对 `app_id`
编写分支。切换 App 不应要求重新烧录 ESP32。

## 3. 目标分层

```text
PB App / test client
        |
        v
Transport (HTTP / local injection)
        |
        v
PB Runtime Core
        |
        v
PB Hardware HAL
        |
        v
Profile Adapter -> Renderer -> Old Panel Driver -> Hardware
```

横向数据：

- PB App Protocol：App、Cloud 与 Runtime 的版本化契约。
- Hardware Profile：能力、物理输入映射、接线、电气信息和默认配置。
- Platform Port：NVS、时间、任务、队列和网络等 ESP-IDF 依赖。

## 4. 责任约束

### PB App

- 保存业务状态并决定业务动作含义。
- 根据业务状态生成 PB View、Timer 和 Overlay 意图。
- 不知道 GPIO、显示芯片、段码或 LP 接线。

### PB App Protocol

- 定义版本化 View、Action、Event、Timer、Overlay 与 Capability 语义。
- Wire format 与设备内 C 表示可以不同，不能把结构体内存布局当作永久协议。
- 协议升级必须显式升版；PB App Protocol v2 不做静默破坏性修改。

### PB Runtime Core

- 接纳和切换 App Binding。
- 维护当前 View、单计时器、Overlay 优先级和本地输入手势。
- 协调持久化、事件队列与 Capability 校验。
- 不依赖 `old_panel_key_t`、GPIO、芯片名或具体 `profile_id`。
- 不包含 ONE-specific 状态、Prompt、业务判断、Secret 或 Device Token。

### Transport

- 负责认证、编码、超时、重试和 Cloud API。
- 阻塞网络调用必须运行在 Worker 中，不能直接阻塞 Core。
- Transport 不直接 Render，也不越过 Core 修改状态。

### PB Hardware HAL

- 接受通用 Presentation，输出通用 Logical Control Event。
- 暴露真实 Capability 与 Render Result。
- 公共接口不出现 Driver 类型、显示芯片和 GPIO。

### Profile Adapter / Renderer / Driver

- Adapter 将 Profile、Renderer 与 Driver 组合，并把物理键映射为 Logical Control。
- Renderer 将 Presentation 降级为具体字符、段码和指示灯；降级必须可观察。
- Driver 只负责总线、刷新、采样和消抖，不实现计时器或 App 行为。

## 5. 最小协议契约

### View

当前 v2 保留数字 `value` 作为兼容能力。后续 Typed View 至少区分：

- `number`：有符号整数及明确的溢出策略。
- `duration`：语义时长与显示精度，不由 Core 永久转换为“向上取整分钟”。

Typed View 属于新协议版本，不能通过改变 v2 `value` 的含义实现。

### Input 与 Action

输入链路固定为：

```text
physical sample -> logical control -> event envelope -> binding action
```

Driver 提供采样时刻；Runtime 以采样时间而不是队列消费时间判断短按和长按。
物理键到 Logical Control 的映射属于 Profile，Logical Control 到 Action 的映射属于
App Binding。v2 的 `primary` / `primary_long` 名称继续兼容。

事件至少需要稳定 `event_id`、Device、App Binding、状态版本、启动会话、单调时间，
以及可选 Unix 时间。离线重试必须保留原始上下文，旧 Binding 的事件不能被新 App
按当前语义重新解释。

### Timer

第一阶段只支持一个计时器：`duration + state + monotonic deadline`。
Timer 是生命周期能力，不只是不断变化的数字 View。按键如何启动、暂停或结束由
Binding 决定，不由 Runtime 写死全局业务规则。

### Overlay

第一阶段只支持一个活动 Overlay。Overlay 到期后根据最新 Timer / App 状态重新
计算呈现，不恢复过期前保存的像素快照。`888`、`404` 等硬件显示码属于 Renderer，
不是 Core 语义。

### Capability 与降级

Capability 是硬件可兑现的契约，至少覆盖显示类型和范围、Logical Controls、LED、
亮度级别、刷新限制、Profile 版本与协议版本。

- Required 能力不满足：拒绝安装或切换 Binding。
- Optional 能力不满足：执行已声明的降级。
- Renderer 结果：`applied`、`degraded`、`unsupported`、`busy` 或 `error`。

## 6. Local First 验收定义

1. 已配置设备断网后仍可输入、运行计时器和显示本地 Overlay。
2. 离线重启可恢复最后有效 Binding、基础 View 与 Timer 配置，并能本地开始新一轮。
3. 首次启动无网络时，可通过本地测试注入安装最小 Binding。
4. 运行中的计时器断电后不伪造完成；重启回到 ready 并保留待同步事件。
5. 未配置 Cloud 凭据不阻塞本地交互。

## 7. 并发与持久化

- Core Task 是运行状态的唯一写入者。
- Network Worker 执行所有阻塞 HTTP，并通过有界消息与 Core 通讯。
- Worker 只处理事件不可变副本，成功后以 `event_id` 回执。
- 业务事件队列容量当前为 24；满时不得覆盖最老事件，并应暴露未同步状态。
- 输入队列溢出必须可诊断，并取消不完整手势或重新同步按键状态，防止丢失 release
  被误判成长按。

## 8. 当前实现差距

截至当前基线：

- `apps/pb-runtime/main/main.c` 仍承担编排、同步、Timer、Overlay 与事件刷新。
- `pb_view`、`pb_overlay`、`pb_actions` 仍直接依赖 Old Panel SDK。
- HTTP State 拉取和 Event 提交仍可能阻塞本地循环。
- `old_panel_key_event_t` 尚无 Driver 采样时间。
- NVS 主要保存 View 与 revision，Binding 和完整 Timer 配置尚未持久化。
- Duration 呈现仍由 Runtime 固定转换为分钟数字。
- 离线事件尚未绑定原始 App Binding 上下文。

这些是后续迭代的输入，不通过在 `main.c` 中继续增加 App 特例解决。

## 9. 渐进实施顺序

1. 固定 LP-003 基线：Profile、可复现配置、构建和实机记录。
2. 抽取最小 `pb-runtime-core`、`pb-hal` 和 Old Panel Adapter。
3. 增加 Network Worker、输入采样时间、Binding 持久化和安全切换。
4. 接入第二种 VFD 硬件并运行相同 Conformance 流程。
5. 根据双硬件证据冻结最小接口。
6. 让 ONE 仅通过公开 PB App Protocol 重新接入并回归。

每一步都必须保持已有 LP-001 / LP-003 能构建；不复制第二套 Runtime 分支，也不创建
ONE-specific ESP32 firmware。
