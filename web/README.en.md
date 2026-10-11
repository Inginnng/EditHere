# EditHere Web

[简体中文](README.md)

Open **EditHere.html** directly in a browser, or serve this single file on a static website. After the page loads, file reading, annotation, layout changes and exports run locally in the browser. There is no backend, CDN, account or external runtime dependency. The page's Content Security Policy blocks network connections.

## Use

1. Drop an image or video, choose a file, or paste an image. Edit one source at a time.
2. Add smart-region, point, rectangle or whole-frame comments. With the selection tool (V), drag annotation points or rectangles, and adjust rectangle bounds with eight handles. Click a numbered marker to edit its text.
3. Choose **大爆炸** to enter component editing with a diagonal wave matching the desktop app. Click again to leave component editing while preserving movements and comments. Drag a region, resize with its eight handles, or expand **位置与尺寸** in the inspector for numeric editing. Ctrl / Command + click selects multiple regions for a shared move or scale. The wheel scales a selected component proportionally; without a selection, it cycles through overlapping region sizes. Manually draw missing regions with **框出组件**. Movement and scaling show an arrow from source to destination: click it or its inspector entry to add or edit text. Comments persist in projects and remain attached to the same movement in exported feedback. Reset a selected component to its original geometry when needed.
4. Pause a video and annotate the current frame. Select a saved timestamp to revisit its comments. Playback, pausing and seeking alone do not add exported frames.
5. Export or copy image feedback or `video-feedback-1` JSON compatible with current desktop EditHere feedback. Original images or annotated video frames are embedded by default. Annotation PNGs include both numbered comments and movement arrows.
6. Save a `.edithere-web.json` project to retain images, notes and layout state. Video projects contain annotated still frames rather than the whole source video; select the original local video again after reopening.

The page can download its own standalone offline HTML. Save a project before closing: this version does not automatically persist files, upload media, or store projects in browser storage.

## Free canvas workspace

Pan by dragging the empty workspace, using the hand tool (H), or holding Space / the middle mouse button. Ctrl / Command + the wheel zooms around the pointer. Fit avoids floating controls and the inspector; 1:1 shows original pixels. The workspace can pan freely, while component geometry stays within the original image for feedback compatibility.

Wide windows use a content-sized floating inspector on the right. Narrow, square and portrait windows use compact bottom cards with a centered image above them. Saving a note automatically reveals it; multiple comments scroll within about a quarter of the window height, above the video timeline. The top-right badge retains the comment count when collapsed. Coordinates remain in comment tooltips and numeric controls expand on demand. Short landscape windows use compact tools and a narrow side inspector. Find theme switching and shortcuts at the bottom left. Hiding markers does not remove comments or alter exported feedback.

The existing `assets/icons/edithere.svg` and `assets/brand/edithere-lockup*.svg` files are embedded byte for byte at build time and retain their original aspect ratios. Existing light/dark variants cover both themes; very narrow windows use the original symbol. The demo canvas also uses the original lockup.

## Limits

- Local color and edge detection is included; desktop table hierarchy detection has not been ported. Draw a region manually when automatic detection misses it.
- System OCR, global screenshot shortcuts, scrolling capture, tray integration and always-on-top pinned images remain desktop capabilities. This standalone HTML accepts uploaded or pasted media.
- Layout movement operates on rectangular pixel regions, leaving transparent holes at their source positions. It does not generate backgrounds, segment semantic objects or render edited videos.
- Images are limited to 32 million pixels. Video support depends on the browser and operating-system codecs; use H.264 MP4 or WebM. Where available, video frame callbacks supply the presented frame's media time; other browsers fall back to playback position. This is not a frame-by-frame video editor.
- Web projects use `edithere-web-project-1`. Desktop `.edithere` project files are not supported. Current image/video feedback with embedded images can be imported.
- Browsers cannot automatically read arbitrary local paths or return feedback to a CLI. Hand the JSON to your AI tool; automatic local integration would require a separate connector.
- Reopening a hosted page offline depends on browser caching. Download the HTML for reliable offline reopening. This version has no Service Worker.

## Develop and verify

Sources live in `src/`. Detection reuses the repository's `origin/web` implementation; layout partitioning follows the current desktop model. Build the distributable without installing dependencies:

```text
node web/build.cjs
node --test web/tests/layout.test.cjs web/tests/movement.test.cjs
node web/tests/browser.test.cjs
node web/tests/visual.test.cjs
node web/tests/negative.test.cjs
node web/tests/panel.test.cjs
```

`tests/browser.test.cjs` uses Playwright Core. Set `EDITHERE_PLAYWRIGHT_MODULE`, `EDITHERE_BROWSER_PATH` and `EDITHERE_TEST_VIDEO` to existing installations and a local video fixture. Ignored test artifacts live in `tests/output/`. Rebuild after changing source files.

`visual.test.cjs` checks both themes at six window sizes, original brand bytes and proportions, text contrast and comment wrapping, and saves screenshots. `negative.test.cjs` exercises corrupt files, invalid coordinates, inconsistent movements and video metadata in an offline browser. It verifies unchanged pixels, comments and undo history after rejection, plus canceled replacements/drags, blank input and shortcuts during an import.

`panel.test.cjs` focuses on eight square and portrait windows: short-note height, centered and usable image size, automatic reveal, counts and scrolling, numeric editing, desktop wave animation, non-destructive repeated toggles, and reduced-motion support.

The repository license applies to this implementation.
