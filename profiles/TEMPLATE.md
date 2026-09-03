# LP 硬件档案模板规范

> 本文件用于规范「老硬件重启计划」中每一个 LP（Old Panel Hardware Profile）的目录结构、文档内容和图片格式。  
> 当前版本优先面向中文用户，文档以中文为主；英文仅用于代码、字段名、文件名和必要的技术术语。

---

## 1. LP 是什么

LP 是「Old Panel」硬件档案编号，用于标识一块已经被研究、接管或正在接管中的旧设备前面板。

例如：

- `LP-001`
- `LP-002`
- `LP-003`

LP 编号只代表一个具体的前面板硬件档案，不代表 Panel Box 的产品型号。

例如：

- `LP-001`：某一款旧机顶盒前面板
- `PB01`：Panel Box 01 产品系列

同一种 PB 产品可以使用不同 LP 作为硬件来源。

---

## 2. 每个 LP 的标准目录结构

每个 `LP-XXX` 目录必须包含以下文件和目录：

```text
profiles/
└── LP-XXX/
    ├── README.md
    ├── profile.yaml
    ├── pinout.md
    └── photos/
        ├── lpxxx-panel.jpg
        └── lpxxx-pinout.png
```

例如：

```text
profiles/
└── LP-001/
    ├── README.md
    ├── profile.yaml
    ├── pinout.md
    └── photos/
        ├── lp001-panel.jpg
        └── lp001-pinout.png
```

### 命名规则

- LP 目录统一使用大写：`LP-001`
- 文件名统一使用小写
- 图片文件名中的 LP 编号不带连字符，例如：
  - `lp001-panel.jpg`
  - `lp001-pinout.png`

---

## 3. README.md

`README.md` 用于让一个第一次看到这块硬件的人，在 1 分钟内理解：

- 它来自什么设备
- 这块前面板有什么
- 目前已经接管到什么程度
- 如何继续查看 Pinout、驱动和示例代码

建议结构：

```md
# LP-XXX

## 简介

一句话说明这块前面板来自什么设备，以及当前状态。

## 原始设备

- 品牌：
- 型号：
- 设备类型：
- LP 编号：

## 前面板能力

- 显示：
- LED：
- 按键：
- 红外：
- 其他结构：

## 当前支持状态

- Pinout：
- Driver：
- SDK：
- 生产可用：

## 快速开始

说明该 Profile 在 Old Panel SDK 中使用哪个 `profile_id`。

## 文件说明

- `profile.yaml`
- `pinout.md`
- `photos/lpxxx-panel.jpg`
- `photos/lpxxx-pinout.png`
```

README 以事实为主。

如果某项尚未确认，应明确写：

> 待确认

不要猜测。

---

## 4. profile.yaml

`profile.yaml` 用于保存结构化硬件信息。

它是未来以下能力的基础：

- Supported Hardware 页面
- 自动文档
- ERP
- 网站
- 工具脚本

建议至少包含以下字段：

```yaml
id: LP-XXX

category: set_top_box_front_panel

original_hardware:
  brand: ""
  model: ""
  type: ""

connector:
  name: J1
  pin_count: null

capabilities:
  display:
    type: ""
    digits: null
    decimal_point: null

  leds:
    physical_count: null
    controllable_count: null

  keys:
    count: null

  infrared:
    present: null
    supported: null

  card_slot:
    present: null
    supported: null

driver:
  name: ""
  sdk: old-panel
  status: ""

status:
  pinout: ""
  sdk_verified: false
  production_ready: false

photos:
  panel: "photos/lpxxx-panel.jpg"
  pinout: "photos/lpxxx-pinout.png"

notes: []
```

### profile.yaml 编写原则

1. 只记录已经确认的信息。
2. 未知字段使用 `null`、空字符串或明确状态，不要猜测。
3. `physical_count` 表示硬件物理存在数量。
4. `controllable_count` 表示当前 SDK 实际可控制数量。
5. “硬件存在”和“SDK 已支持”必须分开记录。

