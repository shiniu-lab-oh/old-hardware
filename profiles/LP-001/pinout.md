# LP-001 Pinout

## Status

第一版已根据原始 `front_panel.c` 实验代码整理。

LP-001 已经成功连接 ESP32 并实现显示、LED 和按键控制。

正式硬件文档发布前，仍建议逐项复核实物连接。

## Pinout

| Pin | Signal | Direction | Voltage | ESP32 GPIO | Notes |
| --- | --- | --- | --- | --- | --- |
| TODO | D1 | Output | 3.3V logic | GPIO25 | Digit select 1, active low |
| TODO | D2 | Output | 3.3V logic | GPIO26 | Digit select 2, active low |
| TODO | D3 | Output | 3.3V logic | GPIO32 | Digit select 3, active low |
| TODO | D4 | Output | 3.3V logic | GPIO33 | Auxiliary indicator bank select |
| TODO | CLK | Output | 3.3V logic | GPIO22 | 74HC164 clock |
| TODO | DATA | Output | 3.3V logic | GPIO21 | 74HC164 serial data |
| TODO | LOCK | Output | 3.3V logic | GPIO19 | Green LED control, active high |
| TODO | K0 | Input | 3.3V logic | GPIO34 | Key scan input; ESP32 input-only GPIO |
| TODO | IR | Input | 3.3V logic | GPIO35 | IR receiver input; SDK v0.1 disabled |
| TODO | VCC | Power | 3.3V | 3V3 | Panel supply |
| TODO | GND | Ground | 0V | GND | Common ground |

## Display

- Display module: `SM310361K-0`
- Type: 3-digit seven-segment display
- Common: common anode
- Digit select: `D1`, `D2`, `D3`, active low
- Auxiliary bank: `D4`
- Segment driver: `74HC164`
- Segment outputs are active low

## Segment Mapping

| 74HC164 Output | Segment |
| --- | --- |
| Q0 | Decimal point |
| Q1 | D |
| Q2 | E |
| Q3 | A |
| Q4 | C |
| Q5 | G |
| Q6 | F |
| Q7 | B |

## Keys

| SDK Key | Original Label | 74HC164 Output |
| --- | --- | --- |
| `OLD_PANEL_KEY_1` | MENU | Q2 |
| `OLD_PANEL_KEY_2` | CH- | Q4 |
| `OLD_PANEL_KEY_3` | CH+ | Q7 |
| `OLD_PANEL_KEY_4` | VOL- | Q6 |
| `OLD_PANEL_KEY_5` | VOL+ | Q1 |
| `OLD_PANEL_KEY_6` | OK | Q3 |

## LEDs

| SDK LED Index | Color | Control |
| --- | --- | --- |
| 0 | Red | Hard-wired power LED, not software-controllable |
| 1 | Green | Controlled by `LOCK`, active high |
