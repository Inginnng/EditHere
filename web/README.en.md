# EditHere Web

[简体中文](README.md)

Open **EditHere.html** directly in a browser, or serve this single file on a static website. After the page loads, file reading, annotation, layout changes and exports run locally in the browser. The standalone HTML has no backend, CDN, account or external runtime dependency, and its Content Security Policy blocks network connections. The MCP Apps mode below uses host-managed stdio and embedded HTML; the browser compatibility mode uses a local service.

For an Agent browser panel, run `node web/preview.cjs` from the repository root (requires an installed Node.js), then open `http://127.0.0.1:7330/EditHere.html`. The compatibility launcher reuses this project's service or attempts to start a background service and checks the served HTML. The process may end when the host stops its task; an independent daemon is not guaranteed. A refused connection means the local service is not listening; refreshing the page does not start it. The preview binds only to localhost and does not upload media.

## MCP Apps and the Agent skill

For setup, give the agent the [AI installation guide](AI-SETUP.en.md) and an actual `web-agent` ZIP. Distributed server and official App SDK bundles need no npm install. MCP requires Node.js 20+; the browser compatibility CLI supports Node.js 18+. Neither requires desktop EditHere.

The MCP integration follows [Cowart's stdio service and UI resources](https://github.com/zhongerxin/Cowart/blob/main/mcp/server.mjs) and uses the [official MCP Apps SDK](https://github.com/modelcontextprotocol/ext-apps). Tool metadata associates an HTML resource through `_meta.ui.resourceUri`. The host embeds that UI; the page calls persistence tools and requests a conversation message through the App SDK. This entrypoint has no port 7330 listener, localhost HTTP dependency or CDN.

Merge `web/mcp/mcp.json` into the host configuration with actual absolute Node.js and `web/mcp/server.bundle.cjs` paths. Reload and verify the discovered `edithere_web_open` / `edithere_web_read` tools. Open accepts an absolute `imagePath` and optional `name/outputPath`. Check `uiSupport/state` and confirm the image is visible. Hosts that do not declare MCP Apps support receive unsupported with compatibility instructions, without creating an orphan pending session. WorkBuddy support needs an actual test and cannot be inferred from CodeBuddy documentation.

Install the optional Skill:

```text
node web/install-skill.cjs
```

The default target is `$CODEX_HOME/skills/edithere-web` or `~/.codex/skills/edithere-web`. Other hosts can use `--destination <absolute-skill-directory>`. The installer checks files and records MCP/CLI paths plus configuration in the local reference. It does not edit host MCP settings, overwrite existing Skills or modify the desktop Skill. Retain the extracted runtime and update references after moving it.

For “Help me annotate the game homepage with EditHere”, the Skill prefers connected MCP and falls back to the compatibility entrypoint when embedded UI is unavailable. Screenshots come from actual host browser/window tools and must represent this turn's target interface. EditHere has no built-in system screenshot implementation; old images and fixed test renders cannot impersonate the current interface.

Feedback submission first persists complete desktop-compatible JSON, then requests a host conversation message. Persistence, message acceptance and AI implementation are reported separately. Rejected or unsupported messaging leaves saved feedback readable; the user can say “continue”. The Agent reads the original sessionId and verifies completed state and SHA before acting. Host message acceptance does not mean code was changed, and updateModelContext does not itself wake the AI.

Acknowledged MCP drafts and sessions default to `~/.edithere/web-sessions`, configurable with an absolute persistent `EDITHERE_WEB_DATA_DIR`. After host restart, open the original sessionId to restore a draft or inspect submission. Do not reopen after a user stop/cancellation without renewed intent. Outputs retain the original image and native feedback schema; read receipts omit image bytes. Never redistribute private sessions, images, tokens or logs.

Browser compatibility flow:

```text
node web/agent.cjs start --image <actual-absolute-image> --output <new-absolute-json> --timeout 86400
node web/agent.cjs poll --session <sessionId-from-start>
```

Open the session URL returned by start. This mode has no host message bridge and defaults to manual “continue” without scheduling. Completed feedback remains readable after the service ends. Only an explicit per-session scheduling choice uses the Skill's follow-up rules; do not promise invisible or immediate wakeup. Closing a tab does not itself cancel the server session. Respect explicit stops and clean up associated tasks. Static/offline HTML retains download and copy exports.

## Use

1. Drop an image or video, choose a file, or paste an image. Edit one source at a time.
2. Add smart-region, point, rectangle or whole-frame comments. With the selection tool (V), drag annotation points or rectangles, and adjust rectangle bounds with eight handles. Click a numbered marker to edit its text.
3. Choose **大爆炸** to enter component editing with a diagonal wave matching the desktop app. Click again to leave component editing while preserving movements and comments. Drag a region or resize with its eight handles. The small **组件布局** button immediately left of the global-note button opens numeric controls in a popup. It starts closed and consumes no inspector height. Ctrl / Command + click selects multiple regions for a shared move or scale. The wheel scales a selected component proportionally; without a selection, it cycles through overlapping region sizes. Manually draw missing regions with **框出组件**. Movement and scaling show an arrow from source to destination: click it or its inspector entry to add or edit text. Comments persist in projects and remain attached to the same movement in exported feedback. Reset a selected component to its original geometry when needed.
4. Pause a video and annotate the current frame. Select a saved timestamp to revisit its comments. Playback, pausing and seeking alone do not add exported frames.
5. Expand the top save button to choose **Save JSON file**, **Copy JSON content**, **Save project**, or **View JSON file**. Selecting a mode only remembers the choice; it does not copy, download, or open a window. Click the main save button or press Ctrl / Command + S to run the selected mode. Choose **View JSON file**, then click the main button to open the viewer with comment/movement counts and file size. The viewer also remembers whether to include images (enabled by default). Exclude them only when the Agent has the same original image. Copy and download use identical complete desktop-compatible JSON; a blocked clipboard selects the complete text for manual copying. The export dialog also saves annotation PNGs with numbered comments and blue movement arrows.
6. To save a `.edithere-web.json` project, select the project mode and then click the main save button, use the bottom-left project button, or press Ctrl / Command + Shift + S. Projects retain images, notes and layout state. Video projects contain annotated still frames rather than the whole source video; select the original local video again after reopening.
7. Bottom-left help provides illustrated annotation and handoff instructions, plus recovery steps for save failures, undelivered messages, an Agent that has not continued, disconnected sessions and missing images.

Use the standalone HTML from the distribution for offline editing; the page no longer downloads itself. Save a project before closing: the standalone HTML does not automatically persist files, upload media, or store projects in browser storage. Browser storage remembers only the save mode and image preference; when the host blocks storage, the current view retains those choices. An Agent session writes its predefined feedback output only when the user explicitly submits.

A movement immediately creates a real note with optional text, in creation order among other notes. Editing or clearing its text preserves its number; project reload preserves its identity and order. Reset removes an empty movement note and retains one with text, following desktop REG-110.

Feedback follows desktop `exportFeedback`: actual trajectories establish objects first, movement text joins the matching original source, and ordinary comments use current result coordinates. Empty movements export `annotations: []`. A detached child does not shrink its parent's tracked frame; adjusting an existing movement preserves trajectory order. The preview folds image bytes and shows one image object per line; copy and download retain the full compact JSON. Sidebar note order and exported object grouping follow their respective desktop rules.

## Free canvas workspace

Pan by dragging the empty workspace, using the hand tool (H), or holding Space / the middle mouse button. Ctrl / Command + the wheel zooms around the pointer. Fit avoids floating controls and the inspector. The workspace can pan freely, while component geometry stays within the original image for feedback compatibility.

Wide windows use a content-sized floating inspector on the right. Narrow, square and portrait windows use compact bottom cards with a centered image above them. Saving a note automatically reveals it; multiple comments scroll within about a quarter of the window height, above the video timeline. The top-right badge retains the comment count when collapsed. Coordinates remain in comment tooltips and numeric controls expand on demand. Short landscape windows use compact tools and a narrow side inspector. Zoom controls stay on one row at the same height as the top islands; narrower windows reduce labels and brand size or use the existing brand symbol. Editing tools sit near the bottom edge, with the inspector positioned above their measured height. The comment icon in the inspector header adds a global note. Find project saving, theme switching and illustrated help at the bottom left, or in the bottom-left More tools menu in narrow windows. Hiding markers does not remove comments or alter exported feedback.

The existing `assets/icons/edithere.svg` and `assets/brand/edithere-lockup*.svg` files are embedded byte for byte at build time and retain their original aspect ratios. Existing light/dark variants cover both themes; very narrow windows use the original symbol. The demo canvas also uses the original lockup.

## Limits

- Local color and edge detection is included; desktop table hierarchy detection has not been ported. Draw a region manually when automatic detection misses it.
- System OCR, global screenshot shortcuts, scrolling capture, tray integration and always-on-top pinned images remain desktop capabilities. This standalone HTML accepts uploaded or pasted media.
- Layout movement operates on rectangular pixel regions, leaving transparent holes at their source positions. It does not generate backgrounds, segment semantic objects or render edited videos.
- Images are limited to 32 million pixels. Video support depends on the browser and operating-system codecs; use H.264 MP4 or WebM. Where available, video frame callbacks supply the presented frame's media time; other browsers fall back to playback position. This is not a frame-by-frame video editor.
- Web projects use `edithere-web-project-1`. Desktop `.edithere` project files are not supported. Embedded image/video feedback, file feedback with companion images and ZIP bundles can be imported.
- Ordinary static/offline pages cannot read arbitrary local paths or automatically return feedback to an Agent. Embedded return uses MCP Apps; browser compatibility mode saves through a local image session and the Agent reads it.
- Reopening a hosted page offline depends on browser caching. Download the HTML for reliable offline reopening. This version has no Service Worker.

Image exports default to a companion original (`file`), with `embedded` and `none` alternatives and an optional numbered JPEG. Multi-file downloads use ZIP. Import a ZIP or select the JSON and its companion images together; imports verify MIME, dimensions and SHA-256. Agent sessions publish companion files beside their JSON and keep base64 out of read summaries.

## Develop and verify

Sources live in `src/`. Detection reuses the repository's `origin/web` implementation; layout partitioning follows the current desktop model. Build the distributable without installing dependencies:

```text
node web/build.cjs
node --test web/tests/layout.test.cjs web/tests/movement.test.cjs web/tests/feedback.test.cjs web/tests/agent-session.test.cjs web/tests/install-skill.test.cjs
node web/tests/agent-flow.test.cjs
node web/tests/browser.test.cjs
node web/tests/visual.test.cjs
node web/tests/negative.test.cjs
node web/tests/panel.test.cjs
node web/tests/save-flow.test.cjs
node web/tests/latest-parity.test.cjs
node web/tests/native-video-boundaries.test.cjs
```

`tests/browser.test.cjs` uses Playwright Core. Set `EDITHERE_PLAYWRIGHT_MODULE`, `EDITHERE_BROWSER_PATH` and `EDITHERE_TEST_VIDEO` to existing installations and a local video fixture. Ignored test artifacts live in `tests/output/`. Rebuild after changing source files.

`visual.test.cjs` checks both themes at six window sizes, original brand bytes and proportions, text contrast and comment wrapping, and saves screenshots. `negative.test.cjs` exercises corrupt files, invalid coordinates, inconsistent movements and video metadata in an offline browser. It verifies unchanged pixels, comments and undo history after rejection, plus canceled replacements/drags, blank input and shortcuts during an import.

`panel.test.cjs` focuses on nine square and portrait windows: short-note height, centered and usable image size, automatic reveal, counts and scrolling, numeric editing, desktop wave animation, non-destructive repeated toggles, and reduced-motion support.

`latest-parity.test.cjs` checks movement slots, creation order, text editing and undo, empty-note project reload, reset, and complete exported data against existing desktop tests. `native-video-boundaries.test.cjs` covers native empty-video feedback, source relinking, and microsecond timestamps within the last millisecond. JSON assertions read actual downloads, with readable previews tested separately.

The repository license applies to this implementation.

MCP and standalone builds are separate. After changing MCP sources, run `npm --prefix web/mcp ci` and `node web/mcp/build.mjs` from the repository root to regenerate the official SDK HTML, server bundle, third-party licenses and hash manifest. Downloaded web-agent packages need no build dependencies. Distribution checks source and artifact hashes and rejects stale bundles.
