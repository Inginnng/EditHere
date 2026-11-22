# 跨技术栈画面采集 SOP

本指南定义如何为 EditHere 采集高保真、与当前代码一致的截图。无论后续交给桌面端、MCP Apps 还是普通网页批注，采集质量直接决定 AI 定位和修改的准确度。

## 1. 先判定图片来源

- **用户已提供图片或设计稿**：直接使用原文件并保持原始像素尺寸，无需重复截图。
- **用户要求评审当前运行页面/游戏界面**：必须从实际运行的程序或页面截取本轮画面。目录中的旧图、示例图、或固定种子/固定帧测试渲染不能替代当前真实运行画面。
- **精准定位目标**：依据项目路由、打开的窗口或任务上下文锁定目标页面（注意排除 EditHere 自身窗口）；仅在无法唯一确定目标时向用户确认。

## 2. 文件命名与画质要求

- 新截图放入工作区 `.edithere/captures/`（或本轮组图目录 `.edithere/results/shots/round-NNN/`），使用唯一文件名与绝对路径，避免覆盖用户原有文件。
- 优先使用**无损 PNG**，并核对图像真实像素宽高与屏幕 DPR（高 DPI 下逻辑尺寸与真实设备像素不同）。不要把缩小后的 JPEG 预览当作原图，也不要通过改扩展名伪装无损 PNG；若工具仅支持 JPEG，应保留真实扩展名并如实说明质量限制。

## 3. Web 前端采集 SOP

### 3.1 页面与状态枚举

1. 扫描项目路由配置、导航菜单与页面目录，列出全部已实现且可访问的路由。
2. 对每个页面补充与本轮任务直接相关的独立状态：关键弹窗/抽屉、空状态/错误态、以及有验收要求时的浅色/深色主题、桌面端（如 `1440x900`）与移动端（如 `390x844`）视口。
3. 登录态或后端数据阻断某些页面时，保留已完成页面并在结果中说明缺口，不伪造覆盖率。

### 3.2 优先使用技能自带的 `scripts/capture-web.mjs`

当项目或环境已安装 `playwright` / `playwright-core` 时，优先调用本技能自带的 `scripts/capture-web.mjs`（自动等待网络空闲与 `document.fonts.ready`，输出 DPR=2 无损 PNG，可选直接更新组图清单）：

- **单页采集（并可选同步到组图清单）**：

```powershell
node "<skill-dir>/scripts/capture-web.mjs" `
  --url "http://127.0.0.1:5173/inventory" `
  --output ".edithere/results/shots/round-001/inventory.png" `
  --viewport 1440x900 --dpr 2 --theme light `
  --manifest ".edithere/results/project.json" --id inventory --name "物品栏"
```

- **批量采集（一次启动浏览器顺序采集多页）**：准备 `batch.json`（含 `pages` 数组，每项 `{ id, name, url, output, viewport?, dpr?, theme?, fullPage?, selector?, waitMs? }`），执行：

```powershell
node "<skill-dir>/scripts/capture-web.mjs" `
  --batch ".edithere/captures/batch.json" `
  --manifest ".edithere/results/project.json"
```

若返回 `code: "playwright_not_found"`，不要擅自替用户全局安装包，改用宿主已连接的浏览器截图工具。

### 3.3 宿主浏览器工具（已登录会话 / 无 Playwright 时）

- 先读取宿主浏览器工具文档，根据真实标签页列表定位目标页面；仅截取当前可见视口（`fullPage: false`）或按需求截取整页。不要强行刷新用户正在手动操作的页面状态。
- **Tabbit 特别注意**：`page.screenshot()` 返回的是 PNG 文件路径回执，读取 `result.nextAction.path` 即可（不是 Playwright Buffer，不要调用 `.toString('base64')`）。
- 其他工具若只返回内存图像或截图块，使用其正式导出能力保存成真实 PNG 后再检查路径；对话里贴过预览图不等于磁盘上已有高分 PNG。

## 4. 桌面应用与游戏 HUD 采集 SOP

- **窗口优先于全屏**：使用宿主获准的窗口截图能力精确截取目标窗口，避免无关桌面背景混入；截取前确认窗口未被遮挡、未处于加载启动页或错误弹窗。
- **游戏 / 多状态界面**：按功能进入真实可玩状态（主界面、背包/物品栏、战斗 HUD、设置弹窗等）分别截帧；区分「用户当前运行窗口的实时截图」与「项目渲染脚本按当前代码重现的截图」，如实向用户标注来源。
- **Windows `computer-use` / `@oai/sky` 注意事项**：先通过 `list_windows()` 获取真实窗口标识，再调 `get_window_state({ window, include_screenshot: true, include_text: false })`；确认 `screenshots` 中唯一对应目标画面后解码 `data:image/(png|jpeg);base64,...` 落盘，不要打印完整 data URL 到对话中。
- **`edithere-cli capture` 的边界**：它只唤起交互式桌面截图并立即返回受理状态，**不接受 `--output` 或 `--window` 参数**，不能代替静默窗口截图脚本。

## 5. 交付前视检清单（5 项必查）

把截图加入组图或提交批注前，必须快速核对：
1. 图片文件存在、非空且可正常解码；
2. 画面确属目标页面及目标主题/视口；
3. 无黑屏、白屏、骨架屏、未加载完的字体或无关系统弹窗遮挡；
4. 保留原始像素分辨率与无损格式（无非预期缩放裁剪）；
5. 页面内容与当前最新代码版本一致。
