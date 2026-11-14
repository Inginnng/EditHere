# 桌面界面参考与实施约束

研究日期：2026-10-29。先阅读实际截图与源码，再实现。本文件中的“审美”属于对截图和结构的归纳；技术栈、尺寸、颜色和控件来源可由所列源码核对。

## Magpie

固定参考 [Blinue/Magpie 241e0a4](https://github.com/Blinue/Magpie/tree/241e0a447d1c45fc95781f9cd4b5a9198e738a3b)。

- [窗口代码](https://github.com/Blinue/Magpie/blob/241e0a447d1c45fc95781f9cd4b5a9198e738a3b/src/Magpie/MainWindow.cpp)与[依赖](https://github.com/Blinue/Magpie/blob/241e0a447d1c45fc95781f9cd4b5a9198e738a3b/src/Magpie/packages.config)：C++/WinRT、Win32 DesktopWindowXamlSource、XAML Islands、WinUI 2.8.7。不能写成 WinUI 3。
- [页面框架](https://github.com/Blinue/Magpie/blob/241e0a447d1c45fc95781f9cd4b5a9198e738a3b/src/Magpie/PageFrame.xaml)：28 DIP 半粗标题、40 DIP 页面边距、最大内容宽1000 DIP。
- [设置卡片](https://github.com/Blinue/Magpie/blob/241e0a447d1c45fc95781f9cd4b5a9198e738a3b/src/Magpie/SettingsCard.Resource.xaml)：最小高68、内边距16、20 DIP 图标；统一 SettingsCard / SettingsExpander，沿用成熟控件范式。
- [导航](https://github.com/Blinue/Magpie/blob/241e0a447d1c45fc95781f9cd4b5a9198e738a3b/src/Magpie/RootPage.xaml)：NavigationView，按宽度收起导航；主题资源支持深浅色。
- [实际截图](https://github.com/Blinue/Magpie/blob/241e0a447d1c45fc95781f9cd4b5a9198e738a3b/img/main-window.png)：侧栏与内容同色系，系统强调色仅表示选择和关键操作。淡黄绿色来自 Mica 材质与环境，不应抄成整页暖绿。

审美归纳：Windows Fluent 原生工具感。依靠清晰的系统文字、整齐设置行、稳定控件位置和分组节奏。C++ 并不构成视觉上限。

## CC Switch

固定参考 [farion1231/cc-switch cd9c721](https://github.com/farion1231/cc-switch/tree/cd9c721086fbee36a1d9b1d986a8f3e14ac69147)，版本4.0.21。参考[新版侧栏截图](https://github.com/farion1231/cc-switch/blob/cd9c721086fbee36a1d9b1d986a8f3e14ac69147/assets/release-notes/v4.0.0/main-zh.png)，而不是旧版顶栏截图。

- [依赖](https://github.com/farion1231/cc-switch/blob/cd9c721086fbee36a1d9b1d986a8f3e14ac69147/package.json)：React + TypeScript + Vite + Tailwind；Tauri 2 / Rust 桌面后端；shadcn/Radix 控件。
- [设计系统](https://github.com/farion1231/cc-switch/blob/cd9c721086fbee36a1d9b1d986a8f3e14ac69147/docs/design-system.html)：正文13/20、辅助12/18、卡片名称14/20，控件28/32/36高；圆角6/10/14；4px间距刻度；侧栏200px，收起72px。
- [语义颜色](https://github.com/farion1231/cc-switch/blob/cd9c721086fbee36a1d9b1d986a8f3e14ac69147/src/index.css)：中性侧栏#f5f5f7、正文#18181b、辅助#65656e；橙色用于主操作，连接模式和启用状态有明确颜色语义。
- [ProviderCardActions](https://github.com/farion1231/cc-switch/blob/cd9c721086fbee36a1d9b1d986a8f3e14ac69147/src/components/providers/ProviderCardActions.tsx)：主操作/使用状态占固定位置，后接编辑和更多菜单。
- [AddProviderDialog](https://github.com/farion1231/cc-switch/blob/cd9c721086fbee36a1d9b1d986a8f3e14ac69147/src/components/providers/AddProviderDialog.tsx)：实际采用 FullScreenPanel；先选预设再填表，底部固定取消/提交。主页用来管理配置，不摊开全部字段。

审美归纳：紧凑、可读、状态清晰的桌面生产力工具。边框负责平面分组，阴影只给浮层。信息顺序是名称→状态→动作。

## EditHere 的实施规则

1. 中性灰同色侧栏和内容；移除深绿品牌背景、虚构工作空间、营销口号、英文装饰眉题。
2. 系统字体，正文13px、辅助12px、页面标题24px，输入/按钮32px，4px基础间距；以可读性为先。
3. 单一蓝色强调选择与主要动作。深浅主题使用同一套语义变量，不逐页写不同的灰/绿。
4. 模型服务首页为横向配置卡列表；清楚显示默认配置和缺少Key等真实状态。编辑/新建进入独立面板，高级项折叠，取消与保存固定在面板底部。
5. 偏好设置使用“分组标题→说明→左标签/右控件”的设置行；批注页保持画布、批注列表与底部工具栏。
6. 使用 Radix 的菜单、对话框、开关处理键盘与焦点。采用设计原则，不复制 Magpie GPL 源码，不照搬 CC Switch 的代理/额度/赞助等无关产品功能。
7. 交付独立 Electron Windows 桌面预览，保留0.13.0功能分支。当前阶段验证交互与视觉，不宣称已接入原C++后端或真实API。
