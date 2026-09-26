# BOE VFM041SSBR1-S1 VFD Support Package v0.1

Manufacturer：BOE；PCB 丝印型号：**VFM041SSBR1-S1**（用户确认）。
Driver：PT6312BLQ；Display：4 digit + colon VFD。
版本：PT6312 Driver v0.1、BOE Profile v0.1、VFD Mapper v0.1。

## 已确认硬件

| J1 Pin | 信号 | 实测连接 |
| --- | --- | --- |
| 1 | +5V | PT6312 Pin14、Pin38，均蜂鸣导通 |
| 2 | GND | 与 J1-6 蜂鸣导通 |
| 3 | CLK | PT6312 Pin8 |
| 4 | STB | PT6312 Pin9 |
| 5 | DIN | PT6312 Pin6 |
| 6 | GND | PT6312 Pin7 |

用户实测：外部输入 **5.0 V**、初始电流约 **97 mA**；Pin14/38 VDD 约
**4.9 V**，Pin27 VEE 约 **−20 V**；灯丝可见微弱发光。
以上为既有台架数据，不是本次重测，也不是满亮持续运行的电流/温升结论。
原板已具备 VFD 负压和灯丝供电，本支持包不外接独立高压或灯丝驱动，不控制电源。

## 已验证的 ESP32 接法

| 信号 | 现有路径 |
| --- | --- |
| CLK | GPIO18 → Level Converter LV1 → HV1 → J1-3 |
| DIN | GPIO23 → Level Converter LV2 → HV2 → J1-5 |
| STB | GPIO19 → J1-4，当前直接连接 |
| GND | ESP32、VFD、转换模块、实验电源全部共地 |
| 电源 | J1-1 使用外部 5 V；转换模块 HV 使用同一 5 V，LV 接 ESP32 3V3 |

本接法已由用户完成 ALL ON 验证；此次封装沿用，不更改通信路径。
GPIO 宏位于工程 `components/pt6312/include/pt6312.h`。

## 软件边界

```text
Application / Demo / SELF_TEST
             ↓
        VFD Renderer
             ↓ 使用实测映射
 BOE VFM041SSBR1-S1 Profile
             ↓ 经 Renderer 写 RAM
        PT6312 Driver
             ↓
           Hardware

ALL_ON / Mapper → PT6312 Driver（原始 RAM 诊断工具）
```

- PT6312：GPIO、bit-bang、命令、RAM、亮度、显示开关和原初始化。
  不包含数字字体、玻璃映射或 Mapper 循环。
- Profile：Digit1..4 的 A..G、整体冒号、完整 8 字节可见位掩码；零位保留 UNUSED 语义。
- Renderer：逻辑 0..9 字体，通过 Profile 组成完整 22 字节 RAM 图像。
  单 digit 更新保留其它 digit 和冒号；所有未用 RAM 位维持零。
- 工具：原 `pt6312_all_on()` 函数体原样移到 `main/pt6312_tools.c`，
  交互 Mapper 保留。旧无人交互 Mapper 循环移除，避免两份扫描实现。

数据来源：[独立实测映射表](boe_vfm041ssbr1_s1_map.md)，其中包括全部 64 状态与 UNUSED。
API position 0..3 对应用户实测 Digit1..4，最终左右顺序以 1234 目视验收为准。
一个冒号 bit 控制整体冒号，未建立独立上下点映射。

## 构建与验证状态

| 项目 | 状态 / 证据 |
| --- | --- |
| Hardware identification / Power | 用户已确认型号、接口及上述电压电流 |
| Protocol / ALL ON | 用户已确认四位数字和冒号稳定全亮 |
| Mapper | 用户已完成 64 状态实测；原始数据保留 |
| Driver extraction | GPIO、bit-bang、初始化函数及 ALL ON 函数体迁移时逐字核对一致 |
| Profile | 已逐项核对原始 64 状态，与代码段位和 visible mask 一致 |
| Build | 本次 ESP-IDF v6.0.2 / esp32 实际 `idf.py build` 成功，退出码 0 |
| Renderer / Demo / SELF_TEST | 已实现并编译，待实机验收 |
| 本次固件烧录 | 本次未执行；此前版本已烧录与实测 |
| 30 分钟稳定性 | 仅提供测试模式，未执行或宣称通过 |

驱动迁移基线：原文件从开头到 `pt6312_all_on()` 之前的文本 SHA256 为
`2f1b82701d24d4b17edd33398127adcdf6f4ced66b214c72fff8a9e4b6314390`；
新 `components/pt6312/pt6312.c` 与该部分一致（按 UTF-8 / LF 比较）。

逻辑分析仪参考：Renderer 写完整 RAM，每次 `[40] [C0 + 22 bytes] [88|brightness]`。
前三组有效 RAM00..07 的预期图像如下，RAM08..15H 均为零：

| 画面 | RAM00..07 |
| --- | --- |
| 1234 | `06 00 5B 00 4F 00 66 00` |
| 8888 | `7F 00 7F 00 7F 00 7F 00` |
| 12:34 | `06 00 5B 00 CF 00 66 00` |

## HARDWARE VERIFICATION CHECKLIST

- [x] PCB 型号和 J1 六针接口已确认（用户记录）。
- [x] 5 V、VDD、VEE、灯丝工作已确认（用户记录）。
- [x] 既有 ALL ON 与 64 状态 Mapper 实测完成（用户记录）。
- [ ] 烧录本次支持包；默认 12:34 正确，无重复重启。
- [ ] `f` 回归原 ALL ON；`m`、`n/p/r/c/a/s` 回归 Mapper 和清屏。
- [ ] `1` 显示 1234，核对四位从左至右顺序和所有数字笔画。
- [ ] `2` 显示 8888，四位 A～G 完整，冒号关闭。
- [ ] `3` 显示 12:34，冒号位置正确，无多余符号。
- [ ] 分别调用 `vfd_set_digit(0..3, digit)`，确认只改变目标 digit，保留其它 digit 和冒号。
- [ ] `t` 完整检查 CLEAR / 0000 / 1111 / 1234 / 5678 / 8888 / 00:00 / 12:34 / 23:59。
- [ ] SELF_TEST 的 Colon OFF / ON 只改变冒号；亮度 0..7 变化正常。
- [ ] `s` 可中止 SELF_TEST / AUTO 并保持当前帧，切换工具不残留旧 RAM 位。
- [ ] `b` 运行满亮 8888 + 冒号至少 30 分钟，记录电流、温度、闪烁、复位情况。

人工稳定性记录：

| 项目 | 实测填写 |
| --- | --- |
| 日期 / 环境温度 | 待填写 |
| 开始时间 / 结束时间 | 待填写 |
| 初始 / 30 分钟电流 | 待填写 |
| 初始 / 30 分钟温度与测点 | 待填写 |
| 显示 / 闪烁 / 复位 / 异常 | 待填写 |
| 验收结论与人员 | 待填写 |

完成 1234、8888、12:34 实机验收后，方可将 Renderer 标记 verified。
30 分钟稳定性结论需独立填写，不能由编译或 SELF_TEST 日志代替。
