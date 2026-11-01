# 系统与硬件兼容性

## 运行环境

Windows 11 x64，Intel GPU 及对应图形驱动。

已实测组合：Intel Core Ultra X7 Processor 358H + Intel Arc B390 GPU。

## 性能监控

CPU 和内存数据使用 Windows 系统接口，CPU 型号没有白名单限制。GPU 引擎与显存数据使用 Windows 性能计数器，频率和功率使用 Intel Graphics Control Library。CPU 封装功率通过 Windows EMI 能量接口读取；指标可用性由系统及驱动提供的接口决定。

## AV1 即时回放

以下为具有对应硬件能力的适配候选系列：

- Intel Arc A 系列及 B 系列独立显卡。
- Intel Core Ultra Series 1、Series 2、Series 3 中具备 AV1 硬件编码的集成显卡。
- Intel Arc Pro A / B 系列，需验证专业驱动接口。

程序启动编码时检查 oneVPL、硬件 AV1、LowPower、NV12、CBR 及 D3D11 设备。当前编码格式为 SDR 8-bit AV1，声音为 AAC，输出 MP4。

传统 Core i 系列的 UHD / Iris Xe 核显可用于适用指标监控；当前 AV1 回放需要另配支持 AV1 编码的 Intel GPU。

## 显示与帧统计

- 当前录制一个显示器，显示器连接到程序选中的 Intel GPU。
- 多 Intel GPU 环境优先选择连接主显示器的设备。
- 旋转显示器与多屏拼接录制尚未实现。
- FPS 使用目标进程的 DXGI Present 事件，支持的游戏呈现路径由实际事件来源决定。
