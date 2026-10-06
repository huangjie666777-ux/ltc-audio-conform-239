# ltc_conform239

无界面（命令行）LTC 录音解析程序。从 48 kHz PCM16 WAV 录音的时码声道中恢复 SMPTE 80 位 LTC 帧，输出 JSON。纯 C++17，使用 g++ 与 make 构建，第三方依赖仅 dr_wav（WAV 读取）与 nlohmann/json（JSON 输出）。

## 构建

```sh
make            # 生成 ltc_conform239、ltc_gen、test_decoder
make test       # 运行单元/集成测试
make example    # 生成 example.wav 并解析为 example.json
```

## 调用

```sh
./ltc_conform239 <input.wav> [--channel 0|1|auto] [--chunk N] [--json out.json]
```

- `input.wav`：48 kHz、16-bit PCM、单声道或立体声。原文件不被修改。
- `--channel`：时码声道；缺省 `auto`，按边沿活跃度自动选择，其余声道不解码。
- `--chunk`：读取块长（帧数，默认 4096）。块长不影响解码结果。
- `--json`：JSON 写入文件；缺省输出到 stdout。

## 输出 JSON

- `frames[]`：逐帧 `timecode`（HH:MM:SS:FF）、`bits_hex`（80 位内容，bit0 为最低位）、`sample_start`/`sample_end`（从 0 起的码字采样半开区间）及对应秒数 `seconds_start`/`seconds_end`。
- `segments[]`：连续段摘要，含首末时码、首末采样位置与秒数、帧数及中断原因（`signal_loss`、`invalid_frame`、`timecode_jump`、`position_gap`、`end_of_stream`）。

## 支持范围

- 仅支持 25 fps 正向非丢帧 LTC；丢帧标志置位的帧不交付。
- 双相标记编码自行从采样恢复：按边沿间隔跟踪位周期（非固定样本数切位），容忍 ±2% 恒定速度偏差；极性反转不敏感；支持任意前置静默与从帧中途开始的录音（首尾残帧不输出）。
- 帧验证：同步字 0x3FFD、BCD 时间字段、小时 0–23、分秒 0–59、帧 0–24。失锁后继续搜索。
- 连续段仅在音轨位置相邻且时码加一时延续（允许午夜 24 点回绕）；静默、坏帧、时码跳变结束该段，不插值补造帧。
- 分块流式读取，跨块保留状态，不累计整份音频；改变块长不改变有效帧及其位置（有测试保证）。
- 不支持：视频读取、音频播放、网络服务、其他帧率/丢帧模式。

## 代码结构

- `src/audio_reader.*`：WAV 分块读取与声道选择（dr_wav）。
- `src/biphase.*`：边沿检测与双相标记位恢复（自适应位周期跟踪）。
- `src/ltc_frame.*`：同步字搜索、80 位帧组装与 BCD/范围验证。
- `src/segments.*`：连续段整理与中断原因标注。
- `src/main.cpp`：命令行入口与 JSON 输出。
- `tools/ltc_encoder.h`、`tools/ltc_gen.cpp`：可复现样例 WAV 生成器。
- `tests/test_decoder.cpp`：往返、极性、±2% 速度、帧中途开始、块长不变性、丢帧拒绝、坏帧/跳变/静默断段、午夜回绕、立体声声道测试。
