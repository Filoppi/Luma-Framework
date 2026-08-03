# Luma for Granblue Fantasy: Relink

这是一个只面向《碧蓝幻想 Relink》（Granblue Fantasy: Relink，GBFR）的 Luma 分支，不再维护其他游戏、通用游戏模板或 Graphics Analyzer。

当前组合版以 [Filoppi/Luma-Framework](https://github.com/Filoppi/Luma-Framework) `latest-568` 为基线，并保留了 [Hiyajomaho-num9/Luma-Framework](https://github.com/Hiyajomaho-num9/Luma-Framework) 中经过筛选的超分辨率资源生命周期和稳定性改动。GBFR 模组版本为 2.0.3。

## 功能范围

- HDR 输出与相关色调映射修复
- DLSS / FSR 超分辨率集成
- 渲染比例、TAA 与后处理 Shader 调整
- GBFR 专用 ReShade Addon

## 安装

从 GitHub Actions 下载 `Luma-Granblue_Fantasy_Relink`，解压后把以下内容直接放入游戏可执行文件所在目录：

```text
dxgi.dll
nvngx_dlss.dll
Luma-Granblue Fantasy Relink.addon
Luma/
```

卸载时删除以上四项即可。覆盖或更新前建议先备份现有 ReShade/Luma 文件。

## 开发与构建

建议环境：

- Windows 11
- Visual Studio 2026（MSVC `v145`）
- Windows 10/11 SDK

打开 `Luma.sln`，选择 x64 配置构建。发布包使用 `Publishing-Release|x64`。也可以直接运行：

```powershell
msbuild "Source\Games\Granblue Fantasy Relink\Granblue Fantasy Relink.vcxproj" /m /p:Configuration=Publishing-Release /p:Platform=x64 /p:PlatformToolset=v145
```

仓库只保留一个 GitHub Actions 工作流：在 `main` 的 PR、推送或手动触发时构建 GBFR x64 发布版并上传单层安装包，不运行其他游戏矩阵。

## 目录

- `Source/Games/Granblue Fantasy Relink`：GBFR Addon 源码
- `Source/Core`：Luma 共享运行时
- `Shaders/Granblue Fantasy Relink`：GBFR Shader
- `Shaders/Global`、`Shaders/Includes`：GBFR 使用的共享 Shader
- `Source/External`：ReShade、NGX、FidelityFX 等构建依赖

本项目继续遵循仓库中的 [LICENSE.md](LICENSE.md)。
