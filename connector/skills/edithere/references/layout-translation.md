# Layout Translation & Local vs. Design-System Guide

EditHere annotations and Explode movements encode **visual intent in image pixels** — NOT CSS pixel literals and NOT instructions to slap `position: absolute` onto elements. Classify scope first, then translate geometry into idiomatic source code.

## 1. Classify Scope Before Editing: Local Tweak vs. Design-System Adjustment

Evaluate every annotation and `groupNotes` item against these two scopes before writing code:

| Dimension | Local Tweak | Design-System / Shared Adjustment |
| --- | --- | --- |
| **Signals** | Single-page copy edits, local element spacing/alignment, single-card resize, section-local reordering | `groupNotes` cross-page rules; repeated feedback across multiple pages on similar buttons/cards/inputs; changes to brand color, border radius, typography scale, or global spacing |
| **Correct Implementation** | Edit only the target page component or local container styling; keep changes tightly scoped to that instance | **Update Design Tokens, CSS variables, Theme config, or shared base components** (`Button`, `Card`, `SettingsRow`) so every consumer updates consistently |
| **Prohibited Practice** | Modifying global tokens or shared components to satisfy a one-off tweak (causes regressions on unannotated pages) | Copy-pasting per-page style overrides, hardcoding magic hex values, or scattering `!important` hacks |
| **Refresh Impact** | Usually re-capture only the annotated page | Trace imports/usages of the modified shared component or token to list every affected page and `upsert` them all |

## 2. Translating Explode (`movement`) Geometry into Idiomatic Code

`objects[].movement` provides `source` (pre-edit bounds), `movement.rect` (target bounds), `dx`/`dy`, and optional `scaleX`/`scaleY`:

1. **Locate the real component first**: Cross-reference the original image, annotated preview, nearby labels, and optional `ocrtext` to find the actual JSX/HTML/Qt/scene node in source code. Never guess component paths from raw coordinates.
2. **Map visual movement to structural layout code**:

| Visual Action in EditHere | Anti-Pattern (Forbidden) | Idiomatic Engineering Implementation |
| --- | --- | --- |
| Move button next to heading / swap cards | `position: absolute; left: ...px; top: ...px` | Reorder JSX/DOM nodes, adjust Flex `order`, move into parent header slot, or update CSS Grid areas |
| Nudge slightly left/down or adjust breathing room | Negative pixel `margin` hacks or magic offsets | Tune container `gap`, `padding`, `justify-content`, `align-items`, or layout spacing tokens |
| Scale element larger or smaller (`scaleX`/`scaleY`) | Hardcoded isolated `width`/`height` breaking responsiveness | Switch size variant (`size="lg"`), adjust typography scale / responsive width constraints / aspect ratio |
| **Parent component moved + `children` sub-notes** | Treating parent and child notes as independent unrelated moves | **Apply parent container layout/position changes first**, then implement each child annotation relative to the updated parent |
| **Geometry `movement` with empty text** | Ignoring because `annotations` has no text | A movement/scale without text is an **explicit layout requirement** — infer alignment/spacing intent from adjacent elements |

3. **Preserve deterministic details**: Keep user-specified copy, colors, font sizes, and numeric values exact. If an ambiguous layout relationship blocks implementation, implement all unambiguous items first and ask a targeted clarifying question.

## 3. Post-Edit Code-Impact Checklist (Drives Targeted Refresh)

Before refreshing screenshots:
1. List every modified source file and trace shared component / token references to identify **all visually affected pages**.
2. Verify every `annotations[].number` and `groupNotes` directive — including text-free movements — is reflected in code.
3. Hand the exact **Affected Pages List** (edited annotated pages + shared-component ripple pages) to `references/capture.md` and `references/project-review.md` for targeted re-capture and `upsert`. Skip untouched pages.
