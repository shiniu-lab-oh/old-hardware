# LP-003

> 康佳 SDC251 数字电视机顶盒前面板，已接入 Old Panel SDK。

## 原始设备

- 品牌：康佳
- 型号：SDC251
- 设备类型：数字电视机顶盒
- LP 编号：LP-003

## 前面板能力

- 4 位七段数码管和中间冒号
- 2 个状态 LED，其中红灯为电源指示灯，绿灯可由程序控制
- 7 个实体按键
- 红外接收硬件存在，当前 SDK 不提供红外解码
- 显示与按键控制芯片按 CT1668 / TM1668 兼容模式驱动

## 当前支持状态

- Pinout：mapped，标准 Pinout 图片和按文档重新接线验证待完成
- Driver：development，已接入 Driver Registry
- PB Runtime：构建、烧录、启动和 Cloud State 拉取已验证
- 生产可用：false

## 按键编号

SDK 只暴露物理编号，不保留机顶盒业务语义：

| SDK Key | 原面板文字 |
| --- | --- |
| `OLD_PANEL_KEY_1` | MENU |
| `OLD_PANEL_KEY_2` | EXIT |
| `OLD_PANEL_KEY_3` | OK |
| `OLD_PANEL_KEY_4` | VOL- |
| `OLD_PANEL_KEY_5` | VOL+ |
| `OLD_PANEL_KEY_6` | CH- |
| `OLD_PANEL_KEY_7` | CH+ |

LP-003 的 PB Runtime Profile 默认将 `OK`（物理按键 3）映射为通用
`PRIMARY_ACTION`。这只是 App Binding 的默认输入映射，不属于 Driver 业务语义。

## 快速开始

PB Runtime 使用 Profile 配置构建：

```powershell
cd apps/pb-runtime
idf.py -B build-lp003 `
  -D SDKCONFIG="$PWD/build-lp003/sdkconfig" `
  -D PB_RUNTIME_PROFILE_DEFAULTS=../../profiles/LP-003/pb-runtime.sdkconfig.defaults `
  build
```

设备身份、Wi-Fi 和 Cloud 凭据只放在未跟踪的 `sdkconfig.secrets` 中。

## 文件说明

- `profile.yaml`：结构化能力、映射与支持状态
- `pinout.md`：参考接线和电气注意事项
- `validation.md`：当前可复现构建与实机证据
- `pb-runtime.sdkconfig.defaults`：LP-003 的公开 Runtime 默认配置
- `../../sdk/old-panel/src/drivers/lp003.c`：正式 Driver
- `../../examples/ct1668-test/`：逆向过程、映射记录和测试代码

标准面板照片与 Pinout 标注图尚未整理，不以占位图冒充实物资料。
