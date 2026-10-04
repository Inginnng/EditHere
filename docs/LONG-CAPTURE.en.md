# Scrolling capture implementation reference

[简体中文](LONG-CAPTURE.md) · **English**

Scrolling capture keeps the existing Qt architecture: Windows, macOS and Linux share the
menu, preview, cropping and stitching logic, while the platform layer supplies live pixels
and scroll input. Vertical capture and manual horizontal capture are supported.

## Platforms and permissions

| Platform | Live pixels | Automatic vertical scrolling | Requirements |
| --- | --- | --- | --- |
| Windows | Native desktop pixels | Wheel messages sent to the original window | Works with ordinary desktop windows |
| macOS 14 and later | ScreenCaptureKit | Targeted CoreGraphics wheel events | Capture needs Screen Recording permission; automatic scrolling also needs Accessibility and event-posting permission |
| Linux X11 | XGetImage | XTest wheel events | Automatic scrolling needs the XTEST extension; manual scrolling still works without it |
| Linux Wayland | ScreenCast Portal + PipeWire authorized screen stream | Not enabled yet | The build needs PipeWire 1.0.4 or later; the desktop needs the ScreenCast Portal and must report geometry that maps to the selected monitor |

macOS converts the selection with the selected display's native pixels and Retina scale,
while accessibility detection keeps using the system's logical coordinates; the capture
filter excludes EditHere's own windows. X11 locks the selected display's native coordinates
and the original window's process, and restores the pointer position after injecting input;
when the window exposes no process information it still captures manually but sends no
automatic wheel events. Wayland locks the authorized screen stream and refuses outright when
the monitor cannot be mapped uniquely instead of guessing the primary display; once
authorized the session is reused, returning to the original selection keeps the authorization,
and finishing or cancelling the whole capture closes the session. Missing input permission
never disables manual scrolling capture.

Wayland currently requires you to scroll manually. Authorizing screen sharing is not
authorizing input: automatic scrolling would also need the RemoteDesktop Portal, and this
version does not request that extra permission. Revoking screen sharing or changing the
display layout stops the capture and asks you to select again.

## Open-source implementations surveyed

