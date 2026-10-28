# Feedback format: v4

## 0.12.0 compatibility

New exports use `feedback-minimal-4`. Readers retain support for v2/v3 image feedback, unversioned `objects`, older `annotations` + `changes`, string `image` values and the same-name PNG fallback. Desktop projects support 1.0.0, 1.1.0, 2.0.0 and 3.0.0; videos retain their v1 container. An unknown explicit version is rejected, not guessed to be legacy. This is new-reader/old-data compatibility: older apps are not guaranteed to understand v4 `kind`, `movement`, `children` or image descriptors.

Manual file handoff defaults to JSON and a companion original; **Embed original image in JSON** selects `embedded`. **Copy JSON content** defaults to `none`; the vertical ⋯ menu inside the button remembers a separate include-image choice. CLI/API file / embedded / none modes and optional numbered previews remain available and are not overridden by manual preferences.


## Migration decision

New image exports use `schemaVersion: "feedback-minimal-4"`; the older optional
`$schema: "urn:edithere:feedback-minimal:4"` remains readable.
Readers continue to accept v3, v2, unversioned object feedback and legacy arrays.
New fields never appear in v3 or v2 exports. Older applications may reject v4; there
is no promise of forward compatibility and no new CLI downgrade switch.
`annotationSpace: "result"` remains required.
New v4 image feedback omits `exportedAt`. Published image v2/v3 and the video v1
container retain their required timestamp; their schemas are unchanged.
Editable project formats keep their existing metadata.
Serialized feedback and the JSON preview keep `width` immediately before `height`.
An optional `image.preview` is a filename relative to the feedback JSON: saving a
bundle writes that JPEG beside the JSON, while copying JSON text alone cannot
deliver its companion images.
The video container remains `video-feedback-1`. Each frame carries the nested v4
feedback; the root index flattens that tree and omits `children` on index entries,
keeping the published v1 index shape. Consumers execute frame objects once,
not the index. IDs identify the same objects in both representations.

## V4: child annotations

A moved component stays one object. An annotation whose box sits inside that
component's moved area belongs to the component instead of becoming a second,
  unrelated object. V4 adds `children` and makes the export timestamp optional:

```json
{
  "kind": "region",
  "id": "o1",
  "source": {"x1": 100, "y1": 100, "x2": 300, "y2": 200},
  "movement": {"rect": {"x1": 200, "y1": 140, "x2": 400, "y2": 240}, "dx": 100, "dy": 40},
  "annotations": [{"number": 1, "text": "整体右移"}],
  "children": [
    {
      "kind": "region",
      "id": "o2",
      "source": {"x1": 220, "y1": 150, "x2": 280, "y2": 190},
      "annotations": [{"number": 2, "text": "按钮要更圆"}]
    }
  ]
}
```

- `children` is omitted when empty and holds boxed regions only; a moved component
  is always a region, so points and global notes can never own children.
- The array is written last: it trails the parent's own `source`, `movement`,
  `annotations` and `id` instead of splitting them apart. Key order is only a
  readability choice; parsers must not depend on it.
- Parent `source` is the component's original screenshot rectangle. A child's
  `source` is an absolute rectangle in the result canvas, never parent-relative.
- Every object, children included, keeps its own `id` and annotation `number`.
  Numbering stays in note order, so the visible badges still line up.
- Exports nest one level: a child that owns a movement of its own is promoted back
  to the top level, so a reader never needs recursive traversal. Readers must still
  tolerate deeper nesting.
- Child notes follow their component in the note list, so the sidebar, the
  annotation numbers and the exported order all agree. Notes unrelated to that
  component keep the order the user created them in.
- Importers flatten the tree before numbering, then restore the ownership, so a v4
  file reopens as the same notes in the same order.
- Exported videos keep nesting in each frame. The root index contains every
  parent and child as separate entries with `frameId` / `timestampMs`.

### Undetermined ownership

A box that sits entirely inside the moved area always belongs to that component, so
the editor links it without asking, whether or not the explosion view is open. A box
that only partly overlaps the component is ambiguous: it is stored as undetermined
(`parent.state = unknown`) in the project, drawn with a red frame and a prominent
card, and the export is refused until the user answers "yes, it belongs to the moved
content" (✅, becomes a child) or "no, it is a separate object" (❌, drops the
association).

Undetermined notes never reach feedback JSON. The `.edithere` project keeps the
association as `parent: {source, state}` where `state` is `child` or `unknown`;
feedback carries only the nesting, never a `parent` field.

Ownership, orphan removal and stable sibling ordering share one model
normalization boundary. The source identity is calculated in one helper: legacy
integer references may resolve to one quantized movement, while fractional
references must match the original bounds. Ambiguous identities lose ownership
instead of selecting an arbitrary component. This is a bounded compatibility
rule, not a persistent component identity across editing rounds.

Automatic empty parent cards and their child are one undo transaction and count
toward the 1000-note limit. Export uses an equivalent projection if a parent card
is missing, without modifying the saved document. Saving unresolved projects is
allowed; exporting any unresolved video frame is blocked.

## Object shape (v3)

- Region: `kind: "region"`, positive-size `source`, `annotations`; omit `movement` when unchanged.
- Point: `kind: "point"`, `point: {x, y}`, no `source`, no movements.
- Global: `kind: "global"`, no `source` or `point`, no movements or OCR.
- IDs and annotation numbers keep their existing image/frame scopes.
- Legacy zero-size rectangles and null sources convert without changing numbers
  or coordinates. Project document formats and their internal note kinds stay
  unchanged.

## Movements (v3)

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
new tests cover types, repeated movement/scaling, nested regions, child nesting,
refused exports while ownership is undetermined, undo/redo, empty movements,
legacy conversion and tampered result/index rejection.
`tests/fixtures/child-feedback-contract.json` is shared by the C++ and JavaScript
writers and independently validated against the v4 schema. It covers sibling
order, ordinary rectangles colliding with movement sources, orphan fallback and
independently moved children. The fixed `serializedObjects` bytes compare the
complete object tree, key order and final newline in both writers, without
depending on platform-specific PNG encoding. CTest's `web` suite includes both `.test.cjs` and
`.test.mjs` model/session tests; browser acceptance scripts run separately.
