# Cross-Stack Lossless Capture SOP

High-fidelity screenshots directly determine how accurately EditHere aligns annotations and component geometry to source code. Follow these rules across Web, Desktop, and Game HUD targets.

## 1. Source Determination

- **User-supplied image or mockup**: Use the supplied file directly at its native pixel dimensions; do not re-screenshot it.
- **Live page / app / game review**: Capture fresh screenshots from the running build. Stale artifacts, marketing assets, or fixed-seed test renders cannot stand in for the live UI (unless the user explicitly asks to reproduce a deterministic test state).
- **Target precision**: Identify the exact route, window, or state from project context and exclude EditHere's own windows. Only ask the user if the target remains genuinely ambiguous.

## 2. File Layout & Quality Rules

- Write screenshots **directly** to their final target directory — `.edithere/results/shots/round-NNN/` for multi-page/round reviews or `.edithere/captures/` for single shots — using unique filenames and absolute paths. Avoid saving to a staging directory and copying (`cp`) files in a second pass.
- Prefer **lossless PNG** matching actual device pixels (check logical viewport vs. DPR pixel size). Never disguise a downscaled JPEG by renaming `.jpg` to `.png`. If a tool only provides JPEG, keep the true extension and disclose the quality limit.

## 3. Web Frontend Capture SOP

### 3.1 Route & State Enumeration
1. Scan routes, navigation config, and page components to build the full accessible page inventory.
2. Add task-relevant interactive states: key modals/drawers, empty/error states, light/dark themes, and desktop (`1440x900`) or mobile (`390x844`) viewports when relevant.
3. If auth or backend dependencies block certain routes, capture accessible routes and clearly note the gaps.

### 3.2 Preferred Tool: `scripts/capture-web.mjs`
When `playwright` or `playwright-core` is available in the target project (or via `EDITHERE_PLAYWRIGHT_MODULE`), invoke the skill's bundled `scripts/capture-web.mjs` (or unified `edithere_capture` with `source: "web"`). It waits for `domcontentloaded`, critical-resource settlement (`300ms` quiet window capped at `1500ms`, ignoring HMR/WebSocket/SSE streams), `document.fonts.ready`, and double-`rAF`, captures DPR=2 lossless PNGs with shared `BrowserContext` reuse (`--concurrency 1..6`, `--wait-selector`, default `--wait-ms 0`), and optionally updates the gallery manifest:

- **Single page capture (+ optional manifest upsert)**:

```powershell
node "<skill-dir>/scripts/capture-web.mjs" `
  --url "http://127.0.0.1:5173/inventory" `
  --output ".edithere/results/shots/round-001/inventory.png" `
  --viewport 1440x900 --dpr 2 --theme light `
  --manifest ".edithere/results/project.json" --id inventory --name "Inventory"
```

- **Batch capture (single headless browser pass across multiple routes)**: prepare `batch.json` (`{ name?, manifest?, pages: [{ id, name, url, output, viewport?, dpr?, theme?, fullPage?, selector?, waitMs? }] }`) and run:

```powershell
node "<skill-dir>/scripts/capture-web.mjs" `
  --batch ".edithere/captures/batch.json" `
  --manifest ".edithere/results/project.json"
```

If `capture-web.mjs` exits with `code: "playwright_not_found"`, do NOT install Playwright globally or modify the project's dependencies behind the user's back — switch to the host's built-in browser screenshot tool.

### 3.3 Host Browser Tools (Authenticated Tabs / No Playwright)
- Consult the host browser tool's documentation first, pick the real target tab, and capture the visible viewport (`fullPage: false`) unless full-page scrolling is requested. Never reload an active user game/session unnecessarily.
- **Tabbit nuance**: `page.screenshot()` returns a PNG file receipt at `result.nextAction.path` (it is NOT a raw Playwright Buffer — do not call `.toString('base64')`).
- Always verify the image file actually exists on disk; displaying a preview image in chat does not imply a local PNG path exists.

## 4. Desktop Apps & Game HUD Capture SOP

- **Window capture over full desktop**: Use an authorized window-capture tool targeting the exact app/game window to avoid desktop clutter. Ensure the window is unobstructed, past splash/loading screens, and on the target view.
- **Single-process multi-page capture**: When a desktop app or game UI harness supports multi-page batch capture (for example comma-separated views like `--capture=menu,play,shop` writing directly into an output directory), export all target views in one engine launch instead of cold-starting the runtime once per page.
- **Multi-state games / HUDs**: Enter each live state (main menu, inventory, combat HUD, settings modal) and capture separately when no batch harness exists. Explicitly distinguish live interactive window captures from deterministic script-rendered reproductions.
- **Windows `computer-use` / `@oai/sky` rules**: Discover real window targets via `list_windows()`, fetch `get_window_state({ window, include_screenshot: true, include_text: false })`, verify `screenshots` contains the single intended target image, and decode its base64 `data:image/(png|jpeg);base64,...` to disk without printing raw data URLs to chat.
- **`edithere-cli capture` & MCP fast path (`0.14.1+`)**: Prefer `edithere_capture({ source: "screen" | "window", window?, windowId?, annotate: true })` (or `edithere-cli windows` + `edithere-cli capture --output <new.png>` / `capture --annotate --output <new.json> [--screen | --window <title> | --window-id <id>]`). Bare `edithere-cli capture` without `--output` still launches EditHere's interactive selection overlay and acknowledges immediately; headless `capture --output <new.png>` writes a PNG cleanly without disturbing open editor documents.

## 5. Pre-Handoff 5-Point Visual Check

Before opening EditHere or updating the manifest, verify every screenshot:
1. File exists, is non-empty, and decodes cleanly.
2. Frame shows the exact intended route, theme, and viewport.
3. No black frames, loading spinners, skeleton placeholders, unready web fonts, or overlapping OS popups.
4. Native resolution and lossless format preserved (no unintended downscale or crop).
5. Content reflects your latest source edits.