- [ShareX · ScrollingCaptureManager.cs](https://github.com/ShareX/ShareX/blob/73967140f4fd64ca4b93203ae8ad5ac05ade9aaf/ShareX.ScreenCaptureLib/ScrollingCaptureManager.cs):
  capture and stitching advance round by round, waiting out the scroll interval, comparing
  consecutive screenshots to confirm the end of the page; matches avoid both side edges and
  the fixed bottom bar, and distinguish complete success, partial success and failure.
- [deepin-screen-recorder · pixmergethread.cpp](https://github.com/linuxdeepin/deepin-screen-recorder/blob/88d7a015ea4c043389fe5c7658779ca5aebf7974/src/utils/pixmergethread.cpp):
  detects the fixed top and bottom bars first, then uses OpenCV template matching to locate
  overlapping content, and reports an explicit error for matches it cannot trust.
- [wayscrollshot · template_match.rs](https://github.com/jswysnemc/wayscrollshot/blob/master/src/stitch/template_match.rs):
  derives displacement candidates from the template first, rechecks them with the mean pixel
  error over the overlapping region, and compares the runner-up candidate to reject
  ambiguity; reviewed on 2026-10-03.

These are references for the design and the algorithms. EditHere uses its own Qt/C++ implementation,
copies no code from these projects, and adds no OpenCV runtime dependency. The early ShareX and
deepin references are pinned to the commits above; this round also re-checked ShareX's current
implementation and wayscrollshot.

ShareX's current exact row-by-row equality and historical-displacement guessing cannot resolve
localisation when a page animates slightly or jumps back to earlier content, so its guessing
fallback was not adopted. What was adopted is the flow of producing template candidates and
then rechecking them densely: fast sampling produces candidates and raw pixels disambiguate
them, tolerating small local changes. When no adjacent viewport can be matched, the texture
index of the already-saved source rows is used to look for the position again, and the actual
pixels are then read across slices to verify a unique overlap. Slices are only how the complete
source image is stored; the thumbnail on the right takes no part in matching. A capture with no
verifiable overlap, nothing but blank space, or indistinguishable repeating patterns is still
rejected, and no missing content is filled in from a guessed displacement.

## Trade-offs in EditHere

The interaction follows [PixPin's scrolling capture documentation and animations](https://pixpin.cn/docs/capture/long-capture):
a selection fixed at a screen position, a live thumbnail preview of the whole image, a green
current viewport, start/stop, moving the selection along the axis, and cropping at either end.
EditHere starts from the current position and can extend upwards or downwards; revisiting
content that was already captured only updates the current position instead of appending it
twice. Horizontal mode scrolls left and right, or drags the horizontal scrollbar. Manual
scrolling is the default, and the capture keeps waiting when the scrolling stops, leaving the
finishing touch to you. Automatic downward scrolling and reverse automatic cropping are both
off by default.

Once the frozen selection window is hidden, a translucent grey mask covers everything outside
the selection, and a hollow border marks the region actually captured. The mouse passes through
inside the frame to the original application. A compact toolbar sits below the selection, with
the scroll direction, automatic scrolling, cropping, start/stop, edit, pin, save, close and copy
on one row. The menu follows the application's light, dark or system theme, and edit, pin, save,
crop, copy and close reuse the same icons as ordinary capture and the editor. Tooltips are shown
even when the tool window does not have focus.

The separate preview on the right appears only while capture is running, and a green box marks
the viewport currently matched. The vertical preview is a fixed 196 logical pixels wide, and new
content extends it upwards and downwards: the whole image is never scaled down as the capture
grows. A horizontal preview has a fixed height and extends sideways. Once a screen edge is
reached it shows and follows only the source region near the current viewport, at an unchanged
scale. The preview allocates an image the size of the screen, so even a 2 million pixel capture
never creates a Qt bitmap of the same height. The move handle on the right of the dimensions
moves only along the capture direction while running; sampling pauses during the drag, and
releasing it keeps appending along the captured image while preserving the existing length,
crop state and raw pixels. After stopping, the selection can be moved freely and resized by
dragging its edges. Stopping discards the current round; starting again builds a new round from
the current frame. Actually changing the size or the direction also clears the current stitching
round, while merely moving the selection does not.

The up/down buttons in the scissors menu — left/right in horizontal mode — cut away the raw
pixels beyond the corresponding boundary of the current viewport, and capture can continue.
Editing, copying, saving and pinning all use the cropped pixels, and the preview marker never
enters the result. Saving goes through the ordinary save dialog to choose the path and format.
The optional reverse automatic crop trims away the part that scrolled back, along the initial
growth direction, and re-decides the direction once the capture is back to a single viewport.
The default direction and automatic cropping can also be configured in the settings. Horizontal
automatic scrolling is not enabled because the existing platform interfaces send vertical wheel
events.

Manual mode samples about every 60 ms. A sample taken mid-scroll with a high-confidence overlap
updates immediately, with the preview holding a fixed scale and marking the current viewport;
a lower-confidence approximate match still waits for the image to settle before it is judged.
Scrolling too fast to find an overlap does not end the capture: it asks you to scroll back a
little, and once you return to already-captured content the stitching recovers by relocating.
Sampling no longer repeatedly hides the progress window while it is excluded from screen capture.

The capture selection uses the screen's native pixels, so logical coordinates cannot misalign
the result on a display with high scaling. After the page scrolls, consecutive samples confirm
that it has settled before the stitcher is handed the frame. Manual mode sends no wheel events
and does not take focus back from the page, and keeps waiting while nothing changes; automatic
mode sends wheel events to the same window that was identified at the start, confirms once more
when nothing changed, and treats "cannot match" and "already at the bottom" separately. Switching
mode discards the previous round's pending callback, so cancelling automatic mode does not leave
scrolling running.

When there is no room around the capture region for the progress window, the controls are hidden
briefly before each sample and each wheel event. Chromium
[re-routes wheel input through `WindowFromPoint`](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/win/mouse_wheel_util.cc),
so sending messages straight to the browser can still be intercepted by a window covering it;
this behaviour was verified against a real browser page.

Stitching adds new content only at either end where there is a trustworthy overlap, and covered
regions only update the viewport; a fixed top bar is kept once and a fixed bottom bar goes at the
end of the result. Ambiguity caused by low-texture or repeating patterns has to be rejected, and
a historical displacement must never be guessed. In normal mode the capture pauses at a 32767
pixel edge, 32 million pixels or the 600-frame ceiling and keeps what has been stitched. With
"Ultra-long capture" enabled in the settings, raw pixels are stored as temporary disk slices, the
600-frame ceiling no longer applies, and a single axis can reach 2 million pixels. Beyond the
ordinary image limits, saving uses tiled PNG export, so the whole image never has to exist in
memory. Above the editor's 32767 pixel edge or 32 million pixel limits, editing, copying and
pinning are unavailable while saving a PNG still works. How usable a very large image is in
practice also depends on system memory and the image software. When nothing new was captured,
the first frame is not delivered as a scrolling capture.

## Where verification starts

`scrollstitch_tests` checks the stitching results and the rejection conditions;
`scroll_controller_tests` checks starting from a mouse button, retrying at the original position
after a failure, stopping, cancelling and delivering the result; on Windows
`scroll_platform_tests` uses a real window in another process to check target selection, wheel
delivery and desktop pixel capture. The native controller cases begin with the full-screen
screenshot taken by `Controller::capture()` and click the toolbar after drawing a selection,
rather than testing only the shortcut. On Linux `linux_platform_tests` verifies live pixels,
wheel events in both directions, target identity and the input-extension fallback against an X11
window owned by another process inside Xvfb; `scroll_wayland_tests` verifies the authorized
session and native pixel mapping using a test Portal and an injectable video source.

The current development environment can build and test Windows and Ubuntu/WSL; there is no macOS
host and no real Wayland desktop available for authorization. Native compilation on macOS, Retina
testing and acceptance on a real Wayland desktop stream still have to happen on those systems — a
test Portal passing does not mean a real desktop has been accepted. The existing `capture_tests`
and `ui_tests` for ordinary screenshots remain the related regression checks. Tests on a real
desktop cannot be replaced with `-platform offscreen`.

For the browser acceptance run, open `tests/fixtures/scroll-browser.html` and run
`scroll_controller_tests realBrowserProducesCompleteImage` with `H2D_SCROLL_BROWSER_TEST=1` set;
that case uses the real Windows wheel and real screen pixels and checks the 60 rows of body text,
the fixed bars and the final dimensions. The default CTest run skips cases that need a browser
page opened by hand.
