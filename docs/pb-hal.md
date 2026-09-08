# PB Hardware HAL

> 状态：目标契约草案。将在 Runtime Core 抽取阶段以 LP-001 与 LP-003 的共同需求实现。

## 目的

PB Hardware HAL 是 Runtime Core 与 Old Panel SDK 之间唯一的设备呈现边界。它让 Core
只理解“显示一个 Presentation”和“收到一个 Logical Control”，而不理解具体面板。

## 输入模型

HAL 输出的每个输入事件至少包含：

```c
typedef struct {
    pb_control_t control;
    bool pressed;
    uint64_t sampled_at_ms;
} pb_input_event_t;
```

约束：

- `sampled_at_ms` 来自单调时钟，并尽量接近 Driver 稳定采样时刻。
- HAL 不输出 `old_panel_key_t`。
- Profile Adapter 负责把 `KEY_1..KEY_n` 映射为 `pb_control_t`。
- 长按判断属于 Runtime Core，Driver 和 HAL 不生成业务 Action。

## 呈现模型

第一阶段支持：

```c
typedef enum {
    PB_PRESENTATION_NUMBER,
    PB_PRESENTATION_DURATION,
    PB_PRESENTATION_SYSTEM_OVERLAY,
} pb_presentation_type_t;
```

Presentation 还可携带亮度、LED 状态和可选格式提示。数字、时长和 Overlay 保留语义，
具体显示为 `7`、`007`、`1:30` 或设备码由 Renderer 决定。

## Render Result

```c
typedef enum {
    PB_RENDER_APPLIED,
    PB_RENDER_DEGRADED,
    PB_RENDER_UNSUPPORTED,
    PB_RENDER_BUSY,
    PB_RENDER_ERROR,
} pb_render_result_t;
```

`APPLIED` 只表示命令成功提交给硬件接口；没有光学反馈的面板不能声称已观察到物理显示。

## Capability

HAL Capability 至少描述：

- 支持的 Presentation 类型
- 可显示数字范围与固定宽度行为
- Duration 可用精度与分隔符能力
- Logical Control 集合
- 可控 LED 数量
- 亮度级别或连续范围
- 最小刷新间隔
- Profile ID / Profile 版本

静态物理数量与 Runtime 实际支持数量必须分开。例如 LP-003 有红、绿两灯，但 HAL
只暴露一个可控 LED。

## 依赖方向

```text
PB Runtime Core -> PB HAL interface
Old Panel Adapter -> PB HAL interface + Old Panel SDK
Old Panel SDK -> LP Driver
```

禁止反向依赖，也禁止 HAL 接口包含 `old_panel.h`、GPIO 或 CT1668 / 74HC164 类型。

## 第一阶段退出条件

- Runtime Core 的源文件不再包含 `old_panel.h`。
- LP-001 与 LP-003 通过同一个 HAL 接口运行。
- Profile 决定物理输入映射；Core 不再读取 `CONFIG_PB_PRIMARY_KEY_INDEX`。
- Renderer 对不支持和降级返回明确结果，不再静默截断。
