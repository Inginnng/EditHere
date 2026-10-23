# Video annotation

[简体中文](VIDEO-ANNOTATION.md) · **English**

**0.10.0 adds annotation at video timestamps.** Video annotation keeps the change requests for several points in a video together in one project. The source video is referenced by its address, and a frame screenshot is saved only when you start actually annotating; the exported JSON reuses the image annotation structure inside each frame, so AI can locate a change by time, frame and coordinates at once.

## Workflow

1. Open a video file, or open a saved `.edithere` video project.
2. Click “Play” to watch the video; click “Pause” on the frame you want, or drag the timeline and use `−1s` / `+1s` to seek, then stop on the frame.
3. Pausing still shows the video frame. Once you have confirmed the frame and the time, add the first actual point, rectangle or global annotation: only then does the app save a screenshot of this frame and switch to the annotation canvas, and later annotations use the existing tools.
4. Keep playing or seek to another timestamp; the matching screenshot is saved when you add the next set of annotations. Several annotations on the same frame share one screenshot.
5. Use the “Annotated frames: X” dropdown, or click the cyan downward-pointing marker on the timeline, to return to a timestamp that already has annotations. An entry shows the time and the annotation count, for example `00:12.540 · 2 annotations`.
6. Save the project to keep editing it, or export the JSON. When working with AI, click **“Finish and return to AI” (完成并返回 AI)** to submit the feedback for these timestamps in one go.

Seeking on the timeline is not an exact previous-frame/next-frame command. The actual annotation time comes from the timestamp of the captured frame, so the time on the playback bar and the screenshot cannot disagree.

The video frame is drawn by an ordinary widget, and frames are converted on a background thread; this no longer uses `QVideoWidget`'s native window, which would turn the annotation canvas into a native window too and stop the drawn rectangles from showing.

Play and Pause use the same button background; the play button, the time display and the annotated-frame list keep a stable width, and video playback and the annotation canvas share one display area. Opening the first frame, an ordinary pause, seeking back or forward, or dragging the timeline only stops the video on the target frame without taking a screenshot immediately. Clicking `−1s` / `+1s` while paused keeps it paused; clicking while playing jumps to the target frame and then pauses. Clicking “Play” again or pressing Space resumes playback. Clicking `−1s` / `+1s` repeatedly accumulates steps towards the pending target.

Choosing an annotation mode, zooming or panning never saves a new screenshot. The pause hint reads “Paused · The frame is saved when you start annotating”; only the first actual point, rectangle or global annotation saves this frame. The dropdown of existing annotated frames restores the saved screenshot and annotations directly. Saving a project or exporting JSON works on the existing annotated frames and never adds a frame you merely paused on to the project automatically.

Switching frames at the same resolution keeps the current zoom, pan, annotation tool and sidebar state, and does not force the window to activate or move the annotation input focus. When frames change quickly, block analysis waits for seeking to settle briefly before it runs, which reduces repeated work.

## Files and the source video

A project records the source video's address and does not put the whole video into the JSON. Screenshots are created when actual annotation begins, so opening, playing, pausing and seeking on their own accumulate no screenshots, and the project size grows mainly with the number of annotated frames. When a screenshot is embedded as Base64, the image data grows by about one third.

If the video is moved or deleted, the saved screenshots and annotations can still be viewed. Click “Locate video again” to choose the matching video and restore playback and seeking; choose a video with the same content, or the old timestamps may point at different frames.

On export, `frames[n].feedback.image` holds the embedded screenshot; `frames[n].imageFile`, when present, records the name of a screenshot file in the same folder as the JSON. When you export JSON without embedded images, keep those screenshot files alongside it when sharing with AI, or export a version with embedded images again.

## JSON structure

