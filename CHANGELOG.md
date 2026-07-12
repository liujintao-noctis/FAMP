# Changelog

本文件记录 FAMP 已发布版本的用户可见变化。

## [0.1.1] - 2026-07-12

### Added

- 新增离线快速入门、快捷键和版本信息对话框。
- 新增选中二维图元的 5° 顺时针/逆时针旋转，支持菜单、工具栏和快捷键。
- 新增 PCD/LAS 拖放打开和最近 8 个点云文件记录。

### Changed

- 打开对话框默认回到最近使用的点云目录。
- BMP 图像和 PCD 切割点云改为原子写入，并自动补全正确扩展名。
- LAS 读取逻辑从主窗口中抽离，增加有界预分配和非有限坐标过滤。

### Fixed

- 修复了无效、空、不存在、不可读或不支持的点云仍可能进入渲染流程的问题。
- 修复了图像/点云保存未检查实际写入结果却报告成功的问题。
- 改善了 PCD、LAS 以及导出文件在中文等 Unicode 路径下的兼容性。

## [0.1.0] - 2026-07-12

- 建立可复现的 CMake/vcpkg Release 构建、GoogleTest 测试和 Linux/Windows CI 打包流程。
- 增加统一应用版本号、窗口标题版本显示和 Linux/Windows 应用图标。
- 补充 Linux/Windows 从零构建、测试、运行和排障文档。

[0.1.1]: https://github.com/liujintao-noctis/FAMP/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/liujintao-noctis/FAMP/releases/tag/v0.1.0
