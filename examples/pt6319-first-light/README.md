# PT6319LQ VFD Interactive Mapper v0.1

沿用当前已由用户成功点亮的 First Light 工程，目录和固件名保持 `pt6319-first-light`。
上电执行原 `pt6319_init()`，清屏、打印帮助、显示 State 0，然后等待人工输入。
不自动扫描，不读按键/DOUT，不包含字体、图标命名、Profile 或电源控制。

## Golden Baseline

GPIO18 CLK、GPIO19 STB、GPIO23 DAT；10 µs 延时、LSB first、CLK 上升沿采样、
mode 0x02、12 字节 RAM、初始化命令、亮度 7 均保持原配置。
三路信号保持已验证的 BSS138 接线，DAT 仍为只写 OUTPUT。
用户确认目前已建立灯丝阴极参考并正常点亮；本版本不改变外部电气条件。

原 `pt6319.c` 的所有既有函数与修改前文本一致。
因为原 RAM 写函数是 static，只在文件末尾新增 `pt6319_write_frame()`：
检查初始化后调用原 `pt6319_write_ram(0, ram, PT6319_RAM_BYTES)`。
头文件增加该声明及 `PT6319_RAM_BYTES=12`，原 `PT6319_RAM_LENGTH` 改为它的别名，值不变。
`f` 直接调用原 `pt6319_all_on()`，未另写一套 FULL。

原 `PT6319_TEST_MODE_ALL_ON` 宏保留以免改动驱动原有断言；现在不选择启动模式。
当前 main 固定启动交互 Mapper，全亮使用 `f`，清屏使用 `c`。

## 范围与单 bit 保证

`PT6319_RAM_BYTES=12`，`VFD_MAPPER_STATES=(PT6319_RAM_BYTES*8)`。
扫描 RAM **0x00..0x0B**，共 **96 状态（0..95）**：

```text
addr = state / 8
bit = state % 8
value = 1 << bit
```

每次先调用原 clear 清零完整 12 字节窗口，再将一个全零 12 字节数组的唯一目标 bit
置 1，通过原 RAM 写入方式一次写完。这也避免跨地址后退时，串行写入过程中
旧 bit 尚未清除而新 bit 已置位。只操作已验证的 12 字节窗口，不扩大范围。
每状态不发送亮度调整命令。ALL ON 是明确的多 bit 诊断模式；回到 Mapper 时先清屏。

以 State 37 为例，12 字节图像为：

```text
00 00 00 00 20 00 00 00 00 00 00 00
```

发送帧为 `[40] [C0 + 12×00] [40] [C0 + 单 bit 的12字节帧]`。
每个方括号独占一次 STB LOW；RAM 帧内不切换 STB。

## 串口操作

沿用 console UART0 / 115200，使用 UART 接收接口避免 stdin 行缓冲。
单字符命令不用回车；回车、换行和空白被忽略。`g` 命令例外，须回车提交。

| 命令 | 行为 |
| --- | --- |
| n | 下一状态，95 → 0 |
| p | 上一状态，0 → 95 |
| r | 回到 State 0 |
| c | 清屏，保留 Mapper 游标 |
| f | 原 ALL ON，保留 Mapper 游标 |
| a | 从 State 0 / AUTO PASS 1 开始自动扫描 |
| s | 停止自动，保持当前显示 |
| ? | 帮助，不改变显示或扫描状态 |
| g 37 + Enter | 跳到 State 37；只支持十进制 state，不支持 g addr bit |

`n/p/r/c/f/g` 均停止自动模式，收到 `g` 时就停止，不等回车。
`c/f` 后可用 `r` 回到 0，或用 `n/p` 从保留的游标继续。

GOTO 使用有界缓冲区；支持 Backspace/Delete 修正、Esc 取消。
负数、超范围、空参数、非数字、额外参数或过长输入均拒绝，保留当前画面。
输入 g 后进入参数输入状态，直到回车或 Esc；其间其它字符被当作参数。
如取消参数输入后想单步，先 Esc，再按 n/p。

每次状态显示同时打印可读区块和独立机器可读行：

```text
========================================
PT6319 VFD MAPPER
State : 37 / 95
Addr  : 0x04
Bit   : 5
Value : 0x20
Command: n=next  p=prev  c=clear  f=full
========================================
MAP,state=37,addr=0x04,bit=5,value=0x20
```

## 自动调度

在 `main/vfd_mapper.h` 修改：

```c
#define VFD_MAPPER_AUTO_INTERVAL_MS 1500
#define VFD_MAPPER_AUTO_PAUSE_MS 2000
```

State0..94 各保持约 1500 ms；State95 正常保持后额外暂停 2000 ms，仍保持该 bit，
然后从 0 开始下一轮，打印 `===== MAPPER AUTO PASS N =====`。
输入优先于自动推进，等待期间不阻塞串口；只有短暂 RAM 发送和日志耗时。
使用同一个 app_main 循环，不创建额外任务，不在调度延迟后快速补扫。

## 记录模板

[docs/pt6319_vfd_map.csv](../../docs/pt6319_vfd_map.csv) 已生成 96 行：

```text
State,Address,Bit,Value,PhysicalElement,Notes
0,0x00,0,0x01,,
...
95,0x0B,7,0x80,,
```

PhysicalElement、Notes 均为空，由人工实测填写。没有可见显示的状态也要保留，
测试后可在 PhysicalElement 填 UNUSED；软件不猜测 Digit、Segment 或 Icon 名称。

## 文件与构建

新增 `main/vfd_mapper.c/.h` 和 CSV 模板；修改 `main/main.c`、`main/CMakeLists.txt`、
`main/pt6319.c/.h`、本 README 和仓库 README。

已激活 ESP-IDF 的 PowerShell：

```powershell
cd D:\Code\shiniu-lab\old-hardware\examples\pt6319-first-light
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

COM3 按实际串口替换。Ctrl+] 退出 Monitor。本机未激活环境时先运行：

```powershell
# 仅在当前终端执行策略阻止脚本时使用：
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1"
```

验证记录（2026-09-27）：实际运行 ESP-IDF v6.0.2 / esp32 的 `idf.py build` 成功，
退出码 0。驱动原有部分文本核对一致；CSV 的 96 行地址/bit/value、空白观察字段已检查。
本次 Mapper 未烧录；交互和可见效果仍待实机确认，不能用编译结果代替。

建议上板检查：启动停在 0；p 到 95、n 回 0；g 37 对应 RAM04 bit5；
c 清屏、f 回归全亮、r 回到单 bit；a 自动、s 停止、n/p/g 抢占自动；
完整走完 0..95，逐项填写 CSV。异常先检查 Mapper 的 state、清屏和写帧逻辑，
不优先改动已经工作的通信参数。
