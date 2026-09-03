# LP-001

> 老硬件重启计划支持的第一个前面板。

## 原始设备

- 品牌：欧视达
- 型号：ABS-209B
- 设备类型：数字电视机顶盒
- Hardware Profile：LP-001
- 状态：✅ 已成功由 ESP32 接管

## 已验证能力

- 3 位数码管显示
- 2 个状态 LED（红 / 绿）
- 6 个实体按键
- ESP32 控制
- ESP-IDF 开发

## 当前进展

LP-001 已经可以脱离原机主板，由 ESP32 独立控制前面板。

目前已验证：

- 数码管显示
- LED 控制
- 实体按键读取

现有控制代码已经重构为 Old Panel SDK 的第一个注册驱动，并通过
`hello-panel` 与 `factory-test` 在真实硬件上验证。

## 项目中的角色

LP-001 不是 Panel Box 的产品型号。

它代表一种被 Old Panel SDK 支持的旧硬件前面板。

基于 LP-001 可以构建：

- Panel Box 01 Dev
- 自定义 ESP32 应用
- Old Panel SDK 示例项目

## 状态

- Driver: Development
- SDK Support: Integrated
- Production Ready: No
