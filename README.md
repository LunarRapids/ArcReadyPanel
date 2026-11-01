# ArcReadyPanel

面向 Windows 11 x64 和 Intel GPU 的原生性能监视器与桌面即时回放工具。非 Intel 官方项目，与 Intel 官方 Arc Control 无隶属关系。

当前版本：0.2.18。完整实机验证组合：Intel Core Ultra X7 Processor 358H + Intel Arc B390 GPU；其他平台属于候选兼容范围，详见 [兼容性说明](docs/compatibility.md)。

## 功能

- CPU、内存及 Intel GPU 指标监控，支持逐项开关、采样周期、字体、颜色与历史曲线。
- 基于 DXGI ETW 的平均 FPS、1% Low、平均帧时间与 P99 帧时间；并非所有游戏呈现路径均可保证读取。
- 透明、鼠标穿透、独立主题及缩放的桌面监视器。
- 单显示器桌面回放：Intel oneVPL AV1 硬件编码、SDR 8-bit、WASAPI 默认播放设备系统声音、AAC 音频；不采集麦克风。
- RAM / 磁盘环形缓存、异步 MP4 保存、自定义快捷键、托盘通知及合盖暂停后恢复。
- 软件旁保存 ArcReadyPanel.settings；支持登录启动及静默启动。

## 环境要求

Windows 11 x64、适用的 Intel 图形驱动。即时回放要求驱动提供匹配设备的 oneVPL 硬件 AV1、LowPower、NV12 和 CBR；不回退 CPU 软件编码。录制显示器须连接到所选 Intel GPU。功率和频率指标取决于系统与驱动提供的接口。

默认视频目录：Windows“视频”目录下的 ArcReadyPanel；默认磁盘缓存：%LOCALAPPDATA%/ArcReadyPanel/ReplayCache。全新配置默认关闭监视器和即时回放；之后保存并恢复用户开关状态。

## 编译

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载、Windows SDK 和 CMake 3.25 或更新版本；需要支持 C++23 的 MSVC。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

输出：build/Release/ArcReadyPanel.exe。第三方头文件已包含在 third_party；oneVPL 和 Intel 控制库在运行时从系统/驱动加载。

## 验证

```powershell
ctest --test-dir build -C Release --output-on-failure
```

部分测试会创建窗口、采集实际桌面、使用 Intel AV1 编码器或验证帧事件，需要适用硬件与交互式 Windows 会话。不是所有测试都适合无 GPU 的 GitHub 托管执行器。BridgeRealTests 是显式提权的手动测试目标，不属于默认 CTest。

## 性能说明

采用按需初始化、事件等待、压缩数据缓存和 GPU 纹理路径。可用时，转换结果直接作为编码输入；不支持时回退 GPU 复制。未证明驱动内部完全零拷贝，也尚未证明所有场景功耗低于 OBS。[研究记录](docs/replay-research-2026-10-05.md)。

## 许可证与图标

第三方组件按各自许可证分发，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
