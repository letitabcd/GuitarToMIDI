# Guitar To MIDI

[English](README.md) | **简体中文**

面向 Apple Silicon 的实时单音「吉他 DI → MIDI」转换器。提供 **AU、VST3 和独立应用**。
吉他进入声卡 Hi-Z / Instrument 输入，识别出的 MIDI 音符用于演奏软件乐器。
第一版专注单音旋律、solo、重复拨弦和推弦；同时弹奏和弦不会得到可靠的复音 MIDI。

![Guitar To MIDI 界面](docs/ui-tracking.png)

*这是使用合成 G3 信号渲染的实际插件界面。素材已内嵌在插件中。*

## 安装与构建

目标平台为 Apple Silicon Mac（arm64）。需要 Git、CMake 3.22 或更新版本、Ninja 和 Apple Command Line Tools；不需要完整 Xcode。
尚未安装 Apple 开发工具时运行 `xcode-select --install`。如果已使用 [Homebrew](https://brew.sh)，可运行 `brew install cmake ninja` 安装其余构建工具。
JUCE 9.0.3 是固定到提交 `be29c81492b6151c8ea8d14c840e1311963b3a83` 的 Git 子模块。

```bash
git clone --recurse-submodules https://github.com/letitabcd/GuitarToMIDI.git
cd GuitarToMIDI
# 构建、运行全部测试、签名、安装，并执行苹果 AU 验证
./scripts/build-and-install.sh
```

如果克隆时未获取子模块，先运行 `git submodule update --init --recursive`。
如果下载了不含 Git 元数据的源码 ZIP，先运行 `git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git deps/JUCE` 获取依赖。

| 文件 | 安装位置 |
|---|---|
| AU | `~/Library/Audio/Plug-Ins/Components/Guitar To MIDI.component` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Guitar To MIDI.vst3` |
| 独立应用 | `~/Applications/Guitar To MIDI.app` |

安装脚本会把旧版本移入项目的 `.backups/`，再安装新版本。
使用本机 ad-hoc 签名，未进行 Developer ID 签名或公证。
本仓库提供源码，按上述命令在本机编译；安装后重新打开宿主。

只构建而不安装：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## Logic Pro：直接使用 AU

使用两条轨道：音频轨接收吉他并转换 MIDI，软件乐器轨加载音源发声。

```text
吉他 → 声卡 → 音频轨 / Guitar To MIDI
                         ↓ Virtual MIDI
                    软件乐器轨 → 扬声器 / 耳机
```

1. 安装后启动或重新打开 Logic。在「设置 → 插件管理器」中确认厂商 **renyi** 的 **Guitar To MIDI** 通过验证。
2. 吉他连接声卡的 **Hi-Z / Instrument** 输入，在 **Logic Pro → 设置 → 音频 → 设备** 中选择声卡。创建一条**单声道音频轨**，输入选吉他所在通道，例如 Input 1，打开这条轨的输入监听 **I**。
3. 在该轨的 **Audio FX → Audio Units → renyi → Guitar To MIDI** 中加载插件。
4. 插件中开启 **Virtual MIDI**，关闭 **Monitor DI** 和 **Bypass**。起始设置使用 **Input Channel: Left / Mono**、**MIDI Channel: 1**、**Pitch Bend: Off**。底部会显示端口名，例如 `Guitar To MIDI`；多实例使用带编号的独立端口。
5. 创建一条**软件乐器轨**，加载钢琴、合成器等音源，选中并开启该轨的 **R（录音启用）**。
6. 在 **Logic Pro → 设置 → MIDI → 输入** 中启用上述端口；看不到“输入”页面时先在“设置 → 高级”中启用完整功能。有 MIDI 输入端口选择的版本，可以把乐器轨设为仅接收该端口；没有时先使用默认 MIDI 路由。音频轨继续保持 **I** 开启。
7. 弹单音，插件会显示音名和电平，软件乐器收到 MIDI 后发声。录制软件乐器轨即可保存 MIDI。

AU 类型为 `aufx`，可以插入音频轨的 Audio FX。插件既输出宿主 MIDI，也提供 CoreMIDI 虚拟端口；Logic 的两轨连接使用虚拟端口。
[Apple 的 MIDI 输入说明](https://support.apple.com/en-gb/guide/logicpro/lgcp418a5bbb/11.2/mac/14.4)介绍了端口的启用和轨道选择。不同版本和语言的菜单名称可能略有差异。

如果切换到软件乐器轨后停止识别，进入 **设置 → 音频 → 通用**，开启“软件监听”，取消“输入监听启用时仅会用于聚焦轨道”选项，让未选中的吉他轨继续处理输入。参见 [Apple 通用音频设置](https://support.apple.com/zh-cn/guide/logicpro/lgcp0ed343a9/mac)。

**Monitor DI** 默认关闭，避免干琴与音源同时响起；想同时听干琴时开启它。
开始建议使用 48 kHz、64 或 128 samples 的声卡缓冲，并保持吉他 DI 未加失真、混响或延迟。

## GarageBand / 独立应用

GarageBand 的跨轨连接优先使用独立应用：

1. 打开 `~/Applications/Guitar To MIDI.app`。
2. **Options → Audio/MIDI Settings** 中选择声卡和吉他输入通道。首次运行需要允许音频输入权限。
3. 独立应用自动启用虚拟 MIDI 输出；保持 **Monitor DI** 关闭。
4. 在 GarageBand 新建并选中一条软件乐器轨，加载音源，接收 `Guitar To MIDI` 端口发出的 MIDI。

独立应用已修正 JUCE 默认静音输入的问题。只运行一个转换来源：如果同时开启独立应用和 AU 的 Virtual MIDI，乐器可能收到两份音符。

其他 VST3 宿主可使用其支持的音频转 MIDI 路由，或让接收音源选择转换器的 CoreMIDI 虚拟端口。项目会构建 VST3，但各宿主路由不同；本文重点说明 Logic 和 GarageBand 的用法。

## 参数

| 参数 | 作用 / 起始建议 |
|---|---|
| Input gain | 检测增益，默认 0 dB。响弹时建议约 -18 至 -6 dBFS，避免声卡输入削波。 |
| Gate | 噪声门，默认 -55 dBFS。设在不弹时的底噪之上；过高会截断弱音和尾音。 |
| Clarity | 音高可信度阈值，默认 0.78。降低更容易触发，也更容易误判。 |
| Dynamics | MIDI 力度对弹奏强弱的响应，默认 0.6。 |
| Retrigger | 重复拨同一个音时的触发灵敏度，默认 0.55。 |
| Input Channel | Left / Mono、Right 或 L + R。立体声中仅一路接吉他时选择对应通道；单声道使用唯一输入。 |
| Pitch Bend | Off、±1、±2、±7、±12 半音。发送 RPN 弯音范围；不接受 RPN 的音源需手动设成相同范围。 |
| MIDI Channel | MIDI 通道 1–16，默认 1。 |
| Analysis Speed | Fast / Balanced / Precise；默认 Balanced。Fast 降低分析窗口时长。 |
| Virtual MIDI | AU / VST3 向独立 CoreMIDI 端口发送音符，插件默认关闭；独立应用始终启用。 |
| Monitor DI | 是否通过插件音频输出监听原始 DI，默认关闭。 |
| Bypass | 停止识别并释放当前 MIDI 音符。 |
| Release notes | 释放音符、发送 All Notes Off / All Sound Off 并复位弯音，音源卡音时使用。 |

连续滑音在弯音范围内保持同一 MIDI 音符；超出范围时改发新音符。
更改 MIDI 通道、弯音范围、输入通道或虚拟输出，以及旁路／设备重启，会释放原来的音符，避免卡音。

## 常见问题

| 现象 | 检查方法 |
|---|---|
| Logic 找不到插件 | 重启 Logic，在设置 → 插件管理器中查找 renyi / Guitar To MIDI，检查验证状态，必要时复位并重新扫描选中的插件。 |
| 插件没有音名和电平 | 检查声卡、轨道输入、音频输入权限、Input Channel、I 和 Bypass；Gate 过高时适当降低。 |
| 有音名但音源不发声 | 开启 Virtual MIDI，在 Logic 中启用对应端口，选中并启用音源轨录音，检查音源、输出和 MIDI 通道。 |
| 切换轨道后停止识别 | 开启软件监听，取消仅监听聚焦轨道的限制，保持吉他轨的 I 开启。 |
| 干琴与音源同时响 | 关闭 Monitor DI；只想听音源时，也关闭声卡的硬件直通监听。 |
| 音符重复 | 停止其他转换实例或独立应用，或让音源只接收一个虚拟端口。 |
| 错音／跟踪不稳定 | 使用干净 DI，制音未弹奏的弦，一次弹一个音，检查削波和底噪，再调整 Gate 与 Clarity。 |
| 卡音 | 点击 Release notes，并检查音源的延音设置。 |
| 延迟明显 | 先用 48 kHz、64 或 128 samples 的 I/O 缓冲，可尝试 Fast 并减少其他高延迟效果。过载或爆音时增大缓冲。 |

## 验证范围与延迟

自动测试包含四组：

- `dsp_tests`：62 项 Karplus–Strong 合成拨弦检查，包括音高、力度、门限和旋律。
- `dsp_regression_tests`：44.1 / 48 / 96 kHz、三个速度档、Drop D 到高把位、八度换音、重复拨弦、连续推弦、异常输入和音频线程内存分配。
- `processor_tests`：实际 JUCE 处理器的 16 个通道、旁路、防卡音、状态保存、左右输入、超大／空缓冲和界面菜单。
- `au_integration_tests`：加载真正的 arm64 AU，输入合成音频，检查宿主 MIDI 回调和真实 CoreMIDI 接收端的 Note On / Note Off。

标准合成拨弦测试中，Balanced 模式的算法起音延迟约 **17–52 ms**。
这是合成信号从开始输入到生成 MIDI 的延迟，**不包括**声卡、宿主缓冲、虚拟 MIDI 调度和音源的延迟，也不是实琴端到端测量。
真实 DI 的跟踪表现仍需用你的声卡、吉他和弹奏方式试听调整。
本机构建已通过四组测试及 Apple AU 验证，项目所有者也反馈了成功的实琴使用体验；其他宿主和设备组合尚未全面验证。

音域设计约为 **70–1350 Hz**，覆盖标准六弦吉他、Drop D 和高把位单音；不覆盖低音贝斯或更低的降调。
开启弯音时，相邻换音与滑音在纯音高信息上存在歧义，演奏键盘式分离音符时建议关闭 Pitch Bend。

## 界面

界面采用深炭黑与暖铜色，包含吉他弦谐波背景、大字号音名、调音刻度、输入电平与音高可信度表。
旋钮、菜单和开关为原生 JUCE 控件，支持参数自动化、键盘操作和 Retina 渲染；双击旋钮可恢复该参数的默认值。
所有图像已嵌入插件，不需要在安装目录旁额外放置素材。

`docs/ui-tracking.png`、`docs/ui-tracking-retina.png` 和 `docs/ui-idle.png` 为实际界面的渲染预览；
跟踪预览使用合成 G3 信号。图片素材及生成提示词见 `Assets/README.md`。

如需重新导出界面预览，可执行：

```bash
G2M_UI_PREVIEW_DIR="$PWD/docs" ./build/processor_tests
```

## 源码

| 文件 | 作用 |
|---|---|
| `Source/PitchEngine.*` | 纯 C++ 引擎：滤波、FFT 自相关、MPM / NSDF、音符状态机。 |
| `Source/PluginProcessor.*` | JUCE 参数、输入选择、宿主 MIDI、弯音 RPN、防卡音。 |
| `Source/VirtualMidiOutput.*` | 固定容量队列与 MIDI 工作线程，音频回调不直接调用 CoreMIDI。 |
| `Source/PluginEditor.*` | 音名、电平、可信度、参数和路由显示。 |
| `Source/Standalone.cpp` | 音频设备设置及独立应用入口。 |
| `scripts/build-and-install.sh` | arm64 构建、测试、签名、备份与安装。 |

欢迎提交问题和改进。报告问题时附上 macOS／宿主版本、声卡、采样率、缓冲大小、插件设置和最小复现步骤；分享音频时使用可公开的录音。修改音频或 MIDI 代码后，请运行四组测试再提交。

## 开源许可

项目代码、文档和项目素材在版权适用范围内采用 **GNU AGPL-3.0-only**，完整条款见 [LICENSE](LICENSE)，归属和依赖说明见 [NOTICE.md](NOTICE.md)。

JUCE 9.0.3 独立采用 AGPLv3 和商业 JUCE 双重许可，内含第三方组件保留各自的许可。参见 [JUCE 许可](https://github.com/juce-framework/JUCE/blob/9.0.3/LICENSE.md)与[依赖清单](https://github.com/juce-framework/JUCE/blob/9.0.3/JUCE.spdx.json)。在 AGPL 条款之外使用 JUCE 时可能需要商业 JUCE 许可，本仓库不授予商业 JUCE 许可。初始化子模块即可获取框架完整源码与许可声明。