```json
{
  "schemaVersion": "video-feedback-1",
  "video": {
    "source": "E:/videos/demo.mp4",
    "durationMs": 30000,
    "width": 1920,
    "height": 1080
  },
  "frames": [
    {
      "id": "frame-12540123",
      "timestampMs": 12540,
      "timestampUs": 12540123,
      "imageFile": "frame-12540123.png",
      "feedback": {
        "annotationSpace": "result",
        "image": "data:image/png;base64,...",
        "objects": [
          {
            "source": {"x1": 120, "y1": 80, "x2": 360, "y2": 160},
            "movements": [],
            "annotations": ["The heading here appears too early; move it to the next second"]
          }
        ]
      }
    }
  ],
  "objects": [
    {
      "source": {"x1": 120, "y1": 80, "x2": 360, "y2": 160},
      "movements": [],
      "annotations": ["The heading here appears too early; move it to the next second"],
      "timestampMs": 12540,
      "frameId": "frame-12540123"
    }
  ]
}
```

The `...` in the example above is only a documentation placeholder; a real export contains the full Base64 image data.

`timestampMs: 12540` means `00:12.540` after the start of the video. `timestampUs` keeps the microsecond precision of the actual captured frame time, and `timestampMs` is that value divided by 1000, truncated to an integer; these are media-relative times, not a date or system time. AI should locate a change with `frameId + timestampMs + coordinates`, because the same coordinates can carry completely different feedback at different times.

`source` and `movements.to` both use the pixel coordinates of the matching frame screenshot, with the origin at the top-left corner. A rectangle's bottom-right boundary is exclusive, so the region in the example above is `240` wide and `80` high; `source: null` means a global note for that timestamp. The coordinates cannot be treated directly as screen coordinates or web CSS pixels.

A point annotation also uses `source`, with `x1 == x2` and `y1 == y2`, meaning a pixel position rather than an empty rectangle to change. The connector describes it explicitly as a “source image point”.

Each frame's `feedback` is one existing image-feedback payload; the root `objects` is a flattened index of the same objects, attaching a timestamp directly to each graphic object, so tools that walk objects only can read it. AI should not treat the two duplicate records as two change requests.

## How AI reads it

The MCP connector's video summary gives the time, coordinates, text and screenshot entry point for each frame. With `includeImage: true`, every image is preceded by its frame ID and time, and up to 8 screenshots are attached directly; beyond the image count or size limit, the indices of frames that are not yet attached are listed explicitly. The full screenshots and annotations still live in the JSON or in screenshot files in the same folder, so AI should keep reading from `frames[n]`.

A summary of many annotations also reports the parts it did not expand explicitly. The summary is only an entry point for reading; locating and making changes should be based on the timestamps, screenshots and annotations in the full feedback.

## Verification record

`0.10.0-pause-update`, after a full rebuild on Windows 11 / Qt 6.8.0 MSVC, passed **24/24 regression groups** (77.91 seconds). The real video interface passed both in a Windows native window and in windowless mode, covering ordinary play/pause and seeking with zero screenshots, a first point/rectangle/global annotation taking exactly one screenshot, annotating across frames, saving and reopening a project, the real export dialog, copying the compressed JSON and re-importing the export folder, and viewing screenshots and switching language live when the source video is missing. The native test group scored **10 PASS, 0 FAIL, 0 SKIP**, with the first click verified through a QWindow hit test; the screenshot evidence comes only from Qt widget rendering, and GPU desktop-composited capture was not verified. Logs are in `artifacts/video-pause-update-full-ctest.log` and `artifacts/video-pause-update-native-test.log`; the existing JSON and the independent AI acceptance below illustrate the format and the locating ability, and the old report was not counted as a fresh independent AI acceptance run this round.

The online CLI acceptance used an isolated test entry point in the same production CLI implementation, really completing MP4 opening, three annotations at two timestamps, saving the project, returning two embedded frames to AI, reopening the project and a `--no-image` return; **0 failures, 0 skips**. The isolation affects only the test endpoint, does not change the behaviour of the released app, and did not terminate an older version already running on the computer. When trying a preview package, quit the older EditHere from the tray first, then run the new package; otherwise the single-instance mechanism may hand the request to the older version still running.

The runtime acceptance above is for Windows; macOS/Linux build dependencies have been updated, but this round did not run verification on those systems.

