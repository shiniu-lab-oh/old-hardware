# CT1668 / TM1668 compatibility test

CT1668 compatibility assumption：本工程暂按 TM1668 命令、RAM 和时序实现，
不代表 CT1668 已确认完全兼容。康佳 SDC251 的段码和按键映射以本板实测记录为准。

## 接线

| 来源 | 面板排线 |
| --- | --- |
| ESP32 GPIO25 | pin 1 STB（板上经过约 100Ω） |
| ESP32 GPIO26 | pin 3 CLK |
| ESP32 GPIO27 | pin 4 DIO |
| ESP32 GPIO32（输入） | pin 5 IR_OUT（待动态验证） |
| ESP32 GPIO33（输出） | pin 6 GREEN_LED_CTRL，高电平亮 |
| ESP32 GND | pin 7 GND |
| 独立可调电源 +3.3V | pin 8 VDD |
| 独立可调电源 GND | pin 2 GND |

pin 2 与 pin 7 已实测属于同一个 GND 网络。用户已连接 GPIO33→pin6、GPIO32←pin5。
GPIO32 仅输入，无上下拉和中断，不做红外解码。红灯按用户确认作为 POWER 指示，
由面板供电，不提供软件开关。
面板由独立电源供电，ESP32 与面板共地。无红外、Wi-Fi 或蓝牙业务代码。

## 编译和运行

指定开发和测试版本为 `D:\esp\v6.0.2\esp-idf`。
普通 PowerShell 中先加载本机安装器生成的环境脚本（每个新终端执行一次）：

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1"
$env:ESP_IDF_VERSION = "6.0.2"
idf.py --version
cd D:\Code\shiniu-lab\old-hardware\examples\ct1668-test
```

版本检查应显示 ESP-IDF v6.0.2。显式设置完整版本号，以供组件管理器使用。
若出现“禁止运行脚本”，可先执行 `Set-ExecutionPolicy -Scope Process Bypass`，
然后重新加载脚本；该设置仅作用于当前 PowerShell 会话。
加载环境后，在本目录执行：

```sh
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

将 `<PORT>` 替换为实际端口，例如 `COM5`。用 Ctrl+] 退出 monitor。
工程对 main 组件全部 C 文件启用 `-Wall -Wextra -Werror`。
已使用上述 ESP-IDF 6.0.2 路径完成 ESP32 全量构建，日志无 warning/error。
已烧录至 COM5。2026-09-07 修正接线后，芯片端采样解码出完整初始化：
`00`、`40`、`C0` 加 14 个 `00`、`88`；用户确认诊断循环能点亮四个 8
及中间冒号，小数点未亮。四位七段及冒号映射随后已完成实测，
7 个物理按钮的单键读取也已实测确认。用户进一步确认绿灯由 pin6 高电平控制，
红灯作为常亮 POWER 指示；小数点未亮原因仍待验证，
不代表 CT1668 与 TM1668 完全兼容。
当前默认使用物理按键测试，保持独立 3.3V 供电和最低亮度；
测试方法和映射进度见 [按键接管记录](KEY_MAPPING.md)。
此前自动扫描版已构建、烧录，并通过 COM5 日志验证：112 个地址/bit/value
顺序全部正确，扫描完成后清屏，5 秒后从 C0 bit0 重新开始。

此次有效采样 `qqq.sr` 的探头接法：CH1/D0 = CLK（芯片 pin2），
CH2/D1 = DIO（芯片 pin1），CH3/D2 = STB（芯片 pin3），GND = pin22。
分析仪通道编号与芯片引脚编号不是一一相同。

## 当前按键测试模式

`ENABLE_KEY_TEST=1`，每 20ms 读 5 字节，各按钮独立消抖，记录原始值和变化位。
7 个物理按钮映射均已实测确认，直接按按钮即可输出 `[BUTTON] MENU DOWN/UP` 等事件。
串口 `1`～`7` 仍可加实验标签，但不参与自动识别；按 `r` 打印快照。
读键版已构建、烧录并读到稳定原始值 `00 00 00 00 00`；
MENU 已由用户确认对应 `raw[1] bit4`（0x10），按下置位、松开清零，
已观察到 4 次可重复变化；EXIT 已确认对应 `raw[1] bit3`（0x08），
按下置位、松开清零，2 次可重复。OK 已确认对应 `raw[1] bit0`（0x01），
截图中 4 个置位/恢复循环一致。VOL-/VOL+/CH-/CH+ 分别对应
`raw[0]` 的 bit1/bit0/bit3/bit4，各有两次按下/松开确认，均为高有效。
`sdc251_keys.h/.c` 提供独立于 GPIO 的解码、消抖及 held/down/up 事件接口。
2026-09-07，命名事件版本已通过 7 组主机测试和 ESP-IDF 6.0.2 无警告构建，
烧录 COM5 并完成写入校验。运行日志确认 `raw=00 00 00 00 00`、
`[BUTTON] held=0x00 raw_valid=yes`；用户随后确认七个实体按键均测试通过。
详见 [按键接管记录](KEY_MAPPING.md)。

