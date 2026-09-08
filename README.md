# 老硬件重启计划 / Old Hardware Project

## 让旧硬件，再启动一次。

这是一个刚刚开始的项目。

第一块支持的硬件，是我们从车库里翻出来的一台旧机顶盒。

我们不知道它最终能支持多少设备。

如果你手上也有一块舍不得扔的旧硬件，欢迎一起折腾。

## 当前组成

- `sdk/old-panel/`：公开的前面板 SDK 与 LP Driver
- `sdk/pb-hal/`：PB 通用硬件接口与 Old Panel Adapter
- `sdk/pb-runtime/`：可复用的 View、Action、Timer、Overlay 与事件队列
- `profiles/`：硬件能力、Pinout 和验证记录
- `apps/pb-runtime/`：运行在 ESP32 上的通用 PB Runtime
- `sdk/pb-app-protocol/`：公开的 PB App Protocol
- `examples/`：最小示例、工厂测试与逆向工具

PB Runtime 是通用运行时，不包含 ONE 等具体 App 的业务逻辑。架构边界与推进顺序见
[PB 架构](docs/pb-architecture.md)，跨硬件验收状态见
[PB Conformance](docs/pb-conformance.md)。

## 已支持硬件

- LP-001：欧视达 ABS-209B，3 位七段显示与 6 键
- LP-003：康佳 SDC251，4 位七段显示、中间冒号与 7 键

详细状态见 [Supported Hardware](docs/supported-hardware.md)。
