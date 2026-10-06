# ltc_conform239

无界面（命令行）LTC 录音解析程序。从 48 kHz PCM16 WAV 录音的时码声道中恢复 SMPTE 80 位 LTC 帧，输出 JSON。纯 C++17，使用 g++ 与 make 构建，第三方依赖仅 dr_wav（WAV 读取）与 nlohmann/json（JSON 输出）。

## 构建

```sh
make            # 生成 ltc_conform239、ltc_conform239_sync、ltc_gen、test_decoder、test_sync
make test       # 运行单元/集成测试
make example    # 生成 example.wav 并解析为 example.json
make sync-example  # 生成双机素材并交付 sync_out.wav + sync_out.json
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

## 双机同期交付（ltc_conform239_sync）

把两台录音机的节目声道按期码对齐为一份 48 kHz、PCM16 立体声 WAV：左声道来自第一份节目音频，右声道来自第二份。

```sh
./ltc_conform239_sync --a A.wav --a-channel 1 --a-segment 0 \
                      --b B.wav --b-channel 0 --b-segment 0 \
                      --start 01:00:00:10 --frames 200 \
                      --out out.wav [--json handoff.json] [--chunk N]
```

- 两份输入均为 48 kHz、PCM16 立体声：`--a-channel`/`--b-channel` 指定时码声道，另一声道为节目音频；原录音不被修改。
- `--a-segment`/`--b-segment`：各自录音中的连续段编号（从 0 起，与解析 JSON 中 `segments[]` 顺序一致）。
- `--start`：共同起始时码（25 fps 解释，允许跨午夜）；`--frames`：交付帧数，须为正且不足 24 小时（< 2160000）。
- 逐帧复用解码器给出的码字真实采样半开区间：输出第 i 帧的第 j 个采样对应源区间起点加 j×区间长度/1920，线性插值且帧末不越过最后一个源采样，因此 ±2% 以内的恒定速度偏差被逐帧抵消；不混入时码声道，也不用整段平均偏移替代逐帧映射。
- 拒绝交付（且不留下输出 WAV）的情形：段不存在、起始时码在段内缺失或重复、范围内帧缺失或跨断码；不会拼接另一段或静音补齐。
- 输出先写入 `out.wav.part`，全部成功后才改名，失败即删除。
- 交接 JSON：共同起止时码、帧数、输出采样数、左右来源（文件/时码声道/节目声道/段号）及 `frame_map[]` 每帧两份来源的采样半开区间。
- 分块流式读写，不把整份音频放入内存。

## 支持范围

- 仅支持 25 fps 正向非丢帧 LTC；丢帧标志置位的帧不交付。
- 双相标记编码自行从采样恢复：按边沿间隔跟踪位周期（非固定样本数切位），容忍 ±2% 恒定速度偏差；极性反转不敏感；支持任意前置静默与从帧中途开始的录音（首尾残帧不输出）。
- 帧验证：同步字 0x3FFD、BCD 时间字段、小时 0–23、分秒 0–59、帧 0–24。失锁后继续搜索。
- 连续段仅在音轨位置相邻且时码加一时延续（允许午夜 24 点回绕）；静默、坏帧、时码跳变结束该段，不插值补造帧。
- 录音尾部仅有静默时，段以 `signal_loss` 结束而非 `end_of_stream`。
- 分块流式读取，跨块保留状态，不累计整份音频；改变块长不改变有效帧及其位置（有测试保证）。
- 不支持：视频读取、音频播放、网络服务、其他帧率/丢帧模式。

## 代码结构

- `src/audio_reader.*`：WAV 分块读取与声道选择（dr_wav）。
- `src/biphase.*`：边沿检测与双相标记位恢复（自适应位周期跟踪）。
- `src/ltc_frame.*`：同步字搜索、80 位帧组装与 BCD/范围验证。
- `src/segments.*`：连续段整理与中断原因标注。
- `src/conform.*`：双机区段选择、逐帧采样映射与 WAV 写出。
- `src/main.cpp`：解析命令行入口与 JSON 输出。
- `src/sync_main.cpp`：双机同期交付命令行入口。
- `tools/ltc_encoder.h`、`tools/ltc_gen.cpp`：可复现样例 WAV 生成器（`--program-tone` 可在节目声道叠加正弦音）。
- `tests/test_decoder.cpp`：往返、极性、±2% 速度、帧中途开始、块长不变性、丢帧拒绝、坏帧/跳变/静默断段、午夜回绕、立体声声道、尾部静默断段原因测试。
- `tests/test_sync.cpp`：双机交付集成测试（速度偏差抵消、交接记录、各类拒绝不残留输出）。
