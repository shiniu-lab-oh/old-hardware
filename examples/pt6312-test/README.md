# BOE VFM041SSBR1-S1 / PT6312B VFD Support Package v0.1

基于用户已验证的 ALL ON 与 64 状态 Mapper，封装当前 BOE 四位数字 + 冒号 VFD。
PT6312 Driver v0.1、BOE Profile v0.1、VFD Mapper v0.1；默认启动显示静态 **12:34**。
Renderer 已编译，实际数字显示与稳定性仍待人工验收。

## 最短使用步骤

1. 外部 **5 V → J1-1**。
2. 公共 **GND → J1-2、J1-6**；ESP32、转换模块、电源均共地。
3. **GPIO18 → LV1/HV1 → J1-3 CLK**；**GPIO23 → LV2/HV2 → J1-5 DIN**；
   **GPIO19 → J1-4 STB**。转换模块 LV 接 3V3、HV 接同一外部 5 V。
   沿用已经验证的接线和板载电源，不更改 GPIO 或时序。
4. 激活 ESP-IDF，编译。
5. 烧录、打开 Monitor。
6. 默认显示 **12:34**；串口按 `3` 可随时恢复该画面，无需回车。

当前机器 PowerShell：

```powershell
# 仅当未签名脚本被当前 PowerShell 策略阻止时使用，作用域限当前终端。
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1"
cd D:\Code\shiniu-lab\old-hardware\examples\pt6312-test
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

其它机器使用自己的 ESP-IDF 环境；目标为经典 ESP32。COM3 如有变化请替换。
Ctrl+] 退出 Monitor。不会连接 Wi-Fi，也不会计时或同步时钟；12:34 是静态测试帧。

## 模式和串口按键

| 按键 | 功能 |
| --- | --- |
| 1 | Renderer 显示 1234，冒号 OFF |
| 2 | Renderer 显示 8888，冒号 OFF |
| 3 | Renderer 显示 12:34 |
| f | ALL_ON：原始 8 字节 FF 基线（原函数体未改） |
| m | MAPPER：帮助、State 0、等待输入 |
| n / p | Mapper 下一位 / 上一位，63 ↔ 0 循环 |
| r | Mapper 回到 State 0 |
| c | 清空全部 RAM，保留 Mapper 游标 |
| a | Mapper AUTO，从 State 0 / PASS 1 开始 |
| s | 停止自动任务，保持当前显示 |
| t | SELF_TEST，运行一轮后保持最后画面 |
| b | STABILITY：8888 + 冒号 ON + 亮度 7，持续保持 |
| ? | 帮助，不改变显示模式 |

回车与空白忽略。手动显示命令会停止正在运行的 AUTO / SELF_TEST；同一 app_main
处理串口与调度，无并发 RAM 写入。原 Mapper 输出格式 `MAP,state=...,addr=...,bit=...,value=...`
保留。ALL_ON / Mapper 操作裸 RAM；返回 Renderer Demo 时清空软件缓存与硬件 RAM。

启动模式在 `main/main.c` 的 `TEST_MODE` 修改：`PT_TEST_ALL_ON`、`PT_TEST_ALL_OFF`、
`PT_TEST_MAPPER`、`PT_TEST_SELF_TEST`、`PT_TEST_TIME`（默认）、`PT_TEST_STABILITY`。
所有启动模式均保留串口命令。修改后需重新 build / flash。

SELF_TEST 顺序（每步 1500 ms）：

```text
CLEAR → 0000 → 1111 → 1234 → 5678 → 8888
→ 00:00 → 12:34 → 23:59 → Colon OFF → Colon ON
→ Brightness 0 → 1 → ... → 7 → 保持 23:59
```

间隔在 `main/vfd_demo.h` 的 `VFD_SELF_TEST_INTERVAL_MS` 修改。
Mapper 扫描 RAM00..07 的 64 bit，每次先清完整 RAM；自动间隔在
`main/vfd_mapper.h` 的 `VFD_MAPPER_AUTO_INTERVAL_MS`（1000 ms）修改。
State 63 额外保持 `VFD_MAPPER_AUTO_PAUSE_MS`（2000 ms）。所有等待期间可输入命令。

`b` 为人工 30 分钟稳定性实验准备画面，程序不自动判定温升、电流或通过情况。
它会一直保持，直到其它显示命令或复位；`s` 仍保持当前画面，`c` 才清屏。

## 文件与职责

```text
components/
  pt6312/
    pt6312.c                原通用通信和初始化
    include/pt6312.h        GPIO / 时序宏，通用 API
  vfd/
    vfd_renderer.c          0..9 逻辑字体、22 字节缓存、写屏
    include/vfd_renderer.h  上层 API
    include/vfd_profile.h   段位置及 Profile 类型
    profiles/boe_vfm041ssbr1_s1.c/.h  用户实测映射