例如某面板有红、绿两个 LED，但只有绿灯可控：

```yaml
leds:
  physical_count: 2
  controllable_count: 1
```

---

## 5. pinout.md

`pinout.md` 是实际接线时最重要的技术文档。

必须包含：

1. Pin 编号规则
2. Pinout 图片引用
3. 完整引脚表
4. 电气注意事项
5. 验证状态

建议结构：

```md
# LP-XXX Pinout

## Pin 编号规则

所有引脚编号均以 PCB 元件面观察。

J1 最靠近电源 / 状态指示灯一侧的触点定义为 Pin 1，
然后沿连接器向另一侧依次编号。

参考图片：

`photos/lpxxx-pinout.png`

## 引脚定义

| Pin | Signal | Reference GPIO | Direction | Notes |
|---:|---|---:|---|---|
| 1 | | | | |
| 2 | | | | |
| ... | | | | |

## 电气说明

- 面板供电电压：
- 逻辑电平：
- 是否需要电平转换：
- 其他注意事项：

## 验证状态

- Pinout：
- hello-panel：
- factory-test：
```

### Direction 统一规则

Direction 一律以主控 / ESP32 视角描述：

- 主控 → 面板
- 面板 → 主控
- 电源
- 双向

---

## 6. Pin 1 统一定义规范

为减少不同 LP 之间的理解成本，Old Panel Profile 统一采用以下规则：

> **所有 Pinout 图片均从 PCB 元件面观察。**

对于长条形机顶盒前面板：

> **J1 最靠近电源 / 状态指示灯一侧的触点定义为 Pin 1。**

然后沿连接器向另外一侧依次编号。

Pinout 图片中必须明确标记：

- `J1`
- `PIN 1`
- Pin 编号方向

不要使用：

- 排线颜色
- 红边
- 连接器卡扣方向

作为唯一的 Pin 1 判断依据。

---

## 7. photos 目录

每个 LP v0.1 固定只保留两张标准图片：

```text
photos/
├── lpxxx-panel.jpg
└── lpxxx-pinout.png
```

暂时不要求上传更多照片。

这样既方便维护，也避免仓库体积快速增长。

---

## 8. lpxxx-panel.jpg

用途：

> 用于识别这块 LP 原本来自什么设备。

内容要求：

- 原始设备正面照
- 尽量完整展示整个机顶盒 / 原设备前脸
- 能看清显示区域、LED、按键布局和主要外观特征
- 尽量正拍
- 背景尽量简单
- 不要求拆机

它回答的问题是：

> **这块前面板原来长在哪里？**

---

## 9. lpxxx-pinout.png

用途：

> 作为实际接线时的唯一标准视觉参考图。

内容要求：

- 使用前面板 PCB 正面 / 元件面照片
- PCB 尽量完整
- J1 接口必须清晰可见
- 必须标出 `J1`
- 必须标出 `PIN 1`
- 建议标出编号方向，例如 `1 → 11`
- 不需要同时提供 PCB 背面 Pinout 图

它回答的问题是：

> **Pin 1 在哪里？引脚从哪个方向开始数？**

---

## 10. 图片尺寸规范

为了兼顾清晰度和 GitHub 仓库体积，统一采用：

### 推荐尺寸

- 图片长边：`1200 px`

### 允许范围

- 最小长边：`800 px`
- 最大长边：`2000 px`

### 文件大小

- 单张图片建议：`300 KB ～ 1 MB`
- 单张最大：`1.5 MB`

### 格式

- `panel` 图片使用 JPG：
  - `lpxxx-panel.jpg`
- `pinout` 标注图使用 PNG：
  - `lpxxx-pinout.png`

如果原图很大，请压缩后再提交。

---

## 11. 图片处理原则

允许：

- 裁剪
- 旋转
- 调整亮度
- 轻度锐化
- 添加 Pin 1 箭头和编号

不建议：

