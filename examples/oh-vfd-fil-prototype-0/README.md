# OH-VFD-FIL Prototype 0

用 ESP32 MCPWM 驱动 DRV8837，验证低压双极性灯丝供电。默认经典 ESP32，
使用 ESP-IDF 5.3+ 的 `driver/mcpwm_prelude.h` 与 `esp_driver_mcpwm` 组件。
不使用软件 delay、定时中断或 FreeRTOS 任务翻转 GPIO。

构建验证：2026-09-16，ESP-IDF 6.0.2 / `esp32`，`idf.py build` 通过，
已生成固件。随后通过 COM3 烧录至 ESP32-D0WD-V3（revision v3.1），
写入哈希校验通过，复位后串口确认 `20 kHz filament AC started`，
约 6 秒日志采集中未见错误或重复重启；波形和带载台架测量仍待完成。
开发板实际 Flash 为 4 MB，当前固件按默认 2 MB 使用，启动时有容量提示，
不影响本测试固件运行。

## 接线与电源

| 来源 | DRV8837 |
| --- | --- |
| ESP32 3V3 | VCC、nSLEEP（模块可能标 SLEEP） |
| ESP32 GPIO18 | IN1 |
| ESP32 GPIO19 | IN2 |
| ESP32 GND、实验电源负极 | GND，共地 |
| 实验电源正极 | VM |
| 灯丝两端 | OUT1、OUT2 |

VM 保持 **1.00 V，限流 100 mA**。VCC 与 VM 是独立供电，勿短接。
确认模块在 VCC-GND 和 VM-GND 各有靠近芯片的 0.1 µF 陶瓷去耦电容。
先不接灯丝验证波形；接负载或改线前关闭 VM 输出。

## 构建、烧录

在已激活 ESP-IDF 的 PowerShell 终端，从仓库根目录执行：

```powershell
cd examples/oh-vfd-fil-prototype-0
idf.py set-target esp32
idf.py build
# COM5 仅作示例，替换成开发板的实际串口；烧录时先关闭 VM。
idf.py -p COM5 flash monitor
```

串口应输出 `20 kHz filament AC started`。用 Ctrl+] 退出 monitor。
确认 GPIO 波形后开启 VM。此程序启动后持续输出；停止实验时关闭 VM。
nSLEEP 直接接 3V3，软件没有独立关断驱动器的控制线；初始化的双 LOW
不保证整个上电、复位和烧录过程的引脚状态。

## 时序和预期测量

同一 timer、operator、comparator 控制两个 generator，1 MHz 计时、50 ticks 周期，
25 ticks 比较点，两路使用相反的事件动作。所有驱动 API 均检查返回值。

| 每周期时间 | GPIO18 / IN1 | GPIO19 / IN2 | OUT1−OUT2（理想） |
| --- | --- | --- | --- |
| 0–25 µs | HIGH | LOW | +VM |
| 25–50 µs | LOW | HIGH | −VM |

完整周期 50 µs = **20 kHz，即每秒 20,000 个完整周期**；每 25 µs 换向，
所以每秒换向 40,000 次。两路 GPIO 都是约 0–3.3 V、50% 占空比。
双极性电压指 **OUT1 相对 OUT2 的差分电压**，单个 OUT 对地并不输出负压。

VM=1.00 V 时，理想差分波形约 ±1.00 V、2.00 Vpp、1.00 Vrms、平均值 0 V。
这是方波，不能使用正弦波的峰值/√2 换算。导通压降、切换瞬态、供电压降
会影响实测值。100 mA 是实验电源限流设置，进入恒流模式时 VM 可能跌落。

## 台架验收（待实测）

1. 不接灯丝，先检查 GPIO18/19：周期约 50 µs，高、低各约 25 µs，边沿同步且反相。
2. 开启 VM，测量 OUT1、OUT2：普通共地示波器两根地夹都接系统 GND，
   探头分别接 OUT1、OUT2，用 CH1−CH2 数学功能看差分。**勿把地夹接到任一 OUT**，
   否则会把桥输出短接到地。也可使用合适的差分探头。
3. 确认差分波形正负对称，记录频率、正负平台电压、占空比、平均值和 VM 实测值。
   普通万用表 AC 档未必支持 20 kHz 方波，不能作为唯一验收依据。
4. 关闭 VM 后接灯丝；重新开启，保持 1.00 V / 100 mA，记录同样数据及电流、
   是否进入恒流模式，观察至少一分钟是否掉波或 ESP32 复位。

该工程验证波形发生器；是否适合具体 VFD 灯丝，还要对照灯丝额定电压、电流
以及带载测量。当前没有灯丝型号和实测数据，不能把编译通过等同于灯丝供电验证通过。

## 参考

- [Espressif MCPWM 文档](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32/api-reference/peripherals/mcpwm.html)
- [TI DRV8837 数据手册](https://www.ti.com/lit/ds/symlink/drv8837.pdf)：VM/VCC 范围、IN1/IN2 真值表和去耦要求。
