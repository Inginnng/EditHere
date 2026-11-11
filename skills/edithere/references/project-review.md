# 项目组图与结果审批

EditHere 可以作为项目的结果批注台。AI 运行项目、截取各页面的真实结果、维护组图；用户查看结果，在需要修改的画面上批注，最后统一提交。实现、验证和截图更新由 AI 完成。

桌面 Windows / macOS / Linux 共用组图实现；离线 Web、Web Agent、MCP Apps 和 DSH 使用同一页面 ID 与 `image-set-feedback-1` 反馈容器。本次集成位于主源码、0.12 开发线和 0.13 开发线；历史安装包需要重新构建才会包含这些能力。

## AI 维护项目清单

每个项目保留一个清单，例如 `.edithere/results/project.json`：

```json
{
  "schemaVersion": "edithere-image-set-1",
  "name": "桌面程序页面",
  "pages": [
    {"id": "home", "name": "首页", "path": "home.png"},
    {"id": "settings", "name": "设置", "path": "settings.png"}
  ]
}
```

`path` 相对清单目录，也接受本机绝对路径。`id` 表示页面身份，改名、换图和修改尺寸时继续沿用；新页面才使用新 ID。截图应来自实际运行的项目，保持原始尺寸；示意图应明确标为设计稿。AI 自主决定覆盖哪些页面和重要状态，用户不必手动整理截图。

仓库命令如下；分发的 skill 在自己的 `scripts/` 中携带同样的工具：

```powershell
node scripts/image-set.mjs create .edithere/results/project.json --name "桌面程序页面"
node scripts/image-set.mjs upsert .edithere/results/project.json --id home --name "首页" --image shots/home.png
node scripts/image-set.mjs upsert .edithere/results/project.json --id settings --name "设置" --image shots/settings.png
```

更新设置页只需再次 `upsert --id settings --image ...`，原有页序、其他页面和 ID 都会保留。工具记录原图 SHA-256；截图字节变化后需同步 `upsert`，避免无意提交旧清单。`list` 查看清单，`remove --id ...` 移除已下线的页面引用，原图文件仍保留。

## 交给用户审批

每轮反馈使用新的输出文件或目录。当前审核读取截图快照；AI 更新清单不会悄悄改变正在批注的画面。完成一轮修改后重新打开同一清单，即可展示更新后的整组页面。

| 入口 | 发起方式 |
| --- | --- |
| 桌面 | `edithere-cli annotate <清单绝对路径> --output <新的反馈绝对路径>`；也可多选、拖入多张图片 |
| MCP Apps | `edithere_web_open({projectPath:"<清单绝对路径>",outputPath:"<新的反馈绝对路径>"})`；恢复本轮仍只传 `sessionId` |
| Web Agent | `node web/agent.cjs start --project <清单绝对路径> --output <新的反馈绝对路径>`，打开回执 URL |
| DSH | `node dsh/cli.mjs start --project <清单路径> --output <新的反馈路径> --open` |
| 离线 Web | `node scripts/image-set.mjs bundle <清单路径> --output <新的网页项目.json>`，把生成的项目交给用户打开；也支持直接多图导入 |

`bundle` 内嵌全部页面，浏览器不需要读取清单指向的任意本机路径。桌面保存的 `.edithere`、Web 保存的网页项目保留全部页面和草稿；AI 的清单独立保留，供后续换图。

用户可逐页写意见、拖动组件，也可写不绑定单页的“整组意见”。用户点击完成才提交结果；AI 不代点完成，也不把未提交草稿当作修改要求。取消、超时不会产生完成反馈。无批注的组图可以提交为空反馈，表示这轮没有提出修改意见。

## AI 读取与更新

反馈中的 `pageIndex` 是整组上下文索引，只包含名称、ID、尺寸和是否有批注。`pages[]` 只携带有批注或实际布局移动的页面与原图；`groupNotes` 单独表达整体要求。未批注页面不携带图片。页内坐标使用该页原图像素，页面尺寸可以不同。

按 `pages[].id` 对回项目清单，再读取页内批注；按图中 `number` 对照意见。不要用完整组图的页号当作 `pages[]` 的数组下标。新开发线的页内反馈保留现有 typed objects、子批注与图片描述符；旧开发线继续使用原有图片反馈格式。

AI 按已提交意见修改项目、完成验证，再运行相关页面并更新截图。只对变化页面执行 `upsert`，其余截图继续复用。下一轮打开同一组图清单，旧批注不自动套用到新截图；历史反馈留在各轮输出目录，便于追溯。

当前上限为 64 页、单图 48 MiB、组图总像素 1.28 亿。Web Agent/MCP 的快照还受 96 MiB 会话消息限制；大项目应缩放截图或拆成有意义的组。macOS 和 Linux 共用源码，本机只验证了 Windows 构建。
