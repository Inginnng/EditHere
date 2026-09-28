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

## 五、大爆炸与布局

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-037 | 选中并移动一个块后，其他块的蓝色引导框全部消失 | 0.9.0 | 绘制引导框时把"有选中项"当成了"不画引导框"的条件 | `tests/canvas_feedback_test.cpp::layoutGuidesStayVisibleForUnselectedComponents` | 🆕 |
| REG-038 | 嵌套块被移出父组后仍跟随父组移动 | 0.9.0 | 移出父组时未立即解除父子关系 | `tests/layout_test.cpp::movedChildStaysIndependentOfLaterParentTransforms` | ✅ |
| REG-039 | 整体移动大块只显示一条轨迹；单独调整小块时不增加它自己的轨迹 | 0.8.9 → 0.8.10 | 轨迹按像素分区而非按实际调整的选区记录 | `tests/layout_test.cpp::trajectoriesTrackSelectedGroupsInsteadOfNestedPixelPartitions`、`tests/core_test.cpp::separatelyMovedRegionsKeepTheirOwnTrajectories` | ✅ |
| REG-040 | 大爆炸拖动过程中窗口失焦，未提交的操作被保留下来 | 0.8.0 → 0.8.1 | 失焦时未取消进行中的手势 | `tests/ui_test.cpp::explosionCancelsInterruptedGesturesAndPreservesSelectionOnFocusChange` | ✅ |
| REG-041 | 嵌套移动的箭头与编号关系错乱；同一区域反复调整生成多条轨迹 | 0.8.10 | 合并规则按像素级分区而不是按跟踪组 | `tests/canvas_feedback_test.cpp::nestedMovementsKeepOneArrowPerSelectedComponent`、`mergedMovementKeepsAdditionalNoteBadgesEditable`、`tests/layout_test.cpp::minimalChangesMergeAParentAndKeepNestedEditsSeparate` | ✅ |

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

## 八、版本与仓库材料

| 编号 | 现象 | 首现 → 修复 | 根因 | 回归检查 | 状态 |
| --- | --- | --- | --- | --- | --- |
| REG-056 | 程序版本在多个文件里各说各话（`connector/README.md` 停在 0.8.21，程序已到 0.9.2） | 0.8.0 → 0.9.2 | 版本号没有唯一来源，改一处漏一处 | `check-packaging.py::version_consistency` | 🆕 |
| REG-057 | 打包脚本把旧程序标记成新版本 | 0.8.0 → 0.8.1 | 打包脚本里另写了一份版本号 | `check-packaging.py::version_consistency`（`project()` 为唯一来源；`version.txt` 由 CMake 构建后生成，打包脚本只读它） | 🆕 |
| REG-058 | 在 Windows 上用 Visual Studio 生成器构建直接失败（`MSB6001 ... 关键字 "PATH"`） | 开发环境 | 环境里同时存在 `Path` 与 `PATH` 两个拼写，MSBuild 的 ToolTask 崩溃 | `check-packaging.py::build_recipe`（脚本、文档、CI 中不得出现 VS 生成器，只用 Ninja） | 🆕 |

## 九、截图工具条、长截图与文字识别

截图不再"松手即批注"：松开鼠标只把选定的区域定下来，选区上方的工具条（批注、识别、贴图、保存、复制、长截图）与它右侧的样式列（圆角、阴影/边框、贴图、重置）才是动作的入口。本节记录实现这套流程时踩到的问题。

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

## 新增检查清单

本次归档补齐的检查（`REG-xxx` 列中标记 🆕 的条目）：

| 检查 | 位置 | 覆盖条目 |
| --- | --- | --- |
| `packaging` | `scripts/check-packaging.py`，注册为 CTest 测试 | REG-001/002/004/005/006/007/008/009/010/011/012/019/020/022/023/056/057/058/061/063 |
| `translations` | `scripts/check-translations.py`，注册为 CTest 测试（需要 Linguist） | REG-021 |
| `update_tests::staleDownloadIsDroppedBeforeAppending` | `tests/update_test.cpp` | REG-003 |
| `i18n_tests::brokenProjectMessagesStayOneString` | `tests/i18n_test.cpp` | REG-026 |
| `canvas_feedback_tests::layoutGuidesStayVisibleForUnselectedComponents` | `tests/canvas_feedback_test.cpp` | REG-037 |
| `ocr_tests`（14 例） | `tests/ocr_test.cpp`，CTest 名 `ocr` | REG-061/062 |
| `capture_tests`（36 例） | `tests/capture_test.cpp`，CTest 名 `capture` | REG-059/060/066/067/069/070/071/072/073/074/075/077/079/081 |
| `ui_tests`（69 例） | `tests/ui_test.cpp`，CTest 名 `ui` | REG-065/068/076/080/083/085/086/087/088/089/090/091/092 |
| `core_tests`（22 例） | `tests/core_test.cpp`，CTest 名 `core` | REG-092 |

长截图的做法对照过两个开源实现，取舍记在这里：ShareX 的 `ScrollingCaptureManager` 用 `ScrollDelay` 等页面停稳、用 `ScrollMethod`（滚轮/方向键/PageDown/`WM_VSCROLL`）适配不同窗口、并保留"历史最佳匹配"把部分成功标成黄色；deepin-screen-recorder 的 `PixMergeThread` 用 `getTopFixedHigh()`/`getBottomFixedHigh()` 先把固定的顶底栏裁掉再拼接，并用 `cv::matchTemplate` + 0.8 阈值匹配。EditHere 采纳了**固定顶底栏检测**、**抓到帧先确认页面已停稳**、**一次没新内容再补一轮**和**容差阶梯**（等价于 ShareX 的部分成功，用 `partial()` 报告），没有采纳自动回到顶部（`AutoScrollTop` 默认为假，且会把"从这里往下截"变成"从整页开头截"）与多滚动方式（需要平台侧新增按键注入，暂不在范围里）。

`scripts/check-packaging.py` 不依赖 Qt 与 NSIS（找不到 `makensis` 时只跳过实编译，其余检查照常执行），所以它能在 CI 里跑；`scripts/check-translations.py` 需要 `lupdate`，CI 的精简 Qt 没有 Linguist，因此只在本地 Qt 完整安装时注册为测试。i18n 的工具无关部分（`.ts` ↔ `.qm` 逐条比对）已并入 `check-packaging.py`，保证 CI 也能拦住"改了 `.ts` 忘了 `lrelease`"。

## 新增问题时的约定

1. 先在本页分配一个 `REG-xxx`。
2. 修复时**同时**补一条会因这个 bug 而复发的自动化检查；优先选仓库内可复现的断言，而不是"我记得改过了"。
3. 确实无法写成测试的，写进第九节并说明原因，不要留空。
4. CHANGELOG 里写用户可见的变化，本页写内部原因与护栏。
