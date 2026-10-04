# EditHere · 改这里：问题归档与回归测试

[返回产品介绍](../README.md) · [开发说明](DEVELOPMENT.md) · [更新记录](../CHANGELOG.md)

本页记录项目出现过的**问题**（不是功能清单），并为每一条指定**防止复发的自动化检查**。

- 与 [CHANGELOG](../CHANGELOG.md) 的分工：CHANGELOG 面向用户，写"这个版本变了什么"；本页面向维护者，写"这个问题当时为什么发生、现在由哪条测试盯着"。同一条目两处都会出现，但重点不同。
- 编号 `REG-xxx` 一经分配不再复用，删除条目前先确认对应测试也一并删除。
- 状态列的含义：

| 状态 | 含义 |
| --- | --- |
| ✅ | 已有测试直接覆盖，测试名即为回归护栏 |
| 🆕 | 本次归档时新补的检查（见"新增检查清单"） |
| ⚠️ | 只有部分覆盖，或测试在本机/CI 会被跳过，另有兜底手段 |
| 📄 | 无法用自动化测试固化，只能靠流程与文档（本页已写明原因） |

## 稳定性排查（0.10.0）

本轮以能复现的运行和数据稳定性缺陷为依据。以下测试在 Windows 本地构建执行；macOS/Linux 的平台专属行为仍需各自 CI 或实机验证。

新增 40 个测试场景。最终 Windows 构建的 20 组 CTest 全部通过（51.27 秒），包括真实 OCR、多分带进程回收、Windows 平台与 NSIS 安装事务；真实导出 schema、610 条翻译和 NSIS 实编译检查通过。测试结果保存在 `artifacts/stability-tests.xml`，定向复现与修复后日志保存在 `artifacts/`。

| 编号 | 问题与修复 | 回归检查 | 状态 |
| --- | --- | --- | --- |
| REG-123 | 同中心缩放因中心位移为零被导出和标注过滤；按整个矩形判定变化，缩放使用角点轨迹 | `LayoutTests::centeredResizeSurvivesFeedbackRoundTrip`、`CoreTests::separatelyMovedRegionsKeepTheirOwnTrajectories`、`CanvasFeedbackTests::centeredResizesRemainVisibleAndClickable`（两种画布） | ✅ |
| REG-124 | 源坐标用默认浮点精度构造对象键，临近区域被合并；使用可往返的 17 位精度 | `LayoutTests::feedbackKeepsDistinctFractionalSourceRegions` | ✅ |
| REG-125 | 对象反馈的越界/反向/单轴零尺寸源被裁剪或当作点，点对象的异常移动被忽略；重建前检查全部几何 | `CoreTests::objectFeedbackRejectsInvalidGeometry`（8 行） | ✅ |
| REG-126 | 对象数被误当作批注数，1000 条批注加移动的合法导出无法重开；分别限制累计批注和移动，并同步 schema | `CoreTests::objectFeedbackLimitsCountAnnotationsAndMovementsSeparately`、`validate-exports.py` 的真实 1001 对象样本 | ✅ |
| REG-127 | 顺序移动最终位置已更新，批注却取第一项目标；批注与最后一次目标保持一致 | `CoreTests::objectFeedbackNotesUseTheFinalDestination` | ✅ |
| REG-128 | 超过 511 个旧变化重新导入时被可选组上限阻断；批量重建保留全部轨迹，限制可选组及临时引用数量 | `LayoutTests::feedbackKeepsChangesBeyondTheSelectableGroupLimit`（512 个变化，导出、像素和完整布局往返）、`objectReconstructionBoundsTemporaryMembership`（重复/密集重叠/空目标） | ✅ |
| REG-129 | Windows OCR 只处理 finished，启动失败后请求不返回；补启动失败/超时完成路径与当前进程身份检查，回收每个已结束分带进程 | `OcrTests::missingHelperReportsFailureAndStopsBeingBusy`（修复前回调数为 0）、`completedBandsLeaveNoHelperObjects`（真实多分带 OCR） | ✅ |
| REG-130 | 复用 OCR 窗口识别新图时预览/语言仍是旧值；统一替换输入并清除旧文字和高亮 | `OcrTests::aReusedDialogShowsOnlyTheNewPictureAndLanguage`（预览中心像素、文字列表和语言） | ✅ |
| REG-131 | 切换历史图片后旧检测 worker 把屏幕区域覆盖到新图；重新检测并检查代次，清理旧 native probe | `UiTests::historySwitchDoesNotUseDetectionFromThePreviousPicture`（修复前空白图仍选中旧表格区域；返回实时截图重新检测） | ✅ |
| REG-132 | legacy 桌面 socket 只在 EOF 读取，缓冲满时发送和接收互相等待，空闲连接不释放；持续消费、限制 64 KiB、5 秒截止，处理回调注册前的 EOF | `AgentCliTests::desktopRequestsConsumeFragmentsAndWaitForEof`、`desktopRequestAlreadyAtEofIsNotLost`、`invalidDesktopRequestsAreDiscardedAndNextClientWorks`；另验证 6 类 Agent 协议错误后下一连接恢复 | ✅ |
| REG-133 | MCP 逐块转字符串破坏 UTF-8；仅凭成功 JSON 忽略退出码/超时；无限收集输出和宿主断开后遗留 CLI；畸形 JSON 无回应 | `tests/connector_test.mjs`（6 例，真实 Node 子进程与 stdio；修复前全部失败），注册为 CTest `connector` | ✅ |
| REG-134 | 设置只保存截图样式数值，边框/阴影颜色及透明度在重启后丢失；新增 ARGB 持久化，缺失/异常值回落默认 | `SettingsTests::captureStyleColoursSurviveRestart`（修复前失败，修复后通过） | ✅ |
| REG-135 | 更新测试在 CTest 中超时，直接写结果文件却已完成；测试刻意启动损坏 EXE 会触发 Windows 加载器行为。改成确认期间移除已验证文件，仍验证真实启动失败 | `UpdateTests::cancellationAndLaunchFailureNeverAnnounceInstallation`；修改前 CTest 两次超时，修改后通过 | ✅ |
| REG-136 | 取消 OCR 时先删临时目录，辅助进程还在写日志，残留目录且延迟销毁运行中的 QProcess；终止后有限等待，再释放临时文件 | `OcrTests::cancellingAnActiveHelperDoesNotAnswerAnEarlierRequest`（旧回调不再回答，辅助进程和目录均释放） | ✅ |

## 长截图点击修复（0.10.0）

| 编号 | 问题与修复 | 回归检查 | 状态 |
| --- | --- | --- | --- |
| REG-137 | 长截图启动失败或没有滚动时，提示插入操作栏使按钮移位；再次点击原位置可能命中保存等其他动作。失败提示改为独立区域，操作栏尺寸和按钮位置保持不变 | `ScrollControllerTests::toolbarMouseClickStartsScrollingWithoutSaving`、`failedStartupKeepsMouseTargetForRetry`、`noMovementReturnsSelectionWithoutCreatingLongScreenshot`；修复前按钮中心从 `(424,32)` 移至 `(810,32)`。原生 `realScrollingWindowProducesCompleteImage` 从生产全屏截图入口框选并点击按钮，核对完整结果且无保存信号 | ✅ |
| REG-138 | 长截图默认自动滚动、自动结束，用户无法自己选择滚动范围。改为每次默认手动滚动、自动拼接，稳定画面不动时持续等待；点击完成才交付，可勾选自动滚动。手动连续滚动不因等待稳定超时而停止，模式切换废弃过期回调，采样不抢页面焦点 | `ScrollControllerTests::manualCaptureWaitsForUserWithoutScrollingOrCompleting`、`manualContinuousMovementWaitsForPauseWhileCaptureKeepsResponding`、`manualCaptureFinishesWhileRunningAndRejectsLateFrames`、`automaticChoiceCancelsPendingManualFrameAndCanReturnToManual`、`manualCaptureRequiresOriginalWindowAndStopsBeforeCapturingAnother`；原生窗口及浏览器用例由独立滚轮驱动页面，断言控制器自身滚轮次数为 0，最后点击完成 | ✅ |
| REG-139 | 设置、保存、长截图等图标不直观或彼此相似。设置改为八齿齿轮，保存统一为软盘，长截图改为纵向取景框和长度箭头，打开使用文件夹，文字识别使用取景框内的 T；适应窗口与截图区分，修正智能选择四角和复制图片边界 | `icon_gallery` 渲染全部 39 种图标及真实截图工具栏、手动/自动/停止进度窗，人工检查浅色、深色及 20/24 px。实际预览曾发现齿轮外圈未绘制，修复首点 `moveTo` 后再次确认 | ⚠️ |

## 视频播放交互修复（0.10.0）

浅色、深色两组使用真实视频解码的原生 Windows 用例均已通过。需设置 `EDITHERE_VIDEO_TEST_SOURCE` 才会执行解码断言；未提供视频时会跳过，因此验收需核对两组均为 PASS。macOS/Linux 的原生视频表面仍需各自验证。

| 编号 | 问题与修复 | 回归检查 | 状态 |
| --- | --- | --- | --- |
| REG-140 | 暂停中点击 `±1s` 时，定位所需的后台解码被显示成用户播放，按钮文字变长使控件移位；播放与暂停使用不同显示区域，留白背景也不一致。区分后台定位与用户播放，固定控件宽度，视频和画布叠放在同一显示区域；定位后暂停供批注，连续步进累计目标时间，同分辨率换帧保留缩放、平移与侧栏状态 | `VideoUiTests::playbackViewportAndSeekRemainStable(light)`、`playbackViewportAndSeekRemainStable(dark)`，注册为 CTest `video-ui`；定位全过程采样按钮、时间轴、视口与画布几何，核对暂停按钮文字、缩放/平移及折叠侧栏不变；播放/暂停留白像素分别为浅色 `#efeff2`、深色 `#18191e`，连续步进累计目标，播放中定位后暂停且空格可继续播放。原生 Windows 两组 PASS 见 `artifacts/video-0100-native-test.log` | ✅ |
| REG-141 | 普通暂停和定位立即转换为截图画布，单纯查看视频也创建帧，视频表面切换还造成停顿。`0.10.0-pause-update` 改为首帧、暂停和定位只停住视频表面，首次实际点、框或全局批注才保存截图；选择模式、缩放和平移不截图，保存项目/导出 JSON 不创建新帧，选择已有批注帧直接恢复截图 | `VideoUiTests::playbackViewportAndSeekRemainStable(light/dark)`、`firstAnnotationCapturesOnlyOnce(point/rectangle/global/global-shortcut)` 和 `playbackSeekAnnotateAndReopen`；普通暂停/定位截图计数为零，视频持续可见，首次批注只捕获一次且坐标、PTS 与画面匹配，同帧后续批注复用截图，保存和实际 JSON 复制不创建新帧。点选通过 Windows QWindow 命中测试。原生 Windows 共 10 PASS、0 FAIL、0 SKIP，见 `artifacts/video-pause-update-native-test.log`；截图仅证实 Qt 控件渲染，未验证 GPU 桌面合成截图 | ✅ |

## 长截图与视频批注修复（0.10.0）

| 编号 | 问题与修复 | 回归检查 | 状态 |
| --- | --- | --- | --- |
| REG-142 | 真实网页手动连续滚动时，每 120 ms 只在画面停稳后才采样，相邻样本早已没有重叠，第一次匹配就提示“无法匹配相邻画面”并停止。手动模式改为 60 ms 采样，滚动中的样本只要与已拼接部分精确重叠（误差 ≤ 1）就立即追加，停稳后再确认；仍然断开时不停止，提示稍微往回滚动，对上后继续。进度窗不被采到时不再每次采样都隐藏/显示，预览改为可滚动的整张缩略长图并跟随最新内容 | `ScrollStitchTests::movingSamplesAppendOnlyExactContinuations`、`planDoesNotChangeStitchUntilCommitted`、`livePreviewKeepsWholeResultAtPanelWidth`；`ScrollControllerTests::manualScrollingStitchesInMotionAndRecoversFromAGap`（禁用滚动中拼接后失败）。真实 Edge 长页面在 1 格/300 ms 至 3 格/100 ms 的连续滚轮下拼出完整 14550 px 结果；原 `realBrowserProducesCompleteImage` 连续 3 次通过 | ✅ |
| REG-143 | 视频暂停后无法批注：`QVideoWidget` 在 Windows 上创建原生子窗口，连带把图像区与画布变成原生窗口，画布绘制的框不再出现在屏幕上（真实鼠标拖框时屏幕像素零变化，数据里却有批注）；视频每帧在主线程转换也让操作卡顿。改用自绘视频表面，帧在工作线程转换并缩放到显示尺寸，新帧覆盖未处理的旧帧；首次批注保存截图改用不透明 PNG 快速编码（1080p 约 114 ms → 63 ms，仍为无损） | `VideoUiTests::playbackSeekAnnotateAndReopen` 断言画布与视频表面均无原生窗口句柄；真实 `SendInput` 鼠标在原生窗口播放、暂停、拖框，拖动中屏幕截图可见虚线框 | ✅ |
| REG-144 | 时间轴上的橙色（用户称黄色）批注标记只能看不能点。悬停时变为手形并提示时间与批注数，点击直接打开该帧截图与批注，并同步“已标注画面”列表；其余位置仍为定位 | `VideoUiTests::playbackSeekAnnotateAndReopen`：离开批注帧后悬停/点击标记，核对图片、批注文字、列表选中项且不新建截图（禁用标记点击后失败）；真实鼠标点击标记同样回到对应批注 | ✅ |

