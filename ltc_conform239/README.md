# ltc_conform239

无界面（命令行）LTC 录音解析与双机同期交付程序。从 48 kHz PCM16 WAV 录音的时码声道中恢复 SMPTE 80 位 LTC 帧，输出 JSON；亦可将两台录音机的节目声道按共同时码范围对齐导出为一份立体声 WAV。纯 C++17，使用 g++ 与 make 构建，第三方依赖仅 dr_wav（WAV 读写）与 nlohmann/json（JSON 输出）。

## 构建

```sh
make            # 生成 ltc_conform239、ltc_gen、test_decoder
make test       # 运行单元/集成测试
make example    # 生成 example.wav 并解析为 example.json
make dual       # 生成双机素材并执行一次同期交付（dual_out.wav + dual_handoff.json）
```

## 调用

```sh
./ltc_conform239 <input.wav> [--channel 0|1|auto] [--chunk N] [--json out.json]
```

- `input.wav`：48 kHz、16-bit PCM、单声道或立体声。原文件不被修改。
- `--channel`：时码声道；缺省 `auto`，按边沿活跃度自动选择，其余声道不解码。
- `--chunk`：读取块长（帧数，默认 4096）。块长不影响解码结果。
- `--json`：JSON 写入文件；缺省输出到 stdout。
## 双机同期交付（conform）

```sh
./ltc_conform239 conform \
    --a A.wav --channel-a 0|1 --segment-a N \
    --b B.wav --channel-b 0|1 --segment-b N \
    --start HH:MM:SS:FF --frames N --out out.wav \
    [--chunk N] [--json handoff.json]
```

- 两份输入均为 48 kHz、16-bit PCM 立体声 WAV：指定的时码声道用于定位，另一声道为节目音频。原文件不被修改。
- `--segment-a/--segment-b`：各自录音中的连续段编号（从 0 起，与解析 JSON 中 `segments[]` 的顺序一致）。
- `--start`：共同起始时码（按 25 fps 解释，允许跨午夜）；`--frames`：交付帧数，必须为正且范围不满 24 小时。
- 输出：48 kHz、PCM16 立体声 WAV，左声道来自 A 的节目声道，右声道来自 B。每帧对应 1920 个输出采样。
- 采样映射：输出第 i 帧第 j 个采样对应源位置 `start_i + j * (end_i - start_i) / 1920`，其中 `[start_i, end_i)` 为该帧码字在各自录音中的真实采样半开区间；线性插值，帧末不越过最后一个源采样。逐帧映射可抵消 ±2% 支持范围内的恒定速度偏差，不以整段平均偏移替代。
- 拒绝交付（退出码非 0，且不留下输出 WAV）：起始时码在段内不存在或重复、段编号不存在、范围内帧缺失或跨断码。不拼接另一段，不静音补齐。
- 交接 JSON（`--json`，缺省 stdout）：`start_timecode`/`end_timecode` 共同范围、`frames`、`output_samples`、两机输入参数及 `frame_map[]` 逐帧两份来源采样区间。
- 全程分块流式读写，不把整份音频放入内存；输出先写临时文件，完成后才改名就位。

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
- `src/conform.*`：双机同期交付（区段选择、逐帧采样映射、WAV 写出、交接 JSON）。
- `src/main.cpp`：命令行入口与 JSON 输出。
- `tools/ltc_encoder.h`、`tools/ltc_gen.cpp`：可复现样例 WAV 生成器。
- `tests/test_decoder.cpp`：往返、极性、±2% 速度、帧中途开始、块长不变性、丢帧拒绝、坏帧/跳变/静默断段、午夜回绕、尾部静默归因、立体声声道测试。
- `tests/test_conform.cpp`：区段选择拒绝路径、重采样映射、双机端到端交付与失败不留文件测试。
