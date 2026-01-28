# 屏幕选定区域色反滤镜（开源版）

一个原生 Windows 滤镜程序：在屏幕上框选矩形区域后，该区域以实时“色反”显示，但鼠标点击/滚轮完全穿透到下层应用，不抢焦点、不影响其它程序。

## 应用场景
- 辅助视觉对比：对指定区域做反色，便于在强光/弱色场景下识别内容
- 无感展示叠层：作为非交互叠层滤镜，不改变原应用输入行为
- 多屏环境：支持多显示器，按选区所在屏幕刷新率自适应（120/144/240Hz 等）

## 环境
- Windows 10/11
- Visual Studio 2022（或同等 MSVC 工具链）
- CMake 3.20+
- Windows 10 SDK（随 VS 安装即可）

## 源码结构
- src/main.cpp：核心逻辑（覆盖层、选区、Magnification 反色、刷新自适应）
- CMakeLists.txt：构建配置

## 构建
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成的可执行文件：

`build\\Release\\ScreenInvertFilter.exe`

## 打包
```powershell
cmake --build build --config Release
mkdir dist
copy .\\build\\Release\\ScreenInvertFilter.exe .\\dist\\
copy .\\README.md .\\dist\\
Compress-Archive -Path .\\dist\\* -DestinationPath ScreenInvertFilter.zip -Force
```

## 下载可执行文件（CI 构建产物）
本仓库包含 GitHub Actions 工作流，会自动在 Windows 上构建 Release 并上传 `ScreenInvertFilter.exe` 作为构建产物（Artifacts）。
- 进入仓库的 Actions 页面，找到最新一次构建，下载 `ScreenInvertFilter` Artifact。

## 使用
1. 运行 `ScreenInvertFilter.exe`，会出现一个小的控制窗口
2. 点击“选择区域”，拖拽框选矩形
3. 框选完成后，滤镜会自动开启（该矩形区域实时色反显示）
4. 点击“关闭滤镜/开启滤镜”可开关显示
5. 选择区域时按 `ESC` 取消

## 原理
- 使用 Magnification API（Magnification.dll）创建 `WC_MAGNIFIER` 控件
- 设置源矩形到屏幕选区，倍率为 1.0，仅做颜色矩阵“反相”
- 覆盖层窗口 `TOPMOST + TRANSPARENT + NOACTIVATE`，实现点击穿透与不抢焦点
- 通过显示器刷新率自适应定时重绘，提升高刷下的流畅度

## 许可证
MIT License。欢迎基于此仓库进行二次开发与场景扩展。

## 贡献
- 提交 issue 描述你的场景/需求
- 发起 PR：保持代码简洁、遵循原有风格与安全原则
