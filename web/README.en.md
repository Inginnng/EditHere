# EditHere Web

[简体中文](README.md)

Open **EditHere.html** directly in a browser, or serve this single file on a static website. After the page loads, file reading, annotation, layout changes and exports run locally in the browser. The standalone HTML has no backend, CDN, account or external runtime dependency, and its Content Security Policy blocks network connections. Agent sessions described below use a separate localhost service to load an image and return feedback.

For an Agent browser panel, run `node web/preview.cjs` from the repository root (requires an installed Node.js), then open `http://127.0.0.1:7330/EditHere.html`. The launcher reuses this project's service or starts it in an independent background process and checks the served HTML. Run it again to restore a stopped service. A refused connection means the local service is not listening; refreshing the page does not start it. The preview binds only to localhost and does not upload media.

## Agent skill and automatic return

For first-time setup, give the agent the [AI installation guide](AI-SETUP.en.md) and an actual `web-agent` ZIP download URL or local file. The guide does not require the Skill to be installed first. A locally generated package does not imply a public download endpoint.

`skills/edithere-web/` is an installable Agent skill. It requires Node.js ≥ 18. Extract the Agent Web package into a writable location you will retain, then run `node web/install-skill.cjs` (or `node install-skill.cjs` when this directory is the package root). The default destination is `$CODEX_HOME/skills/edithere-web` or `~/.codex/skills/edithere-web`; use `--destination <absolute-skill-directory>` for another host. The installer checks the runtime and records this CLI and Node.js installation in `references/local-installation.md`. It refuses existing destinations, preserves existing skills, and does not modify the desktop `edithere` skill. Keep the CLI's adjacent page and service files. The service currently writes private session state and logs into `tests/output/` beside the runtime. The repository skill retains a relative entrypoint without developer-specific paths. Use the web skill for browser panels and the desktop entrypoint when the user explicitly requests the desktop app.

Once installed, a request such as “Help me annotate the game homepage with EditHere” or `$edithere-web` starts this flow. Provided images are used directly. For a running page, the Agent uses permitted host browser/window tools to capture and inspect a fresh screenshot before loading it into the right-side session page. Capture depends on host tools; the web bridge has no built-in system screenshot implementation. Old images or fixed test renders cannot stand in for the currently running interface. The user clicks the top **反馈** button (Finish and return to AI), which writes feedback to this session's unique output. Manual JSON copying is unnecessary.

Equivalent CLI flow (use actual absolute image/output paths and an output file that does not yet exist):

```text
node web/agent.cjs start --image <image-path> --output <new-feedback-path> --timeout 86400
node web/agent.cjs poll --session <session-id-from-start>
```

Open the URL returned by `start`, rather than the ordinary preview URL. The skill saves the session record, hands editing to the user, and ends the turn. When the host supports scheduling and the user authorizes follow-up, register one heartbeat for this conversation to `poll` the original session every two minutes, silently while pending. Promise automatic follow-up only after successful registration; resuming the conversation after submission has scheduling latency. Without scheduling or authorization, the user says “continue” after submitting and the Agent polls the same ID once. Use `wait`, bounded to 55 seconds per call, only when the user explicitly asks the Agent to wait now. Continue only for a parseable `completed` result, stop scheduled follow-up on terminal states, and do not use stale output after cancellation or expiry. The CLI itself cannot message an ended conversation or immediately wake its host. The service listens only on loopback, without sending images or feedback to external servers. Static and offline pages retain their download/copy export flow.

## Use

1. Drop an image or video, choose a file, or paste an image. Edit one source at a time.
2. Add smart-region, point, rectangle or whole-frame comments. With the selection tool (V), drag annotation points or rectangles, and adjust rectangle bounds with eight handles. Click a numbered marker to edit its text.
3. Choose **大爆炸** to enter component editing with a diagonal wave matching the desktop app. Click again to leave component editing while preserving movements and comments. Drag a region or resize with its eight handles. The small **组件布局** button immediately left of the global-note button opens numeric controls in a popup. It starts closed and consumes no inspector height. Ctrl / Command + click selects multiple regions for a shared move or scale. The wheel scales a selected component proportionally; without a selection, it cycles through overlapping region sizes. Manually draw missing regions with **框出组件**. Movement and scaling show an arrow from source to destination: click it or its inspector entry to add or edit text. Comments persist in projects and remain attached to the same movement in exported feedback. Reset a selected component to its original geometry when needed.
4. Pause a video and annotate the current frame. Select a saved timestamp to revisit its comments. Playback, pausing and seeking alone do not add exported frames.
5. Export or copy image feedback or `video-feedback-1` JSON compatible with current desktop EditHere feedback. Original images or annotated video frames are embedded by default. The export dialog also saves annotation PNGs with numbered comments and blue movement arrows.
6. Save a `.edithere-web.json` project to retain images, notes and layout state. Video projects contain annotated still frames rather than the whole source video; select the original local video again after reopening.