## 状态冲突排查与诊断日志（0.10.0）

| 编号 | 问题与修复 | 回归检查 | 状态 |
| --- | --- | --- | --- |
| REG-145 | 长截图准备回调未返回时切换方向会废弃当前准备会话；准备期间仅更新方向，取得目标窗口后再采样 | `ScrollControllerTests::directionChangedDuringPreparationKeepsThePendingSession` | ✅ |
| REG-146 | 普通截图取消后重开，仅检查 capturing 无法区分旧回调；用会话代次及弱引用拒绝旧 prepare/read 结果 | `cancelledScreenPreparationCannotStartTheNextCapturesRead`、`cancelledScreenReadCannotPopulateTheNextCapturesOverlays` | ✅ |
| REG-147 | 贴图的批注直接替换编辑器，覆盖未保存内容或 AI 会话；统一检查捕获、模态及 allowReplace | `pinnedAnnotationHonorsCancelAndDiscardOfUnsavedChanges`、`pinnedAnnotationCannotReplaceAnActiveAgentDocument`、`pinnedAnnotationCannotReplaceDuringCaptureOrAnotherDialog` | ✅ |
| REG-148 | 视频表面 clear 后后台转换仍把旧图写回；转换结果只在代次一致时显示 | `VideoUiTests::clearingVideoDoesNotRestoreAnInFlightPreview` | ✅ |
| REG-149 | 视频进入预览后 doc 为空，status 仅检查 doc dirty 会漏报工程修改；改用 hasUnsavedChanges | `AgentCliTests::statusRetainsVideoDirtyStateWhilePreviewHasNoDocument` | ✅ |
| REG-150 | 拖入空图或超限图片未捕获 fromImage 异常；与打开、粘贴入口一致，显示错误并保留当前文档 | `UiTests::droppingAnInvalidImagePreservesTheExistingDocument`（空图、超限边长） | ✅ |

日志核心为 `app/diagnostics.h/.cpp`，独立 QtCore 静态库；界面为 `app/diagnosticspage.h/.cpp`。`diagnostics_tests` 覆盖并发完整性、文件轮换、Qt handler 恢复、脱敏、原子导出、写入失败、实际报告大小及活跃/强制退出进程；固定修改时间的 999/1000/1001 分片验证稳定的数值排序，避免平台文件时间精度导致结束标记和清理顺序错误。`SettingsTests::diagnosticsAreAccessibleWithoutChangingSettings` 从设置导出报告，`i18n_tests` 验证中英文切换。日志只收集诊断信息；未实现原生崩溃转储，未正常结束的会话标记不能替代崩溃栈。完整证据见 `artifacts/diagnostics-audit/README.md`。

## 英文文档与发布页（0.10.0）

英文材料此前只有 `README.en.md` 一份：更新记录、使用说明与发布页均只有中文，英文 README 指向它们时还要标 `(Chinese)`。本次为面向使用者的七份文档补出英文版，并让发布页在同一个页面同时给出两种语言。英文文档不参与构建，缺了不会有任何编译或测试报错，因此每一条都由静态检查盯着。

| 编号 | 问题与修复 | 回归检查 | 状态 |
| --- | --- | --- | --- |
| REG-151 | 中英文文档之间没有互相指向的入口，英文页即使存在也无从到达；两份文件各加一行语言切换，`README.en.md` 不再把英文文档标成 `(Chinese)`，且必须链接到每一份英文页 | `check-packaging.py` 的 `translated_docs`：逐对检查文件存在、双向链接，并核对 `README.en.md` 的链接 | 🆕 |
| REG-152 | 发布说明只有中文，英文读者在发布页看不到本版变化；新增 `docs/releases/<版本>.en.md`，由 `release.yml` 折叠进同一个发布正文 | 同上 `translated_docs` 检查当前版本的中英两份发布说明是否都在 | 🆕 |
| REG-153 | `release.yml` 的发布正文可能退回单语而无人发现（发布页是外部可见的最终产物） | `check-packaging.py` 的 `release_notes`：要求工作流仍读取 `docs/releases/%s.en.md` 并写出 `<summary>English</summary>` 折叠段 | 🆕 |
| REG-154 | 长截图在英文界面里有两个名字：工具栏、托盘菜单、设置页与进度窗标题写 "Scrolling capture"，另有 3 条提示写 "long capture"；统一为 "Scrolling capture"（`超长截图` 的 "Ultra-long capture" 是另一个功能，不受影响） | `check-packaging.py` 的 `english_names`：逐条检查含 `长截图` 且不含 `超长` 的源串，其译文不得只写 "long capture"；并要求 `README.en.md` 保留 "Scrolling capture" | 🆕 |
| REG-155 | 「完成并返回 AI」的英文在界面里是 "Done, back to AI"，`README.en.md` 引用同一个按钮却写 "Finish and return to AI"；统一为 "Finish and return to AI" | `check-packaging.py` 的 `english_names`：核对 `完成并返回 AI` 的译文与 `README.en.md` 引用的按钮名一致 | 🆕 |
| REG-156 | 中英两份文档互相链接，多份页面还直接链到别的页面里的某个标题；标题改了措辞或文件名写错，读者的语言切换入口就断了，而没有任何测试会报错（本次就出现两处：改标题为 "Recognition and platform limits" 而引用方指向 `…-boundaries`，以及 `AI-SETUP` 指向 `#windows-agent-desktop-access` 而标题实为 "Desktop access for Windows agents"） | `check-packaging.py` 的 `doc_links`：解析全仓 `*.md` 的相对链接与锚点，锚点必须能在目标页的标题里找到，链接目标必须存在（构建产物与 `artifacts/` 下的再生证据除外） | 🆕 |

## 本次更新修复（0.9.9）

Linux 专项 `linux_platform_tests` 在独立 DBus 会话中覆盖开机启动路径转义、Portal 截图成功/取消/无效 URI、快捷键注册/激活/取消/缺失服务；Xvfb 下覆盖 X11 截图及快捷键冲突与真实按键。`OcrTests::tesseractLinesKeepBandCoordinates` 覆盖 TSV 行合并、坐标还原和无效结果，已有 OCR 运行用例验证真实 Tesseract。实际 GNOME/KDE 授权与托盘仍需实机验收。

批注图片按统一展示宽度排版，避免高分辨率原图导致批注过小；`CoreTests::previewLayoutIsIndependentOfSourceResolution` 对比同一内容的 1 倍与 4 倍分辨率，验证图片区域和批注正文布局一致，并保留项目坐标。

| 编号 | 问题与修复 | 回归检查 | 状态 |
| --- | --- | --- | --- |
| REG-116 | 设置保存丢失截图样式；草稿保留圆角、阴影和边框 | `settings_test::savingSettingsPreservesRememberedCaptureStyle` | ✅ |
| REG-117 | 空校验值跳过验证或错配校验文件；强制合法 SHA-256 与同名资产 | `update_test::hashMustBePresentWellFormedAndMatch`、`hashAssetMustBelongToSelectedPackage` | ✅ |
| REG-118 | 取消确认或安装器启动失败仍退出；确认置于启动前并检查启动结果 | `update_test::cancellationAndLaunchFailureNeverAnnounceInstallation`；编辑器保存确认仍依赖现有 UI 测试，完整交互需人工验收 | ⚠️ |
| REG-119 | macOS 误选 Windows 包、旧安装器不支持便携更新 | `update_test::automaticUpdatesRespectPlatformAndInstallerCapabilities` | ✅ |
| REG-120 | 逐文件覆盖失败留下混合版本；NSIS 暂存、目录切换与回滚 | `installer_transaction_test.py` 四种隔离安装场景，需要真实 NSIS | ⚠️ |
| REG-121 | 升级参数被启动选项读取覆盖、PATH 选择丢失、误检查其他目录实例 | `check-packaging.py` 编译检查；注册表选项保留尚需安装版实机验收 | ⚠️ |
| REG-122 | 0.9.9 的发布页上只有 Windows 与 macOS 三个包，Linux 包不存在 | 标签触发的 `release.yml` 只等 `build-windows` 与 `build-macos` 两个构建，`native-linux.yml` 又只有 `workflow_dispatch` 与 `pull_request` 触发——打标签时没有任何任务会去构建 AppImage。发布说明里"提供 AppImage"那句因此成了一句空话 | `native-linux.yml` 增加 `workflow_call`，`release.yml` 新增 `build-linux` 任务调用它，`needs` 与"每包齐全"护栏一并纳入 AppImage，发布资产与 `SHA256SUMS.txt` 补上 AppImage 与 `edithere-cli`；包与构建日志拆成两个 artefact 上传，好让包解压后落在归档根目录 | `check-packaging.py` 的 `release_assets` 检查：Linux 工作流必须可被调用、必须收集 AppImage 与 CLI、`release.yml` 必须把 AppImage 同时列进"必须有"清单与发布资产 | ✅ |

## 一、安装、升级与打包

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-001 | 免安装版点"立即更新"后版本不变，程序文件没被替换 | 0.9.0 → 0.9.2 | `Compress-Archive -LiteralPath <目录>` 把顶层目录一起压进 ZIP，更新脚本 `tar -xf` 后文件落在嵌套子目录 | `scripts/check-packaging.py::portable_zip_flat`（含已有 `dist/*.zip` 时校验顶层必须有 `EditHere.exe`） | 🆕 |
| REG-002 | 免安装版更新报"未找到校验文件，无法验证安装包完整性" | 0.9.0 → 0.9.1 | 发布资产只对有安装器生成的 `exe.sha256`，没有 `zip.sha256`，而便携版更新强制要求校验文件 | `check-packaging.py::release_assets`（断言为 `*.zip` 与 `*.exe` 都生成同名 `.sha256`） | 🆕 |
| REG-003 | 同一版本第二次点"立即更新"报"校验失败：安装包已损坏或不完整" | 0.9.0 → 0.9.2 | 下载用 `QIODevice::Append` 写入固定临时路径，且开始前不清理旧文件，新数据追加在旧包后面 | `tests/update_test.cpp::staleDownloadIsDroppedBeforeAppending`；`check-packaging.py::stale_download` | 🆕 |
| REG-004 | 应用内"立即更新"后程序关掉了、更新却没装上，且因为静默安装看不到任何提示 | 0.9.0 → 0.9.4 | 应用先拉起安装器、之后才 `quit()`，安装器进 Section 就检查"是否在运行"并直接 Abort，两者竞态 | `check-packaging.py::installer_wait`（断言静默分支会等待、失败时重新拉起应用）；`tests/agent_cli_test.cpp` 的单实例/socket 用例保证退出请求本身可用 | 🆕 |
| REG-005 | 安装或卸载时必须由用户手动从托盘退出 EditHere | 0.8.20 → 0.9.4 | 安装器只做静态检查 + 中止，没有"请实例自己退出再等待"的环节 | `check-packaging.py::installer_wait` + `quit_capability_pair` | 🆕 |
| REG-006 | 安装/卸载时报"意外的标记 'Check'""Try 语句缺少自己的 Catch 或 Finally 块"，0.9.2 安装包完全无法安装 | 0.9.2 → 0.9.3 | `integrate.ps1` 以**不带 BOM** 的 UTF-8 保存，Windows PowerShell 5.1 按 ANSI/GBK 解码，中文字符串吞掉闭合引号导致解析错位 | `check-packaging.py::ps1_utf8_bom`（所有含非 ASCII 的 `.ps1` 必须以 `EF BB BF` 开头） | 🆕 |
| REG-007 | 打不出 Windows 安装包 | 0.9.0 → 0.9.1 | `edithere.nsi` 用了 `${IfNotErrors}`，NSIS LogicLib 从无此宏（正确写法 `${IfNot} ${Errors}`）；该文件此前从未被 makensis 编译过 | `check-packaging.py::nsi_source`（静态断言）+ 同脚本在能找到 `makensis` 时以 `/WX` 实编译 | 🆕 |
| REG-008 | 通过命令行请一个**旧版** EditHere 退出，结果在用户屏幕上触发了一次截图 | 0.9.4 开发期踩到 | `--quit`（客户端）与 socket `quit`（服务端）都是 0.9.4 引入；旧版把未知参数当普通启动 → 取不到锁 → 向运行中的实例写 `capture` | `check-packaging.py::quit_capability_pair`（两侧能力必须同时存在；`$quitRequestSince` 不得高于当前版本） | 🆕 |
| REG-009 | CI 里 aqtinstall 找不到 MinGW，构建失败 | 0.9.0 → 0.9.1 | aqtinstall 3.x 把工具分类改名为 `tools_mingw1310`、变体改为 `qt.tools.win64_mingw1310` | `check-packaging.py::ci_toolchain` | 🆕 |
| REG-010 | CI 打包步骤失败 | 0.9.0 → 0.9.1 | runner 上没有 NSIS，`package-installer.ps1` 找不到 `makensis.exe` | `check-packaging.py::ci_toolchain`（断言发布工作流先 `choco install nsis`） | 🆕 |

