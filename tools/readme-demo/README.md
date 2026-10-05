# README 动图录制 · README demo recorder

README 里的动图全部由这个工具驱动**真实的 EditHere 界面**录制：窗口、按钮、画布、对话框都是程序自己的控件，鼠标与键盘操作经由窗口系统送达。只有窗口背后的“桌面”（`scenes/` 生成的虚构仪表盘）、指针、字幕和 AI 终端是工具画的；AI 场景通过本地 socket 发送真实的 `annotate` 请求并显示真实回执。

Every animation in the README is recorded from the **real EditHere interface** by this tool. Only the desktop behind the windows (a fictional dashboard rendered from `scenes/`), the pointer, the captions and the agent terminal are drawn by the tool.

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
