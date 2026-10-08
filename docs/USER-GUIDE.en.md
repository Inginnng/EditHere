# EditHere: User guide

[简体中文](USER-GUIDE.md) · **English**

[Back to the product overview](../README.en.md) · [AI and the command line](AGENT-CLI.en.md) · [Changelog](../CHANGELOG.md) · [Development notes](DEVELOPMENT.en.md)

This page covers **0.10.3** and describes capture, video annotation, comments, component adjustments, project saving, feedback export and platform limits. The public download links below point to GitHub's current public release. Paths are relative to the project root; see the [licence](../LICENSING.md) for download and usage terms.

## Contents

- [Running and updating](#running-and-updating)
- [Capture](#capture)
- [Annotation editing](#annotation-editing)
- [Video annotation](#video-annotation)
- [Explode](#explode)
- [Save and export](#save-and-export)
- [Feedback JSON](#feedback-json)
- [Settings](#settings)
- [Working with AI](#working-with-ai)
- [Recognition and platform boundaries](#recognition-and-platform-boundaries)

## Running and updating

### Installation

For the first upgrade after the rename, download the new package directly from the [latest release page](https://github.com/Inginnng/EditHere/releases/latest) after quitting the old version from the tray. Legacy project formats are no longer supported — use `.edithere` projects. If launch at login reports an old path, keep the startup option selected and save to refresh it; you do not need to close and reopen it. Saving a project once in the new app registers the `.edithere` file association. Older versions' automatic update checks may not recognize the new repository URL.

On Windows the [installer](https://github.com/Inginnng/EditHere/releases/latest/download/EditHere-win-x64-setup.exe) is recommended (a local build is named like `dist/EditHere-<version>-win-x64-setup.exe`; releases use a single name without the version). Release asset names do not include a version, so the link always points to the latest version. It installs to `%LOCALAPPDATA%\Programs\EditHere` for the current user only, with no administrator privileges. The installer creates a Start menu entry and an uninstaller and registers the `.edithere` project association; on the components page you can choose launch at login, add to PATH and a desktop shortcut. A new installation selects launch at login and PATH by default; upgrades preserve the startup option according to the existing per-user startup registration. Reopen your terminal and AI tools after installation so they can pick up the new PATH.

Windows [portable ZIP](https://github.com/Inginnng/EditHere/releases/latest/download/EditHere-win-x64.zip): a local build is named like `dist/EditHere-<version>-win-x64.zip`. Extract the entire archive and run `EditHere.exe`, keeping the DLLs and plugin folders alongside it; Qt, Python, Node and .NET do not need to be installed. The portable version does not add itself to PATH; run the CLI with its full path.

The Windows installer is not yet code-signed. Download it from this repository's Releases and verify the [application package SHA-256 checksums](https://github.com/Inginnng/EditHere/releases/latest/download/SHA256SUMS.txt). In the download folder, open PowerShell and run `Get-FileHash .\EditHere-win-x64-setup.exe -Algorithm SHA256` to compute the installer's hash and compare it with the matching entry in the file.

On macOS, download the [universal DMG](https://github.com/Inginnng/EditHere/releases/latest/download/EditHere-macos-universal.dmg) and drag `EditHere.app` to the **Applications** entry inside. The CLI is at `EditHere.app/Contents/MacOS/edithere-cli`. Your first capture needs Screen Recording permission, and system element detection needs Accessibility permission; it has not yet been acceptance-tested on a real Mac or notarised by Apple.

On Windows you can remove the installed version through the system's “Installed apps” or the uninstall entry in the Start menu. Uninstalling removes only the files installed with the package and their registrations, keeping your settings and projects; you do not need to delete that data to update.

If EditHere is running when you install or upgrade, the installer first asks the running instance to quit on its own (unsaved annotations still go through the app's own save prompt, and the window comes to the foreground automatically), then waits for the process to end before writing files; a silent install shows no prompt. You can also run `EditHere.exe --quit` to ask a running instance to exit through the normal flow.

### In-app updates

In **Settings → About and updates**, click **“Check for updates”**, or right-click the tray icon and choose **“Check for updates…”**. When Windows finds a compatible new version it shows **“Update now”**; after downloading and verifying the SHA-256, confirm your unsaved edits, then the installer starts and the app exits. If you cancel the confirmation or the installer fails to start, the app does not exit. The installed and portable versions share the same NSIS installer; a portable update does not register an installation, modify PATH or create shortcuts. A portable update requires a target release of at least 0.9.9; when no compatible package exists, and on macOS, the button opens the release page for a manual download.

An update prepares a new directory beside the original and then switches to it; it needs write permission on the parent directory and extra disk space. On failure it tries to restore the old directory, and on success it still keeps a backup of it, whose path is recorded in `update-backup.txt` (UTF-16LE text) in the program directory; once the new version works and the backup holds no unique files you want to keep, you can delete the backup yourself. With **“Check for updates on launch”** selected, the app checks once on every launch. Update checks contact GitHub; your capture and annotation data are not uploaded with an update.

Older clients may not recognize the new download address after the rename, so for the first upgrade after the rename use the download links above for a manual download.

### Launch and tray

The first manual launch shows the guide; after you finish or skip it, it no longer appears automatically. Afterwards press **Alt + Shift + 2** or click the tray icon to capture. A shortcut capture does not simulate key presses or activate a temporary window; the tray entry closes menus and pop-ups first, then captures the screen. After you cancel, copy or save the capture, the original window is restored. Existing shortcut settings are unchanged and can be restored to defaults in Settings. Only the capture mask stays on top; the editor window is an ordinary application window.

The tray menu, in order, is: **Capture**, **New annotation (empty window)** (both show the current global shortcut on the right), **Open image or project**, a separator, **Settings…**, **Check for updates…**, a separator and **Quit**. On macOS there is also an extra **Grant Accessibility permission** entry for reading system element boundaries; image detection still works without it.

The default shortcuts are listed below; you can change the corresponding key actions in [Settings](#settings).

## Capture

- Hover for smart select; scroll up to choose a larger region and down to choose a smaller one. Every detected box that contains the pointer can be cycled through; they do not have to be parent and child of each other. **The level you scrolled to is remembered**: after switching to “whole row / whole table”, moving the mouse or pressing the arrow keys still keeps that level selected; scroll back to return to the smallest block, and a new capture also starts again from the smallest block. **Fine-tuning with the arrow keys does not change the selected box**: as you nudge the pointer box by box, the selected block stays put and does not shrink into a button or cell inside it. Moving the mouse over a wide range is still smart select — it selects whatever you point at. **The arrow keys move the mouse pointer one step at a time** (one step per press, only in the direction pressed, and an exact step even on a scaled display); the detected box highlight and the magnifier follow it, to line up on a single pixel. This works from the moment you enter capture until the selection is settled.
- Click a block or drag to draw a box; **releasing the mouse settles the selection**, the capture toolbar appears **above** it and a column of style tools appears to its **right**; when there is not enough room above, the toolbar moves below automatically. Entering annotation is an action on the toolbar (click “Annotate” or press Enter), no longer the default result of releasing the mouse.
- Once the selection is settled you can still adjust it: drag inside the region to move it, drag a corner to resize, and drag outside to draw a new selection; **the arrow keys move the mouse pointer** (1 pixel at a time, with the magnifier following, to line up on a single pixel), `Shift` + arrow key shrinks 1 pixel from that side, and `Ctrl` + arrow key expands 1 pixel outwards. Esc cancels the capture.

### Capture toolbar

| Action | Default shortcut | Description |
| --- | --- | --- |
| Annotate | Enter | Open the annotation editor |
| Copy image | Ctrl + C | Put the selection (including the chosen corner radius, border and shadow) on the clipboard |
| Pin to screen | Ctrl + T, Ctrl + 2, P | Pin this block on the screen, **back in the position it came from** — see below |
| Save image | Ctrl + S | Save as PNG or JPEG |
| Text recognition | Shift + C, T | Open the recognition result window — see the next section |
| Scrolling capture (development build) | L | Scroll the page both ways by hand; stitch automatically and show a preview |
| Pick a colour | C | Shortcut only, with no toolbar button: after picking starts the magnifier follows the mouse, then press C or Ctrl + C to copy the colour value (the reading is also shown beside the magnifier while C is not pressed) |
| Previous / next capture in history | < / > | Bring up earlier captures without leaving the capture window |
| Restore last selection | R | Reuse the most recent selection size |
| More options | H | Open the toolbar menu |
| Cancel | Esc | Discard this capture |

- On the left of the toolbar, the selection's top-left coordinates and “width × height px” are shown; click it to type an exact size. When the aspect ratio is locked, the height is derived from the ratio.
- The style column to the right of the selection is: **Rounded corners**, **Shadow / border**, **Reset**. Rounded corners and shadow each open a small panel with sliders; the shadow panel has “Shadow | Border” tabs and an intensity slider (0–100). Once you tick “Set for every capture”, that style is written to the settings and every later capture starts from it. **Pin to screen appears only once, on the toolbar**, and is no longer repeated in the style column.
- While you drag the selection, a **magnifier** appears beside the pointer, enlarging the area around it many times over. It enlarges **what is on screen**: the blue frame and the darkened surroundings are magnified with it, and the frame is exactly as thick after magnification. The panel also shows pixel coordinates and `RGB` / `HEX` / `HSV` / `HSL` readings. Once the selection is settled, the magnifier and readings stay as long as the mouse is inside the selection, without entering colour picking first. The magnifier automatically avoids the toolbar and the style column.
- The “More” menu also contains: a fixed aspect ratio (1:1, 4:3, 3:2, 16:9, 9:16 or a custom one such as `21:9`), the text recognition language, capture history (including previous / next), recent selections and a list of the current shortcuts.
- Rounded corners and border preview on the selection **exactly as they will appear**; copy, save, pin and recognition all use the finished image with these effects. The shadow is **on by default** (medium intensity) and turns off when you drag the intensity slider to 0; it extends the image outwards by the corresponding pixels with a smooth falloff that fades towards the edge, in the interface's accent colour — the blue around the selection — not black. The shadow is **drawn only on pinned images** and is not shown in the capture window: set the intensity and pin the image once to see the effect. Text recognition reads the capture itself, without the shadow's extended edge.
- The last **20 captures** and **10 selections** are kept across sessions. While capturing, press `H` to pick one of the last 5 captures and open it directly, or press `R` to restore the most recent selection size.

**Pick a colour**: once picking starts the magnifier follows the mouse and reads the colour wherever you point. The panel shows pixel coordinates, a colour swatch and `RGB`, `HEX`, `HSV` and `HSL` forms; `Shift` cycles through the four, and the deepest-coloured one is the one that will be copied. Press `C` or `Ctrl + C` to copy the colour value; a mouse click also copies and ends picking, and `Esc` or the right mouse button ends picking.

**Pinned windows**: you can pin several at once, and each lands by default in the position you just captured.

- **Move**: press and drag in the middle of the image.
- **Resize**: move the mouse to the image's **edge or corner** and the cursor changes to the matching resize cursor; press and drag to scale it. The wheel also zooms (10%–400%), and `0` fits to screen. Both share the same zoom level, so the wheel does not jump back after a drag.
- The image's edge is conveyed by the **shadow**, with no extra outline. Once you turn the shadow off in the context menu, there is **no line at all** on the image: it is just a picture sitting on the desktop, and its edge is invisible when the desktop colour is similar. The draggable edges still exist, they are just no longer drawn.
- **The shadow also shows which one is active**: the selected image's shadow is **blue**; once you click another window it turns **grey**. With several pinned at once you can tell them apart at a glance. An image with the shadow off shows neither colour.
- `Ctrl + C` copies; **double-click or `Esc` closes**.
- **Context menu**: the first item is **Annotate** (hand the image to the editor; the shadow is not counted on the canvas), followed by copy image, save image, text recognition, **Turn** (left/right 90°, flip horizontally, flip vertically), **Transparency** (100% / 75% / 50% / 25% / 10%), **Shadow** (toggle at any time), zoom (fit to screen / original size / back to capture position) and close. An image pasted directly from elsewhere has no toggleable shadow and that item is greyed out.
- An image with a shadow carries the shadow with it, and the transparent margin around it shows the desktop through.

### Opening an empty annotation window directly

An image already on disk does not have to be captured again: press the global shortcut **Alt + Shift + 1**, or choose **“New annotation (empty window)”** in the tray menu, to open the annotation window directly, with “drop or paste an image here” in the middle and an **“Import image or project”** button below. The shortcut also works in other applications; change it or clear it to disable it in **Settings → Shortcuts**. **Drop an image into the window** to start annotating; pasting with `Ctrl + V` works too, and the button lets you choose an image or a project from a file dialog.

If there are unsaved changes when you close the annotation window, it asks “Save the project first?”. This dialog has **“Do not ask again; change this in Settings”**: once ticked, later closes discard unsaved changes directly with no prompt. To change it back, tick “Ask about saving when the annotation window closes” in **Settings → Default behavior**.

### Scrolling capture (development build)

Draw a selection around scrolling content, then click “Scrolling capture” on the toolbar or press `L`. The white selection frame outside the grey mask always marks the region actually captured; by default you scroll slowly up and down inside the frame yourself, and starting from the middle of a page you can scroll up first and then down; revisiting content that was already captured is not stitched twice. The preview to the right of the selection keeps a fixed width and extends up and down as the image grows; once it passes the screen edge it shows the part near the current position and the content never shrinks as the capture grows. A green frame marks the current view; an orange frame appears when a match fails, and scrolling back a little restores it, as does returning to earlier captured content further away. The fixed top and bottom bars are kept once. The menu follows the application theme, existing actions reuse the same icons, and hovering over a button shows its effect and limitations. Stopping the scroll keeps waiting, so capture as far as you want and then click edit, copy, save or pin. The selection should sit inside a single scrolling region, avoiding the browser toolbar and separate sidebars where possible, and avoiding a single scroll that passes a whole screen without overlapping content.

When you want the app to scroll for you, click the mouse icon on the toolbar to turn on “Automatic scrolling”, and you can turn it off at any time to return to manual; the switch is off every time you start a scrolling capture. Automatic mode finishes once it confirms it has reached the end. Clicking the red “Stop” cancels this round's capture and result and removes the thumbnail; clicking again restarts from the current frame. `Esc` or “Close” discards this round and returns to the original selection. When the view keeps changing or the overlap cannot be matched reliably, the capture pauses and can still deliver what it has; it cannot finish when there is no new content, and it never treats the first frame as a scrolling capture.

While running, dragging the move handle on the toolbar moves the selection along the capture direction; after stopping you can move it freely and resize it by dragging its edges. Switching “Vertical / Horizontal” or changing the selection size clears this round's stitching and starts again from the current frame. Horizontal mode means scrolling left and right yourself or dragging the horizontal scrollbar.

Click the scissors icon and choose the up/down crop button (left/right in horizontal mode) to crop immediately to the current frame's boundary; you can also turn on “Auto-crop”, off by default, which shortens the stitched result directly when you scroll back. Edit, copy, save and pin all use the cropped image; saving goes through the ordinary dialog to choose a location. Dragging the move handle to the right of the size keeps the existing image, and releasing it continues appending on top of the captured length without recalculating; only actually changing the selection size or switching direction starts again. The default direction and automatic cropping can be changed in Settings. Windows, macOS and Linux share this interface. macOS needs Screen Recording permission, and automatic scrolling also needs Accessibility and event-posting permission; automatic scrolling on Linux X11 needs XTEST. Wayland needs authorised screen sharing and PipeWire support, and currently relies on you scrolling by hand. Settings can enable an “Ultra-long capture” mode, up to 2 million pixels on one axis; beyond the editor's 32767-pixel edge or 32 million-pixel limit it can still be saved as PNG, but cannot be edited, copied or pinned directly. Normal mode still has a 600-frame ceiling. See the [scrolling capture implementation reference](LONG-CAPTURE.en.md) for the design notes and verification entry points.

### Text recognition

- The result is listed **line by line** in a separate window; clicking a line highlights its position on the original image so you can spot misread text. You can copy everything, copy only the selected lines, or re-recognise in another language.
- The recognition language is chosen in **Settings → Appearance → Recognition language** (Follow system / Simplified Chinese / English), and can also be switched in place from the capture toolbar menu; the choice is remembered.
- Recognition uses **built-in system capabilities, with no network, no upload and no added dependency**: Windows uses the system OCR engine and macOS uses Vision. When the system lacks the recognition pack for a language, the window tells you where to add it.
- When an image is larger than the recognition engine's limit, it is split into strips and recognised in segments, then the line boxes are mapped back to the whole image's coordinates, so a large image loses no lines and stays aligned.

## Annotation editing

- The top tools are, in order, **Smart select**, **Point**, **Box**, **Adjust**; then **Explode**, undo, redo, **Fit image**, **Hide / show on-canvas annotations**, **Full screen**, and a button to collapse the annotation column. The default tool is chosen in **Settings → Toolbar → Default annotation tool**.
- The annotation column is permanently reserved from the start. Click an index or a move arrow and edit directly on the right; leaving the input box saves automatically, and the plus sign in the top-right adds a global note. Releasing a box selection immediately lets you type an annotation in the sidebar while keeping the manual region for component adjustments. When the window narrows, a button on the annotation column collapses the sidebar.
- Scrolling on empty space, or `Ctrl` + scrolling, zooms the whole image around the pointer; holding the middle mouse button pans freely, and it works even when the image is smaller than the window and can go past all four edges, in both ordinary annotation and Explode mode. `Ctrl + 0` fits to window. In an ordinary window, drag the title bar, or hold space / `Alt` and drag the image, to move the window.
- The Adjust tool moves or resizes annotation boxes; click an index to edit its text. `Ctrl + Z` undoes, `Ctrl + Y` redoes, and `Delete` removes the selected annotation.
- You can open, drop or paste PNG / JPEG / WebP / BMP, and reopen a `.edithere` project or feedback JSON.
- Closing the current capture prompts you to save unsaved changes, then releases the capture and slice caches; the app stays in the tray.
- The **eye** button at the top hides the marks on the image and keeps the annotation text on the right, to view the original or export a clean comparison image.

Mac editing shortcuts use Command and redo is Command + Shift + Z; global capture uses Option + Shift + 2, and new annotation uses Option + Shift + 1. Mac has not yet been verified on a real machine.

## Video annotation

Open a file or drop in a video to start viewing; click “Play” to play it and “Pause” to hold the frame. After dragging the timeline or using `−1s` / `+1s` to seek, the frame stops where you want: seeking while paused stays paused, and seeking while playing also pauses. An ordinary pause, the first frame and a seek still show the video; only the first time you actually add a point, box or global annotation is a screenshot saved and the annotation canvas entered. Selecting a mode, zooming and panning do not create a screenshot. Click “Play” again or press space to resume, and add the next set of annotations after finding another timestamp. Clicking the cyan downward-pointing tab on the timeline, or choosing from the “Annotated frames” list, restores an existing screenshot and its annotations directly; space toggles play and pause (it still types a space inside a text input box).

Saving an `.edithere` video project references the source video address and embeds the original images of the annotated frames and the editing state. Saving a project or exporting JSON does not automatically add a screenshot of a frame you merely paused to view. If the source video moves or is lost you can still view the saved screenshots; click “Locate the video again” to resume playback. Exported JSON uses `video-feedback-1`: each frame contains the actual screenshot timestamp, image feedback and an optional embedded screenshot, and the root object index attaches `timestampMs` and `frameId` to every shape annotation.

Timestamps are relative to the start of the source video, and coordinates are pixels in the corresponding frame's screenshot. The same coordinate at different timestamps is separate feedback, and an AI needs to read the time, the screenshot and the comment together. See the [video annotation guide](VIDEO-ANNOTATION.en.md) for the full structure, how to share the screenshot files, and the actual verification record. This round's video feature was acceptance-tested on Windows; other platforms follow their own build and acceptance results.

## Explode

Click the “Explode” button at the top; the button changes colour and an iridescent wave sweeps from top-right to bottom-left. Components are adjusted directly inside the **current window**.

Entering it for the first time splits the image into blocks along the detected boxes; when you click the button again to leave the mode, the image keeps its adjustments and the slice cache is kept. Reopening uses the same blocks and adjustments without re-splitting; the cache is cleared only after you close the current capture.

While Explode is on, the annotation column always stays on the right; the Point tool adds comments to the result, and indices and move arrows can be clicked to edit directly. The Smart tools select and adjust blocks, and Box adds manual regions; after selecting a component you can add an annotation on the right. The component adjustment area is fixed at the bottom of the sidebar, and the annotation list scrolls independently. Annotations attached to a component move and scale with it, and the editing result and annotations share the same canvas coordinates.

| Action | Behavior |
| --- | --- |
| Hover and scroll with nothing selected | Cycle through every candidate region under the cursor |
| Click a candidate region | Select a component or component group |
| Drag inside a selected region | Move the component |
| Drag an edge midpoint | Change the width or height |
| Drag a corner | Scale proportionally, without Shift |
| Scroll while pointing at a selected component | Scale the component up or down; pointing at empty space zooms the whole image |
| X, Y, width, height, scale on the right | Set the pixel position, size and ratio precisely |
| Arrow keys | Move 1 px |
| `Ctrl` + click | Multi-select; the selected blocks drag or transform together |
| Right-click, Esc or “Deselect” | Return to hover-based selection |
| Draw a box after “Box” at the bottom | Add an operable region, before or after exploding |
| `Ctrl` + scroll | Zoom the view |

When you select a large box, the components inside move and scale with it, keeping their relative layout; once a block is moved out of its parent group it leaves that group, and moving the parent no longer carries it. Undo and redo are supported. After a move, the original position is left empty and shown as a transparent chequerboard; no background is filled in and no covered content is restored. Adjustments are limited to a canvas the size of the original image.

A move becomes an annotation the moment it happens, listed alongside text annotations in the order it occurred, and its position is not affected by text you write afterwards; it can have no text and can be filled in later by opening it, and its index matches the arrow on the image. Once a block is moved back to its original position, move annotations with no text are removed along with it, while any that already have text are kept.

## Save and export

| Action | Default shortcut | Result |
| --- | --- | --- |
| Save project | `Ctrl + S` | An `.edithere` file that always embeds the original image and the slice cache, and can be reopened to keep editing |
| View JSON | `Ctrl + E` | Open the feedback preview window, where you can copy or save |
| Copy annotated image | `Ctrl + C` | Put the annotated image on the clipboard; the image area is a uniform 960 pixels wide, and indices, text and the sidebar keep a consistent scale |
| Copy JSON file / Copy JSON content / Save image / Hide marks / Global annotation | Not bound by default | Assign them yourself in Settings |

The bottom toolbar shows **Save project**, **Save image**, **View JSON**, **Copy JSON content**, **Copy JSON file**, **Copy annotated image**, **Retake screenshot** and **Fit image** by default; which buttons appear is chosen in **Settings → Toolbar**, and unchecked buttons do not appear in the bottom bar.

The **“Need help?”** entry on the toolbar and at the top of the annotation window explains what to do when an agent cannot take the feedback.

The **View JSON** window provides three options and actions:

- **Includes the source image; fully self-contained**: by default follows “Include the source image in exported JSON by default” in Settings. When unchecked, the JSON contains no image and reopening needs a PNG of the same name alongside it.
- **Compressed preview (same size, may lose slight detail)**: converts the embedded image to JPEG, keeping the size and reducing the file.
- **Copy JSON file**: writes the JSON to a temporary file and copies that file (not the text), for agents that can be sent files directly; the temporary folder is configured in **Settings → Default behavior → Feedback temporary folder**.
- **Copy JSON content**: copies the JSON text, for agents that can only paste text.
- **Save JSON and image**: saves the JSON and the accompanying image to a chosen folder.

Copying and exporting JSON use the same minimal structure; see [Feedback JSON](#feedback-json) for the fields.

## Feedback JSON

Image feedback uses an **object-oriented** structure: each object records a source region `source`, the position changes it undergoes `movements`, and the comments attached to that region `annotations`, so an AI can directly pair the comments and moves of the same object. Video feedback reuses this structure inside `frames[].feedback` and adds frame timestamps, source video information and a timestamped object index; see [Video annotation](#video-annotation) for details.

| Field | Meaning |
| --- | --- |
| `image` | Optional data URL of the image before adjustment (PNG, or JPEG when compressed); the size is unchanged and compression loses a little detail |
| `annotationSpace` | Fixed to `"result"`, meaning annotation coordinates refer to the current adjusted image |
| `objects[].source` | The object's region in the original image; `null` for a global note |
| `objects[].movements` | Records this region's moves or resizes in order, with `to` as the final coordinates |
| `objects[].annotations` | Text notes attached to this object |

Components that do not change in image feedback are not exported; no full slice list, component group references, hashes or timestamps are output. The Base64 source image increases the file size, which is the cost of putting the image in a single JSON and is independent of the number of components. The image format is defined by `schema/feedback-minimal.schema.json`; video feedback and projects are defined by `schema/video-feedback-v1.schema.json` and `schema/video-project-v1.schema.json` respectively.

Coordinates take the top-left of the canvas as the origin and are in image pixels, independent of the window position and view zoom. A box uses the two endpoints `x1/y1/x2/y2`; the bottom-right boundary excludes itself, and the width and height are `x2-x1` and `y2-y1`. Annotation coordinates are integers, while layout change coordinates may contain decimals.

The example below omits the optional `image` field and shows a point comment, a region comment, a moved object and a global comment:

```json
{
  "annotationSpace": "result",
  "objects": [
    {"source": {"x1": 240, "y1": 180, "x2": 240, "y2": 180},
     "movements": [], "annotations": ["Change this to blue"]},
    {"source": {"x1": 200, "y1": 140, "x2": 400, "y2": 240},
     "movements": [], "annotations": ["Add rounded corners"]},
    {"source": {"x1": 100, "y1": 100, "x2": 300, "y2": 200},
     "movements": [{"to": {"x1": 200, "y1": 140, "x2": 400, "y2": 240}}],
     "annotations": ["Move the whole block down and to the right a little"]},
    {"source": null, "movements": [], "annotations": ["Overall lighter and quieter, less decoration"]}
  ]
}
```

To reconstruct the result, first extract each object's `source` region from the original image and leave the original position empty, then draw them to `to` in the order of `movements`; finally interpret or draw the annotations on the result. Regions that were not moved stay as they are.

The parallel `annotations` and `changes` arrays used by earlier versions (the legacy format) can still be imported, and are merged into the object structure by source region on import:

| Format | Original image | Annotation coordinates | Layout information |
| --- | --- | --- | --- |
| v1/v1.1/v2 projects from 0.5 and earlier | `capture.pngBase64` can be embedded, or references an external PNG | Original image | v2 saves every slice, group and the original/target coordinates |
| 0.6 minimal feedback | External PNG with the same name | Original image | Only the changed `from/to` |
| 0.7 / 0.8 feedback | `image` can be embedded | Adjusted image | Only the changed `from/to` in `changes` |
| 0.9 onwards (current) | `image` can be embedded, included by default in the interface | Adjusted image | `source` and `movements` recorded per object in `objects` |
| 3.0.0 `.edithere` project | Always embeds the original image | Adjusted image | Full slice cache and current editing state |

The app no longer opens legacy projects or 0.6 minimal feedback, and old annotations are no longer converted to the position in the adjusted result. When you clear the option to include the source image, reopening needs an original PNG of the same name alongside it. An `.edithere` project saves the full slices and editing state (`schemaVersion: "3.0.0"`) and restores directly when reopened. Closing the capture releases the in-memory cache. When you reopen feedback JSON, the layout is rebuilt from the original image and the changes it contains.

## Settings

Right-click the EditHere tray icon at the bottom-right of the taskbar and choose “Settings”. The window has five tabs: “Shortcuts”, “Appearance”, “Default behavior”, “Toolbar” and “About and updates”. Click “Save” to apply the settings; shortcuts and appearance take effect immediately, the default tool and zoom apply to the next image, and the startup behavior applies on the next launch. “Cancel” does not change the current configuration.

### Shortcuts

There are 22 configurable shortcuts: capture and new annotation (empty window) are global shortcuts and also work in other applications; the rest are used in the EditHere editor window. Click a field and press a new combination, or clear it to disable that shortcut. “Restore defaults” resets the current settings draft and still needs saving to take effect. Duplicate assignments, system conflicts or a failed save show the reason and keep the existing configuration.

The following are the Windows defaults; common Mac editing combinations use Command, redo is Command + Shift + Z, and real-machine verification is still pending.

| Scope | Action | Default shortcut |
| --- | --- | --- |
| Global | Capture | Alt + Shift + 2 |
| Global | New annotation (empty window) | Alt + Shift + 1 |
| In-app | Open image or project | Ctrl + O |
| In-app | Paste image | Ctrl + V |
| In-app | Save project | Ctrl + S |
| In-app | View JSON | Ctrl + E |
| In-app | Copy annotated image | Ctrl + C |
| In-app | Copy JSON / Save image / Hide or show annotation marks / Add global annotation | Not bound by default |
| In-app | Undo / Redo | Ctrl + Z / Ctrl + Y |
| In-app | Fit to window | Ctrl + 0 |
| In-app | Delete selected annotation | Delete |
| In-app | Cancel / Close capture | Esc |
| In-app | Smart select / Point / Box / Adjust | B / P / R / V |
| In-app | Toggle Explode / Adjust components | E / M |

The keys on the capture toolbar (Enter, C, T, P, L, `Ctrl + C`, `Ctrl + S`, `Ctrl + T`, `<`, `>`, R, H, `Esc`) are specific to the capture window and are not configured here.

### Appearance

- **Appearance mode**: Follow system, Light or Dark, defaulting to Follow system. After switching, the editor, annotation column, component sidebar, menus and Settings window update together; the original pixels of the source image and exported images are unaffected by the theme.
- **Interface language**: Follow system, Simplified Chinese or English. Switching previews immediately, takes effect on save, and returns to the previous language on cancel. The system file dialogs, message boxes and colour picker follow it too. The JSON field values exported for AI do not change with the interface language.
- **Recognition language**: Follow system, Simplified Chinese or English, used for text recognition on the capture toolbar and also switchable in place from the toolbar menu.

### Default behavior

| Option | Description |
| --- | --- |
| Screenshot right after launch | When off, the app only stays in the tray on launch; click the tray icon or press the global shortcut to start capturing |
| Start EditHere at login | After you sign in it only stays in the tray and does not capture automatically; keep the folder the app is in, and after moving it save once to refresh the path |
| Fit to window when opening an image | When off, images open at 100%; you can still zoom or fit to window at any time |
| Include the source image in exported JSON by default | Sets the initial state of “Includes the source image” in “View JSON”; saving a project always includes the source image |
| Ask about saving when the annotation window closes | When off, closing no longer prompts and discards unsaved annotations directly; ticking “Do not ask again” in the annotation window also turns this off |
| Feedback temporary folder | Where temporary feedback JSON files are stored; leave empty to use `feedback` under the default cache directory |

Windows uses the current user's startup entry and needs no administrator privileges. If you enable it for the portable version, keep the folder the app is in; if Windows has disabled the startup entry, the app tells you to restore it manually in the system's Startup Apps settings and does not force the disabled state to change. Mac uses the system login items and warns you clearly if the system asks for approval; Mac still has not been acceptance-tested on a real machine.

### Toolbar

- **Default annotation tool**: Smart select, Point, Box or Adjust; sets the tool selected by default after opening an image.
- **Shown in the bottom toolbar**: Save project, Save image, View JSON, Copy JSON content, Copy JSON file, Copy annotated image, Retake screenshot and Fit image. Unchecking removes a button from the bottom bar; the feature is still available through a shortcut or the menu.

### Diagnostics

After a problem, open **Settings → Diagnostics**, click **Export diagnostics…**, and report the exported text file together with the steps to reproduce and when the problem occurred. **Open log folder** shows the automatically saved JSONL logs directly. The logs record the version, the system and Qt environment, capture sizes, operation states and errors, but not screenshots, videos or annotation text, and they are never uploaded automatically.

By default each log file is at most 1 MiB and the last 10 files are kept; the log of a running process is protected and the limit resumes after it exits. A single exported report is at most 4 MiB and includes recent logs and the runtime environment. Logs use the current user's data directory: on Windows `%LOCALAPPDATA%/EditHere/EditHere/logs`, and on macOS and Linux each platform's Qt `AppLocalDataLocation`. On launch the app records a previous session that did not end cleanly; this may come from a crash, a forced termination or a write failure, and you cannot determine the cause from that record alone. Logs filter out your home directory, URL query parameters and common secret fields, but you can still review them before sharing.

### About and updates

Shows the current version and licence information. You can tick **Check for updates on launch**, or click **Check for updates** to check manually; once a new version is found the button becomes **Update now** — see [In-app updates](#in-app-updates). The **Open the guide** button closes Settings and plays the guide from the start, and unsaved settings are not saved; **Restore defaults** resets the current settings draft and still needs saving to take effect.

Settings are saved in the current user's Qt `AppConfigLocation/settings.ini` and are kept after the app closes. On Windows the default location is `%LOCALAPPDATA%/EditHere/EditHere/settings.ini`; on Mac it is decided by the system configuration directory.

## Guide and launch at login

- The first manual launch shows a six-step guide in the real editor window, and you can skip it at any time; it uses a sample when there is no capture and keeps your content when there is one. Both the **?** in the top-right and **Open the guide** at the bottom of Settings start it from the beginning. The guide pages forwards and backwards, and Esc only leaves the guide.
- The guide state is saved separately from your settings preferences, so restoring the default settings does not make the guide reappear repeatedly. Replaying the guide from Settings closes the Settings window and does not save changes you have not applied.
- After you sign in, the app only stays in the tray and does not capture automatically; “Screenshot right after launch” applies only to an ordinary manual launch.

## Working with AI

You can copy the JSON manually, or let an AI connected to the CLI start an annotation session. In the latter, the AI opens the image, you add annotations or adjust components, and finally click **“Finish and return to AI” (完成并返回 AI)** at the top; only then does the CLI export this round's feedback and return success. Clicking “Cancel” or waiting past the timeout does not submit feedback, and the editor keeps the current document. A broken connection can leave the result unclear, so check whether this round has already completed rather than starting the session again.

Each round of feedback uses a new output file, and the AI reads only the result explicitly submitted in this round and must not treat an earlier file as new feedback. See [AI and the command line](AGENT-CLI.en.md) for the companion skill's installation, commands and examples.

## Recognition and platform boundaries

Image detection runs entirely on the machine, using connected colour regions, edge aggregation and horizontal/vertical line grid analysis, with no network, account or model download. It can detect cards, images and fairly clear outlines, but it is not OCR or semantic object recognition and cannot guarantee that every candidate matches what you think of as a component; you can draw a box manually to add one.

Windows uses UI Automation and Mac uses Accessibility to read the element boundaries the target application exposes. Detection runs in a separate short-lived process and stops on timeout. The detection information does not include the DOM, component names or source locations in a code repository; DOM-based annotation of web pages remains a separate direction.

Each capture takes a region within one monitor and does not stitch across screens. Mixed scaling, multi-monitor layouts, expanded tray panels, protected content and different browsers still need more device verification.

| Platform | Status |
| --- | --- |
| Windows x64 | Developed and tested on Windows 11; the minimum build target is Windows 10 1809+, and older systems have not been tested individually |
| Windows capture and UI Automation | Verified against pixels, physical sizes and button boundaries using this app's real test windows |
| Windows tray panel and multi-monitor | Still needs more acceptance testing in real environments |
| Mac | The GitHub macOS build and automated tests pass, producing a temporarily signed DMG; **not yet acceptance-tested on a real machine** |
| Mac ARM / Intel | A universal arm64 + x86_64 app is compiled and CI runs tests on Apple silicon; Intel has not been acceptance-tested |

Mac targets macOS 14+ and uses ScreenCaptureKit. Your first capture needs Screen Recording authorisation; system element detection needs Accessibility authorisation, with an entry in the tray menu. Importing ordinary images needs none of these permissions. There is currently no Mac test device available. A public Mac release also needs the product owner's Apple signing and notarisation.
