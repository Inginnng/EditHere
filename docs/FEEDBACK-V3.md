# Feedback v3: J08 / J09

## Migration decision

New image exports use `schemaVersion: "feedback-minimal-3"`; the older optional
`$schema: "urn:edithere:feedback-minimal:3"` remains readable.
Readers continue to accept v2, unversioned object feedback and legacy arrays.
New fields never appear in v2 exports. Older applications may reject v3; there
is no promise of forward compatibility and no new CLI downgrade switch.
`annotationSpace: "result"` remains required. J10 and J11 are separate work.
The video container remains `video-feedback-1`; its frames and repeated index
carry the same v3 objects. Consumers execute frame objects once, not the index.

## J08

- Region: `kind: "region"`, positive-size `source`, `annotations`; omit `movement` when unchanged.
- Point: `kind: "point"`, `point: {x, y}`, no `source`, no movements.
- Global: `kind: "global"`, no `source` or `point`, no movements or OCR.
- IDs and annotation numbers keep their existing image/frame scopes.
- Legacy zero-size rectangles and null sources convert without changing numbers
  or coordinates. Project document formats and their internal note kinds stay
  unchanged.

## J09

A moved region has a single `movement: {rect, dx, dy}` and optional `scaleX/scaleY`.
`movement.rect` is the final absolute target, stored once per object, in result image pixels.
`dx = rect.x1 - source.x1`, `dy = rect.y1 - source.y1`, and the scale factors
divide final width/height by the original source width/height, rounded to two
decimal places (minimum 0.01). Each omitted scale axis means 1.

New exports round source, target and point coordinates to integer image pixels;
displacement is the difference of those integer coordinates. A scale that rounds
to 1 restores the original dimension before export, so targets carry no hidden
resize jitter. A final target equal to its source after normalization drops the
whole trajectory, retaining attached annotations and OCR. Positive regions remain
at least one pixel and stay inside the canvas. Editable project geometry retains
its precision; older fractional feedback and exact scale ratios remain readable.
Movement sources rounding to the same integer rectangle share the last target,
retaining their annotations; standalone notes remain independent.
Screenshot text is a sibling `ocrtext` string; older `recognizedText` metadata
remains readable, but new exports omit its wrapper and engine/language fields.

New exports contain neither `result` nor the `movements` array. Older files using that pair remain readable; readers validate their arithmetic and use the last absolute target. A new `movement` cannot be mixed with those legacy fields.

Objects follow sidebar note order, using the first attached note for grouped opinions; unlinked movements follow the note list. Empty movement notes retain their order when new feedback is imported. Deleting a movement annotation restores its surviving source pieces to their original positions and sizes, keeps detached children unchanged, and removes the annotation in one undoable step.

Readers reject inconsistent derived results, unknown fields, mixed point/source
shapes, zero-size regions and point/global movements. Numeric comparison uses
absolute tolerance `1e-7` and relative tolerance `1e-9` for larger values;
scale values must match either the exact legacy ratio or the rounded ratio.
JSON Schema checks shapes; readers check the arithmetic.

## Validation

Desktop, Web, CLI receipts, MCP summaries, video frame/index matching and the
bundled Schemas must agree. Preserve old fixtures to exercise backward reads;
new tests cover types, repeated movement/scaling, nested regions, undo/redo,
empty movements, legacy conversion and tampered result/index rejection.
