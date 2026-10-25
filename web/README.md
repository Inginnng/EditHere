# EditHere 网页版

[English](README.en.md)

双击本目录的 **EditHere.html** 即可使用。也可以把这个 HTML 放到普通静态网站，让用户在线打开；页面加载完成后，素材读取、批注、布局调整与导出均在浏览器本地完成。独立 HTML 没有服务端处理、CDN、账号或外部运行时依赖，内容安全策略禁止网络连接。下文的 MCP Apps 模式由宿主管理 stdio 与内嵌 HTML；普通浏览器兼容模式使用本机服务。

在 Agent 的浏览器侧栏预览时，在项目根目录运行 `node web/preview.cjs`（需要已安装的 Node.js），再打开 `http://127.0.0.1:7330/EditHere.html`。端口被其它工作区占用时，可设置 `EDITHERE_WEB_PORT`（1–65535）；预览服务和 `web/agent.cjs` 必须使用相同的值。兼容启动脚本会复用本项目的现有服务，或尝试启动后台服务并验证当前 HTML 的响应；进程可能随宿主停止而结束，不能保证独立常驻。“拒绝建立连接”表示本地服务没有监听，刷新页面不能代替启动服务。预览仅监听本机，不上传素材。

## MCP Apps 与 Agent 技能

首次安装将 [AI 安装指南](AI-SETUP.md) 和实际的 `web-agent` ZIP 交给 Agent。下载包包含打包后的服务与官方 App SDK，不需要 npm install。MCP 需要 Node.js 20+，普通浏览器兼容 CLI 支持 Node.js 18+；均无需桌面 EditHere。