- 过度美化
- 改变 PCB 原本颜色
- 使用会遮挡线路 / 元件的大面积水印
- 添加与技术无关的装饰

Pinout 图首先是一张技术图。

---

## 12. LP 状态建议

为了避免“已经研究过”和“已经可以生产”混在一起，建议使用分级状态。

### Pinout

```text
unknown
mapped
verified
```

含义：

- `unknown`：尚未完成
- `mapped`：已经初步确定
- `verified`：已经完全按照文档重新接线并实机验证成功

### Driver

建议：

```text
new
development
sdk_verified
deprecated
```

### Production

```text
production_ready: false / true
```

### 重要原则

代码写完，不等于 verified。

只有实机测试通过，才能升级状态。

---

## 13. Verified Pinout 的最低要求

Pinout 标记为 `verified` 前，至少需要完成：

1. 根据当前成功运行的黄金样机逐脚确认。
2. 完成 J1 Pin 1 ～ Pin N 的完整映射。
3. 拍摄并制作标准 `lpxxx-pinout.png`。
4. 断开原接线。
5. 只根据 `pinout.md` 重新接线。
6. `hello-panel` 实机运行成功。
7. `factory-test` 实机运行成功。

完成以上步骤后，才能：

```yaml
status:
  pinout: verified
```

---

## 14. SDK Verified 的最低要求

Driver 标记为 `sdk_verified` 前，至少满足：

- Driver 已接入 Driver Registry
- `hello-panel` 运行成功
- `factory-test` 运行成功
- 显示正常
- 可编程 LED 正常
- 所有 SDK 支持的实体按键正常
- 使用项目当前标准 ESP32 主控测试通过

---

## 15. Production Ready 的最低要求

`production_ready: true` 属于比 SDK Verified 更高一级的状态。

需要额外完成：

- 标准低压供电方案
- 稳定线束 / Adapter
- 主控可靠固定
- 内部绝缘
- 多次断电重启测试
- 老化测试
- PB 产品样机验证

因此：

> LP Driver 已经 SDK Verified，并不代表它已经 Production Ready。

---

## 16. 新增一个 LP 的最小流程

新增 LP 建议按照以下顺序：

```text
发现新硬件
    ↓
分配 LP 编号
    ↓
建立标准目录
    ↓
添加 panel.jpg
    ↓
识别硬件
    ↓
完成 Pinout
    ↓
制作 pinout.png
    ↓
编写 Driver
    ↓
接入 Driver Registry
    ↓
运行 hello-panel
    ↓
运行 factory-test
    ↓
更新 README / profile.yaml
```

---

## 17. 新 LP 提交前检查清单

提交 Pull Request 或正式纳入仓库前，应检查：

- [ ] 目录名符合 `LP-XXX`
- [ ] 存在 `README.md`
- [ ] 存在 `profile.yaml`
- [ ] 存在 `pinout.md`
- [ ] 存在 `photos/lpxxx-panel.jpg`
- [ ] 存在 `photos/lpxxx-pinout.png`
- [ ] 两张图片符合尺寸要求
- [ ] Pin 1 标注清晰
- [ ] profile.yaml 不包含未经确认的猜测数据
- [ ] README 状态与实际测试状态一致
- [ ] Pinout 表与实机一致
- [ ] Driver 状态与实机测试一致

---

## 18. 当前版本原则

本模板为早期 v0.1 标准。

目标不是一次把所有可能情况设计完整，而是：

> **让 LP-001、LP-002、LP-003 从第一天开始拥有统一、简单、可持续的档案结构。**

未来如果出现：

- VFD
- 点阵显示
- 字符屏
- 旋钮
- 特殊触摸输入
- 更复杂的面板

再根据真实需求升级模板。

在没有实际需求前，不提前增加大量字段和文件。

---

## 核心原则

> **文档少而准确，比文档多但混乱更重要。**

> **不知道的数据宁可留空，也不要猜。**

> **每一个 LP 都应该让一个没有参与破解过程的人，仅靠仓库资料就能理解并重新接线。**
