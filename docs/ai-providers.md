# AI 修改意见的 API 预设

桌面端通过独立 API 请求理解本次批注的 JSON 和图片，产出可编辑、复制的纯文本修改意见。每次请求只包含本次任务材料，不带入其他 Agent 对话历史。原始批注继续作为数值与坐标依据。

预设保存在 `assets/ai/provider-presets.json`，编译时作为应用资源打包。选择服务商后会填入 API 基础地址、协议和默认视觉模型；用户填写对应开放平台的 API Key 即可。地址与模型均可修改，预设不保证账号已经开通相应模型或拥有余额。

`name`、`note` 为中文显示文字，`nameEn`、`noteEn` 为英文显示文字；两种界面共用同一组地址、模型和协议。

## 预设与官方依据

核实日期：**2026-10-28（Asia/Hong_Kong）**。地址均为基础地址，客户端按协议追加 `/responses`、`/chat/completions` 或 `/messages`。

| 预设 | 基础地址 | 默认模型 | 协议 | 官方依据 |
| --- | --- | --- | --- | --- |
| OpenAI | `https://api.openai.com/v1` | `gpt-6.1-sol` | Responses | [视觉输入](https://developers.openai.com/api/docs/guides/images-vision)、[模型能力](https://developers.openai.com/api/docs/models/gpt-6.1-sol) |
| Claude | `https://api.anthropic.com/v1` | `claude-sonnet-5-5` | Anthropic Messages | [模型列表与视觉支持](https://platform.claude.com/docs/en/models/overview)、[视觉请求格式](https://platform.claude.com/docs/en/build-with-claude/vision) |
| DeepSeek | `https://api.deepseek.com` | `deepseek-flash` | Chat Completions | [视觉输入与三种协议](https://api-docs.deepseek.com/guides/vision/)、[基础地址](https://api-docs.deepseek.com/) |
| GLM（中国） | `https://open.bigmodel.cn/api/paas/v4` | `glm-4.6v` | Chat Completions | [官方模型与请求示例](https://docs.bigmodel.cn/cn/guide/models/vlm/glm-4.6v) |
| GLM / Z.AI（全球） | `https://api.z.ai/api/paas/v4` | `glm-4.6v` | Chat Completions | [视觉模型](https://docs.z.ai/guides/vlm/glm-4.6v)、[API 介绍](https://docs.z.ai/api-reference/introduction) |
| Kimi（中国） | `https://api.moonshot.cn/v1` | `kimi-k3` | Chat Completions | [图文请求示例](https://platform.kimi.com/docs/api/chat)、[模型列表](https://platform.kimi.com/docs/models) |
| Kimi（全球） | `https://api.moonshot.ai/v1` | `kimi-k3` | Chat Completions | [图文请求示例](https://platform.kimi.ai/docs/api/chat)、[模型列表](https://platform.kimi.ai/docs/models) |
| 自定义 | 用户提供 | 用户提供 | 三种协议任选 | 按服务商的多模态文档配置 |

DeepSeek 的当前 `deepseek-flash` 已支持图像；不沿用早期“DeepSeek API 仅支持文本”的判断。官方已退役的 `deepseek-v4-flash-vision-exp` 名称不作为新配置默认值。

Kimi 官方模型列表提示，K2 系列从 2026-10-31 起停止向新注册用户开放，并计划于 12 月底下线。因此新用户预设选择仍受支持的 `kimi-k3`，不选择 `kimi-k2.5` 或 `kimi-k2.6`。模型更新时应重新核对官方列表，避免只从第三方配置照搬模型名称。

## 普通 API 与 Coding Plan

- GLM 预设使用普通开放平台 `/api/paas/v4`，不使用 `/api/coding/paas/v4` 或 Coding Plan 的 Anthropic 兼容地址。该功能直接提交图文给视觉模型，Coding Plan 的模型映射和视觉 MCP 不作为同一种接口处理。
- Kimi 预设使用 Moonshot 普通 API；Kimi Code 的 `/coding/` 端点和 `kimi-for-coding` 模型别名属于另外的产品。Kimi 官方说明普通 API 按量计费，Kimi 会员、Kimi Business 与 Kimi Code 的认证和权益分别管理：[官方产品区别](https://www.kimi.ai/help/kimi-api/api-overview)。
- 中国站与全球站分别列出。Key、账号、模型开通与余额是否互通，以各自控制台为准，应用不会自动把请求切换到另一地区或另一个提供商。
- OpenAI Platform 与 Claude Console 的 API Key 按各平台 API 规则使用，不把消费版聊天订阅当作 API 凭证。

## 密钥存储

复用 [QtKeychain 0.15.0](https://github.com/frankosterfeld/qtkeychain/tree/0.15.0) 的系统凭据库后端。CMake 固定上游源码归档及 SHA-256，静态编入应用，许可证为 Modified BSD（BSD-3-Clause）；原始许可和来源随分发放在 `licenses/qtkeychain`，归属见 `packaging/THIRD-PARTY-NOTICES.md`。API Key 不写入普通设置文件，不启用明文存储回退。设置页的“保存密钥”和“删除密钥”直接操作系统凭据库，普通地址、模型和超时选项由底部“保存”持久化。

## 复用 CC Switch 的范围

来源：[farion1231/cc-switch](https://github.com/farion1231/cc-switch)，固定核对提交 **`983113a132ab1754e6bb1fab3994fb3b23d15f62`**，MIT，Copyright (c) 2025 Jason Young。

本功能从下列成熟预设中选取官方服务商与地区数据，并将其配置布局适配为桌面应用需要的 JSON：

- [`src/config/claudeProviderPresets.ts`](https://github.com/farion1231/cc-switch/blob/983113a132ab1754e6bb1fab3994fb3b23d15f62/src/config/claudeProviderPresets.ts)：沿用 `name`、`websiteUrl`、`apiFormat` 以及官方提供商按地区分项的组织方式；本应用的 `note`、`docsUrl` 根据厂商官方文档补充，不复用推广入口。
- [`src/config/codexProviderPresets.ts`](https://github.com/farion1231/cc-switch/blob/983113a132ab1754e6bb1fab3994fb3b23d15f62/src/config/codexProviderPresets.ts)：复用官方预设中的 `base_url`、模型字段和 `endpointCandidates` 对基础地址的约定，尤其 Kimi 中国站/全球站的 `/v1` 地址与 `kimi-k3`。本应用每项只有一个明确地址，不会自动测速切换地址。

字段映射为 `name → name`、`base_url/endpointCandidates → baseUrl`、`model → model`、`apiFormat → protocol`。协议名归一为 `responses`、`chat-completions`、`anthropic-messages`。CC Switch 的预设主要服务于编程 Agent，因此 GLM 的模型和端点改按官方视觉 API 文档选择，不能把其编程模型和上下文后缀直接当作本功能的图文模型。

未复制赞助商、推广地址、联盟参数、OAuth 反代、账号切换、数据库或整套代理服务。API 请求由 EditHere 既有 Qt Network 运行时发送；应用无需启动 CC Switch 或额外本地代理。

上游完整 MIT 许可保留于 `packaging/licenses/cc-switch/LICENSE`，分发归属见 `packaging/THIRD-PARTY-NOTICES.md`。