Connector test command: `node --test tests/connector_test.mjs`, currently **17/17 passing**. It covers different feedback at the same coordinates in two frames, point-annotation semantics, pairing screenshots with frame times, embedded and external screenshots, the index hint for excess screenshots, the hint for an oversized summary, invalid references and timestamp mismatches, and source-image format compatibility.

Feedback actually exported from `E:/Project/EditHere宣传/动画/out/EditHere-zh.mp4` passed the connector read acceptance: two `1920×1080` screenshots correspond to `timestampMs=64983` (`01:04.983`) and `104983` (`01:44.983`), keeping all three change requests intact: the “只有像素。” subtitle at the bottom, the red milk-carton point, and the “第 3 轮” label box on the left. The frame ID, timestamp and screenshot index in front of each MCP image match, the SHA-256 of the returned image matches the corresponding embedded data and the PNG in the same folder exactly, and the summary does not output the Base64 or the notes from the root flattened index twice.

For records you can check, see [the real export feedback](../artifacts/video-acceptance/video-acceptance.json) and [the connector acceptance result](../artifacts/video-acceptance/connector-acceptance.json). The connector acceptance result stores only the summary, annotations and image metadata, and no longer copies the Base64 image data.

An independent AI, without reading the product implementation, the tests, the expected answer or any other agent's implementation context, read the real JSON and screenshots and completed **3/3 change-point localisations**. It then checked the animation project provided by the user read-only and located the editable text/CSS and the photo asset; no different JSON format was needed this round. The source animation, photo and video were not modified.

| Change point | Screenshot location | Location in the source animation project |
| --- | --- | --- |
| Enlarge “只有像素。” and move it up | `64.983333s`, rectangle `(764,800)–(1118,889)` | the `.tls .only` style at `src/scenes/tools.js:36` and the text at `src/strings.js:32`; you can take the font size from `64→76.8px` and the baseline from `top 800→770px`, keeping the entrance animation. |
| Change the red milk carton to blue packaging | `104.983333s`, point `(1187,528)` | `assets/desk-before-2k.jpg`, referenced at `src/scenes/point.js:37`; after scene scaling/translation it maps to about photo pixel `(1163,741)`, on the carton body. Editing the photo or using a derived asset of the same size is required. |
| Change “第 3 轮” to “第 3 次修改” | `104.983333s`, rectangle `(325,592)–(468,646)` | the third line's `.rl b` at `src/scenes/point.js:76`, with the text from `src/strings.js:46`; keep the `#e8edf8` colour at `point.js:22` and the “改 1 处” below it. |

Source file paths in the table above are relative to `E:/Project/EditHere宣传/动画/`. The milk-carton photo is also referenced by other scenes and thumbnails; this round's annotation locates the instance in the current frame and does not automatically expand to every shared instance.

The independent acceptance also extracted the five frames around each `timestampUs` from the source video and kept the real PTS comparison. The video is `1920×1080` at `60fps` with a `1/15360` time base:

| Export time | Best-matching source-frame PTS / s | Time difference | Whole-frame mean absolute RGB error |
| --- | --- | --- | --- |
| `64983333μs` | `998144 / 64.983333333s` | `+0.333333μs` | `1.258272` colour levels |
| `104983333μs` | `1612544 / 104.983333333s` | `+0.333333μs` | `0.999960` colour levels |

Both exact-PTS candidates have the lowest whole-frame error in their respective five-frame windows; the three annotated regions all reach their lowest error at `(0,0)` in the `±2px` shift comparison. The images differ slightly in colour values, so this comparison proves the time and position correspondence; it does not claim that a decoded source-video screenshot is pixel-for-pixel identical to the exported PNG.

Detailed evidence is in [the independent AI acceptance report](../artifacts/video-independent-reader/report.md) and [the per-object localisation and PTS comparison result](../artifacts/video-independent-reader/result.json), which keep the SHA-256 of the source/asset versions, the extracted-frame record and the comparison values. The SHA-256 of the independent report's input JSON matches the real export file.
