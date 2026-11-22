# Project Gallery & Multi-Round Review

EditHere acts as a multi-page visual review workbench for AI-generated and iterative UI development. When the user requests a project-level gallery review, the AI launches the project, captures all accessible pages into a manifest, waits for the user's explicit submission, implements code changes, refreshes only the code-affected screenshots, and re-opens the same gallery for the next round.

Desktop (Windows / macOS / Linux), Desktop MCP Connector, Trae Plugin, Web Agent / MCP Apps, Offline Web, and DSH all share the same stable page `id` model and `image-set-feedback-1` container. Actual capabilities always follow the running binary `--version`, `--help`, and connected host tools.

## 1. Initial Page Coverage

Project gallery reviews cover all implemented and accessible pages by default (unless the user explicitly narrows the scope):

- **Enumerate routes & states**: Inspect router tables, navigation menus, scene directories, and entry points. Cover main pages plus relevant dialogs/drawers, empty/error states, and light/dark themes or mobile viewports when requested.
- **Real runtime screenshots only**: Existing mockups are design references only. Never pass off stale screenshots, demo art, or fixed-seed test renders as live app output. When login gates, external services, or sandbox limits block specific routes, keep the accessible pages and clearly report the missing gaps.
- **Lossless quality**: Capture lossless PNGs at real device pixels (respecting DPR). Never resize JPEG previews up or rename `.jpg` to `.png`. See `references/capture.md` for Web and Desktop/Game capture SOPs.

## 2. Maintaining a Stable Project Manifest

Maintain one manifest per project (default `.edithere/results/project.json`):

```json
{
  "schemaVersion": "edithere-image-set-1",
  "name": "Project Review Gallery",
  "pages": [
    {"id": "home", "name": "Home", "path": "shots/home.png"},
    {"id": "inventory", "name": "Inventory", "path": "shots/inventory.png"}
  ]
}
```

- **Stable `id` identity**: Use durable page IDs (`home`, `inventory`, `settings.ai`). Preserve IDs and page order across renames, resizes, and screenshot replacements; only new pages get new IDs. Keep `name` human-readable for non-coders.
- **Round isolation**: Store round artifacts separately (e.g., `shots/round-001/`, `reviews/round-001/feedback.json`). Never overwrite screenshots or feedback files currently open for user review.
- **Use the bundled `scripts/image-set.mjs`** (invoke by its absolute path inside this skill directory; zero external dependencies):

```powershell
node "<skill-dir>/scripts/image-set.mjs" create .edithere/results/project.json --name "Project Review Gallery"
node "<skill-dir>/scripts/image-set.mjs" upsert .edithere/results/project.json --id inventory --name "Inventory" --image shots/inventory.png
node "<skill-dir>/scripts/image-set.mjs" list .edithere/results/project.json
node "<skill-dir>/scripts/image-set.mjs" remove .edithere/results/project.json --id deprecated-page
```

`upsert` updates the file path and SHA-256 checksum; always re-run `upsert` whenever a screenshot changes. `remove` removes only the manifest reference and keeps the image file on disk.

## 3. Handing Off for User Review

Every review round MUST use a **new, non-existent output path or directory**:

| Entry Point | How to Launch |
| --- | --- |
| Desktop CLI | `edithere-cli annotate <abs-manifest.json> --output <new-feedback.json>` (keep the same process waiting) |
| Desktop MCP / Plugin | `edithere_annotate_start({ imagePath: "<abs-manifest.json>", outputDir: "<round-dir>" })` then poll with `edithere_annotate_poll` |
| MCP Apps | `edithere_web_open({ projectPath: "<abs-manifest.json>", outputPath: "<new-feedback.json>" })` (resume via `sessionId`) |
| Web Agent Fallback | `node <web>/agent.cjs start --project <abs-manifest.json> --output <new-feedback.json>` and open returned URL |
| DSH | `node <dsh>/cli.mjs start --project <abs-manifest.json> --output <new-feedback.json> --open` |
| Offline Web | `node "<skill-dir>/scripts/image-set.mjs" bundle <manifest.json> --output <web-project.json>` for manual import/export |

- `open` / `edithere_open` is view-only and cannot substitute for an active `annotate` session with **Finish and return to AI**.
- Only act after explicit submission. Never read unsubmitted drafts or click complete on the user's behalf. Empty feedback is a valid submission meaning "no changes requested".

## 4. Reading Feedback & Implementing Changes

- `pageIndex` is the full gallery index (ID, name, dimensions, `annotated` flag); `pages[]` contains **only pages with annotations or layout movements**; `groupNotes` carries cross-page directives.
- Match each `pages[]` item back to its screenshot and source route via `pages[].id` (never treat gallery page index numbers as `pages[]` array indices).
- Before editing code, classify feedback using `references/layout-translation.md` into **Local Tweak** vs. **Design-System Adjustment**, then modify real components, styles, routes, or tokens. Editing screenshots or manifests alone does not count.

## 5. Code-Impact-Driven Screenshot Refresh & Next Round

After implementing and verifying source changes, determine which screenshots to refresh **from your actual code edits and dependency graph** rather than blindly re-capturing every page or running pixel-diff sweeps:

1. **Build the Affected Pages List**:
   - **Mandatory pages**: Any page that received annotations/movements in `pages[]` and had its source modified (e.g., if the user annotated `inventory`, re-capture and `upsert` `inventory` once edited).
   - **Ripple pages**: Any unannotated page affected by edits to shared components, layouts, theme tokens, or global CSS/styles — identify these directly from code imports/references and include them in the refresh list.
   - **Skipped pages**: Pages untouched by your code changes (or touched only in non-visual internal logic) keep their existing screenshot, `id`, and order untouched.
2. **Targeted re-capture & `upsert`**: Re-capture lossless PNGs only for the affected pages in live runtime and update the manifest using their existing `--id`. Append newly added pages and `remove` deleted routes.
3. **Self-check & open next round**: Visually inspect updated screenshots against the round's notes to confirm fixes landed cleanly with no regressions, then re-open the same manifest with a fresh output path. Summarize which feedback items were resolved, which pages were refreshed (including ripple pages), and any remaining ambiguities.
4. **Loop termination**: When authorized for multi-round review, re-opening the updated gallery continues the task without asking permission again. Stop cleanly when the user approves, requests pause/stop, or submits empty feedback; never auto-reopen after cancel or timeout.

## 6. Desktop AI Interpretation & Capacity Limits

- By default, the active Agent reads feedback JSON and screenshots directly; Desktop "AI Interpretation (Preview)" is an optional standalone text-formatting tool, never a prerequisite for this loop.
- Current limits: **64 pages max**, **48 MiB per image**, **128M total pixels** per set, and **96 MiB** Web/MCP session payload. Split large apps into module sets if exceeded rather than silently downscaling images. Never report Windows verification as macOS/Linux verification.
