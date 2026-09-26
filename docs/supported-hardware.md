# Supported Hardware

| ID | Original Device | Display | LEDs | Keys | Status |
| --- | --- | ---: | ---: | ---: | --- |
| LP-001 | 欧视达 ABS-209B | 3 位七段 | 2 / 1 | 6 | SDK verified |
| LP-002 | 待整理 | - | - | - | 待整理 |
| LP-003 | 康佳 SDC251 | 4 位七段 + 冒号 | 2 / 1 | 7 | Driver integrated |

LED 列表示“物理数量 / 当前 SDK 可控数量”。详细能力与验证级别以各 Profile
目录中的 `profile.yaml` 和 `validation.md` 为准；`Driver integrated` 不等于
`production_ready`。

## 独立 VFD 支持包

[BOE VFM041SSBR1-S1 / PT6312BLQ](boe_vfm041ssbr1_s1.md)：四位数字与整体冒号。
供电、通信、既有 ALL ON、64 状态映射已由用户实测确认；v0.1 Profile / Renderer
已实现并编译，Renderer 和 30 分钟稳定性待实机验收。此包不接入 PB Runtime，
不分配尚未确认的 LP 编号。