The page can download its own standalone offline HTML. Save a project before closing: the standalone HTML does not automatically persist files, upload media, or store projects in browser storage. An Agent session writes its predefined feedback output only when the user explicitly submits.

A movement immediately creates a real note with optional text, in creation order among other notes. Editing or clearing its text preserves its number; project reload preserves its identity and order. Reset removes an empty movement note and retains one with text, following desktop REG-110.

Feedback follows desktop `exportFeedback`: actual trajectories establish objects first, movement text joins the matching original source, and ordinary comments use current result coordinates. Empty movements export `annotations: []`. A detached child does not shrink its parent's tracked frame; adjusting an existing movement preserves trajectory order. The preview folds image bytes and shows one image object per line; copy and download retain the full compact JSON. Sidebar note order and exported object grouping follow their respective desktop rules.

## Free canvas workspace

Pan by dragging the empty workspace, using the hand tool (H), or holding Space / the middle mouse button. Ctrl / Command + the wheel zooms around the pointer. Fit avoids floating controls and the inspector; 1:1 shows original pixels. The workspace can pan freely, while component geometry stays within the original image for feedback compatibility.

Wide windows use a content-sized floating inspector on the right. Narrow, square and portrait windows use compact bottom cards with a centered image above them. Saving a note automatically reveals it; multiple comments scroll within about a quarter of the window height, above the video timeline. The top-right badge retains the comment count when collapsed. Coordinates remain in comment tooltips and numeric controls expand on demand. Short landscape windows use compact tools and a narrow side inspector. Zoom controls sit between the top islands, wrapping onto a second row in narrow windows. Editing tools sit near the bottom edge, with the inspector positioned above their measured height. The comment icon in the inspector header adds a global note. Find theme switching and shortcuts at the bottom left, or in the bottom-left More tools menu in narrow windows. Hiding markers does not remove comments or alter exported feedback.

The existing `assets/icons/edithere.svg` and `assets/brand/edithere-lockup*.svg` files are embedded byte for byte at build time and retain their original aspect ratios. Existing light/dark variants cover both themes; very narrow windows use the original symbol. The demo canvas also uses the original lockup.

## Limits

- Local color and edge detection is included; desktop table hierarchy detection has not been ported. Draw a region manually when automatic detection misses it.
- System OCR, global screenshot shortcuts, scrolling capture, tray integration and always-on-top pinned images remain desktop capabilities. This standalone HTML accepts uploaded or pasted media.
- Layout movement operates on rectangular pixel regions, leaving transparent holes at their source positions. It does not generate backgrounds, segment semantic objects or render edited videos.
- Images are limited to 32 million pixels. Video support depends on the browser and operating-system codecs; use H.264 MP4 or WebM. Where available, video frame callbacks supply the presented frame's media time; other browsers fall back to playback position. This is not a frame-by-frame video editor.
- Web projects use `edithere-web-project-1`. Desktop `.edithere` project files are not supported. Current image/video feedback with embedded images can be imported.
- Ordinary static/offline pages cannot automatically read arbitrary local paths or return feedback to a CLI. Use the Agent skill above for a localhost image session with automatic return, limited to its explicitly supplied image and output.
- Reopening a hosted page offline depends on browser caching. Download the HTML for reliable offline reopening. This version has no Service Worker.

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
node web/tests/latest-parity.test.cjs
node web/tests/native-video-boundaries.test.cjs
```

`tests/browser.test.cjs` uses Playwright Core. Set `EDITHERE_PLAYWRIGHT_MODULE`, `EDITHERE_BROWSER_PATH` and `EDITHERE_TEST_VIDEO` to existing installations and a local video fixture. Ignored test artifacts live in `tests/output/`. Rebuild after changing source files.

`visual.test.cjs` checks both themes at six window sizes, original brand bytes and proportions, text contrast and comment wrapping, and saves screenshots. `negative.test.cjs` exercises corrupt files, invalid coordinates, inconsistent movements and video metadata in an offline browser. It verifies unchanged pixels, comments and undo history after rejection, plus canceled replacements/drags, blank input and shortcuts during an import.

`panel.test.cjs` focuses on nine square and portrait windows: short-note height, centered and usable image size, automatic reveal, counts and scrolling, numeric editing, desktop wave animation, non-destructive repeated toggles, and reduced-motion support.

`latest-parity.test.cjs` checks movement slots, creation order, text editing and undo, empty-note project reload, reset, and complete exported data against existing desktop tests. `native-video-boundaries.test.cjs` covers native empty-video feedback, source relinking, and microsecond timestamps within the last millisecond. JSON assertions read actual downloads, with readable previews tested separately.

The repository license applies to this implementation.
