# EditHere 图标视觉规范

自 0.11.1 起适用：Windows、macOS、Linux 桌面操作图标及 Web / MCP 界面。品牌图和品牌文字使用现有资源。跨端共用 `assets/ui-icons/manifest.json` 及其 SVG 文件，不按操作系统维护不同图形。当前运行时清单包含 67 个图标：61 个 Lucide 官方图形及 6 个 EditHere 自有或改绘图形。

## 来源与选择

主库为 [Lucide](https://lucide.dev/icons/)，固定到 1.55.0，Git commit `70562c1ee1c4fdcf736fe97bc893fb8511927934`。常规功能优先采用同库现有图标，避免混搭多个视觉体系。参考官方 [设计语言](https://lucide.dev/contribute/icons/design-principles)、[设计规格](https://lucide.dev/contribute/icons/specification)、[图标选择指南](https://lucide.dev/how-to/icon-guide) 和 [许可](https://lucide.dev/license)。

图形应支持文字理解：成熟符号用于成熟操作，复杂或独特功能保留准确的提示和可访问名称。不得用“看起来差不多”的图形替代不同输出对象，也不得为同一个操作在不同窗口临时绘制另一套形状。

## 几何

| 项目 | 规则 |
|---|---|
| 坐标 | `viewBox="0 0 24 24"` |
| 线宽 | 2 逻辑单位；随图形等比缩放 |
| 端点 / 连接 | `round` / `round` |
| 填充 / 颜色 | `fill="none"`，单色 `stroke="currentColor"` |
| 安全边距 | 实际描边距画布边缘至少 1 单位 |
| 留白 | 互不连接的元素尽量保留至少 2 单位可见间距 |
| 圆角 | 大于 8 单位的常规直角取半径 2，小细节取半径 1；特殊功能允许更大半径 |
| 曲线 | 连续轮廓、平滑切线，优先简洁圆弧或二次曲线 |
| 密度 | 不堆叠说明文字线、额外角标或装饰；20px 时仍能分辨主体 |

圆连接不能代替几何圆角。圆角轮廓必须由同一条路径连续过渡，不能先画完整直角再叠圆弧。框、圆、箭头等基础几何优先沿用 Lucide 既有路径。

## 尺寸、颜色与状态

通常显示 20×20px；紧凑控件按现有布局显示 16–18px；独立展示为 24×24px。20px 图形的实际线宽约 1.67px，18px 为 1.5px，16px 为 1.33px；不另行改变 `stroke-width` 补偿。Qt 使用 SVG 按设备像素比栅格化，Web 内联同一 SVG 几何。

同一区域保持图标尺寸和按钮留白一致。图标使用界面现有前景色；悬停、选中、禁用由按钮及主题样式统一处理，不在 SVG 内保存主题色。选中操作保持形状稳定，使用按下态与强调色；显隐状态使用 `eye` / `eye-off`。状态切换及加载新素材时，图标、按下态、实际功能状态和提示必须同步。

## 功能语义

| 操作 | 共享 ID | 采用图形 | 说明 |
|---|---|---|---|
| 保存可编辑项目 | `save` | Lucide save | 软盘仅对应项目保存 |
| 保存图片 | `image-save` | Lucide image-down | 图片主体 + 下载箭头 |
| 复制图片 | `image-copy` | Lucide images | 各截图、长截图、编辑器使用同图 |
| 查看 JSON | `json` | Lucide file-braces（file-json） | 文件 + 花括号，区别于批注显隐 |
| 保存 JSON 文件 | `download` | Lucide file-down | 文件 + 下载箭头 |
| 复制 JSON 与图片 | `json-copy-file` | Lucide files | 复制文件对象 |
| 复制 JSON 正文 | `json-copy-text` | Lucide clipboard-type | 文字进入剪贴板 |
| 保存反馈目录 | `feedback-save` | Lucide folder-output | JSON 与图片组合输出 |
| 普通文本 / 路径复制 | `copy` | Lucide copy | 不代替 JSON 或图片的专用操作 |
| 应用设置 | `settings` | Lucide settings | 齿轮 |
| 自定义工具栏 | `layout-settings` | Lucide sliders-horizontal | 横向滑杆 |
| 组件布局调节 | `layout` | Lucide sliders-vertical | 竖向滑杆 |
| 局部放大镜 | `magnifier` | Lucide search | 无加号镜头 |
| 整体放大 / 缩小 | `zoom-in` / `zoom-out` | Lucide zoom-in / zoom-out | 镜头内带加减号 |
| 添加批注 | `note-add` | Lucide message-square-plus | 不与画布放大共用加号 |
| 撤销 / 重做 | `undo` / `redo` | Lucide undo-2 / redo-2 | 单步历史操作 |
| 恢复默认 / 复位 | `reset` | Lucide rotate-ccw | 区别于撤销一步 |
| 二维移动 | `move` | Lucide move | 四向；运行中的长截图按轴使用 move-vertical / horizontal |

完整清单在 manifest 中记录每个功能的图形、来源、固定版本、文件校验值。关闭 / 取消等公认通用符号可复用，不要求每个按钮都有一个独一无二的图形。含义不同且容易混淆的同区操作必须区分。

## 本轮专用图形与候选

长截图推荐使用“长矩形 + 上下箭头”，只保留长画面与滚动两个概念。备选为 Lucide `stretch-vertical` 与 `rectangle-vertical`；后者更简洁但含义需要标签支持。

圆角推荐由 Lucide `square-round-corner` 改为单条连续轮廓，右上大圆角，其余为小圆角。另提供整体圆角矩形、开放单角两种候选；三种都不含重叠的直角和圆弧。

大爆炸最终选择 Lucide `blocks` 原版（`explode-blocks`）：L 形组合与右上独立方块表达组件拆出，已更新 `icons/explode.svg`。长截图与圆角分别确认 `scroll-frame-arrows`、`corner-one-large`，三项运行时 SVG 均与所选候选逐字节一致。共享清单记录已选项、来源和 SHA256，桌面与 Web 构建统一使用该源。

大爆炸是抽象功能，保留“大爆炸 / 拆分组件”标签和开关状态。图形表达分离的图层或组件，不采用火花、烟花和其他装饰性“爆炸”符号；避免用全屏箭头和普通阴影图形替代拆解含义。第二轮 3 个改绘在 16px 时仍保留可见间隔：完整双层之间约 2.12px；错位双层更宽；组件浮起的两个上层块之间约 1.33px，与底层约 2.4px。

四向长截图裁剪采用一致旋转关系：实线轮廓表示保留部分，断线表示移除部分，单独的分割线表示裁剪边界。裁去轮廓以两侧配对的圆弧和中央短线明确绘制，不使用从单端自动铺设的 `stroke-dasharray`，防止末段错位造成不对称。圆角统一为半径 2 的圆弧；断线之间、断线与分割线之间保留 2 单位净空。上/下图形沿竖直中轴对称，左/右沿水平中轴对称；四方向从同一几何绕 (12,12) 旋转得到。SVG 保存已展开的几何坐标，不在不同端重新解释旋转。

其他候选保留在评审页，不作为运行时独立风格混用。`selected` 表示用户已确认的图形，`recommended` 表示设计建议；评审页默认选中 `selected`。后续调整共享源时重新生成两端资源。

## 许可与维护

Lucide 官方许可保存在 `assets/ui-icons/LICENSE`，所有分发保留该文件。官方原图保持不变，改造图形明确记录衍生来源，自有图形遵循项目许可。SVG 不加载远程资源，不使用脚本、字体、嵌入位图或滤镜。

新增 / 替换图标时验证：24px 视口与 2 单位线宽、XML 可解析、已知功能映射完整、显隐等状态同步、16/20/24px 明暗预览、桌面与 Web 使用相同源图形。只有新增的实际风险需要补充回归用例。
