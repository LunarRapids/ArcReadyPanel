# ArcReadyPanel 0.2.18 兼容性核查（2026-10-05）

## 已实测
Windows 11 x64、Intel® Core™ Ultra X7 Processor 358H、Intel® Arc™ B390 GPU。通过真实 FPS 帧事件、硬件 AV1/AAC 保存、合盖暂停与恢复、鼠标穿透等测试。其他型号尚未获得实机验证，不能视为保证完整支持。

## CPU 范围
程序不再限制 CPU 型号。Windows 11 x64 支持的 Intel® Core™ i3/i5/i7/i9、Intel® Core™ Ultra（Series 1/2/3）、AMD Ryzen™ 可按 Windows 接口提供系统 CPU、进程 CPU、内存和频率数据，但必须搭配 Intel GPU 才能启动本程序。Xeon、Pentium、Celeron 等其他 x64 CPU 同样不设型号门禁，是否运行取决于操作系统和驱动，未验证。
CPU 频率来自 Windows 性能接口估算，不保证与任务管理器刷新时刻、算法一致。CPU 封装功耗当前使用 Windows EMI 的 RAPL_Package0_PKG 通道；未提供该通道的平台不会显示有效功耗，尤其不能据此承诺 AMD 封装功耗支持。

## GPU：具备 AV1 硬件编码能力的候选平台
- Intel® Arc™ A-Series：A310、A380、A580、A750、A770；移动版 A350M、A370M、A530M、A550M、A570M、A730M、A770M。
- Intel® Arc™ B-Series：B570、B580。
- Intel® Arc™ Pro A-Series：A30M、A40、A50、A60、A60M；仅列为待测候选，专业驱动需通过本程序运行时检测。
- Intel® Arc™ Pro B-Series：B50、B60、B65、B70；仅列为待测候选，专业驱动需通过本程序运行时检测。
- Intel® Core™ Ultra Series 1 的 Intel® Arc™ Graphics。
- Intel® Core™ Ultra Series 2 的 Intel® Arc™ Graphics，包括 130V、140V、130T、140T。
- Intel® Core™ Ultra Series 3 的 Intel® Arc™ Graphics，包括 B370、B390；其中 B390 已实测。

Intel 官方确认 Arc A/B 和 Core Ultra Series 1/2/3 的 Arc GPU 支持 AV1 编码：[视频编解码能力表](https://www.intel.com/content/www/us/en/support/articles/000098345/graphics.html)。这仅证明硬件能力，不证明上述每个型号均已通过 ArcReadyPanel 测试。程序要求驱动提供 oneVPL 硬件 AV1、LowPower、NV12、CBR，以及匹配的 D3D11 设备；失败时不回退软件编码。

## GPU：监控候选平台
Intel® Iris® Xe Graphics、Intel® Iris® Xe MAX Graphics、Intel® UHD Graphics（例如 620、630、710、730、750、770）可尝试 Windows GPU 引擎/显存监控。频率和功耗还要求 Intel Control Library 对应接口可用。这些系列不能承诺当前 AV1 回放可用；当前没有 H.264/HEVC 替代编码路径。其他 Intel GPU 也没有型号白名单，只要能通过 Windows D3D11 硬件设备初始化即可尝试监控；不能因此承诺每个旧型号都有适用 Windows 11 驱动或完整传感器。

## 当前边界
- Windows 11 x64；没有 ARM64 原生版本。NVIDIA/AMD GPU 不支持。
- 多 Intel GPU 优先选择连接主屏的适配器，其次选择有连接显示器的 Intel GPU。监控和编码使用该设备。
- 桌面回放要求目标显示器连接到选中的 Intel GPU。混合显卡显示路径可能无法复制；当前录制一个显示器，不拼接多个显示器。旋转显示器暂不支持回放。
- FPS 使用目标进程的 DXGI 帧事件；并非所有图形 API、游戏呈现路径均可保证读数，也不等同于显示器最终包含生成帧的帧率。
- 禁用指标不被总览图表重新启用。监视器关闭即停止监控采样；回放开关独立。
- 尚未进行与 OBS 的整机功耗 A/B 对比，不宣称已证明低于 OBS。

注意：以上不是逐型号保证支持名单。唯一完整实机测试组合仍为358H/B390。CPU不设型号门禁，无法据此生成有限的全部CPU SKU清单。GPU仅按Intel厂商ID和驱动能力筛选；AV1硬件能力不等于软件完整兼容。部分Core Ultra集显名称仅为Intel Graphics，也需按运行时AV1能力判定，不能根据是否带Arc名称判断。Meteor Lake U/H可参考[官方编码说明](https://edc.intel.com/content/www/us/en/design/products/platforms/details/meteor-lake-u-p/core-ultra-processor-datasheet-volume-1-of-2/003/hardware-accelerated-video-encode/)。专业型号参考[Intel Arc Pro A系列](https://www.intel.com/content/www/us/en/products/details/discrete-gpus/arc/workstations/a-series.html)与[Intel Arc Pro B系列](https://www.intel.com/content/www/us/en/products/details/discrete-gpus/arc/workstations/b-series.html)。
