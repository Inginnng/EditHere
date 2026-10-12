# Install EditHere Web with an AI agent

[简体中文](AI-SETUP.md) · **English**

The Web Agent package includes a real MCP Apps entrypoint and a browser compatibility entrypoint. MCP uses host-managed stdio and an embedded HTML resource, with no localhost listener. Installing the Skill alone does not connect MCP. Standalone `EditHere.html` needs no server.

## Prompt for the agent

> Follow this guide to install EditHere Web Agent. I will provide the web-agent ZIP URL or local file and its SHA-256. Inspect existing installations and extract the complete package into a retained user directory. Prefer connecting EditHere Web MCP to the current host, verify discovered tools and the embedded UI, and install the edithere-web Skill. If the host lacks MCP Apps support, explain the actual limitation and use the browser compatibility entrypoint. Preserve existing MCP entries and Skills. Tell me how to annotate the current page.

## Obtain and check the package

Choose the ZIP containing **`-web-agent-`** in its name. It includes `web/mcp/server.bundle.cjs`, `web/EditHere.mcp.html`, `web/install-skill.cjs` and third-party licenses. The `-web-` package containing only `EditHere.html` is for standalone editing.

Use an actual distributor-provided URL or local package, not a guessed release asset. Verify the supplied SHA-256, then extract into a new, persistent writable directory. Use PowerShell `Get-FileHash -Algorithm SHA256`, macOS `shasum -a 256` or Linux `sha256sum`. Retain the runtime after installation.

MCP requires **Node.js 20+**; the browser compatibility CLI supports Node.js 18+. Distributed server and App SDK bundles need no npm install, Qt, Python or desktop EditHere. Screenshot capture depends on actual host browser/window tools.

## Connect MCP and install the Skill

1. Check `node --version`, the host's current MCP configuration and existing EditHere tools. Use a working absolute Node.js path and reuse valid installations.
2. From the extracted directory, run:

   ```text
   node web/install-skill.cjs
   ```

   The default target is `$CODEX_HOME/skills/edithere-web`, or `~/.codex/skills/edithere-web` when unset. For another host, locate its Skill directory and use:

   ```text
   node web/install-skill.cjs --destination <absolute-host-skill-directory/edithere-web>
   ```

   The installer returns `state:"installed"` and local entries, and records absolute paths plus MCP configuration in `references/local-installation.md`. It does not edit host settings. Existing destinations are refused; inspect and reuse them or use a user-selected new directory rather than deleting the existing Skill.
3. Read the `web/mcp/mcp.json` template or the install receipt's `mcpConfig`. Merge its stdio entry using the host's documented configuration: `command` is the absolute Node.js path, and the sole `args` entry is the extracted `web/mcp/server.bundle.cjs` absolute path. Preserve other MCP entries. No HTTP URL is needed. Convert to TOML or the host's settings UI where required.
4. Reload MCP and Skills through the host. Verify `edithere_web_open` and `edithere_web_read` in the actual tool list; file existence alone does not prove connection.
5. Call `edithere_web_open` with an actual test image, check `uiSupport/state`, and verify that image in the embedded UI. MCP tool discovery does not prove MCP Apps support. A host that does not declare UI capabilities receives `unsupported` with compatibility commands, without creating an orphan session. **This WorkBuddy version needs an actual test**; CodeBuddy Code Web UI / IDE documentation does not establish WorkBuddy support.

## Use and acceptance

For “Help me annotate the game homepage with EditHere”, capture and inspect the actual current interface, then call the discovered `edithere_web_open` with:

```json
{"imagePath":"<absolute-current-image-path>","name":"Game homepage"}
```

An optional `outputPath` must be absolute and not yet exist. Otherwise the server generates a unique feedback file under `.edithere` beside the source image. Retain the sessionId and output path. The host displays an MCP UI resource without starting port 7330.

Feedback submission first persists complete desktop-compatible JSON, then requests a host conversation message. Saving and messaging have separate states. If messaging is rejected, only fills the input box, or is unsupported, the user says “continue” in the chat. Read the original sessionId with `edithere_web_read` and verify `completed/sha256` before acting. Read receipts omit image base64; the full image remains in the output JSON. Host message acceptance does not prove the AI has implemented the feedback.

Sessions and drafts default to `~/.edithere/web-sessions`. Set `EDITHERE_WEB_DATA_DIR` to a persistent absolute directory in MCP configuration if needed. Host shutdown preserves already saved data. After reconnecting, reopen with `edithere_web_open({"sessionId":"<original-ID>"})` or read submitted results. Respect user cancellation and stops; do not automatically restart stopped work.

Verify image loading, annotations and movements, matching disk/read results, readable feedback after messaging failure, and reopening after MCP restart. Report the host actually tested; a reference-host test is not a WorkBuddy test.

## Browser compatibility entrypoint

When MCP Apps is unsupported, use:

```text
node web/agent.cjs start --image <actual-absolute-image> --output <new-absolute-feedback-json> --timeout 86400
node web/agent.cjs poll --session <sessionId-from-start>
```

Open the session URL returned by start. Use `node web/preview.cjs --json` to check the port 7330 preview service. Inspect PID/path conflicts rather than terminating unrelated services. This mode defaults to manual “continue” after submission and has no host message bridge. A terminal background service may end when the Agent stops; do not promise an independent daemon. Completed poll results can be read from persistent data after the service ends.

Compatibility logs and legacy state may be in `web/tests/output/`. Session directories and outputs contain private images; exclude them from redistribution. The Skill's [follow-up reference](skills/edithere-web/references/follow-up.md) covers an explicit per-session choice of scheduled follow-up.
