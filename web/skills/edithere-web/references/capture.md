# 取得本次批注图片

用户要求批注当前网页、应用页面或游戏运行界面时，先按本文件取得图片，再返回 [SKILL.md](../SKILL.md) 启动网页会话。截图来源与批注载体是两件事：原生游戏窗口的截图也可以传入 EditHere 网页，不因此改用桌面批注。

## 先确定图片来源

- 用户明确指定本轮附件、图片文件或设计图：直接使用该图，保留原始像素尺寸；无需为了批注重新截图。
- 用户指定“当前页面”“游戏弹珠机页面”“现在的界面”等实际页面：默认从实际运行的页面或窗口取得本轮截图。没有运行实例时，可正常启动并通过界面进入目标状态。目录中的旧图，以及刚重新生成的固定种子、固定帧测试渲染，都不能代替实际运行截图。
- 先依据当前项目、已打开的目标页面、窗口清单及任务上下文定位对象。EditHere 自己的网页不是待批注的目标。仅在这些证据仍不能唯一确定目标时询问页面或窗口对象。

取得图片后，用一句话说明来源，例如“已截取当前运行中的弹珠机窗口，准备用这张新图批注”或“使用你本轮提供的设计图”。只有实际截取了正在运行的窗口，才称为当前窗口截图。

## 准备本轮文件

新截图使用独立文件，优先 PNG；工具原生返回 JPEG 时保留 JPEG 与正确扩展名。保留原始像素尺寸，不拉伸、不重新绘制。优先将图片放在当前工作区的 `.edithere/captures` 或宿主允许写入的任务输出目录；使用绝对路径和唯一文件名，避免覆盖用户文件。例如支持 PNG 的工具可准备：

```powershell
$EditHereCaptureDirectory = Join-Path (Get-Location).Path '.edithere\captures'
New-Item -ItemType Directory -Force -Path $EditHereCaptureDirectory | Out-Null
$EditHereImage = Join-Path $EditHereCaptureDirectory (([guid]::NewGuid().ToString()) + '.png')
```

这是待生成文件路径，尚不表示截图已经保存。截图工具若自行创建唯一的本地 PNG，也可以直接使用它实际返回的绝对路径；若需归档到上面的路径，复制原文件，不重新编码。已提供的 JPEG 等图片保留原格式，不仅修改扩展名伪装成 PNG。

## 浏览器页面

优先使用当前环境已提供、允许操作目标浏览器的截图工具。先读取该工具文档，依据真实标签页清单选择目标，导航到用户要求的页面或游戏状态，再截图。一般批注当前可见页面使用视口截图；用户明确需要整页时再使用完整页面截图。不要为截图重新加载用户正在操作的一局游戏。

若使用已安装的 Tabbit：先按 `tabbit` 技能连接并取得目标 `page`，然后在同一任务的 `nodejs` 程序中执行：

```javascript
return await page.screenshot({fullPage: false});
```

Tabbit 的 `page.screenshot()` 返回 PNG 文件回执，使用实际响应中的 `result.nextAction.path`，并通过宿主图像查看工具打开该路径。它不是 Playwright Buffer，不要对回执调用 `toString('base64')`，也不要假定未返回的 `path` 字段。继续使用或结束浏览器任务时遵循 Tabbit 自身流程。

其他浏览器工具按自身文档处理：返回本地文件路径时直接检查该文件；返回图片数据或图片块时，使用其正式提供的保存/导出能力落盘，或使用宿主明确可访问的实际图片数据保存。对话中显示了图片不等于已经有本地 PNG。不要猜测截图保存 API、虚构图片路径，或为了取图绕过浏览器访问限制。

## 原生应用或游戏窗口

使用宿主实际提供且获准的窗口截图能力，选择真实返回的目标窗口，截取用户要求的界面。窗口截图比全桌面截图更适合批注；若当前窗口处于错误页面、启动画面或被弹窗遮挡，先通过已授权的应用操作到达目标状态。避免无关桌面区域混入图像。

Windows 环境若提供 `computer-use` 插件，先读取其 `SKILL.md` 和该文件所指的 `../../docs/guidance.md`、`../../docs/api.md`。通过 `node_repl` 按其文档初始化 `@oai/sky` 并用轻量的 `list_windows()` 检查实际可用性，不根据另一个工具禁用原生能力就推断此插件不可用。遵守该插件自身的能力和权限边界，从真实回执中唯一确定目标；不要凭空构造窗口 ID。取得真实目标 `targetWindow` 后：