## GPIO33 绿灯验证

`main/sdc251_panel.h/.c` 封装独立 GPIO：初始化时 GPIO33 输出 LOW，绿灯熄灭；
GPIO32 只设为输入。GPIO33 采用推挽，HIGH 点亮、LOW 熄灭；GPIO 复位期间仍可能
因面板已有电路而微亮。CT1668 的最低亮度只用于数码管，不调节独立绿灯。

当前 `ENABLE_KEY_TEST=1`，数码管保持空白；以下为验证程序的演示按键用途：

| 操作 | 绿灯动作 |
| --- | --- |
| 物理 MENU / 串口小写 o | 点亮 |
| 物理 EXIT / 串口小写 f | 熄灭 |
| 物理 OK / 串口小写 t | 切换 |

按住物理按钮不连发，UART 无需回车；同次收到多个 DOWN 时，EXIT 优先于 MENU、OK。
其余四键仍报告原始值和命名事件。串口数字 1～7 依旧只是实验标签。
日志 `[GREEN_LED] ON GPIO33=1 pad=1` / `OFF GPIO33=0 pad=0` 包含 ESP32 数字电平回读，
回读不是光学点亮确认。观察绿灯响应，并确认红灯保持原有常亮状态。

可复用 API：`sdc251_panel_init()`、`sdc251_green_led_set(bool)`、
`sdc251_green_led_toggle()`、`sdc251_green_led_get(bool *)`。
get 返回上次成功写入的命令状态；单调用者使用，不在 ISR 或多个任务中并发调用。
绿灯驱动不写 CT1668 RAM。此前未烧录的 a/b/c 路径诊断已退出当前程序，
历史测量过程保留于 [状态 LED 接管记录](LED_MAPPING.md)。

本次 GPIO33 版本已用指定 ESP-IDF 6.0.2 完成无警告构建，应用 169216 字节；
已通过 IDF 环境中的 esptool 烧录 COM5，写入哈希校验通过。
串口实机测试 OFF→ON→toggle OFF→toggle ON→OFF 全部通过，GPIO33 pad 回读均一致，
七键空闲读数仍为全零。测试结束已设为 OFF 并释放串口。
新固件的物理按键与绿灯可见联动仍需用户现场观察；GPIO 回读不代替光学观察。
日志：`build/green-led-build.log`、`build/green-led-flash.log`、`build/green-led-runtime.log`。

## 可选手动扫描模式

需要手动扫段时，设 `ENABLE_KEY_TEST=0`、`ENABLE_MANUAL_SCAN=1`、`ENABLE_HARDWARE_DIAG=0`。
初始化先清空 14 字节再最低亮度开屏，随后直接停在 **C0 bit0**。
不再执行自动 A/B，也不会自动切换或循环，方便观察和记录。
在 `idf.py -p COM5 monitor` 中按以下小写键，立即生效，无需回车：

| 按键 | 动作 |
| --- | --- |
| `n` 或空格 | 下一个 bit |
| `p` | 上一个 bit |
| `r` | 重新点亮并打印当前 bit |
| `0` | 回到 C0 bit0 |
| `c` | 清屏，保留当前选择；按 `r` 恢复 |
| `h` 或 `?` | 打印帮助 |

每步打印 `[SCAN] addr=... bit=... value=...` 和 `[MANUAL] step=.../112 HOLD`。
到 CD bit7 后继续前进仍停在最后一项；回到首项使用 `0`。
回车/换行被忽略。RAM 只在收到操作时改写，UART 等待期间不自动前进。
控制通过 ESP32 现有 USB 串口进行，无需额外接线，手动扫段模式不读面板实体按键。
手动版已使用 ESP-IDF 6.0.2 无警告构建并烧录到 COM5，写入校验通过；
串口按键交互的实机验证尚未完成。
将 `ENABLE_MANUAL_SCAN` 设为 0 可恢复自动 A/B/C；诊断模式优先于手动模式。

用户已观察到自动扫描时四位依次出现亮段，各位从上横段开始；
已进一步实测确认 C0/C2/C4/C6 的 bit0 分别为第 1/2/3/4 位上横段，
C2 bit7 为中间冒号。完整进度见 [段位实测记录](SEGMENT_MAPPING.md)。
四位 C0/C2/C4/C6 的 bit0～bit6 均已实测为上横、右上、右下、下横、左下、左上、中横。
C0/C4/C6 bit7 均无可见变化，C2 bit7 是冒号。
其他地址各 bit 也均无可见变化：本轮 112 个 bit 中 29 个有可见响应，83 个无可见变化。
四位七段及冒号映射已确认，小数点和状态 LED 的控制方式仍待确定。