## 二、自动更新

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-011 | 发布说明只有一行 `**Full Changelog**: v0.9.2...v0.9.3`，修改日志没出来 | 0.9.0 → 0.9.4 | 工作流只用 `generate_release_notes`，它汇总的是**已合并的 PR**，而本项目直接推 commit | `check-packaging.py::release_notes`（断言 CHANGELOG 提取步骤 + `body_path` + 版本标题以版本号开头） | 🆕 |
| REG-012 | 删除旧 Release 后，文档里的下载链接变成死链 | 未发布 | 资产名与文档下载链接都写死了版本号（资产名带版本、链接指向具体 tag 而不是 `latest`） | `check-packaging.py::release_assets` + `download_links` | 🆕 |
| REG-013 | 改成无版本号资产名后，旧客户端再也认不出更新包 | 未发布（0.9.4 发布时预留） | 客户端只按带版本号的名字匹配资产 | `tests/update_test.cpp::resolvesPackagesWithAndWithoutVersionInName`（已有）；`check-packaging.py::release_assets` 断言两种命名并存策略仍在 | ✅ |
| REG-014 | 网络超时、私有仓库无权限、未发布被当成"已是最新版" | 0.8.3 | 失败路径没有与"无更新"区分 | `tests/update_test.cpp::malformedOrInaccessibleNeverMeansCurrent` | ✅ |
| REG-015 | 版本比较把 `0.8.10` 判成小于 `0.8.9` | 0.8.3 | 按字符串比较版本号 | `tests/update_test.cpp::numericOrdering` | ✅ |
| REG-016 | 测试版/草稿/预发布被当成正式更新提示 | 0.8.3 | 未过滤 `prerelease`/`draft` | `tests/update_test.cpp::rejectsUntrustedAndNonStableReleases` | ✅ |
| REG-017 | 免安装版在用户机器上无法用 HTTPS 检查更新 | 0.8.3 | 便携包未带 TLS 后端 | `tests/update_test.cpp::httpsBackendAvailable` | ✅ |

## 三、多语言

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-018 | 中文界面下文件对话框与消息框按钮仍是英文 | 0.8.x → 未发布 | 打包脚本用 `windeployqt --no-translations` 排除了 Qt 自带翻译，且从未单独补进去 | `tests/i18n_test.cpp::qtOwnStringsFollowTheInterfaceLanguage`（部署目录齐备时运行）；`check-packaging.py::qt_translations` | ⚠️ |
| REG-019 | 部署包里查不到 Qt 中文翻译，即使文件就在旁边 | 未发布 | `windeployqt` 把 `qtbase_zh_CN.qm` **改名**成 `qt_zh_CN.qm`，代码只查前者 | `check-packaging.py::qt_translations`（断言 `qtbase_*` → `qt_*` 依次尝试）；运行时由 `i18n_test` 覆盖 | 🆕 |
| REG-020 | 中文界面下 Qt 自带文案始终缺失，且打包脚本直接中止 | 未发布 | `qttranslations` 是 Qt 仓库里**独立归档**，`--archives qtbase qtimageformats` 装不到它 | `check-packaging.py::qt_translations`（断言三个工作流的 `--archives` 都含 `qttranslations`） | 🆕 |
| REG-021 | 新增文案在英文界面**永远**翻译不到，且没有任何报错 | 未发布 | `lupdate` 按最内层类/命名空间推断上下文（`h2d::WindowsRunValueStore`），运行期按名字查找却命中文件内 `tr` 转发（`h2d`），两边不一致 | `scripts/check-translations.py`（本次接入 CTest：`translations`）；`tests/i18n_test.cpp::switchingLanguagesReachesEveryContext` | 🆕 |
| REG-022 | 改了 `.ts` 忘记 `lrelease`，CI 用的精简 Qt 回退到仓库里那份旧 `.qm`，界面文案没变 | 未发布 | `.qm` 是提交进仓库的预编译产物，不是构建时生成的 | `check-packaging.py::translations_in_qm`（逐条断言 `.ts` 的原文与译文都出现在 `.qm` 里，不需要 Linguist 工具，CI 可跑） | 🆕 |
| REG-023 | 同一功能在英文文档里叫 `Explode`、在界面上叫 `Exploded view` | 未发布 | 界面沿用工程制图术语，未与 README 和代码标识（`explodeButton`、快捷键 `E`）对齐 | `tests/i18n_test.cpp::oneFeatureKeepsOneEnglishName`（钉住 4 条译名）；`check-packaging.py::english_names`（README 与 `.ts` 两侧都不许出现 `Exploded view`） | ✅ |
| REG-024 | 从英文切回简体中文时界面不重建，停留在英文 | 未发布 | Qt 只在**安装**翻译时补发 `QEvent::LanguageChange`，切回源语言不安装任何翻译，什么都不发 | `tests/i18n_test.cpp::switchingLanguagesReachesEveryContext`（断言切回中文后对话框标题变回中文） | ✅ |
| REG-025 | 面向 Agent 的中文区域标签被当成界面文案翻译，导出 JSON 随用户语言漂移 | 未发布 | `色块区域`/`整个图片` 是写进反馈 JSON 的契约值，不能当作普通文案 | `tests/i18n_test.cpp::simplifiedChineseIsTheSourceLanguage`；`check-translations.py` 的上下文白名单 | ✅ |
| REG-026 | 英文界面出现多余或缺失的空格：`无法%1当前用户的开机自启项`、`项目缺少字段：` + 变量两段拼接 | 未发布 | 用"前缀 + 变量"拼句子，中文读不通，英文必然出错 | `tests/autostart_test.cpp::startupNoticesExplainRepairAndSystemDisableSeparately`；`tests/i18n_test.cpp::brokenProjectMessagesStayOneString` | 🆕 |

## 四、界面与交互

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-027 | 没有批注时"点击画面添加批注"的空提示反复出现/闪烁 | 0.8.9 → 0.8.10 | 提示控件在每次刷新时销毁重建 | `tests/ui_test.cpp::emptyNoteHintSurvivesSynchronousDocumentChanges`、`emptyNoteHintReturnsAfterDraftCancellationAndLastDeletion` | ✅ |
| REG-028 | 编辑批注时按 Esc 被窗口关闭快捷键抢占，直接关掉了窗口 | 0.8.9 → 0.8.10 | 快捷键优先级未按"引导 → 批注 → 全屏 → 关闭"排列 | `tests/ui_test.cpp::fullscreenPreservesTheEditingSession`（含 Esc 优先级断言）、`tests/guide_test.cpp::navigationEscapeAndCompletionDoNotCloseDocument` | ✅ |
| REG-029 | 横向滚动或零增量滚轮事件把选区误缩小 | 0.8.0 → 0.8.1 | 未过滤 `angleDelta().y() == 0` 与横向滚轮 | `tests/ui_test.cpp::canvasIgnoresHorizontalAndZeroWheelEvents` | ✅ |
| REG-030 | Windows 上窗口最大化时点"全屏"按钮不生效（普通 QWidget 正常，只有 Editor 有问题） | 0.9.0 | 最大化状态下直接 `showFullScreen()` 不改变窗口状态 | `tests/ui_test.cpp::fullscreenPreservesTheEditingSession`（`_data` 覆盖 maximized 行，并断言退出全屏后回到最大化） | ✅ |
| REG-031 | 从系统托盘图标触发截图时，托盘弹出面板被一起截进图 | 0.9.0 | 截图前未先关闭瞬态窗口 | `tests/platform_test.cpp::preparationDismissesTransientWindowBeforeCapture` | ⚠️ 需要真实桌面，`platform_tests` 在本机与 CI 的跳过策略见开发说明 |
| REG-032 | 中键平移被限制在图片边界内，小图或缩放后无法自由拖动 | 0.8.11 → 0.8.12 | 平移量被钳制在图像矩形上 | `tests/canvas_feedback_test.cpp::middleDragPansWithoutEditing`、`tests/ui_test.cpp::middleDraggingPansTheImageWithoutEditing` | ✅ |
| REG-033 | 中键拖动中窗口失去焦点后拖动状态未结束，鼠标一动就继续平移 | 0.8.10 → 0.8.11 | 未处理失焦事件 | `tests/canvas_feedback_test.cpp::middleDragCancellationDoesNotStick` | ✅ |
| REG-034 | 缩放后鼠标下方的像素发生漂移，平移过或图片小于窗口时尤其明显 | 0.8.11 → 0.8.12 | 缩放锚点用了视口中心而非指针位置 | `tests/ui_test.cpp::wheelZoomKeepsThePixelUnderThePointer` | ✅ |
| REG-035 | 保存文件失败只提示"保存失败"，无法定位是权限还是被占用 | 0.9.0 | 错误信息未带 QQFile 的 `errorString()` 与目标路径 | `tests/settings_test.cpp::failedWriteReportsError`（设置写入路径）；导出路径由 `invalidExportKeepsUnsavedWork` 覆盖 | ⚠️ |
| REG-036 | 过期的一次异步识别结果覆盖了当前窗口的候选 | 0.8.16 → 0.8.17 | 异步结果未按窗口顺序/请求代次校验 | `tests/startup_flow_test.cpp::overlappingWindowsNeverSelectBehindFront` | ✅ |
| REG-115 | macOS 构建**间歇性**失败（近 20 次里 7 次），失败的永远是 `ui` 测试，以 `SIGSEGV`（地址 `0x70`）结束，日志里紧挨着 `QProcess: Destroyed while process ("…/ui_tests") is still running` | 早已存在 → 未发布 | `Overlay` 用 `new QProcess(this)` 起"看指针下是什么元素"的子进程（把**自己**当子进程再跑一次：`--inspect x y pid`），但 `Overlay` 没有析构函数。窗口销毁时 `QObject` 最后才销毁子对象，而该子进程的 `finished` 回调会读写 `debounce_`、`picker_` 并调用 `update()` —— 这些成员那时**已经析构**，回调踩到已释放内存。macOS 运行器上子进程（整套 ui_tests）更慢，正好撞进这个时间窗；本机与 Windows 时序躲开了 | **无法在套件里固化**：要复现就得让"探测子进程仍在跑时销毁窗口"，而测试进程里这个子进程只在特定时序（方块检索未完成时鼠标移动）才起得来——本机沙箱起不了子进程，运行器上按键又落在另一条分支。改为看 macOS 构建本身：修复后连跑 3 次全绿（此前 20 次里 7 次失败） | 🆕 |
| REG-114 | 批注窗口底部工具条里带文字的按钮文字被截断（「查看 JSON」「复制 JSON 文件」「复制 JSON 内容」「复制并带批注图片」） | 0.9.7 → 0.9.8 | 按钮宽度按"文字宽度 + 40"固定，没算样式里的 `padding:7px 13px`、1px 边框和图标占位；而且这个尺寸设在 `unpolish/polish` 之前，样式生效后的实际内容区更窄。0.9.7 换上更长的按钮文字后才露出来 | `tests/ui_test.cpp::toolbarPreferencesKeepCoreToolsAndSettingsReachable`（按 `SE_PushButtonContents` 对比"文字 + 图标 + 间距"所需的宽度）；反向验证：换回旧算法即在 `exportJson` 上失败 | 🆕 |

## 五、大爆炸与布局

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-037 | 选中并移动一个块后，其他块的蓝色引导框全部消失 | 0.9.0 | 绘制引导框时把"有选中项"当成了"不画引导框"的条件 | `tests/canvas_feedback_test.cpp::layoutGuidesStayVisibleForUnselectedComponents` | 🆕 |
| REG-038 | 嵌套块被移出父组后仍跟随父组移动 | 0.9.0 | 移出父组时未立即解除父子关系 | `tests/layout_test.cpp::movedChildStaysIndependentOfLaterParentTransforms` | ✅ |
| REG-039 | 整体移动大块只显示一条轨迹；单独调整小块时不增加它自己的轨迹 | 0.8.9 → 0.8.10 | 轨迹按像素分区而非按实际调整的选区记录 | `tests/layout_test.cpp::trajectoriesTrackSelectedGroupsInsteadOfNestedPixelPartitions`、`tests/core_test.cpp::separatelyMovedRegionsKeepTheirOwnTrajectories` | ✅ |
| REG-040 | 大爆炸拖动过程中窗口失焦，未提交的操作被保留下来 | 0.8.0 → 0.8.1 | 失焦时未取消进行中的手势 | `tests/ui_test.cpp::explosionCancelsInterruptedGesturesAndPreservesSelectionOnFocusChange` | ✅ |
| REG-041 | 嵌套移动的箭头与编号关系错乱；同一区域反复调整生成多条轨迹 | 0.8.10 | 合并规则按像素级分区而不是按跟踪组 | `tests/canvas_feedback_test.cpp::nestedMovementsKeepOneArrowPerSelectedComponent`、`mergedMovementKeepsAdditionalNoteBadgesEditable`、`tests/layout_test.cpp::minimalChangesMergeAParentAndKeepNestedEditsSeparate` | ✅ |
| REG-112 | 按 `project-v3.schema.json` 或 `feedback-v2.schema.json` 校验程序自己导出的项目，会被判定为非法 | 0.9.0 → 未发布 | 0.9.0 让"手动切分"的区域在创建时记下成员的原始范围（`addLayoutRegion` 写入 `sourceBounds`，目的是之后移除成员也不会带动这块区域），但两份 schema 仍写着"手动区域必须 `originalRectangle: null`"，比 `validateLayout` 和导出器都更严；当时没有地方拿真实导出比对 schema，所以一直没暴露 | `scripts/check-packaging.py::layout_schema`（写入器仍记原始范围时，schema 不得强制手动区域为 `null`）；`scripts/validate-exports.py` 在 macOS 构建里校验真实导出（含新增的 `project-v3.json` 样本） | 🆕 |

