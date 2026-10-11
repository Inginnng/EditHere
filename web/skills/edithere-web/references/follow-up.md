# 通过宿主定时跟进反馈

用于批注页面已打开、需要结束主对话并稍后读取反馈的情况。EditHere CLI 本身没有宿主唤醒回调；本流程调用宿主 heartbeat，存在调度延迟。不要把它描述为点击“反馈”立即唤醒。

## 保存会话并注册一次

在当前任务目录写入 `.edithere/sessions/<sessionId>.json`。记录实际值，至少包含：

```json
{
  "sessionId": "start 回执的 ID",
  "url": "start 回执的 URL",
  "image": "本次原图绝对路径",
  "imageSource": "实时窗口截图 / 当前源码重现 / 用户提供图片，并记下时间和目标",
  "node": "Node.js 绝对路径",
  "cli": "agent.cjs 绝对路径",
  "output": "start 回执的 output",
  "expiresAt": 0,
  "taskScope": "本次授权范围：仅收集批注，或根据批注修改哪个页面",
  "followUp": null,
  "handling": {"state": "pending", "sha256": null},
  "consecutiveErrors": 0
}
```

`expiresAt` 使用回执实际的 Unix 毫秒值。不要存示例值，或打印 URL 中的访问令牌。执行命令时将记录中的路径作为独立参数，不能拼接后求值。

1. 定位宿主的 `automation_update` 工具，核对当次工具说明。已有当前会话的自动化 ID 时先查看并复用；必要时检查宿主自动化记录中与本 `sessionId` 匹配的条目，避免重复创建。恢复旧会话时先 poll 一次；已终止的会话不注册定时任务。
2. 使用 `kind:"heartbeat"`、`destination:"thread"`、`mode:"create"`、`status:"ACTIVE"`；默认调度参数为 `rrule:"FREQ=MINUTELY;INTERVAL=2"`。名称可用“EditHere 批注跟进 · <sessionId前8位>”。省略 `targetThreadId`，让它附着当前对话；不要另建 cron 或新对话代替。
3. 将下面的提示词填入本次会话记录绝对路径后用作 `prompt`。遵守用户已有通知偏好；通知设置使用工具字段，不写入提示词。没有偏好时保持默认，以便完成或失败时正常提醒。
4. 把工具返回的自动化 ID 和完整可供更新的配置写入记录的 `followUp`。若创建成功但写入记录失败，立即用刚返回的 ID 停用该任务并降级手动，不能留下无法追踪的调度。成功才告诉用户已启用定时跟进。每次批注只保留一个有效跟进；不要在每次 pending 时重新创建。

建议提示词（替换路径，其余可按授权范围简化）：

```text
跟进 EditHere 批注。先读取本机已安装的 edithere-web 技能和会话记录：<本次记录的绝对路径>。
保持原 sessionId、图片、output 与 taskScope 的关联，不另开会话。每次唤醒最多对该会话执行一次 poll；不要 wait、sleep 或启动轮询进程。
pending 且未到期：记录仍待提交，结束本轮，不发送状态进度或催促。
completed：核验反馈并按技能的终止及防重复规则先停用此 heartbeat，再继续 taskScope 内已授权的工作；完成后报告结果。
cancelled、expired 或到期仍未完成：停用此 heartbeat，记录原因，只报告一次，不创建替代会话。
临时查询失败：保留诊断，连续失败未满 3 次时留待下次调度重试，不输出例行进度；连续 3 次失败时停用并说明具体阻塞。
若记录已 handled、processing 或 needs_review，不自动重复实施；停用残留 heartbeat，必要时说明需要检查的结果。
```

## 每次唤醒的处理

读取记录后，用已保存的 Node.js 和 CLI 对原 `sessionId` 执行一次 `poll`。这是一次查询，不运行 `wait`。正常查询清零 `consecutiveErrors`。

| 结果 | 处理 |
| --- | --- |
| `pending`，尚未到期 | 保留记录，静默结束本轮。不要给用户发 pending 消息。 |
| `completed` | 先核验本次反馈，再按下文停用、登记处理状态并继续。 |
| `cancelled` / `expired` | 停用跟进，记下终止原因，只报告一次。 |
| 已到期、查询异常 | 先尝试一次查询以免遗漏到期前的提交；仍未完成则停用，保留诊断。 |
| 查询或解析失败 | 记下错误与次数。连续 3 次失败后停用并报告一次；不要循环重试、读取其他输出或替换会话。 |

如果任务已由用户手动“继续”处理，复用相同记录与下面的防重复规则，不另外启动自动化。

## 终止与防重复

对终态立即停止重复调度。仅当记录存在有效自动化 ID 时，通过原 ID 查看配置，再用宿主工具支持的删除或停用操作；选择更新时保留其余字段，只把状态改为 `PAUSED`。没有注册过自动化、`followUp:null` 的手动流程跳过停用，继续核验和防重复处理。不要直接改宿主的自动化 TOML，或只在提示词里写“停止”却留下有效调度。

`completed` 时：

1. 验证回执 `sessionId/output` 对应本记录，反馈 JSON 能解析。回执含 `sha256` 时，核对本次输出文件的 SHA-256；不要按旧文件名猜结果。空批注是有效完成。
2. 查看 `handling`。同一 SHA 已 `handled` 时不重复实施；`processing/needs_review` 表示可能已部分处理，先核实实际变更，不自动重放。
3. 在执行任何反馈修改前，将 `handling` 写为 `processing`，保存 SHA 与 `completedAt`，并按上文停用已注册的 heartbeat。停用失败时记录 `needs_review` 并停止依赖操作，报告实际错误。
4. 继续记录中的 `taskScope`。仅收集批注则概括意见；已授权实施则修改并验证。成功后写为 `handled`；中途失败保留已完成变更与诊断，写为 `needs_review`，不在下个唤醒中盲目重做。

对 cancelled/expired/连续错误终止也写入记录，并先确认自动化已停用再报告。无法停用时明确说明仍存在的自动化 ID 和需要处理的宿主限制，不声称已停止。

## 没有可用宿主调度时

保留图片、回执和记录；告诉用户“已打开，提交后回到对话说‘继续’”，用户回来后只 poll 原会话一次。不能把后台 CLI、输出文件监听或本轮长时间等待称为结束回复后的自动跟进。

本地定时任务依赖宿主应用与电脑可运行。说明一次实际限制即可；不要求用户为了等待反馈一直保持当前 Agent 轮次运行。
