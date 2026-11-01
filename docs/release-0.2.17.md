# ArcReadyPanel 0.2.17 编码输入免额外复制

使用应用分配的 D3D11 NV12 纹理，同时具备 RENDER_TARGET、SHADER_RESOURCE、VIDEO_ENCODER 标志。D3D11 VideoProcessor 将桌面 BGRA 画面缩放、转换并写入这张纹理，oneVPL 外部分配器通过 GetHDL 暴露同一纹理供固定功能 AV1 编码使用。删除正常路径的中间 NV12 纹理和 CopyResource。保持 LowPower=ON、BestSpeed、AsyncDepth=1、无原始画面 CPU 回读。

纹理不支持或外部分配器初始化失败时重新创建硬件会话，回退到内部纹理池。转换输出视图不支持时保留 GPU 复制兜底；不启用软件编码。输入只有在编码同步完成后才复用，检测到纹理仍锁定则停止而非覆盖。不增加后台轮询或 UI 定时器。

本机 Intel Core Ultra X7 Processor 358H / Intel Arc B390 GPU：720p、1080p、1440p、4K 各 180 帧，direct=180、copy=0。独立 AV1 解码检查均为 180 帧，动态图像颜色变化正确。1080p 分配纹理为1920×1088，裁切输出为1920×1080。强制内部纹理池对照 direct=0、copy=180。RAM/磁盘回放、系统声音、默认音频设备重开、MP4保存、缓存清理通过；9/9回归测试通过。

这里的免额外复制指应用控制的转换纹理到编码输入之间不再复制。桌面捕获与颜色转换仍需处理，驱动内部是否复制未经 GPU 调用跟踪证明。未进行稳定整机功耗 A/B 或同配置 OBS 对照，不能宣称绝对零拷贝或功耗低于 OBS。其他 Intel GPU 尚未实机验证。

参考：[Intel Windows共享示例](https://github.com/intel/libvpl/blob/main/examples/api2x/hello-sharing-dx11/src/device-d3d11.cpp)、[Intel D3D11外部分配器](https://github.com/intel/libvpl-tools/blob/main/tools/legacy/sample_common/src/d3d11_allocator.cpp)、[内存分配说明](https://intel.github.io/libvpl/latest/programming_guide/VPL_prg_mem.html)。共享导入需要带 DECODER 标志的纹理，和此机器的可渲染纹理要求冲突；因此正式路径采用外部分配器，取消每帧试验性导入。

交替对照：1080p 合成动态图像，每次180帧，旧复制路径1594/1359/1266/1282/1281ms，直接输入1219/1203/1204/1219/1234ms；中位数1282ms与1219ms。此总耗时包含初始化、非限速编码和文件写入，只表示本次试验未观察到吞吐退步，不是游戏帧率或整机功耗结果。
