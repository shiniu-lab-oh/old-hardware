# 老硬件重启计划 / Old Hardware Project

## 让旧硬件，再启动一次。

这是一个刚刚开始的项目。

第一块支持的硬件，是我们从车库里翻出来的一台旧机顶盒。

我们不知道它最终能支持多少设备。

如果你手上也有一块舍不得扔的旧硬件，欢迎一起折腾。

## 当前组成

- `sdk/old-panel/`：公开的前面板 SDK 与 LP Driver
- `sdk/pb-hal/`：PB 通用硬件接口与 Old Panel Adapter
- `sdk/pb-runtime/`：PB Runtime Core、View、Action、Timer、Overlay 与事件队列
- `profiles/`：硬件能力、Pinout 和验证记录
- `apps/pb-runtime/`：运行在 ESP32 上的通用 PB Runtime
- `sdk/pb-app-protocol/`：公开的 PB App Protocol
- `examples/`：最小示例、工厂测试与逆向工具
- [OH-VFD-FIL Prototype 0](examples/oh-vfd-fil-prototype-0/README.md)：ESP32 MCPWM + DRV8837 的 20 kHz 双极性灯丝供电测试
- [BOE VFM041SSBR1-S1 VFD Support Package v0.1](examples/pt6312-test/README.md)：PT6312 驱动、实测 Profile、数字 Renderer、Mapper 与 SELF_TEST（Renderer 待实机验收）
- `tools/pb-conformance-server/`：PB App Protocol 的公开 Mock Cloud 与验收工具

PB Runtime 是通用运行时，不包含 ONE 等具体 App 的业务逻辑。架构边界与推进顺序见
[PB 架构](docs/pb-architecture.md)，跨硬件验收状态见
[PB Conformance](docs/pb-conformance.md)。

## 已支持硬件

- LP-001：欧视达 ABS-209B，3 位七段显示与 6 键
- LP-003：康佳 SDC251，4 位七段显示、中间冒号与 7 键

详细状态见 [Supported Hardware](docs/supported-hardware.md)。

## PB Runtime 构建矩阵

在已激活 ESP-IDF 环境的终端中运行：

```powershell
python tools/build-pb-runtime.py
```

该命令使用独立构建目录编译 LP-001 与 LP-003，并验证生成的 `sdkconfig` 确实采用了
对应 Profile defaults。Wi-Fi 与 Cloud 凭据仍只从被 Git 忽略的
`apps/pb-runtime/sdkconfig.secrets` 读取。
