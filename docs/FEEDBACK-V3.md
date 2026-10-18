# Feedback v3: J08 / J09

## Migration decision

New image exports use `feedback-minimal-3` and `urn:edithere:feedback-minimal:3`.
Readers continue to accept v2, unversioned object feedback and legacy arrays.
New fields never appear in v2 exports. Older applications may reject v3; there
is no promise of forward compatibility and no new CLI downgrade switch.
`annotationSpace: "result"` remains required. J10 and J11 are separate work.
The video container remains `video-feedback-1`; its frames and repeated index
carry the same v3 objects. Consumers execute frame objects once, not the index.

## J08

- Region: `kind: "region"`, positive-size `source`, `movements`, `annotations`.
- Point: `kind: "point"`, `point: {x, y}`, no `source`, no movements.
- Global: `kind: "global"`, no `source` or `point`, no movements or OCR.
- IDs and annotation numbers keep their existing image/frame scopes.
- Legacy zero-size rectangles and null sources convert without changing numbers
  or coordinates. Project document formats and their internal note kinds stay
  unchanged.

## J09

A moved region has required `result: {rect, dx, dy, scaleX, scaleY}`.
`rect` is the last absolute `movements[].to`, in result image pixels.
`dx = rect.x1 - source.x1`, `dy = rect.y1 - source.y1`, and the scale factors
divide final width/height by the original source width/height.

`movements` is an ordered sequence of absolute targets, **not a complete mouse
event history**. Repeated moves may update an existing target. For several
targets of one source, the last target determines the final result, matching
the existing importer; these are sequential transforms, not multiple copies.
Parent/child results use each object's already-composed absolute destination;
do not apply parent transforms a second time. Undo/redo derives results from
the current layout on export. Regions without movements (including a return
home), points and global notes omit `result`.

Readers reject inconsistent derived results, unknown fields, mixed point/source
shapes, zero-size regions and point/global movements. Numeric comparison uses
absolute tolerance `1e-7` and relative tolerance `1e-9` for larger values.
JSON Schema checks shapes; readers check the arithmetic.

## Validation

Desktop, Web, CLI receipts, MCP summaries, video frame/index matching and the
bundled Schemas must agree. Preserve old fixtures to exercise backward reads;
new tests cover types, repeated movement/scaling, nested regions, undo/redo,
empty movements, legacy conversion and tampered result/index rejection.
