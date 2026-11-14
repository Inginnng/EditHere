# EditHere 桌面界面预览 · 0.13.0

使用 Electron + React + TypeScript + Radix UI 实现的 Windows 桌面界面预览，包含模型服务、批注工作台、AI 解析和设置四个页面。程序使用本地界面文件，不需要浏览器或本地网页服务。

## 直接查看

双击运行文件夹中的 `EditHere.exe`。请保留整个程序文件夹，不要只移动 exe。
界面使用 Windows 系统字体，图标和示例画面随程序提供，不依赖网络加载。

## 开发

```powershell
npm install
node node_modules/electron/install.js
npm run desktop
```

`npm run build` 执行类型检查和前端生产构建。`npm run package:desktop` 生成 `desktop-dist/EditHere-win32-x64/` 运行文件夹，不生成 ZIP。输出目录已存在时会停止，避免覆盖先前成果。

`npm run test:desktop` 使用 Playwright 的 Electron 测试接口验证本地程序，使用隔离的测试配置目录；可通过 `EDITHERE_TEST_EXE` 指定已打包的 exe，`EDITHERE_TEST_ARTIFACTS` 指定截图和结果目录。打包时 `EDITHERE_DESKTOP_OUTPUT` 可指定新的输出父文件夹。

## 设计参考

先核对 Magpie 与新版 CC Switch 的官方截图和源码，再实现中性灰、同色导航、清晰状态和统一控件。完整来源、固定提交与实施规则见 [DESIGN-REFERENCE.md](DESIGN-REFERENCE.md)。模型服务首页展示配置列表；添加/编辑进入独立面板；设置采用分组行；菜单、开关、对话框复用 Radix 的焦点与键盘行为。

## 预览范围

- 配置列表、默认解析服务、搜索；独立添加/编辑面板、服务商预设、地区、模型、协议、API 地址和 Key 显隐。取消丢弃草稿。
- 图片导入、批注添加/编辑/删除、画面缩放、JSON 复制及下载。
- AI 解析示例、文字编辑、复制和导出；导航切换保留当前页状态。
- 深色/浅色/跟随系统、窄窗口布局、收起侧栏、紧凑间距和键盘导航（Ctrl/Cmd + 1/2/3/4）。

AI 解析与连接测试使用明确标注的演示数据，不调用付费 API。解析页使用内置示例，未接收工作台自行导入的图片或新批注。配置仅保存在进程内存，退出后清除；请用占位 Key 体验。这是用于确认界面设计的独立桌面原型，尚未接入既有 C++ 业务后端。

## 开源素材

品牌图标沿用项目已有的 Lobe Icons SVG，来源与固定版本见上级 `features/ai-text/resources/providers/README.md`，MIT 许可在 `public/licenses/lobe-icons-MIT.txt`。
界面图标使用 Lucide React（ISC），许可在 `public/licenses/lucide-ISC.txt`。
Radix UI 和 React 按各自 MIT 许可使用，原始许可随运行文件夹分发。只参考 Magpie 的设计原则，未复制其 GPL 实现。
工作台中的 Morrow 家居设计稿为本预览使用 SVG 绘制的原创示例。