main/
  main.c                    启动模式、串口命令和单线程调度
  pt6312_tools.c/.h         原 ALL_ON 函数，裸 RAM 诊断
  vfd_mapper.c/.h          原交互 Mapper
  vfd_demo.c/.h            数字、时间、SELF_TEST、稳定性画面
```

底层驱动从原 main 迁移到独立组件，通信/初始化部分文本保持一致。
ALL_ON 函数体原样迁入工具层。移除已被交互 Mapper 替代的旧自动扫描函数，
不在通用芯片层保留玻璃段位、字体或实验扫描策略。项目根目录不变，无 PB 集成。
复用时可将两个 components 目录加入其它 ESP-IDF 项目的 EXTRA_COMPONENT_DIRS。

## Renderer API

包含 `vfd_renderer.h`，应用先调用原 `pt6312_init()`，再使用 Renderer。所有 API
单调用者使用，成功调用立即更新显示；无效参数返回 ESP_ERR_INVALID_ARG 且不改显示。

| API | 行为 |
| --- | --- |
| `vfd_clear()` | 清空缓存与完整 RAM，保持亮度 |
| `vfd_set_digit(position, digit)` | position 0..3 对应实测 Digit1..4，digit 0..9；保留其它位与冒号 |
| `vfd_set_colon(enabled)` | 独立控制整体冒号，保留数字 |
| `vfd_set_brightness(level)` | 0..7，保留 RAM；0 是最低亮度，不是灭屏 |
| `vfd_show_number(value)` | 0..9999，四位补前导零，冒号关闭 |
| `vfd_show_time(hour, minute)` | hour 0..23、minute 0..59，补前导零，冒号打开 |

参数检查 API 返回 esp_err_t，其余返回 void；底层写命令无芯片应答，ESP_OK 仅代表参数
有效且软件完成调用。Renderer 缓存不是硬件回读，混用原始工具后先调用 `vfd_clear()`。

```c
#include "pt6312.h"
#include "vfd_renderer.h"
#include "esp_err.h"

void app_main(void)
{
    pt6312_init();
    vfd_clear();
    ESP_ERROR_CHECK(vfd_show_time(12, 34));
}
```

没有填入猜测映射。测得的 Digit1..4 地址为 00/02/04/06、A..G 为 bit0..6，
整体冒号为 RAM04 bit7。35 个无可见显示位在正式 Map 标为 UNUSED，Renderer 保持其为零。
未扫描 RAM08..15H 也写零，但不声称这些地址已测为 UNUSED。

## 数据、验证与归档

- [硬件档案与人工验收清单](../../docs/boe_vfm041ssbr1_s1.md)
- [正式 64 状态映射表](../../docs/boe_vfm041ssbr1_s1_map.md)
- [原始 Mapper 记录](SEGMENT_MAPPING.md)

本次实际执行 ESP-IDF v6.0.2 / esp32 的 `idf.py build` 成功（退出码 0），
生成 `build/pt6312_test.bin`，152848 bytes。64 状态原始表与 Profile 逐项一致。
通用驱动核心、ALL_ON 函数体迁移时已核对文本一致。本次未烧录新支持包。

用户此前已实测 ALL_ON 与 Mapper。新增 Renderer 的 1234、8888、12:34、
SELF_TEST 和独立位控制仍需实机验收；30 分钟稳定性没有通过结论。
诊断时先按 f 回归基线，再查 Profile/字体/缓存，不优先更改已验证的电源、GPIO 或时序。