## 可选诊断模式

需要以下诊断时，先设 `ENABLE_KEY_TEST=0`，再设 `ENABLE_HARDWARE_DIAG=1`。
每轮重新初始化（先清 RAM 再开屏），清屏 2 秒、最低亮度全 FF 5 秒，
然后清屏并执行四个各 8 秒的电平测量阶段。完整循环约 39 秒，
可避免独立供电面板错过一次性的开屏命令。设为 0 后由 `ENABLE_MANUAL_SCAN`
选择手动或自动 A/B/C。
键盘读取仍关闭，亮度仍为 0。

万用表置直流电压档，黑表笔接面板 GND（pin 2 或 7），
红表笔分别测 CT1668 的 STB pin3、CLK pin2、DIO pin1。
根据串口 `[DIAG] HOLD` 确认阶段。预期 1 约为 3.3V、0 约为 0V。
以下是预期值，必须用仪表验证芯片端：

| 8 秒阶段 | STB pin3 | CLK pin2 | DIO pin1 |
| --- | --- | --- | --- |
| expected 1 1 1 | 3.3V | 3.3V | 3.3V |
| expected 1 1 0 | 3.3V | 3.3V | 0V |
| expected 1 0 1 | 3.3V | 0V | 3.3V |
| expected 0 1 1 | 0V | 3.3V | 3.3V |

`ESP32 pad readback` 仅是 ESP32 引脚本地数字回读，不能证明信号到达面板。
如芯片端异常，再测对应的 ESP32 引脚及排线端以定位中断位置。
STB 低电平测量阶段不产生 CLK 边沿，其他阶段保持 STB 高，
按 TM1668 协议假设不会写入额外数据。

同时测 CT1668 pin6 对 pin22 的供电，记录电源电流和是否进入限流状态。
保持独立电源 3.3V；仅凭两颗 LED 常亮不能认定显示驱动已正常工作。
本测试没有调整硬件电压。

有逻辑分析仪时，在 STB 下降沿触发，LSB first、CLK 上升沿采样，
应看到四帧：`00`、`40`、`C0` 加 14 个 `00`、`88`。
RAM 帧共 120 个 CLK 上升沿（地址 1 字节加数据 14 字节）。
全 FF 阶段是 `40`、`C0` 加 14 个 `FF`、`88`。

## 测试流程

初始化：GPIO 输出；STB/CLK/DIO 依次置高；等待 100ms；发送 `00`；
发送 `40`；同一帧发送 `C0` 和 14 个零；最后发送 `88`。
日志由 `ESP_LOGI/W/E` 输出，TAG 均为 `CT1668`，会带 IDF 默认前缀。

1. A：清空 14 字节，保持 2 秒。
2. B：最低亮度，14 字节全 FF，保持 5 秒。
3. C：C0 bit0 到 CD bit7，共 112 个状态。每次实际清空整个 RAM 后，
   写入仅一个 bit 为 1 的完整 RAM，每个状态保持约 500ms。
4. 清屏，打印完成，等待 5 秒，再执行 C。A/B 不重复。

用以下格式记录观察结果，不会亮的 bit 也记录为“未观察到变化”：

| RAM 地址 | bit | value | 观察到的位/段/冒号/小数点/状态 LED |
| --- | --- | --- | --- |
| C0 | 0 | 01 | |
| C0 | 1 | 02 | |

全部 RAM 置 FF 只是诊断模式；未使用位可能无效果，状态 LED 是否接入
显示 RAM 仍待实测。GPIO 调用成功不意味着芯片应答或协议已验证。

## 时序和接口

使用 `esp_rom_delay_us(5)`，LSB first。写入在 CLK 上升沿采样，
STB 低电平包住整帧。调度可能延长间隔，适合低速示波器验证。
驱动单调用者使用，无锁；不从 ISR 或多个任务并发调用。
`ct1668_write_byte()` 是不管理 STB 的底层接口。
`ct1668_write_ram()` 调用者必须提供至少 14 字节，只发送 14 字节。
亮度范围 0..7，超出钳位为 7；改变亮度保留开关状态。
`ct1668_all_segments_on()` 只填充 RAM，保留当前亮度和开关状态。

键盘测试目前开启，使用独立的 `ct1668_key_test.c` 持续轮询与消抖。
`42` 后保持 STB 低，DIO 从输出切换输入并启用内部上拉，等待 5us；
按 TM1668 假设，在 CLK 下降沿后等待 5us，再升高 CLK 等 5us 后采样。
40 位完成后先升高 STB，等待芯片释放总线，再恢复 DIO 输出。
后续 RAM 写入重新发送 `40`。7 个单键均已观察到可重复读键响应；
组合键及长按连发未做硬件验证，具体进度见按键接管记录。
