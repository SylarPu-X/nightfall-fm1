# NIGHTFALL FM-1

这是面向 M-VAVE FM-1 的独立演奏固件与浏览器工作台源码，包含合成器、音序器、效果器、实机屏幕渲染、USB-MIDI 通信、网页模拟器和同页编辑器。NIGHTFALL 不是 M-VAVE 官方产品，也不是 Felucca 应用的换皮版本；板级驱动与更新基础设施复用 Felucca 的 GPL 代码，详见 [`THIRD_PARTY.md`](THIRD_PARTY.md)。

**目前只公开源码，不发布可刷写固件包。** 现有实验版只在一台 FM-1_999 上通过开机、USB 身份、参数回读、音符与声纹地形协议测试。完整按键与音质回归、长期稳定性、恢复原厂固件和无恢复器情况下的救援流程都未验证。不要将 `firmware/build/` 中的应用镜像直接写入设备。

## 目录

- `firmware/`：NIGHTFALL 固件源码、主机测试、目标构建脚本及实验打包工具。
- `web/`：静态网页模拟器、实机编辑器和 Web MIDI 更新器。公开版不内置实验刷机包，仅允许用户自行选择本地文件并完成校验与风险确认。

## 本地运行

在 `firmware/` 目录运行 `make test`、`make audio-test` 和 `make preview`。目标机编译需要 JieLi 工具链、Felucca 板级支持和相应 SDK 资源；详见 `firmware/README.md`、`firmware/NOTICE.md`。

网页是纯静态文件：在仓库根目录运行 `python3 -m http.server 8000 --directory web`，再打开 `http://127.0.0.1:8000/`。无设备也可使用模拟器；实机联动需要支持 Web MIDI 的浏览器与 USB 数据线。

源码以 **GPL-3.0-only** 许可发布；第三方字体、采样和代码保留各自的许可与署名。