## 六、检测与导出

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-042 | 4K 浅灰 18×18 密集网格得到**零**候选 | 0.8.1 → 0.8.2 | 线条检测前把图缩到 1100 像素，细线在缩放中被抹平 | `tests/detector_test.cpp::nativeResolutionAndDenseCells` | ✅ |
| REG-043 | 浅灰细线、暗色表格、短断线、交替行底色漏检 | 0.8.1 → 0.8.2 | 只依赖固定边缘阈值，缺方向性线条提取与缺口连接 | `tests/detector_test.cpp::linedTables`、`textAndAlternatingRows`、`mergedCellsAndBrokenRules`、`separatedTablesAndFramedImage` | ✅ |
| REG-044 | 照片纹理、孤立线条等非表格内容被误判成表格 | 0.8.2 | 缺少"拒绝非网格"的反向判据 | `tests/detector_test.cpp::rejectsNonGridTextureAndLines` | ✅ |
| REG-045 | 导出时附加预览失败会一并中断核心反馈（JSON + 原图）的保存 | 0.8.0 → 0.8.1 | 附加产物与核心产物在同一条失败路径上 | `tests/ui_test.cpp::previewLimitDoesNotLoseExportedFeedback` | ✅ |
| REG-046 | 导出参数非法时丢失未保存的批注 | 0.9.0 | 校验失败直接放弃文档状态 | `tests/ui_test.cpp::invalidExportKeepsUnsavedWork` | ✅ |
| REG-047 | 复制 JSON 遇到剪贴板被短暂占用时**误报成功**，实际剪贴板是空的 | 0.8.0 → 0.8.1 | 未重试，也未检查写入结果 | `tests/ui_test.cpp::primaryCopiesCompleteJsonAndSmallButtonOpensPreview` | ⚠️ 只覆盖成功路径与"完整 JSON"内容，占用重试分支无独立用例 |
| REG-048 | 旧版 `annotations` 格式的反馈无法导入 | 0.9.0（改为对象格式时） | 只认新格式 | `tests/layout_test.cpp::legacyAnnotationsMigrateAndFullProjectRoundTrips` | ✅ |
| REG-049 | 大图透明背景在放大后重绘卡顿 | 0.8.0 → 0.8.1 | 整图重绘而非只绘可见区域 | 无。性能问题难以在单元测试中断言，改由"仅绘制可见区域"的实现约定保证 | 📄 |

## 七、Agent 与 CLI

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-050 | 桌面沙箱禁止访问时，CLI 把"无法确认"误报成"程序未运行"，并重复拉起 GUI | 0.8.20 → 0.8.21 | 只区分"在跑/没跑"，没有第三种"无法确认"状态 | `tests/agent_cli_test.cpp::uncertainConnectionsNeverReportStoppedOrLaunch`、`statusOnlyReportsStoppedWhenBothEndpointsAreMissing`、`startupDistinguishesLaunchFailureTimeoutAndPermissionFailure` | ✅ |
| REG-051 | Agent 导出结果覆盖了已经存在的输出文件 | 0.8.20 | 写入前未检查目标存在 | `tests/agent_cli_test.cpp::outputNeverReplacesExistingFiles` | ✅ |
| REG-052 | 文档有未保存批注（或有活动会话）时仍被 Agent 替换 | 0.8.20 | 未检查 `dirty` 与活动会话 | `tests/agent_cli_test.cpp::dirtyDocumentAndActiveSessionRejectReplacement`、`unsavedCapturesAndClipboardImagesRejectAgentReplacement` | ✅ |
| REG-053 | 用户取消或超时后仍然导出了反馈 | 0.8.20 | 取消路径与完成路径共用导出 | `tests/agent_cli_test.cpp::cancellationAndTimeoutKeepDocumentWithoutOutput`、`completionPublishesFeedbackOnlyOnExplicitFinish` | ✅ |
| REG-054 | macOS 上隔离测试的 socket 路径超过 Unix 长度上限 | 0.8.20 → 0.8.21 | 套接字名过长 | `tests/agent_cli_test.cpp::socketNamesFitMacTemporaryDirectory` | ✅ |
| REG-055 | 分片到达的消息被当成完整帧处理 | 0.8.20 | 未按长度前缀切帧 | `tests/agent_cli_test.cpp::fragmentedMessagesRequireCompleteFrame` | ✅ |
| REG-113 | **命令行完全连不上桌面程序**：桌面程序明明在运行，`status` 仍返回 `running:false`，`open`、`capture`、`annotate` 全部报 `startup_timeout`（退出码 3）；`--help`、`--version`、离线 `export` 正常 | 0.9.0 → 0.9.8 | `8740ff1` 统一标识时删掉了 `agentServerName()` 里那 6 行"临时把应用名覆盖成桌面程序身份"的代码，此后两端按各自的应用名算 `AppLocalDataLocation`（GUI `…/Local/EditHere/EditHere`，CLI `…/Local/edithere-cli`），派生出的 `EditHere-agent-v1-*` 与 `EditHere-native-*` 两个名字都不同，CLI 找的管道从来不存在；测试都在同一进程里跑，两侧名字天然相同，所以一直没暴露 | `tests/agent_cli_test.cpp::endpointNamesIgnoreTheCallerIdentity`（同一进程先按 GUI 身份、再按 CLI 身份各算一次名字，两者必须相等；反向验证：去掉身份覆盖即失败，报出 `df9b5574…` 与 `31bef8d2…`）。真机复核：装 0.9.6 时 `status` 报未运行、`open` 超时；新构建的 GUI + CLI 连接正常 | 🆕 |
| REG-111 | **AI 与命令行文档、skill 按旧的 `annotations` / `changes` 描述反馈**，程序导出的却是 `objects`；照它解析的读取方拿到空批注，把"用户提了意见"误判成"没有意见" | 0.9.0 → 0.9.7 | `0.9.0` 的 `614661b` 把精简反馈改成 `objects` 对象结构，`docs/AGENT-CLI.md`、三份 `skills/edithere/SKILL.md` 与 `docs/DEVELOPMENT.md` 仍按 `0.8.21` 的并行数组写；"当前反馈"的 schema 名为 `feedback-minimal.schema.json`，看不出是当前格式（同目录 `feedback-v1` / `v1.1` / `v2` 描述的是早期**项目文档**），打包护栏还在断言 `feedback-v0.7` | `check-packaging.py::feedback_docs`（`exportFeedback` 必须写 `annotationSpace` / `objects` / `movements` / `annotations`；四份文档必须写明 `objects` 结构；`AGENT-CLI.md` 保留三行字段说明，且其中链接的 schema 都要存在） | 🆕 |

## 八、版本与仓库材料

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-056 | 程序版本在多个文件里各说各话（`connector/README.md` 停在 0.8.21，程序已到 0.9.2） | 0.8.0 → 0.9.2 | 版本号没有唯一来源，改一处漏一处 | `check-packaging.py::version_consistency` | 🆕 |
| REG-057 | 打包脚本把旧程序标记成新版本 | 0.8.0 → 0.8.1 | 打包脚本里另写了一份版本号 | `check-packaging.py::version_consistency`（`project()` 为唯一来源；`version.txt` 由 CMake 构建后生成，打包脚本只读它） | 🆕 |
| REG-058 | 在 Windows 上用 Visual Studio 生成器构建直接失败（`MSB6001 ... 关键字 "PATH"`） | 开发环境 | 环境里同时存在 `Path` 与 `PATH` 两个拼写，MSBuild 的 ToolTask 崩溃 | `check-packaging.py::build_recipe`（脚本、文档、CI 中不得出现 VS 生成器，只用 Ninja） | 🆕 |
| REG-109 | 标签已推送、GitHub Release 也建好了，页面上却只有源码压缩包，没有安装包 | 0.9.6 | 发布工作流的 `release` 任务依赖两个构建任务，任一构建失败即被跳过；而 `softprops/action-gh-release` 对同一标签仍会建出一个空资产 Release，看上去"发布成功" | `.github/workflows/release.yml` 的 `Require every package before publishing`：三个包（setup.exe / zip / dmg）任一缺失或为空即令工作流失败，并置 `fail_on_unmatched_files: true` | 🆕 |

## 九、截图工具条与文字识别

截图不再"松手即批注"：松开鼠标只把选定的区域定下来，选区上方的工具条（批注、识别、贴图、保存、复制）与它右侧的样式列（圆角、阴影/边框、贴图、重置）才是动作的入口。本节记录实现这套流程时踩到的问题。