```javascript
globalThis.editHereState = await sky.get_window_state({
  window: targetWindow,
  include_screenshot: true,
  include_text: false,
});
```

返回的截图会自动显示。检查它是否为目标页面；`screenshots` 也可能包含临时窗口区域，不能盲目将任意一项当作主画面。正式 API 的 `screenshots[].url` 是 data URL。下面示例仅适用于已确认只有一张目标截图的情况，将 `editHereImagePath` 替换为前面准备的实际绝对路径，在同一个 `node_repl` 会话执行：

```javascript
{
  const fs = await import('node:fs/promises');
  const path = await import('node:path');
  const editHereImageStem = 'C:/absolute/workspace/.edithere/captures/unique';
  if (!path.isAbsolute(editHereImageStem)) throw new Error('需要绝对图片路径');
  if (editHereState.screenshots.length !== 1) {
    throw new Error('存在多个截图区域，需先检查并选择目标图像');
  }
  const shot = editHereState.screenshots[0];
  const encoded = /^data:image\/(png|jpeg);base64,([A-Za-z0-9+/=\r\n]+)$/.exec(shot.url);
  if (!encoded) throw new Error('截图未返回支持的 data URL，按工具文档处理实际格式');
  const editHereImagePath = editHereImageStem + (encoded[1] === 'png' ? '.png' : '.jpg');
  await fs.mkdir(path.dirname(editHereImagePath), {recursive: true});
  await fs.writeFile(editHereImagePath, Buffer.from(encoded[2], 'base64'), {flag: 'wx'});
  nodeRepl.write(editHereImagePath);
}
```

这次保存的用途是将原图交给 EditHere，不是为重复视检而打印或解码截图；不要输出完整 data URL。有多个截图区域时，先观察并选定正确项，再适配保存代码，不随意拼接不同区域。

所选截图能力实际不可用或被禁用时，不绕过该能力的限制。可检查项目是否有从正常运行界面取图的入口；固定种子或固定帧测试渲染只用于用户要求的重现或对照。仍无法取得实际目标图时说明具体能力缺口，保留已完成的准备，再请求用户提供目标截图。

`edithere-cli capture` 只能唤起桌面截图交互，成功回执仅表示请求受理。它不接受输出图片路径或目标窗口参数，也不保证已保存 PNG。不能执行杜撰的 `capture --output ...`，不能把该回执直接传入网页。用户明确选择其交互截图时，等实际取得保存后的图片路径，再走相同传图流程。

## 项目提供的截图入口

项目已有游戏内截图、预览导出、测试截图或渲染脚本时，先读项目规则及脚本说明，使用实际支持的命令与输出参数。实际运行界面的批注需求优先用正常窗口截图；测试渲染留给用户要求重现或对照的任务，不以重新生成文件作为实时截图的证据。截图实现留在项目约定中；本技能不指定某个引擎、固定场景、种子或项目私有命令。

区分它生成的结果：

- 游戏内截图确实取自正在运行的目标界面：可以说明为本轮运行截图。
- 脚本启动另一个实例、固定种子或测试场景进行渲染：说明为“按当前代码重现的页面截图”，不能称为用户当前正在运行的一局。用户要求当前运行状态时，不将重现结果作为等价替代。

确认命令完成，并检查本轮指定输出确实新生成；运行截图命令成功不保证图像就是正确页面。不要仅根据目录内文件修改时间选择旧图代替本次结果。

## 视检后交给网页

确认实际图片文件存在、非空且可解码。检查它对应目标页面、必要控件完整、画面没有黑屏或加载占位。若截图工具已展示且已视检同一原图，保存后检查文件即可；否则用宿主图像查看工具打开最终文件。遵守截图工具关于展示和解码的限制，不为重复视检重放截图。保留原始像素尺寸；若需裁剪，先保留原图并明确本次传入的是裁剪图。无需重复截图已经确认正确的本轮图像。

将此文件的实际绝对路径赋给 `$EditHereImage`，返回主技能，由主技能唯一执行一次 `start --image`、打开回执 URL 并安排反馈跟进。本文件只负责取得图片，不额外创建批注会话。

`start --image` 会读取本地图并传入网页，无需另行使用文件上传控件或把 base64 粘贴进网页。打开后检查 EditHere 载入的是刚确认的图片。
