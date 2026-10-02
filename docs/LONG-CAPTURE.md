# 长截图实现参考

本次重新实现纵向长截图，保留现有 Qt 架构。目前面向 Windows；普通截图在其他平台继续使用原有实现。

## 已查阅的开源实现

- [ShareX · ScrollingCaptureManager.cs](https://github.com/ShareX/ShareX/blob/73967140f4fd64ca4b93203ae8ad5ac05ade9aaf/ShareX.ScreenCaptureLib/ScrollingCaptureManager.cs)：采集和拼接逐轮推进，以滚动间隔等待页面，比较连续截图确认到底；匹配时避开两侧边缘及固定底栏，区分完全成功、部分成功和失败。
- [deepin-screen-recorder · pixmergethread.cpp](https://github.com/linuxdeepin/deepin-screen-recorder/blob/88d7a015ea4c043389fe5c7658779ca5aebf7974/src/utils/pixmergethread.cpp)：先检测固定顶底栏，再用 OpenCV 模板匹配定位重叠内容，对不可信的匹配明确报错。

以上是设计和算法原理参考，EditHere 使用自行编写的 Qt/C++ 实现，没有复制这些项目的代码，也没有增加 OpenCV 运行依赖。参考的是上述固定提交，而非会继续变化的主分支。

## 在 EditHere 中的取舍

长截图从用户当前选择的位置向下采集，不自动回到页面顶部。默认手动滚动，程序持续采样稳定画面并自动拼接；停下滚动时继续等待，不自动结束。普通选区窗口隐藏后，独立进度窗口显示实时缩略图、完成和停止按钮，让用户能操作真实页面。每次打开的「自动滚动」开关都关闭，可明确开启后交由程序滚动。

采集选区使用屏幕原生像素，避免逻辑坐标在高缩放显示器上造成错位。页面滚动后连续采样确认停稳，再交给拼接器。手动模式不发送滚轮、不抢回页面焦点，连续没有变化时继续等待；自动模式向开始时确定的同一个窗口发送滚轮，一次没有变化会再确认一次，不能匹配与已经到底分别处理。切换模式会废弃旧轮的待处理回调，避免取消自动后继续滚动。

采集区没有空间放进度窗时，每次采集及投递滚轮前暂时隐藏控件。Chromium 会通过 [`WindowFromPoint` 重新路由滚轮](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/win/mouse_wheel_util.cc)，直接向浏览器发送消息仍可能被覆盖其上的窗口拦住；这一行为通过真实浏览器页面验证。

拼接只追加有可信重叠的新增内容；固定顶栏保留一次、固定底栏放在结果末尾。低纹理和重复图案造成的歧义必须拒绝，不能用历史位移猜测。已拼接部分达到编辑器的像素、边长或帧数上限时停止。没有任何新增内容时不会把首帧当成长截图交付。

## 验证入口

`scrollstitch_tests` 检查拼接结果和拒绝条件；`scroll_controller_tests` 检查鼠标按钮启动、失败后原位置重试、停止、取消和结果交付；Windows 的 `scroll_platform_tests` 使用另一进程的真实窗口检查目标选取、滚轮投递及桌面像素采集。原生控制器用例从 `Controller::capture()` 的全屏截图开始，框选后点击工具栏，而非只测试快捷键。

普通截图的既有 `capture_tests`、`ui_tests` 继续作为相关回归检查。真实桌面的测试不能用 `-platform offscreen` 代替。

浏览器验收可打开 `tests/fixtures/scroll-browser.html`，设置 `H2D_SCROLL_BROWSER_TEST=1` 后运行 `scroll_controller_tests realBrowserProducesCompleteImage`；该用例使用实际 Windows 滚轮和屏幕像素，核对 60 行正文、固定栏及最终尺寸。默认 CTest 不运行需要手动打开浏览器页面的用例。
