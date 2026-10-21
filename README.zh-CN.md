# NIGHTFALL FM-1

这是面向 M-VAVE FM-1 的独立演奏固件与浏览器工作台源码，包含合成器、音序器、效果器、实机屏幕渲染、USB-MIDI 通信、网页模拟器和同页编辑器。NIGHTFALL 不是 M-VAVE 官方产品，也不是 Felucca 应用的换皮版本；板级驱动与更新基础设施复用 Felucca 的 GPL 代码，详见 [`THIRD_PARTY.md`](THIRD_PARTY.md)。

![NIGHTFALL Studio 浏览器页面预览](docs/studio-preview.jpg)

*桌面网页预览；下方在线模拟器可操作完整面板。*

![NIGHTFALL 荧光绿声纹地形屏幕渲染预览](docs/terrain-preview.png)

*240 × 240 固件渲染器预览图，不是实机照片；完整可交互面板请打开下方在线模拟器。*

**现公开实验刷机包，风险自负。** [`FM-1_999 GREEN2`](web/firmware/NIGHTFALL-FM1-999-GREEN2.fwsc) 只在一台 FM-1 上通过开机、USB 身份、参数回读、音符与声纹地形协议测试。完整按键与音质回归、长期稳定性、恢复原厂固件和无恢复器情况下的救援流程均未验证。刷写失败可能无法开机，需外置恢复器；USB VID/PID 和产品身份仍是测试用。不要将 `firmware/build/` 中的应用镜像直接写入设备。

可从 [GitHub 实验版预发布](https://github.com/SylarPu-X/nightfall-fm1/releases/tag/v0.1.0-green2-experimental) 下载刷机包和匹配的源码归档。

在线预览：[GitHub Pages](https://sylarpu-x.github.io/nightfall-fm1/)；模拟器无需设备。[网页刷机页](https://sylarpu-x.github.io/nightfall-fm1/update.html) 可在识别实机、校验固件并确认风险后刷入。需要桌面 Chrome / Edge、Web MIDI 的 SysEx 权限和 USB 数据线。内置包 SHA‑256：`8e72edc5d894b99efc556d20e64dfe4ea2bb59877920c5314d9befbf3f612be8`。[对应完整源码归档](web/source/NIGHTFALL-firmware-source-green2.tar.gz) 包含匹配的 NIGHTFALL 源码、Felucca 的 GPL 板级与更新代码、加载器源码、JieLi SDK SPL/config 文件及许可。它不是 M‑VAVE 官方固件。

## 目录

- `firmware/`：NIGHTFALL 固件源码、主机测试、目标构建脚本及实验打包工具。
- `web/`：静态网页模拟器、实机编辑器、Web MIDI 更新器和一份明确标注风险的实验刷机包。

## 本地运行

在 `firmware/` 目录运行 `make test`、`make audio-test` 和 `make preview`。目标机编译需要 JieLi 工具链、Felucca 板级支持和相应 SDK 资源；详见 `firmware/README.md`、`firmware/NOTICE.md`。

网页是纯静态文件：在仓库根目录运行 `python3 -m http.server 8000 --directory web`，再打开 `http://127.0.0.1:8000/`。无设备也可使用模拟器；实机联动需要支持 Web MIDI 的浏览器与 USB 数据线。

源码以 **GPL-3.0-only** 许可发布；第三方字体、采样和代码保留各自的许可与署名。
