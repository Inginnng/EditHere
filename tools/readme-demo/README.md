# README 动图录制 · README demo recorder

本目录通过两套录制器驱动**真实的 EditHere 界面**：窗口、按钮、画布、对话框都是当前源码中的生产控件；背景、指针、字幕及旧版 AI 终端由录制器合成，素材使用虚构页面。

`recorder.cpp` 负责原有七种场景，AI 场景通过本地 socket 发送真实的 `annotate` 请求并显示回执。`feature_recorder.cpp` 负责组图与 AI 解析，在独立的 Qt offscreen 显示器里使用 Qt Test 驱动控件，不移动真实桌面的鼠标，不读取用户配置或系统 API Key。

Every animation in the README is recorded from the **real EditHere interface** by this tool. Only the desktop behind the windows (a fictional dashboard rendered from `scenes/`), the pointer, the captions and the agent terminal are drawn by the tool.

## 组图与 AI 解析动图

在已配置 Qt 6.8+、编译器和 Ninja 的开发终端中运行：

```powershell
cmake -S . -B build-readme -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDITHERE_README_RECORDER=ON
cmake --build build-readme --target readme_feature_recorder
windeployqt --release --no-translations build-readme/readme_feature_recorder.exe

build-readme/readme_feature_recorder image-set . build-readme/feature-frames
build-readme/readme_feature_recorder ai-interpretation . build-readme/feature-frames
python tools/readme-demo/render.py build-readme/feature-frames --only image-set-zh ai-interpretation-zh --width 960
```

每次重录请使用新的输出目录；录制器不会删除或覆盖已有帧。背景页面来自 AI 文字模块已有的 Morrow SVG 示例，不代表真实用户项目。

- `image-set-zh.gif`：在真实组图编辑器中切页、画框、填写意见、保存整组意见并点击完成。录制器检查反馈为两张已批注图片与三页索引，并保存本轮工程和反馈；随后用相同页面 ID 打开更新后的示例页面，展示下一轮复审。换图是预先编排的示例，不声称调用了外部 Agent 修改项目。
- `ai-interpretation-zh.gif`：从真实编辑器快照进入生产解析窗口，发送实际图文请求到本机回放端点，再由生产格式化器校验结果；展示修改文字、编辑与复制。回放使用内存占位密钥，不调用付费 API、不访问系统凭据库；动图与 README 图注均明确标注。输出目录附带实际复制的 `instructions.txt`。

`timeline.json` 和 PNG 帧用于复核与重新编码；最终 GIF 写入 `assets/readme/`。The two feature recordings use production Qt widgets with fictional source images. AI responses are local replays, clearly labelled in the recording; no paid service or real credentials are used.

## 重录 · Re-recording

```bash
# 1. 生成素材（需要 Microsoft Edge、Python 的 Pillow 与 imageio-ffmpeg）
python tools/readme-demo/scenes/build_scenes.py

# 2. 构建录制器
cmake -S . -B build-readme -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDITHERE_README_RECORDER=ON
cmake --build build-readme --target readme_recorder
windeployqt --release --no-translations build-readme/readme_recorder.exe

# 3. 录制（场景：hero capture ocr scrolling annotate explode video，或 all）
#    窗口会出现在真实桌面左上角，录制期间请勿操作鼠标键盘。
build-readme/readme_recorder zh all . build-readme/frames
build-readme/readme_recorder en all . build-readme/frames

# 4. 生成 GIF 到 assets/readme/
python tools/readme-demo/render.py build-readme/frames
python tools/readme-demo/render.py build-readme/frames --only video-zh video-en --width 800
```

Windows 上的文字识别场景需要系统已安装简体中文与英语 OCR 语言包。设置环境变量 `EDITHERE_RECORDER_TRACE=1` 可输出录制进度。

The OCR scene needs the Simplified Chinese and English Windows OCR language packs. Set `EDITHERE_RECORDER_TRACE=1` to print progress.