**长截图实现更新（开发版）**：旧实现于 2026-09-29 移到 `feature/long-capture`。当前 Windows、macOS 和 Linux 后端共用纵向/横向手动采集、选区外遮罩、固定比例预览和原图拼接；Windows、macOS 与 X11 可选自动纵向滚动，Wayland 目前仅手动滚动。稳定帧采样与置信度复核负责匹配，明确勾选自动滚动后才向可验证的锁定目标投递滚轮。下表 **REG-081、REG-101、REG-102、REG-103、REG-104** 保留旧实现的事故记录和历史测试名；当前检查集中于 `scrollstitch_tests`、`scroll_controller_tests`、Windows `scroll_platform_tests`、Linux `linux_platform_tests` 和 `scroll_wayland_tests`，平台条件及尚未完成的实机验收见 [LONG-CAPTURE.md](LONG-CAPTURE.md)。

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-059 | 拖出来的区域比屏幕上看到的框**多一行一列**；向上/向左拖时边缘还会整体错位 | 开发中 | `dragRect()` 用半开语义（`{x1,y1,x2-x1,y2-y1}`），新写的 `captureRectFromDrag()` 用 `QRect(p1,p2)` 构造，后者把两个角点都算进去（101 像素 vs 100 像素） | `tests/capture_test.cpp::anUnlockedDragAgreesWithTheModel`（把两条路径绑死，四种拖拽方向逐一比对） | 🆕 |
| REG-060 | 护栏本身写错：零尺寸拖拽（在原地点一下）的比对永远失败 | 开发中 | `QRect::intersected()` 遇到空矩形会返回**默认构造**的矩形并丢掉原点，`QRect(500,500,0,0)` 被裁成 `QRect(0,0,0,0)` | 同 `anUnlockedDragAgreesWithTheModel`：把"限制在屏幕内"显式写成对四条边的 `std::clamp`，而不是用 `intersected()` | 🆕 |
| REG-061 | Windows 上文字识别一律失败，报"指定的路径无效…超过了最大长度"（`UNABLE_TO_MASK_PATH`） | 开发中 | Windows Runtime 的 `StorageFile` 只接受反斜杠路径，Qt 交出去的是 `C:/...` | `tests/ocr_test.cpp::theBridgeGetsNativePathsAndQuoting`、`packaging::ocr_bridge`（断言 `app/ocr.cpp` 仍调用 `QDir::toNativeSeparators`） | 🆕 |
| REG-062 | OCR 的单元测试**假通过**：断言"读出的文字包含某几个字母"，实际读的是一张纯白图 | 开发中 | offscreen 平台没有字体（`QFontDatabase: Cannot find font directory`），`QPainter::drawText` 画出来是空白，识别结果自然为空但断言写得太松 | `tests/ocr_test.cpp::textIsReadBackFromAPicture`（自绘 5×7 点阵字形，改成断言真实读回的字符串与行框位置） | 🆕 |
| REG-063 | 桥接脚本一旦带上 BOM，或写入非 ASCII 字符，整段调用在 PowerShell 侧静默失败 | 开发中 | 脚本以 Base64（UTF-16LE）形式经 `-EncodedCommand` 传入，BOM 会被解码成脚本顶部的杂散字符；非 ASCII 文案在编解码链上不可靠 | `packaging::ocr_bridge`（断言无 BOM、纯 ASCII、三个占位符在 `.ps1` 与 `app/ocr.cpp` 两侧都在） | 🆕 |
| REG-064 | 用 `QInputDialog::getSize()` 询问选区尺寸，编译不过 | 开发中 | 该静态函数在 Qt 6 已被移除（Qt 5 有） | 构建本身（CI 的 Windows 与 macOS 构建都会失败）。实现改为自建 `QDialog` + 两个 `QSpinBox`，见 `CaptureToolbar::askForSize` | ✅ |
| REG-065 | 截图流程改成"先工具条、再由批注进入"之后，四个既有测试仍然断言"松开鼠标就发出 `accepted`" | 开发中 | 交互契约变了，测试没跟着变；直接改测试而不核对行为，会把回归护栏变成"记录现状" | `tests/ui_test.cpp::screenCropUsesImagePixels`、`detectedBlockClickImmediatelyStartsAnnotation`、`tests/startup_flow_test.cpp::overlappingWindowsNeverSelectBehindFront`、`captureArrowAdjustmentSurvivesRelease`（都改为：松手后 `accepted` 仍为 0、工具条按钮存在，按回车才发出 `accepted`，且矩形与旧断言完全一致） | ✅ |
| REG-066 | **阴影完全看不见**：开了阴影、也设了半径，导出的图跟没开一样 | 开发中 | 阴影是把"越来越透明的圆角矩形"一圈圈叠出来的。最外圈 alpha 已经趋近 0，最内圈又整圈压在图片下面被盖住，能剩下的只有一道几乎透明的台阶 | `tests/capture_test.cpp::theHaloFadesSmoothlyAndIsNeverInvisible`（沿水平方向逐像素走出图片：alpha 必须单调不升、可见像素不少于半径的一半、贴着图片那一侧必须最浓；强度 0 与关闭都必须原样返回） | 🆕 |
| REG-067 | 取到的颜色**只有两种写法**，`Ctrl+C` 按下去复制的是整张图而不是颜色值 | 开发中 | 颜色格式写成了 `bool hexadecimal_`，只能二选一；`Ctrl+C` 一路落到"复制选区图像"那条分支，取色时按下它拿到的是图 | `tests/capture_test.cpp::everyColourFormatNamesTheSameColour`（`RGB`/`HEX`/`HSV`/`HSL` 四种都能写出同一个颜色、无 hue 的灰色写成 0 而不是负数、循环一圈回到起点） | 🆕 |
| REG-068 | **置顶（贴图）落在屏幕正中**，而不是刚才截图的那个位置 | 开发中 | `PinWindow` 只会把图缩到能放进屏幕然后居中；截图时选区在屏幕上的位置根本没有传给置顶窗口 | `tests/ui_test.cpp::aCapturedRegionKnowsWhereItCameFrom`（`Overlay::placementFor` 把图片像素换算成窗口管理器用的逻辑坐标，并按中心对齐，带阴影时也仍然落在原区域上）、`tests/capture_test.cpp::aPinnedRegionKeepsThePlaceItWasTakenFrom`（置顶窗口的尺寸与左上角必须等于传入的位置矩形） | 🆕 |
| REG-069 | 置顶窗口比要求的小了整整 1 像素（要 200×150 得到 199×149），且看不出哪里减的 | 开发中 | **`QSize()` 是"无效尺寸" `(-1,-1)`，不是零尺寸**（`QRect()` 才是空的）。`size + QSize()` 于是把两条边各减 1 | `tests/capture_test.cpp::aPinnedRegionKeepsThePlaceItWasTakenFrom`（尺寸精确相等即能拦住）。写 `QSize()` 当零用时要写成 `QSize(0, 0)` | 🆕 |
| REG-070 | **阴影还是看不见**：开了阴影、导出的图里也有，但截图界面上完全没有 | 开发中 | 阴影只在 `composeCapture()` 里画，也就是只在复制/保存/贴图的成品里；截图窗口只画了边框预览没画阴影预览。一个只能在成品里看到的效果，等于在"调强度"这个唯一需要它的时刻看不到它 | `tests/ui_test.cpp::theShadowIsVisibleOnTheRegionBeforeAnythingIsCopied`（真实渲染 Overlay 后逐像素比对：选区外侧一点的红通道必须比"关阴影时"明显更暗，关掉后必须回到原值）；`tests/capture_test.cpp::theShadowIsDrawnAroundTheRegionBeforeItIsTaken`（`composeShadowPreview()` 的尺寸、中心镂空、四边与四角可见、关闭时返回空图） | 🆕 |
| REG-071 | 阴影把**矩形画成了胶囊**：宽图四角没有阴影，光晕只在四条边的中段出现 | 开发中 | `shadowHalo()` 的圆角半径取成了 `min(半宽, 半高)`，也就是"能取到的最大圆角"，于是符号距离函数算的是一个胶囊而不是矩形；越宽的图四角露得越多 | 同 `theShadowIsDrawnAroundTheRegionBeforeItIsTaken`：断言"矩形的角也要有阴影"（该断言在改回缺陷时确实失败）。圆角半径须由 `CaptureStyle::cornerRadius` 传入并 clamp | 🆕 |
| REG-072 | **截图置顶后画质下降**（看着像压缩，其实不是） | 开发中 | 图片是设备像素，窗口是逻辑像素。缩放屏上 `PinWindow` 先按 `placement.width()/image.width()`（≈0.5）把图**缩小**成 `display_`，再让窗口把它放大回去：先丢掉四分之三的像素再插值补回来。全程没有任何压缩步骤 | `tests/capture_test.cpp::aPinnedRegionKeepsEveryPixelItWasGiven`（窗口 200×150 逻辑像素时，实际绘制的位图必须仍是 400×300，且 `devicePixelRatio()` 为 2.0）。要点：要精确尺寸就用 `QImage(目标尺寸, 格式)` + `drawImage`，不要 `scaled()` | 🆕 |
| REG-073 | 置顶图片的右键菜单里**没有批注**，批注按钮也只是七个图标里的一个 | 开发中 | 置顶窗口的菜单只有复制/保存/识别/缩放/透明度/关闭；工具条的批注按钮没有被强调 | `tests/capture_test.cpp::aPinnedPictureCanAlwaysBeAnnotated`（菜单首项必须是「批注」且触发后带整张图发出 `annotateRequested`）、`annotatingIsTheOneActionDressedAsTheWayOnward`（工具条里带 `primary` 属性的按钮有且只有一个，glyph 为 `edit`、可访问名为「批注」） | 🆕 |
| REG-074 | **双击置顶图片是复制**（用户要的是关闭） | 开发中 | 把 PixPin 的双击=复制照搬过来，但置顶窗口上复制已有 `Ctrl+C` 与菜单项，而"把这个小窗口收起来"反而没有手势 | `tests/capture_test.cpp::aDoubleClickPutsAPinAway`（双击后窗口不可见，且 `copyRequested` 一次都没发） | 🆕 |
| REG-075 | 阴影默认开启后，从置顶图进入批注时画布**凭空大一圈**（多出阴影的外扩） | 开发中 | 置顶窗口拿的是 `composeCapture()` 成品（含四周光晕），而「批注」直接把它整张交给编辑器 | `tests/capture_test.cpp::aPinnedPictureIsAnnotatedWithoutItsShadow`（`PinWindow::setDecoration()` 记录外扩，`editableImage()` 裁掉它；外扩为 0、为负、大于图片本身三种情况都不能崩也不能裁成空） | 🆕 |
| REG-076 | 阴影默认开启后，文字识别读的是**带光晕的成品图**：行框原点整体被外扩量推偏，深色的光晕边缘还被交去"识别" | 开发中 | `Controller::recognizeRegion()` 用的是 `Overlay::selectionImage()`。装饰是给眼睛看的，识别要读的是截到的东西 | `tests/ui_test.cpp::textIsReadFromTheRegionRatherThanFromItsDecoration`（`selectionPixels()` 必须逐像素等于原图上的那块；加圆角与阴影后 `selectionImage()` 按外扩量变大，而 `selectionPixels()` 一点不变） | 🆕 |
| REG-077 | 阴影是**黑色**的，看着像一团灰，看不出是这个程序的效果 | 开发中（用户反馈） | `CaptureStyle::shadowColor` 默认写死 `QColor(0, 0, 0)` | `tests/capture_test.cpp::aShadowIsCastInTheAccentRatherThanInBlack`（默认 tint 的 RGB 必须**等于 `accent()`**、蓝通道明显高于红通道；显式指定黑色时半径与衰减与默认完全一致，即"只换颜色、不换形状"；阴影关闭时 alpha 仍为 0） | 🆕 |
| REG-078 | **进入截图模式后卡顿**：鼠标一动就掉帧，区域越大越明显 | 开发中（用户反馈） | 三个独立的每帧开销叠在一起：① `Overlay::paintEvent` 每次都重算阴影光晕（1024×768 选区实测 29 ms/帧，逐像素开方）；② 每帧把整屏抓图重缩放一次（4K 屏 10 ms/帧）；③ 放大镜又把整屏抓图缩放第二次。实测基准（临时用例）：`halo_first=29ms halo_cached=0ms scale_screen=10ms blit=1ms` | 光晕加**单槽静态缓存**（尺寸+样式相同直接返回，`composeShadowPreview()`）；`Overlay` 缓存缩放后的帧 `scaledFrame()`，换图时失效（`showHistoryPicture()` 里清空）；放大镜只把**放大区域那一条**交给绘制（`p.drawImage(area, frame_.image, source)`），不再整屏重采样 |
| REG-079 | **贴图按钮出现了两次**：顶部工具条一个、右侧样式栏一个 | 开发中（用户反馈） | 侧栏是在顶部工具条之后加的，加入时把顶部已有的动作又做了一遍，没有回去删 | `tests/capture_test.cpp::pinningIsOfferedInOnePlaceOnly`（`CaptureToolbar` 里 `glyphName == "pin"` 的按钮恰好 1 个，`CaptureSidebar` 里必须是 0 个） |
| REG-080 | **放大镜里看不到识别出来的蓝色框**，放大后反而不知道自己在选哪一块 | 开发中（用户反馈） | 放大镜里画的是**选区**（`localRect(selected_)`），而且用的是**未放大**的屏幕坐标——面板里是 10 倍放大的内容，这两套坐标根本不是一回事，画出来是错的；而放大镜真正对准的"当前识别块"（`picker_.current()`）压根没画 | `tests/ui_test.cpp::theMagnifierCarriesTheFrameItIsAimingAt`（真实渲染 Overlay：指针压在被识别块的边缘上，放大区域矩形内必须出现强调色像素。先把新绘制关掉跑一遍确认 **0** 像素、用例失败，再打开得到 113 像素） |
| REG-081 | **长截图基本不可用**：稍微正常一点的页面（有固定顶栏/播放条/底栏）跑完只回一帧原图 | 开发中（用户反馈） | 三层原因：① 匹配时用的是整幅帧，帧首的固定顶栏永远对不上上一帧结尾的行，第一次匹配就失败；② 失败即 `succeed()`，拿第一帧交差，没有重试也没有"退一步用粗糙匹配"的兜底；③ 滚动后死等 180 ms 就抓，而平滑滚动的页面要 300 ms 以上才停稳，抓到的是滚动中途的糊图 | `tests/capture_test.cpp::aFixedHeaderAndFooterDoNotStopTheStitch`（12 行固定顶栏 + 8 行固定底栏的页面，逐帧拼接后总高度必须等于 `顶栏 + 全部内容 + 底栏`，且顶/底栏各只出现一次；同一组帧交给旧的 `appendScrolledFrame()` 必须返回 -1，即旧路径确实会失败）、`theBandsOfAFrameSayWhatDidNotMove`、`aFrameWithSomethingMovingInItIsStillPlaced`（容差阶梯：精确匹配失败 → 放宽后放行，且 `partial()` 必须为真）、`aStitcherThatPlacedNothingStillHasItsFirstFrame`、`twoLooksAtTheSamePlaceSayWhetherThePageMoved` |

