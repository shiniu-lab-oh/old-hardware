# LP-001 Pinout

## Pin 编号规则

所有引脚编号均以 **PCB 元件面（正面）** 观察。

对于 LP-001，J1 最靠近电源 / 状态指示灯一侧的触点定义为 **Pin 1**，然后沿连接器向按键一侧依次编号至 **Pin 11**。

参考图片：

`photos/lp001-pinout.png`

---

## 引脚定义

| Pin | Signal | Reference ESP32 GPIO | Direction | 说明 |
|---:|---|---:|---|---|
| 1 | 3.3V | 3V3 | 电源 | 前面板 3.3V 供电 |
| 2 | LOCK | GPIO19 | 主控 → 面板 | 绿色状态指示灯控制 |
| 3 | DATA | GPIO21 | 主控 → 面板 | 74HC164 串行数据 |
| 4 | CLK | GPIO22 | 主控 → 面板 | 74HC164 移位时钟 |
| 5 | D1 | GPIO25 | 主控 → 面板 | 第 1 位数码管位选 |
| 6 | D2 | GPIO26 | 主控 → 面板 | 第 2 位数码管位选 |
| 7 | KD | GPIO34 | 面板 → 主控 | 按键检测输入 |
| 8 | D3 | GPIO32 | 主控 → 面板 | 第 3 位数码管位选 |
| 9 | IR | GPIO35 | 面板 → 主控 | 红外接收信号；当前 SDK 暂未启用 |
| 10 | D4 | GPIO33 | 主控 → 面板 | 辅助显示 / 指示灯 Bank 选择 |
| 11 | GND | GND | 电源 | 公共地 |

---

## 参考接线

LP-001 当前参考 ESP32 接线：

```text
LP-001 J1        ESP32
-----------------------
Pin 1  3.3V  ->  3V3
Pin 2  LOCK  ->  GPIO19
Pin 3  DATA  ->  GPIO21
Pin 4  CLK   ->  GPIO22
Pin 5  D1    ->  GPIO25
Pin 6  D2    ->  GPIO26
Pin 7  KD    ->  GPIO34
Pin 8  D3    ->  GPIO32
Pin 9  IR    ->  GPIO35
Pin 10 D4    ->  GPIO33
Pin 11 GND   ->  GND
```

---

## 电气说明

- 当前验证方案使用 **3.3V** 为前面板供电。
- ESP32 GPIO 逻辑电平为 **3.3V**。
- 不要向 ESP32 GPIO 直接输入 5V 信号。
- `GPIO34` 和 `GPIO35` 在经典 ESP32 上为输入专用 GPIO，分别用于 `KD` 和 `IR`。
- `IR` 硬件信号存在，但当前 Old Panel SDK 中暂未启用红外功能。
- 红色 LED 为电源指示灯，不由 SDK 控制。
- 绿色状态 LED 通过 `LOCK / GPIO19` 控制。

---

## 当前验证状态

| 项目 | 状态 |
|---|---|
| J1 Pin 1 ～ Pin 11 映射 | ✅ 已完成 |
| 通用 30Pin ESP32 接线 | ✅ 已运行 |
| `hello-panel` | ✅ PASS |
| `factory-test` | ✅ PASS |
| 按本文档完全重新接线复核 | ✅ 已完成 |

---

## 备注

LP-001 为「老硬件重启计划」中第一个接入 Old Panel SDK 的前面板 Hardware Profile。

Pinout 文档以实机接线和已经成功运行的参考 ESP32 配置为依据。若后续硬件复核发现差异，应以实测结果更新本文档，并同步更新 `profile.yaml`。