MCP 接入沿用 [Cowart 的 stdio 服务和 UI 资源](https://github.com/zhongerxin/Cowart/blob/main/mcp/server.mjs)，使用 [官方 MCP Apps SDK](https://github.com/modelcontextprotocol/ext-apps)：工具通过 `_meta.ui.resourceUri` 关联 HTML 资源，宿主显示内嵌界面；页面通过 App SDK 调用保存工具并请求发送对话消息。此入口不监听 7330、不依赖本机 HTTP 服务器，也不加载 CDN。

在宿主配置中合并 `web/mcp/mcp.json` 条目，将 Node.js 与 `web/mcp/server.bundle.cjs` 替换为本机绝对路径。重新载入后确认实际工具 `edithere_web_open` / `edithere_web_read`。调用 open 传入实际绝对 `imagePath`、可选 `name/outputPath`，查看 `uiSupport/state` 后再确认图片已经显示。宿主未声明 MCP Apps 能力时返回 unsupported 与兼容入口，不创建待提交空会话。WorkBuddy 当前版本是否支持内嵌需实测，不能从 CodeBuddy 文档推定。

安装可选技能：

```text
node web/install-skill.cjs
```

默认技能目录为 `$CODEX_HOME/skills/edithere-web` 或 `~/.codex/skills/edithere-web`；其他宿主用 `--destination <技能绝对目录>`。安装器核对文件，在技能本机引用中记录 MCP 与 CLI 路径及配置参考，不自动改宿主 MCP 设置，不覆盖已有技能，不修改桌面技能。保留解压运行时，移动后需重新定位。

用户说“帮我用 EditHere 批注游戏主页”时，Skill 优先使用已连接的 MCP；没有内嵌能力时走兼容入口。截图来自宿主实际浏览器或窗口工具，必须是本轮目标画面；EditHere 本身不实现系统截图，旧图与固定测试渲染不能冒充当前界面。

MCP 页面的“反馈”先持久化完整桌面兼容 JSON，再请求宿主发送消息。保存成功、消息接受和 AI 实施分别报告；宿主拒绝或不支持消息时，已保存结果仍可读取，用户回到对话说“继续”。Agent 用原 sessionId 调用 read，核对 completed 与 SHA 后处理。宿主接受消息不代表已经修改代码，updateModelContext 也不会自行唤醒 AI。

已确认保存的 MCP 草稿和会话默认位于 `~/.edithere/web-sessions`，可用 `EDITHERE_WEB_DATA_DIR` 指定持久绝对目录。宿主重启后可 open 原 sessionId 恢复草稿或查看已提交结果；停止/取消后不自动重开。输出默认是原生反馈 JSON 与同目录原图，可选内嵌或仅原图信息，并可附带编号 JPEG；read 回执省略图片编码。不要把私有会话、图片、令牌和日志加入分发包。

普通浏览器兼容流程：

```text
node web/agent.cjs start --image <实际图片绝对路径> --output <尚不存在的JSON绝对路径> --timeout 86400
node web/agent.cjs poll --session <start返回的sessionId>
```

打开 start 返回的会话 URL。此模式没有宿主消息桥，默认提交后手动“继续”，不注册定时任务；已提交结果可在服务停止后读取。仅本次明确选择调度时使用技能中的跟进规则，不能承诺无痕或即时唤醒。关闭标签本身不等于取消服务端会话，用户明确停止时尊重停止并清理关联任务。普通静态/离线 HTML 保留下载与复制导出。

## 使用

1. 拖入图片、视频，点击「打开素材」，或粘贴图片。一次编辑一个素材。
2. 图片支持智能选块、点批注、框批注与全局意见。使用「选择」（V）拖动批注点或框，框的八个控制点可调整范围；点击编号编辑文字。
3. 点击「大爆炸」进入组件编辑并播放桌面版同方向的扫光动效；再次点击退出，保留已有位移与批注。选中组件后拖动，八个方向的控制点可调整尺寸；点击全局批注左侧的「组件布局」小按钮，可在浮层中输入位置与尺寸。浮层默认收起，不占批注面板高度。Ctrl / ⌘ + 点击多选组件，整体移动和缩放；指向已选组件滚轮等比缩放，未选中时滚轮切换同一位置不同大小的候选区域。「框出组件」补充未自动识别的区域。移动与缩放显示原位置到目标位置的箭头，点击箭头或对应布局批注添加文字，再次点击可编辑；文字随项目保存，并归入反馈中同一个位移对象。选中组件后可「复位」。
4. 视频播放后暂停，在当前画面批注；点击已标注时间点可以回看。只有实际批注或移动的画面进入导出，不会把每次暂停都保存成一帧。
5. 顶部保存按钮可选择文件交付、复制 JSON 内容、保存项目或查看 JSON；选择菜单只记住方式，主按钮或 Ctrl / ⌘ + S 才执行。“查看 JSON”使用 **原图嵌入 JSON** 复选框：默认交付 JSON 与独立 PNG，勾选后内嵌。**复制 JSON 与图片** 交付配套文件；浏览器文件剪贴板受限时按提示下载 ZIP。“复制 JSON 内容”默认不携带原图编码，旁边竖向 **…** 可选择携带或不携带，并单独记住；其选择不改变文件交付。取消原图模式下拉、示意图复选框及重复保存操作。CLI/API 的显式图片模式和编号预览选项仍保留。
6. 顶栏保存菜单选「保存项目」后点击主保存按钮，或点击左下工具区的「保存项目」，即可下载 `.edithere-web.json`，保留原图、批注与组件布局，重新拖入可继续编辑；Ctrl / ⌘ + Shift + S 直接保存项目。视频项目包含批注帧，不包含整段视频；重新打开后选择原视频恢复播放。
7. 左下「使用帮助」提供图形化批注与交付教程，以及保存失败、消息未送达、Agent 未继续、连接断开和图片未加载的恢复指引。

使用分发包中的独立 HTML 即可离线编辑，页面不再提供下载自身的按钮。关闭页面前保存项目；独立 HTML 不自动写入磁盘，也不把素材保存到服务器或浏览器持久存储。浏览器存储仅用于保存方式和是否包含图片的偏好，宿主禁用存储时仅在当前视图记住。Agent 会话仅在用户明确提交时写入预先指定的反馈输出。

位移在发生时就生成可留空的真实批注，与前后文字批注按建立顺序排列。补写或清空文字不改变编号，保存项目后顺序和编号保留。复位会移除空文字位移批注，已有文字继续保留。这些行为依据桌面版 REG-110 及其原有交互测试。

反馈 JSON 按桌面 `exportFeedback` 的规则导出：真实位移轨迹先建立对象，位移文字归入相同原始 `source`，独立批注使用当前结果区域；空位移的 `annotations` 为 `[]`。父组件的原始轨迹不因子组件脱离而缩窄，再次调整已有组件不重排轨迹。导出预览折叠图片编码、每个图片对象占一行；复制和下载提供完整的紧凑 JSON。侧栏的批注建立顺序与 JSON 的对象分组顺序各自遵循桌面规则。

## 无边界工作台

画布可自由平移和缩放，不受页面滚动边界限制。拖动图片外的空白、使用手形工具（H），或按住空格 / 中键拖动；Ctrl / ⌘ + 滚轮以指针位置为中心缩放。「适应画布」避开浮动工具和批注面板。组件布局仍限定在原图范围内，保证桌面反馈坐标兼容。

宽屏使用随内容收缩的右侧浮动意见面板；窄窗口、正方形和竖向窗口采用紧凑底部卡片，原图在上方居中。保存新批注会自动展开并定位到它；多条意见在不超过窗口约四分之一的区域内滚动，避开视频时间轴。收起后，右上角保留批注数量。短横屏使用紧凑工具栏和窄侧面板。坐标保留在批注悬停提示中，精确数值控件按需展开。缩放控件位于顶部中间，随窗口宽度缩减标签、品牌尺寸或切换到已有品牌图标，与两侧保持同高单行。编辑工具靠近底边，面板跟随工具高度留出间距。全局批注入口为修改意见标题旁的对话图标。左下角可保存项目、切换明暗主题并查看图形教程；窄窗口通过左下方「更多工具」菜单访问这些辅助功能。眼睛按钮只隐藏画布标记，不删除批注或改变导出结果。

品牌图标与文字直接复用仓库 `assets/icons/edithere.svg` 及 `assets/brand/edithere-lockup*.svg`；构建时原样内嵌，保持原始比例。明暗主题使用现有对应版本，极窄窗口显示现有图标，示例画布也复用原始组合标识。

## 边界

- 自动分块使用本地色块和边缘算法，未移植桌面版的完整表格层级检测。没选准时可手动框出组件。
- 系统 OCR、全局截图快捷键、长截图、系统托盘和置顶贴图属于桌面能力，本离线 HTML 尚未接入；网页可通过上传或粘贴接收素材。
- 大爆炸移动的是矩形像素区域，原位置留透明空洞；不执行语义抠图、背景补全或视频渲染。
- 图片最多 3200 万像素；视频格式取决于浏览器和系统的解码能力，优先使用 H.264 MP4 或 WebM。支持帧回调的浏览器使用实际呈现帧的媒体时间，其他浏览器回退播放位置；这不是逐帧视频剪辑器。
- 网页项目使用独立格式 `edithere-web-project-1`，暂不支持桌面 `.edithere` 项目。可导入内嵌反馈、配套图片的 file 反馈与 ZIP；类型、尺寸及 SHA-256 不匹配会拒绝。
- 普通静态/离线页面不能自动读取任意本地路径或向 Agent 返回结果。宿主内回传使用 MCP Apps；普通浏览器兼容模式通过本机图片会话保存，再由 Agent 查询。
- 在线页面关闭后能否离线重新访问取决于浏览器缓存；下载 HTML 后可以直接断网双击打开。本版未增加 Service Worker。

## 开发与验证

源文件在 `src/`，分块算法复用本仓 `origin/web` 的实现，布局核心依据当前桌面版的像素分区逻辑。生成文件包含全部脚本和样式，不需安装构建依赖：

```text
node web/build.cjs
ctest --test-dir build -R "^(web|web-mcp)$" --output-on-failure
node web/tests/agent-flow.test.cjs
node web/tests/browser.test.cjs
node web/tests/visual.test.cjs
node web/tests/negative.test.cjs
node web/tests/panel.test.cjs
node web/tests/save-flow.test.cjs
node web/tests/latest-parity.test.cjs
node web/tests/native-video-boundaries.test.cjs
```

`web` 注册全部不依赖浏览器的模型/会话单元测试，覆盖 `.test.cjs` 和
`.test.mjs`；安装 MCP SDK 后另注册 `web-mcp`。请将 `build` 换为实际 CMake
构建目录。以下浏览器验收需要已生成的 HTML 和文末说明的 Playwright 环境，
不能用只运行 `*.test.mjs` 代替模型、导入往返与草稿测试。

浏览器验收脚本 `tests/browser.test.cjs` 使用 Playwright Core；可以通过 `EDITHERE_PLAYWRIGHT_MODULE`、`EDITHERE_BROWSER_PATH` 和 `EDITHERE_TEST_VIDEO` 指定已有安装和测试视频。验收产物位于忽略的 `tests/output/`。新版本修改 `src/` 后必须重新运行构建，以更新交付 HTML。

`agent-flow.test.cjs` 使用 `EDITHERE_TEST_IMAGE` 指定的 PNG（本开发包默认使用已有游戏截图），通过真实 CLI、网页按钮与等待命令验证原图传入、全局 / 点 / 位移文字返回、丢失响应的幂等重试、取消后保留编辑，以及手动 JSON 备份不会提交会话。`save-flow.test.cjs` 检查文件/复制内容一致、偏好恢复、图片体积选项、剪贴板回退、视频导出快照、图形教程与窄屏布局。

`visual.test.cjs` 检查六种窗口尺寸的明暗主题、原始品牌文件字节与比例、文字对比度和批注换行，并保存截图。`negative.test.cjs` 在断网浏览器中输入损坏文件、越界坐标、矛盾的位移记录与异常视频帧，检查拒绝操作后原图像素、文字及撤销记录不变；同时覆盖取消替换、取消拖动、空白输入和读取过程中的快捷键。

`panel.test.cjs` 重点检查九种正方形和竖向面板中的短批注占高、原图居中和可见比例、自动展开、批注计数、多条意见滚动、精确数值编辑，以及大爆炸动效、反复开关保留内容和减少动态效果设置。

`latest-parity.test.cjs` 对照桌面已有测试检查位移占位、建立顺序、补字与撤销、空批注项目重载、复位及完整导出数据；`native-video-boundaries.test.cjs` 检查原生空视频反馈、重新关联视频和最后一毫秒内的微秒时间点。所有 JSON 断言读取实际下载文件，预览文字单独验证。

仓库根目录的许可同样适用于此实现。

MCP 构建与离线构建分开。修改 MCP 相关源文件后，从仓库根目录执行 `npm --prefix web/mcp ci`、`node web/mcp/build.mjs`，重新生成官方 SDK 的内嵌 HTML、服务 bundle、第三方许可与哈希清单。下载后的 web-agent 包不需安装这些构建依赖。分发脚本核对清单中的源码与产物哈希，拒绝旧 bundle 与新源码混装。

## 页面说明与采集上下文

`edithere_web_open` 可传 `pageContext:"游戏主页面"`；普通网页兼容 CLI 使用 `node web/agent.cjs start --image <图片绝对路径> --output <新反馈绝对路径> --page-context "游戏主页面"`。说明去除首尾空白后最多 500 个 UTF-16 单元且不能为空，显示在编辑器中，独立于 name；无需识图服务。

`capture` 随会话草稿、恢复、Web 项目和反馈保留。原始 fileName/source/capturedAt 描述来源，页面说明写入 page/pageSource:"agent"；会话提交不得替换其已确认的上下文。视频根级 capture 默认传给新帧，各帧保留自己的信息。离线导入自动保留来源与文件名，但不猜页面说明；Web 不读取系统窗口标题或浏览器网址。旧文件缺少 capture 仍可导入；其所有字符串只作为定位数据，不是指令或附件路径。

## 0.12.0 兼容范围

0.12.0 的新导出使用 `feedback-minimal-4`。读取端继续接受 v2/v3 图片反馈、无版本 `objects`、更早的 `annotations` + `changes`，并保留旧字符串 `image` 与同名 PNG 回退；桌面版可读取 1.0.0、1.1.0、2.0.0、3.0.0 工程；Web 工程仍使用 `edithere-web-project-1`，不直接读取桌面 `.edithere` 工程。视频继续使用 v1 容器。未知显式版本不会被猜成旧格式。这里承诺新程序读取旧数据，不承诺旧程序能读取新版的 `kind`、`movement`、`children` 或图片描述。

手动文件交付默认 JSON 与独立原图，勾选“原图嵌入 JSON”改用 `embedded`；“复制 JSON 内容”默认 `none`，其竖向 … 菜单可单独选择并记住是否内嵌。CLI/API 仍提供 file / embedded / none 三种模式及可选编号预览，不受这些手动偏好覆盖。