| REG-105 | **置顶图外面多了一圈实线**：带阴影的置顶图，蓝色实线画在**窗口**最外圈而不是图片边缘上，于是浅蓝的阴影外头还套着一个硬边框 | 0.9.6（用户反馈） | `PinWindow` 的窗口是「图片 + 阴影外扩」（`decoration_`），而 `paintEvent` 画轮廓用的是 `rect()`——也就是光晕的外沿。阴影在那里已经衰减到几乎全透明，一条不透明的强调色线压上去，读起来就是"给阴影加了个框"。同一处的 `edgeAt()` 也在拿窗口边缘当可拖动的边，于是可抓的边和图片自己的边也不是一回事 | 新建 `PinWindow::pictureRect()`（按 `decoration_` 占图片的比例换算成窗口单位的内缩矩形，并夹住不让外扩吃掉图片），`edgeAt()` 改用它。**描边本身全部删掉**（用户 2026-09-29 明确：置顶图不要任何边框；关闭阴影就是要"和同色桌面难以区分"，那是需求不是缺陷）——`paintEvent` 现在只画 `display_`，`kOutline` 一并删除。⚠️ 中途做过"只在没有阴影时画细线"，那是把需求当缺陷修了，已撤销 | `tests/capture_test.cpp::aPinWithAShadowIsEdgedByTheShadowAndNotByALine`（真实渲染置顶窗：阴影强度 100（外扩 30 px）时，最左/最右那一列像素的 alpha 必须 < 200（光晕外沿几乎全透明），图片正中必须仍是 255；**关掉阴影后整窗必须逐像素等于 `composeCapture(图片, 无阴影)`**，一个多余的像素都不许有。反向验证：把描边加回去，报"60 个像素不同"、用例失败） | 🆕 |
| REG-106 | 同时钉了好几张置顶图时**分不清正在用的是哪一张**，而且带阴影的置顶图在阴影外面还套着一圈实线（REG-105） | 0.9.6（用户反馈） | 两件事其实是同一个位置的同一个假设：`PinWindow` 把「窗口」当成「图片」。窗口 = 图片 + 阴影外扩，于是① 轮廓画在光晕外沿；② 阴影永远是强调色，不随"用户在不在用它"变化 | ① 阴影颜色跟着激活状态走：`PinWindow::effectiveStyle()` 在 `!active_` 时把 `shadowColor` 换成中性灰 `kIdleShadow`，`changeEvent(QEvent::ActivationChange)` → `setActive(isActiveWindow())` → `rebuild()`（只有颜色变，半径不变，所以尺寸与位置都不动）。② 阴影关掉时 `effectiveStyle()` 不动颜色，也就没有"灰阴影"这回事 | `tests/capture_test.cpp::aPinSaysWhetherItIsTheOneBeingUsedByTheColourOfItsShadow`（阴影强度 100（外扩 30px），在**光晕带的中部**取色——取在最外沿会因 alpha 只有 4 而被量化毁掉，实测得到 `#40407f`：激活时蓝明显高于红、失焦时蓝红差 ≤ 12、回到激活再亮起来、关掉阴影后窗口尺寸等于图片尺寸） | 🆕 |
| REG-107 | **批注已有的图片要先截一次图**，且关闭批注窗口时每次都弹「是否保存」 | 0.9.6（用户反馈） | ① 批注窗口只能由 `setDocument()` 打开，而文档只能来自一次截图/打开/粘贴，没有"先开窗口再放图"这条路；② `Editor::allowReplace()` 同时服务"关闭"与"换成另一张"，两者都无条件弹确认，没有记住答案的地方 | ① 新增 `Editor::openEmpty()`（清空文档 + 显示空态提示 `emptyWell_`）+ 托盘菜单「新建批注（空窗口）」；`dropEvent` 除了文件 URL 也收 `hasImage()`（从别的程序直接拖图进来）。② 关闭单独走 `Editor::confirmDiscardOnClose()`：弹窗带「不再提醒，可在设置中修改」复选框，勾上即把 `AppSettings::confirmBeforeDiscard` 置假、`emit preferencesChanged()` 让 `Controller` 落盘；设置 → 默认 里可改回 | `tests/ui_test.cpp::anEmptyAnnotationWindowSaysItIsWaitingForAPicture`（开空窗口无文档且提示可见、放入图片后提示消失、再开一次仍是空态）；`tests/settings_test.cpp::defaultsAndPortablePersistence` 里钉住 `defaults/confirmBeforeDiscard` 会被写进 ini 并读回 | 🆕 |
| REG-108 | 在**别的 Windows 电脑**上，「组件调整 → 整个图片」里的数值显示成怪符号（0 显示成 O、2 和 8 显示成 ×、4 显示成 ≡），而中文和 `px` 都正常 | 0.9.6（用户反馈） | 界面字体按**名字**硬编码指定（`Microsoft YaHei UI` / `Segoe UI` / `Microsoft YaHei` / `Consolas`，共 15 处）。这些字体在精简版/魔改系统上可能不存在，Qt 对缺失的字符逐个回退，数字落进了某个符号字体——于是每个数字稳定地换成固定的怪字形，而不是乱码方块。与程序内嵌字体无关（从未用过 `addApplicationFont`） | 新增 `app/fonts.h/.cpp`（进 `h2d_core`，不依赖 Widgets）：`resolveFontFamily()` 先读 `QFontDatabase::families()`，从候选列表里选第一个**真实存在**的家族（`latinFontFamily()` = Segoe UI→Tahoma→Arial→Helvetica→Noto Sans、`cjkFontFamily()` = Microsoft YaHei UI→Microsoft YaHei→PingFang SC→…→SimSun、`monoFontFamily()` = Consolas→Cascadia Mono→Menlo→Courier New），一个都没有就返回空串、把字形交给平台默认。`applyInterfaceFont()` 与全部 15 处 `QFont("…")` 都改走这套解析 | `tests/ui_test.cpp::aFontThatIsNotInstalledIsNeverAskedForByName`（未知家族必须解析为空；候选里混入未知名时第一个真实存在的家族必须胜出；`applyInterfaceFont()` 之后 `qApp->font().family()` 必须是已安装家族或为空串） | 🆕 |
| REG-110 | **移动批注总是排在最末**：组件调整里挪动一个块，批注列表末尾出现一张「点击此处为这次移动添加文字…」的卡；不写文字它就不算批注，写了之后这条批注也是**追加**到最后，与它前后文字批注的先后无关 | 0.9.6（用户反馈） | 移动不是批注，而是一条**派生的展示**：`movementMarkers()` 把「没有对应批注的移动」编号为 `notes.size() + 1`（`nextOrphan`），渲染时插在列表末尾；只有点开输入框敲了字，`editMovement()` 才往 `doc_.notes` 追加一条真正的批注。于是它的位置由"什么时候补的文字"决定，而不是"什么时候挪的"。顺序之外还连带一条：空文字的批注在 `validateDocument()` 里不合法（"批注编号或文字不正确"），所以移动的批注必须先有文字才能存盘与导出 | ① 移动在**提交的那一刻**就成为批注：`LayoutCanvas::changed` 的提交分支里把没有对应批注的移动补成一条空文字的批注，追加在当时的列表末尾，此后新建的文字批注自然排在它后面。② 空文字的**移动批注合法化**：`validateDocument()` 对带 `movementSource` 的批注不再要求 `comment` 非空，移动本身就是内容；`exportFeedback()` 同时不再把空串塞进 `annotations`。③ 移动**跟着块走**：提交时先删掉"空文字且源矩形已不在当前移动里"的批注，挪回去等于没有移动，那条空批注也一道消失；已写过文字的批注不受影响。④ 条数与徽章改走 `annotationCount()`（批注 + 未写文字的移动），画布编号与侧栏徽章同源 | `tests/ui_test.cpp::movementKeepsCreationOrderWithoutText`（先写一条全局批注 → 挪一个块 → 再写一条全局批注：顺序必须是「文字 / 移动 / 文字」，移动的徽章为 2，补写文字后位置与徽章不变，存盘再读回顺序一致）；`tests/ui_test.cpp::emptyMovementAnnotationFollowsItsBlock`（挪走 → 空批注出现 → 挪回原位 → 批注清空、`movementMarkers()` 为空、计数回到「批注 0 条」；写过文字的批注挪回去仍然保留）；`tests/ui_test.cpp::movementCardsCountAndOpenTextFromSidebar`（侧栏卡可点开输入框，写完后条数不变）。反向验证：把 `validateDocument()` 的空文字限制加回去，`minimizeRestoresDocumentAndLayout` 立即抛「批注编号或文字不正确」 | 🆕 |
| REG-083 | **悬停找块时整屏跟着指针泛蓝光**：阴影画在截图窗口上，`active` 在还没选定时取的是"指针下的那个候选框"，于是指针移到哪儿哪儿就长出一圈蓝色光晕 | 0.9.6（用户反馈） | 阴影预览（`composeShadowPreview()`）画在 `Overlay::paintEvent` 里，不区分"还在找块"与"选区已定下来"两种状态。用户要的是**阴影只属于置顶图**，所以截图窗口上一律不画（`composeCapture()` 成品与 `PinWindow` 仍然带） | `tests/ui_test.cpp::theCaptureWindowCastsNoShadowAroundTheRegion`（真实渲染：把阴影强度开到 60 与关掉相比，选区左侧那个只有光晕会够到的像素必须**完全不变**。反向验证：把绘制加回去，这条失败） |
| REG-084 | **截图模式下整屏画质明显下降**，不只是置顶图糊（REG-072 的同一个坑，第二次踩） | 0.9.6（用户反馈） | `Overlay::scaledFrame()` 把设备像素的抓图 `scaled()` 到窗口的**逻辑**尺寸再画。缩放屏上抓图是 2880×1620、窗口只有 1920×1080，于是整屏先被砍掉一半像素再由系统插值补回来 | `frameScaled_` 不再重采样，改为保留原像素数并 `setDevicePixelRatio(抓图宽 / 窗口宽)`，让 Qt 一比一地贴。**这与 REG-072 是同一个教训**：`QImage` 是设备像素、`QWidget` 是逻辑像素，两者不能互相 `scaled()` |
| REG-085 | **取色要按 `C` 才有**，选区刚定下来、鼠标就在自己截的那张图上时，放大镜和颜色读数反而不见了 | 0.9.6（用户反馈） | `Overlay::paintEvent` 里放大镜的条件是 `!ready_ \|\| picking_`，也就是**只在选区定下来之前**显示；`ready_` 一为真就撤掉，与用户的用法正好相反 | `tests/ui_test.cpp::theMagnifierStaysUpOverTheRegionThatWasJustTaken`（真实渲染：指针在选区外时记一次暗像素数，移进选区后再记一次，差值必须超过 5000。反向验证：把条件改回原样，差值为 **0**、这条失败） |
| REG-086 | **放大镜里的蓝框比屏幕上细得多**：屏幕上的框是"粗细固定"的两像素线，放大十倍二十倍也不跟着变，于是放大镜里量出来的框和真正截下来会带的框不是一回事 | 0.9.6（用户反馈） | 放大镜是拿**原始抓图**做 `p.drawImage(area, frame_.image, source)`，再用 `toPanel()` 把框的坐标映射进面板、用 `QPen(accent(), 2)` **重画一条两像素的线**。放大的是像素，框是另画的，两者从倍率上就不同源 | `Overlay::drawScene(QPainter &, const QRectF &viewport)`：把"抓图 + 遮罩 + 蓝框/圆角/角点"抽成同一段绘制，窗口要整幅（`rect()`），放大镜要指针下那一小片（`window`），放大镜被 `scale(zoomX, zoomY)` 变换后**再画一遍同一段**，框于是和像素一起被放大。`tests/ui_test.cpp::theEnlargementMagnifiesTheFrameRatherThanRedrawingIt`（真实渲染：指针压在选区左边线上，放大区域里的强调色像素必须超过 2000——两像素的细线约 200。反向验证：改回"抓图 + 重画细线"，得到 **86**、这条失败） |
| REG-087 | **选区定下来之后，方向键会挪动整个截图范围**，刚摆好的框一到手就被键盘推走；而这个阶段真正需要移动的是**鼠标指针** | 0.9.6（用户反馈） | `Overlay::keyPressEvent` 里 `ready_` 分支的普通方向键走的是 `nudgeSelection()`，也就是把 `selected_` 平移一像素；用户要的是"让鼠标移动" | 普通方向键改走 `Overlay::movePointer()`（按屏幕逻辑像素走一步、换算回抓图像素、`QCursor::setPos()` 真把指针挪过去，并按逻辑像素取整——缩放屏上按抓图像素挪半格会被系统round回原位，看起来像按键失灵）；Shift/Ctrl + 方向键仍然收放对应的那条边（`stretchSelection()`），`nudgeSelection()` 已无调用者、删除。同一轮把面板按用户要求缩小四分之一（216×268→162×236、放大区 126→94）并把放大倍率提高 0.5 倍（每像素 10→15 格），两处尺寸常量移到 `app/ui.h`（`magnifierPanelWidth/Height/Padding`、`magnifierZoomHeight`、`magnifierZoomCell`）供窗口与测试共用，避免测试里再写一份会漂移的字面量。`tests/ui_test.cpp::arrowsMoveThePointerRatherThanTheRegion`（真实渲染：按一次右键，`selection()` 必须一字不变；同时放大区那一块的像素必须变了。反向验证：改回 `nudgeSelection`，`selection()` 变了、这条失败） |
| REG-088 | **放大镜面板会被工具条盖住**：面板正好落在工具栏那一带时，放大的内容被横穿一条，读数正好在那时最需要 | 0.9.6（用户反馈） | 面板是 `Overlay::paintEvent` 画的，而工具条与右侧样式列是**子控件**，子控件永远画在父控件之上。面板的放置只看了窗口四条边，没看这两个工具 | 放置规则抽成 `magnifierPlacement(at, window, tools)`（`app/ui.cpp`）：先在指针旁，放不下就翻到另一侧，再对被工具压住的每一块找"离当前位置最近、且确实空出来"的一侧让开（上/下/左/右四选一，压不住且不出窗口才算数）。`Overlay::drawMagnifier` 传可见的 `bar_`/`sidebar_` 几何，测试传同一份，**双方问的是同一个函数，不会再各写一套**。`tests/ui_test.cpp::theMagnifierPanelKeepsClearOfTheTools`（真实渲染：先确认"不看工具时面板确实会落在工具条上"，再断言让开后的面板与工具条不相交；并且放大区从上到下每一行都要看到那条被放大的框——能打断它的只有网格线，所以要求 ≥ 4/5 的行。反向验证：把让开逻辑去掉，几何断言先失败） |
| REG-089 | **还没截图时方向键完全不接**：悬停找块阶段按上下左右没有任何反应，想精确指向某个像素只能靠挪鼠标——而挪鼠标恰恰落不准单个像素 | 0.9.6（用户反馈） | `Overlay::keyPressEvent` 里方向键只有两条分支：拖框中（`drawing_`）调整框的终点、选区定下来后（`ready_`）平移选区。**"还没选、也没在拖"**这个状态没有分支，按键一路落到 `QWidget::keyPressEvent` | `Overlay::keyPressEvent` 把方向键统一成"瞄准指针"：除拖框中仍是拉框以外，其余状态（找块、取色、选区已定）都走 `movePointer()`；找块状态额外 `picker_.update(candidates(), cursor_)` 与 `debounce_.start()`，让被高亮的识别块和放大镜一起跟着指针走。`movePointer()` 里 `magnifierVisible_` 改成 `selected_.isEmpty() \|\| selected_.contains(cursor_)`，没有选区时读数一直在线。`tests/ui_test.cpp::arrowsMoveThePointerBeforeAnythingHasBeenTaken`（真实渲染：按一次右键，选区必须仍为空、放大区那一块像素必须变。反向验证：把该分支关掉，这条失败） |
| REG-090 | **方向键按起来"看心情"**：有时按了没反应，有时只能按上不能按下，有时按上先向右上跑一格再向上、按右变成右下、按左变成左上 | 0.9.6（用户反馈） | 两处都出在"用抓图像素当步长"上。① `movePointer()` 把一步换算成 `qRound(dx * sx)` 个**抓图**像素挪 `cursor_`，再 `qRound(target / sx)` 折回指针的**逻辑**像素。缩放屏上 `sx = 1.25`，一步是 1.25 个抓图像素，来回取整后有时落回起点（按键像失灵），有时一轴进一轴退（斜着走）；而鼠标停在非整像素处（125% 下只能落在 0.8 的格子上）时，第一次按键会顺带把两个轴各自"归整"，这就是"按上先向右上、按右右下、按左左上"，之后再按就正常——因为偏差已被取整抹掉。② `QCursor::setPos()` 之后系统会把它真正落到的位置再报一个 `MouseMove` 回来（125% 下比请求值短 0.2 像素），旧代码照单全收，`pointer_` 被拽回上一格，下一步又从旧位置起算 —— 指针在原地来回摆 | `Overlay` 新增 `pointer_`（**逻辑**像素记账）：一步就是一个整逻辑像素，只在被按的那个轴上加，不再经过抓图像素来回取整；`cursor_`（抓图像素，供放大镜与取色读数）由 `pointer_` 单向换算出来。构造函数 / `showEvent` / 按下 / 移动 / 松开 / 滚轮都同步 `pointer_`。`mouseMoveEvent` 加"回声判定"：不足一个整逻辑像素的回传不算用户移动，不采纳（拖动中无条件采纳）。对外加 `pointer()` 便于观察。`tests/ui_test.cpp::arrowKeysMoveThePointerOneStepAtATime`（125% 的抓图 700×450 + 窗口 560×360，且指针从一个非整像素处起步：连按十次右右上上上下下左右，每次必须正好移动一步、另一轴必须为 0；每次按完补发一次"系统取整后的回声"，指针必须原地不动；十次之后指针必须落在起点 + (1,-1)。反向验证：只退回旧的取整步长 → `moved 0,0, expected 0,-1`（按上没反应）；再把回声照单全收 → 指针被拽回 `202,178`） |
| REG-091 | **滚轮选中的大框一按方向键就被打回小框**：识别给出"单元格 / 整行 / 整张表"几层时用滚轮切到大框，紧接着按方向键移动指针，选中的块立刻变回最小的那个 | 0.9.6（用户反馈） | REG-090 的"回声判定"只挡住了 `pointer_` 与 `cursor_`，**挡不住后面整段重选逻辑**：`mouseMoveEvent` 里 `if (!drawing_) { native_.clear(); picker_.reset(); }` 与随后的 `picker_.update()` 在判定之外无条件执行。`QCursor::setPos()` 的回声一到，`picker_.reset()` 清空层数与锚点，`update()` 因锚点为空把 `index_` 归 0 —— 也就是"该处最小的那一块"。测试环境（offscreen）`setPos` 不产生事件，所以 REG-090 的 66 例全绿也照样漏掉 | 回声在 `mouseMoveEvent` 里**直接 return**：既然不是用户移动，指针位置、抓图像素、候选探测与选块一律不动（顺带也免掉了每次按键都清空原生候选再重新探测）。对外加 `hovered()`（当前会框出来的那一块）便于观察。`tests/ui_test.cpp::theLayerPickedWithTheWheelSurvivesAnArrowKey`（800×600 的表格图，指针落在单元格内 → 滚轮换到更大的一层 → 按一次方向键 → 补发系统回声 → 仍必须是滚轮选的那一块。反向验证：去掉 return，用例报"整行 70,160 600x100 变成单元格 270,160 200x100"，与用户描述完全一致） |
| REG-092 | **滚轮选的层级一移动鼠标就丢**：识别给出"单元格 / 整行 / 整张表"几层时用滚轮切到外层，之后只要**真的**移动鼠标（不是回声），框就掉回该处最小的那一块——表现为"从外层 A 移动进入它里面的 B，框变成 B" | 0.9.6（用户反馈） | `CandidatePicker` 只记"当前选中的那一块"，不记"用户要第几层"。`mouseMoveEvent` 每次真移动都 `picker_.reset()`，`update()` 于是从 `index_ = 0`（**该处面积最小的块**）重新挑；`nearby`（距锚点 ≤ 8 像素）只在原地附近兜住，走远一步就兜不住。也就是说"滚轮选过层"这件事在移动面前完全没有粘性 | `CandidatePicker` 新增 `std::optional<int> chosen_`（滚轮停在第几层），`step()` 写入它、`update()` 把它当作默认层（`clamp` 到当前层数之内），`reset()` **不清**它——清候选不清意愿；新增 `forget()`，只有"重新开始一次截图"时（`Overlay::resetSelection()`）才忘。没滚轮过时 `chosen_` 为空，行为与以前一致（悬停到哪儿就挑那儿最小的块）。`tests/core_test.cpp::theLevelPickedWithTheWheelIsKept`（单元格 → 滚轮到行 → 移到另一个单元格仍是行；`forget()` 后回到单元格；层数不足时给最外层）、`tests/ui_test.cpp::theLevelPickedWithTheWheelIsKeptWhileMoving`（800×600 表格图，滚轮切到整行后移到另一格的另一个单元格，必须仍是整行。反向验证：把默认层改回 0，报"得到 470,360 200x100，想要 70,360 600x100"，即用户描述的"进入 B 就变 B"）。`tableRegionSelection` 与 `equalAreaAndDuplicateRegions` 两处原本断言"移动后回到最小块"，现改为显式 `forget()` 后再断言，语义不变。同一条还有另一半，按用户澄清：**大范围移动鼠标是"重新选块"，方向键是"微调"**——`update(all, p, keep)`，`keep = true` 时只要 `previous` 仍包含指针就保持它（不再受锚点 8 像素限制）。`Overlay::keyPressEvent` 的方向键分支传 `true`，鼠标移动仍传 `false`，悬停智能选块不受影响。⚠️ 中途做成过"指针还在当前这块里面就不换块"（`setKeepInside`，全局生效），**那是错的**：等于把鼠标大范围移动也锁住、关掉悬停智能选块；而且编辑器画布用同一个 `CandidatePicker`（`canvas.cpp` 也会 `picker_.step()`），粘住外层就点不到里面的组件。已撤销，改成只由"是不是方向键"决定。`tests/core_test.cpp::aBlockIsKeptWhileThePointerIsFineTuned`（行内 → 方向键微调进单元格仍是行 → 同一个位置改用鼠标到达则取该处最小的块）、`tests/ui_test.cpp::fineTuningWithTheArrowKeysKeepsTheBlockOnOffer`（表格外的整屏 → 连按十次右键进入表格，必须仍是整屏；随后用鼠标移到表格里，必须换成该处最小的块。反向验证 `reverse-check-finetune.py`：去掉 `keep`，报"0,0 800x600 变成 70,160 200x100"） |

