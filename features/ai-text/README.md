# AI 文字模块

人用 EditHere 表达图形化需求，独立的视觉 AI 将其转成纯文字需求，再把文字交回原对话。原对话不需要接收图片、批注框或几何 JSON。桌面程序和独立工作台使用这里的同一套代码。

AI 的主要工作是把像素定位、拖动与缩放转成对象身份和预期布局关系：改哪个按钮、放到谁旁边、与谁对齐、拉开什么间距、怎样改变大小。只整理用户已经写出的批注不能完成这个任务；没有文字批注的拖动也需要解释。

最终文字应让看不到图的 Agent 能直接实施。示例：“将右侧椅子展示图右移，让图片右边缘与页面分隔线的右端对齐，拉开与左侧标题的间距。”坐标、截图尺寸、DPR、像素位移和内部 ID 只供解析模型理解，不追加到文字结果，也不用“按原位置调整”代替视觉解释。

大爆炸拖动的提示词会重点对照调整前后的位置，检查目标对象与相邻元素、容器边界、分隔线端点之间的边缘或中心线对齐，以及间距、顺序、包含和大小变化。有证据的对齐关系需要明确写出，不能只说“靠近右侧”；没有形成对齐的拖动则描述真实相对位置。

## 直接运行和修改

在仓库根目录运行：

```powershell
.\features\ai-text\dev.ps1
```

脚本复用仓库 `build/CMakeCache.txt` 中的 Qt 和编译器，只构建 `ai_text_workbench`。首次配置可传 `-QtRoot D:\Qt\6.8.0\msvc2022_64`；需要 MSVC 或 MinGW、CMake 和 Ninja。运行文件是 `build/ai_text_workbench.exe`，使用同目录的 Qt 运行库。

工作台默认使用本机回放服务，包含示例画面、批注、连接测试和结果编辑。服务商设置中的请求也转到本机回放。配置和 Key 只保存在本次进程内存中。`--live` 才接入真实 API，并先打开设置；点击开始解析时才发出请求。

```powershell
.\features\ai-text\dev.ps1 -Settings   # 先看连接设置
.\features\ai-text\dev.ps1 -Dark       # 深色界面
.\features\ai-text\dev.ps1 -Live       # 手动填 Key，调用真实服务
.\features\ai-text\dev.ps1 -NoLaunch   # 只编译和准备运行库
.\features\ai-text\dev.ps1 -Standalone # 从本目录独立配置，输出到 build-ai-text/
```

工作台优先读取本目录的 `resources/`；修改提示词、QSS 或服务商预设后重开工作台即可。编译后的桌面程序使用嵌入资源，修改后需要重新构建。也可用 `EDITHERE_AI_TEXT_ASSETS` 指向自己的资源目录。

需要交给其他 AI 或放到另一目录时，可以导出包含必要宿主文件与许可证的最小源码包；目标目录需要是新目录：

```powershell
.\features\ai-text\export-source.ps1 -Destination E:\Project\EditHere-AI文字模块
```

## 改哪里

| 内容 | 文件 |
| --- | --- |
| 解析窗口布局和交互 | `src/aiinstructionsdialog.cpp` |
| 解析窗口外观 | `resources/analysis.qss` |
| 连接设置布局和外观 | `src/aisettingspage.cpp`、`resources/settings.qss` |
| 浅色、深色主题颜色 | `src/aistyle.cpp` |
| 模型提示词 | `resources/instructions-prompt.txt` |
| 服务商、地区、默认模型 | `resources/provider-presets.json` |
| 请求格式、取消、超时和响应读取 | `src/aifeedback.cpp` |
| 批注展开与文字整理 | `src/aifeedbackformat.cpp` |
| 系统凭据库 | `src/aicredentials.cpp` |
| 本地回放和调试入口 | `workbench/main.cpp` |
| 示例输入和模型回复 | `workbench/fixtures/` |
| API 和窗口回归检查 | `tests/` |

## 给其他 AI 的任务入口

请先读本文件。优先只修改 `features/ai-text/`，用工作台查看真实界面和解析流程。提示词、样式、API 传输和结果排版已经分开，可以按目标修改。`workbench/fixtures/feedback.json` 是截图批注输入，`screen.svg` 与 `screen-result.svg` 是调整前后的画面，`reply.json` 是模型回复示例。第三项只有拖动数据、没有文字批注，用于检查图形化修改到文字需求的流程；回复示例为人工编写的本地回放数据。需要改实际交互时修改共用窗口，不要再另外写一套展示页面。

应用通过 `app/aifeedbacksnapshot.*` 将图片/视频工程转换成 `AiFeedbackInput`，这里返回纯文本；`AiInstructionsDraft` 负责在打开 API 设置后保留已编辑的文字和本次要求。主程序仅在 `app/editor.cpp` 接入窗口。工程格式、截图、OCR 和视频业务仍在原位置。

本目录可以作为 CMake 入口单独配置，开发时保留仓库结构。它借用 `app/settings.h` 等类型声明、`app/ui.cpp`、`app/fonts.cpp`、`app/i18n.cpp` 与共享 SVG 图标作为宿主；工作台不编译主编辑器、截图和视频实现。QtKeychain 的固定依赖、品牌图标来源和许可证随原项目保留。

## 验证

在完整仓库构建目录中：

```powershell
cmake --build build --target ai_feedback_tests ai_instructions_tests
ctest --test-dir build -R '^(ai-feedback|ai-instructions)$' --output-on-failure
.\build\ai_text_workbench.exe -platform offscreen --render artifacts/ai-text-module
```

`--render` 生成就绪、结果、深色、窄窗口截图及结果文本。`--fail-once` 可手动检查错误后重试；`--fixtures <目录>` 可替换示例。真实 API 的效果取决于所选模型，可在工作台用 `--live` 单独试用。

结果支持标准条目 JSON、带代码围栏/前后说明的 JSON 和纯文本回复；交付时仅排版模型生成的自然语言需求，不附加原始几何数据、原话或元数据。文字需要说明对象和预期关系，不能依赖截图指代。若条目未完成语义转换，会提示重新解析，不用原批注或坐标冒充已完成需求。原始批注留在侧栏对照；取消和请求失败保留已编辑文字。
