# EditHere 共享操作图标

`manifest.json` 是功能图标唯一清单；`icons/*.svg` 是 Qt 和 Web 共用的几何源。应用品牌继续使用 `assets/icons/edithere.svg`，不属于操作图标库。

成熟功能优先使用 Lucide 1.55.0 官方 SVG。固定 Git revision 为 `70562c1ee1c4fdcf736fe97bc893fb8511927934`；来源是官方 `lucide-static` npm 发行包，包地址及已验证的 SHA-512 integrity 保存在 manifest。这里不需要 npm 运行依赖，不在构建时联网下载图标。保留的原始许可见 `LICENSE`（Lucide ISC，以及 Feather 衍生图形的 MIT 许可）。

视觉规则见 [图标视觉规范](../../docs/icon-visual-spec.md)。所有几何使用 24×24 坐标、2 单位线宽、圆端、圆连接、单色 `currentColor`；通常显示为 20px，紧凑控件按现有布局显示 16–18px，独立展示 24px。不要为某个系统或页面重新画一份，也不要单独覆盖线宽。

`source: lucide` 表示官方 SVG；`source: edithere` 表示按同一规范补充的专用图形。圆角图形由 Lucide `square-round-corner` 改为单条连续外轮廓，长截图及四向裁剪图形是 EditHere 自有几何。所有修改都保留源图标与来源链接。

最终已选 `scroll-frame-arrows`、`corner-one-large` 和 `explode-blocks`，已应用到 `icons/` 的共享运行时 SVG。`candidateGroups.selected` 记录用户确认的选择，`recommended` 仅表示候选建议；二者可以不同。[图标方案 HTML](../../tools/icon-review/EditHere图标方案.html) 保留其他备选，提供 16/20/24px 尺寸和明暗背景预览。候选切换只更新方案页，完整功能清单仍展示当前程序图标；重新生成方案页默认选中已确认的三项。后续修改共享 SVG 后重新构建 Qt 资源和 Web 内联 sprite。运行 `node tools/icon-review/build.cjs` 可重新生成方案页。

新增功能先选语义 ID，再使用已有成熟图形。只有不存在适合图形时才补充自绘。图标名称对应操作及其对象，不按具体系统命名。明确区分项目、图片、JSON 文件、JSON 正文和反馈目录；相同图片复制操作共用 `image-copy`。

通用 X 可用于关闭或取消，通用加减可用于数值增减；窗口最小化是标准减号，播放与长截图开始共用标准三角。它们的上下文与标签明确，属于有意使用的通用符号。删除数据使用垃圾桶；恢复默认使用旋转复位箭头；撤销一步使用回退弯箭头。

旧含混 ID `json-copy` 已拆为 `json-copy-file` 和 `json-copy-text`。没有实际调用的 `picker`、`scroll-download`、`scroll-edit`、`scroll-pin`、`scroll-scissors` 不继续收录；完整退役清单保存在 manifest。