| REG-101 | **长截图一旦开始就停不下来，也没有任何预览**：按下"长截图"后覆盖层被隐藏、程序自己一路滚到底（或滚到上限）才交差，用户既看不到拼到什么程度，也没法在拨到想要的那一段时叫停；横向长截图更是一按就整屏乱滚 | 0.9.6（用户反馈，重做） | 旧实现把"读屏幕"的责任放反了：`ScrollCapture` 自己 `QTimer::singleShot` 起一轮，自己在回调里调 `Platform::scrollAt()` 再抓帧，`Controller` 只在旁边等一个最终结果。于是：① 覆盖层必须先 `hide()` 才能抓到下面真正的窗口，用户眼前一空；② 起停是同一个"进入即开跑"的动作，没有中间态可以停下；③ 没有任何把"已拼接的图"回传给窗口的通道，所以无从预览；④ 横向与纵向共用一条竖向拼接路径，横向根本没有实现 | **把会话改成被事件驱动**（`app/scrollcapture.*`）：`ScrollCapture` 不再持定时器、不碰平台层，只暴露 `begin(first, step)` / `Outcome take(frame)`（`Added` / `Repeat` / `Failed` 三态）/ `picture()` / `atLimit()` / `stop()`；读屏幕的节奏改由 `Controller` 用 `stepScrollCapture() → 等 kScrollSettleMs → readScrollFrame() → placeScrollFrame()` 一条流水线驱动，随时可 `abortScrollCapture()`。覆盖层不再隐藏（Win 靠建窗时就设好的 `WDA_EXCLUDEFROMCAPTURE`、mac 靠 `NSWindowSharingNone` 自己隐身），于是能一直留在屏幕上当取景框。`Overlay::beginScroll()/updateScroll()` 把已拼接的图交给窗口，框右侧（纵向）或下方（横向）出现实时预览。工具条的"长截图"改成 **开始/停止同一按钮** + 纵向/横向子菜单。横向单独走一条路：不滚轮，靠**拖动框**扫过内容（`mouseMoveEvent` 里 `scrolling_` 时只沿 run 轴平移，`scrollRegionMoved` 把新位置回传给 `Controller` 决定下一帧从哪读） | `tests/capture_test.cpp`（会话三态、上限、停止：`aFrameAtATimeGrowsTheLongPicture` / `aFrameThatCannotBePlacedIsToldApartFromTheEndOfThePage` / `aLongCaptureStopsAtItsLimit` / `aLongCaptureHasAFrameLimit` / `aStoppedLongCaptureTakesNothingMore`；预览放置与文案：`theLongPictureGrowsWhereItIsSeen` / `theLongPicturePanelIsAlwaysOnTheScreen` / `theLongPicturePanelWouldRatherVanishThanHideBehindATool` / `aLongPictureSaysItsLengthTheWayAPersonReadsIt`）。`tests/ui_test.cpp`（窗口侧契约：`aLongCaptureLeavesTheRegionWhereItIs` 进入长截图不改选区、不再自己开跑；`aRunningLongCaptureOnlySlidesAlongItsOwnWay` 运行中拖动只沿 run 轴走（反向验证：放开另一轴，宽度/高度断言失败）；`aRunningLongCaptureOffersNoOtherCapture` 运行中右键 / `Esc` 是退出而不是拍照，`Ctrl+C` / 回车不发出复制与 `accepted`）。反向验证 `build-local/adhoc-output/reverse-check-longcapture.py` |
| REG-102 | **一点长截图就立刻结束并跳进批注**，一帧都没滚 | 0.9.6（用户反馈，REG-101 的回归） | 两处叠在一起。① `Platform::scrollAt()`（`app/platform_win.cpp`）在发滚轮之前加了一道 `scrollingWindowUnder(target, axis) == nullptr → return false` 的预检，而 `windowStyleScrolls()` 只认 `WS_VSCROLL` / `WS_HSCROLL`。**现在绝大多数页面（浏览器、Electron、各种自绘列表）根本不带这两个样式**，于是预检一律判"不能滚"，滚轮一个都没发出去——正是长截图最常用的那些窗口。② `Controller::finishScrollCapture()` 用 `picture.isNull()` 判断"什么都没拼出来"，可 `picture()` 从一开始就是选区那一帧、**永远不为空**，于是"没有滚动"被当成"拼了一张一帧的长图"直接 `editor_.setDocument()` 送进批注 | ① 删掉 `windowStyleScrolls()` / `scrollingWindowUnder()` / `scrollableAt()` 三件套，`scrollAt()` 恢复"把滚轮发出去"（这本来就是 0.9.6 之前能用时的行为），能不能滚交给回来的那一帧来判断；② `finishScrollCapture()` 改成在 `scroller_->stop()` **之前**取 `scroller_->frames() > 0` 当判据（`stop()` 会把计数清零，所以必须先读），为 0 才走"选区里的内容没有滚动"的提示而不是批注 | `tests/capture_test.cpp::aRunWhereNothingWasPlacedPlacedNoFrames`（第一帧即 `picture()`、非空；两次 `Repeat` 之后 `frames()` 仍为 0、高度不变；`stop()` 前读到的计数就是唯一判据）。**平台层的回归只有真机能验证**——`scrollAt` 的取舍写在注释里：是否有滚动条样式不可靠，所以恒发滚轮 |
| REG-103 | **长截图还是"选区里的内容没有滚动"**——滚轮发出去了，可页面一帧都没动 | 0.9.6（用户反馈，REG-101 与 REG-102 的延续） | `scrollAt()` 一直是"把光标挪到选区中部，再 `SendInput` 一个滚轮"，而滚轮会送到**光标下**的那个窗口。REG-101 之后覆盖层改成全程留在屏幕上（不再 `hide()`），而它是一扇盖住整块屏幕、且 `HWND_TOPMOST` 的窗——于是 `WindowFromPoint()` 每次都答"这是你自己"，旧代码那句 `if (process == GetCurrentProcessId()) return false;` 直接把滚轮**整批丢掉**，两轮 idle 之后收尾。`finishScrollCapture()` 的判据此时已按 REG-102 改成"已放置帧数为 0"，所以提示正好落在"选区里的内容没有滚动"这句话上。旧版本不会踩到，是因为旧实现开跑前先 `overlay->hide()`（`git show 21f6ee4:app/controller.cpp` 第 596 行），窗口不在屏幕上，`WindowFromPoint` 自然落到真正的应用上 | ① `platform.h` 新增 `void *windowUnderPoint(QPoint)`：从 `WindowFromPoint()` 沿 `GW_HWNDNEXT` **向下**走 z 序，跳过本进程的窗、不可见窗、最小化窗与 `DWMWA_CLOAKED` 的窗，取第一扇矩形真的罩住该点、且不是我们的顶层窗（先把候选 `GetAncestor(GA_ROOT)` 拔到顶层，否则往下走的是子窗的 z 序）。② `scrollAt()` 改成 `PostMessage(WM_MOUSEWHEEL / WM_MOUSEHWHEEL)` **直接发给那扇窗**，不再 `SendInput`——注入的滚轮只会落到光标下（也就是我们自己）。位置随 `lParam` 带上（屏幕坐标），因此`SetCursorPos` 也一并去掉：既省得把光标从用户正在拖的选区里拽走，也让"拖动框去够最后一屏"这件事不再被抢焦点。③ 方向反了：`WM_MOUSEWHEEL` 的 delta 正数是"滚轮朝远离用户的方向转"，即页面往回走；长截图要的是页面往前走，所以改成 `-steps * WHEEL_DELTA`（macOS 侧本来就是 `-steps`，两边至此同号）。④ macOS 同样的问题用 `CGEventPostToPid()` 解决：新增 `processUnderPoint()`（`CGWindowListCopyWindowInfo` 由前到后找第一扇非本进程、且 `CGRectContainsPoint` 的窗），找到就定向投递，找不到才退回 `CGEventPost(kCGHIDEventTap, …)` | `tests/platform_test.cpp::aCoverIsNotTheWindowAtAPoint`（真机：盖住整屏的顶层窗在位时，先断言 `WindowFromPoint()` **确实**答的是这扇盖子，再断言 `windowUnderPoint()` 答的**不是**它——这正是旧代码丢掉滚轮的那一步。反向验证 `build-local/adhoc-output/reverse-check-wheelcover.py`：把 `windowUnderPoint` 改回"OS 说什么就是什么"，断言 `windowUnderPoint(target) != cover` 报 FALSE，与用户看到的现象一致）。`tests/platform_test.cpp::aWheelReachesTheWindowBelowTheCaptureWindow`（真机：跑一个 `--wheel-probe` 子进程，在**另一进程**里放一扇窗，再用本进程的盖子压住它，`scrollAt()` 之后探针必须收到一个 **vertical、且步数为负**的滚轮；负号即"页面往前走"的方向。该用例在本沙箱跑不到底——`QProcess` 建管道被拦（既有的 `captureAndAccessibleElement` 同样报 `CreateFile failed`），只有真机/CI 能验证）。方向与 `wparam` 高字往返由 `build-local/adhoc-output/check-wheel-sign.py` 单独钉住 |

| REG-104 | **长截图的暗色遮罩被一起抓进了每一帧**（风险项，未发到用户手上，但要钉住）：遮罩是一扇真的盖住整块屏幕的顶层窗，只要它没被排除在抓屏之外，采集到的每一帧都会带一层 105/255 的暗色——整张长图发暗，且暗色会参与帧间匹配，让"页面停稳了"的判定失真 | 0.9.6（自查，非用户反馈） | 抓屏（`screen->grabWindow(0)`）拿到的是合成后的画面，窗口"透不透明"与"掺不掺进抓屏"是两件事。`ScrollShade` 只有在 `showEvent` 里调过 `configureNativeWindow(this, true)`（Win: `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`；mac: `NSWindowSharingNone`）才会隐身；而且它和取景框**同为 `HWND_TOPMOST`**，两扇顶层窗里后 show 的在上层，所以 `ensureShade()` 每次把遮罩 show 出来之后还要给取景框**重设一次** affinity 把它抬回遮罩之上——`raise()` 只争普通层的队首，对顶层窗不起作用 | `ScrollShade::showEvent()` 里设 affinity（`app/scrollshade.cpp`），`Overlay::ensureShade()` 在 show 之后重设取景框的 affinity（`app/overlay.cpp`）。⚠️ 这一条**只有真机能验证**：offscreen 平台没有合成器，`grabWindow` 不掺任何真实窗口，测试会假绿。`tests/scroll_controller_test.cpp::theLongCaptureFilmStaysOutOfTheFramesItReads`（真机：先在屏幕上放一块纯红 `rgb(200,30,30)` 的顶层窗，抓屏读它的红通道；再在**别处**开一个长截图，让这块红正好落在选区外、也就是被遮罩盖住的位置，重新抓屏；红通道必须几乎不变（≤ 20）。反向验证：把 `showEvent` 里的 affinity 去掉，红通道会被 105/255 的暗色压下去，断言失败） |

| REG-082 | 文字识别的两个用例**只在 macOS 上失败**——本机只编 Windows，跑了十几轮都绿，是 CI 的 macOS 任务第一次跑 OCR 才暴露的 | 0.9.5（CI 拦下，未发到用户手上） | ① `bridgeIsGivenNativePathsAndSafeQuoting` 断言 `C:/` 必须被换成 `C:\`，而这是 Windows 特有的路径转换，macOS 保留斜杠，断言必然不成立；② macOS 侧（Vision）把识别语言写进了 `OcrResult::engineLanguage`，却没回写 `OcrEngine::language_`，于是 `engine.engineLanguage()` 一直是空 | `tests/ocr_test.cpp` 把反斜杠那两条收进 `#ifdef Q_OS_WIN`（单引号转义那部分跨平台依然有效，仍会断言）；`OcrEngine::finish()` 统一从 `result.engineLanguage` 回写 `language_`，两条路径不再各写一半。教训：**新增平台相关代码时本机跑绿不等于 CI 绿**，`app/*_mac.mm` 与 macOS 分支只有 CI 能验证 |

关于文字识别的实现选择：发布包用 MinGW 构建，没有 C++/WinRT，所以 Windows 侧走"内置 PowerShell 桥接系统 `Windows.Media.Ocr`"这条路（`app/ocrbridge.ps1` + `app/ocr.cpp`）；macOS 侧直接用 Vision（`app/ocr_mac.mm`）。两边都不联网、不上传、不新增依赖。

## 十、无法用测试固化的遗留项

以下问题真实发生过，但属于环境或流程问题，无法在仓库内写成可靠的自动化断言。它们记录在此以免再次误判。

| 编号 | 现象 | 处理方式 |
| --- | --- | --- |
| ENV-001 | 沙箱/无 GUI 环境里 `git` 对所有非 GitHub 主机 HTTPS 推送无限卡死 | 用户级约定：为该主机单独设置 `credential.https://<host>.helper store`。见 `~/.workbuddy/MEMORY.md` |
| ENV-002 | 本机 `windeployqt` 只释放 `platforms/qwindows.dll`，widget 测试用 `-platform offscreen` 会全部挂死（150s 超时） | 需要 `build-local/platforms/qoffscreen.dll`。只影响本机手工部署，不影响 CI 与发布包 |
| ENV-003 | `windeployqt --compiler-runtime` 在本机不生效，测试 exe 脱离 Qt PATH 后 `rc=127` | 手工从 `VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT/` 拷 `msvcp140*.dll`/`vcruntime140*.dll`/`concrt140.dll` |
| ENV-004 | `ctest` 启动子进程报 `0xc0000135`（找不到 DLL），直接运行测试 exe 正常 | 本机验证直接跑测试二进制；CI 用 CTest 正常 |
| ENV-005 | MSVC 链接报 `LNK1104 无法打开文件"EditHere.exe"`，误判为"用户还开着程序"，实际是沙箱回收后 `tasklist` 里的残留条目 | **不要用 `mv` 做判据**：映射中的映像文件允许改名、拒绝写入，改名成功也可能仍被占用（0.9.3 时代实测踩到）。正确判据是查**可执行路径**——`Get-CimInstance Win32_Process -Filter "Name='EditHere.exe'"` 的 `ExecutablePath` 指向构建目录即为自己上一轮的残留，直接 `taskkill /F /PID`；PowerShell 的 `Stop-Process` 在沙箱里不生效 |
| ENV-006 | 沙箱在每次工具调用结束时回收该调用创建的所有子进程，无法替用户把 GUI 程序留在桌面上 | 交付可交互程序时给出确切路径让用户自己双击 |
| ENV-007 | `agent_cli_tests`（依赖管道）与 `platform_tests`（依赖真实桌面）在本机不是回归信号 | 改动前后对比二者结果是否一致，而不是把失败当回归 |
| ENV-008 | 公开英文名"大爆炸 = Explode"的文档侧一致性 | 由 REG-023 的 `english_names` 检查覆盖，但 README 措辞本身仍需人工审阅 |
| ENV-009 | offscreen 平台没有字体目录，任何依赖真实字形的绘制都会得到空白图，容易让测试假通过 | 需要真实文字时自绘点阵字形（见 REG-062），或改用 `WINDIR/Fonts` 下的系统字体并接受平台绑定 |
| ENV-010 | offscreen 平台的 `QCursor::setPos()` 不只是不出事件，它还会在下一次事件循环里回一个**恰好差 1 像素**的"已到达" MouseMove（真机是 0.2 像素，被回声判定挡掉） | 断言"按完方向键之后的状态"时**不要在按键后 `qWait`**——那一等就把这个假移动派发出去，走 `mouseMoveEvent` 重新选块，用例凭空失败（REG-092 的 UI 用例踩到）。要等就等在那之前（如等 `findChildren<QProcess *>()` 清空） |
| ENV-011 | 需要在**真实桌面**上跑的 Qt 测试，常常是"用例跑完、结果文件写全，但进程不退出"（长截图的 `scroll_controller_tests` 在真机上就是 7 秒跑完却要手工 `taskkill`） | 真机验证请用 `-o <文件>,txt` 取结果，别等进程返回；判定看结果文件里的 `Totals` 行 |

## 新增检查清单

本次归档补齐的检查（`REG-xxx` 列中标记 🆕 的条目）：

| 检查 | 位置 | 覆盖条目 |
| --- | --- | --- |
| `packaging` | `scripts/check-packaging.py`，注册为 CTest 测试 | REG-001/002/004/005/006/007/008/009/010/011/012/019/020/022/023/056/057/058/061/063/151/152/153/154/155/156 |
| `translations` | `scripts/check-translations.py`，注册为 CTest 测试（需要 Linguist） | REG-021 |
| `update_tests::staleDownloadIsDroppedBeforeAppending` | `tests/update_test.cpp` | REG-003 |
| `i18n_tests::brokenProjectMessagesStayOneString` | `tests/i18n_test.cpp` | REG-026 |
| `canvas_feedback_tests::layoutGuidesStayVisibleForUnselectedComponents` | `tests/canvas_feedback_test.cpp` | REG-037 |
| `ocr_tests`（14 例） | `tests/ocr_test.cpp`，CTest 名 `ocr` | REG-061/062 |
| `capture_tests`（29 例） | `tests/capture_test.cpp`，CTest 名 `capture` | REG-059/060/066/067/069/070/071/072/073/074/075/077/079/105/106 |
| `ui_tests`（75 例） | `tests/ui_test.cpp`，CTest 名 `ui` | REG-065/068/076/080/083/085/086/087/088/089/090/091/092/107/108/110 |
| `core_tests`（22 例） | `tests/core_test.cpp`，CTest 名 `core` | REG-092 |
| `platform_tests`（6 例） | `tests/platform_test.cpp`，CTest 名 `windows-platform`（仅 Windows，且需要真实桌面） | REG-031；其中 `captureAndAccessibleElement` 依赖另起子进程，本机沙箱拦建管道时跑不到底，只有真机与 CI 能定论 |
| 发布资产完整性 | `.github/workflows/release.yml` 的 `Require every package before publishing`；四个平台包任一缺失即失败 | REG-109/122 |

旧长截图的做法对照过两个开源实现（**该段描述 `feature/long-capture` 的历史方案，当前实现见 [LONG-CAPTURE.md](LONG-CAPTURE.md)**），取舍记在这里：ShareX 的 `ScrollingCaptureManager` 用 `ScrollDelay` 等页面停稳、用 `ScrollMethod`（滚轮/方向键/PageDown/`WM_VSCROLL`）适配不同窗口、并保留"历史最佳匹配"把部分成功标成黄色；deepin-screen-recorder 的 `PixMergeThread` 用 `getTopFixedHigh()`/`getBottomFixedHigh()` 先把固定的顶底栏裁掉再拼接，并用 `cv::matchTemplate` + 0.8 阈值匹配。EditHere 采纳了**固定顶底栏检测**、**抓到帧先确认页面已停稳**、**一次没新内容再补一轮**和**容差阶梯**（等价于 ShareX 的部分成功，用 `partial()` 报告），没有采纳自动回到顶部（`AutoScrollTop` 默认为假，且会把"从这里往下截"变成"从整页开头截"）与多滚动方式（需要平台侧新增按键注入，暂不在范围里）。

`scripts/check-packaging.py` 不依赖 Qt 与 NSIS（找不到 `makensis` 时只跳过实编译，其余检查照常执行），所以它能在 CI 里跑；`scripts/check-translations.py` 需要 `lupdate`，CI 的精简 Qt 没有 Linguist，因此只在本地 Qt 完整安装时注册为测试。i18n 的工具无关部分（`.ts` ↔ `.qm` 逐条比对）已并入 `check-packaging.py`，保证 CI 也能拦住"改了 `.ts` 忘了 `lrelease`"。

## 新增问题时的约定

1. 先在本页分配一个 `REG-xxx`。
2. 修复时**同时**补一条会因这个 bug 而复发的自动化检查；优先选仓库内可复现的断言，而不是"我记得改过了"。
3. 确实无法写成测试的，写进第九节并说明原因，不要留空。
4. CHANGELOG 里写用户可见的变化，本页写内部原因与护栏。
