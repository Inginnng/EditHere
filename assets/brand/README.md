# EditHere wordmark

Original vector lettering designed to complement the existing blue editing icon. All glyphs are paths; no font file, embedding permission or runtime font dependency is needed.

| Asset | Background | ViewBox | Aspect ratio |
| --- | --- | --- | --- |
| `edithere-wordmark.svg` | Light | `0 0 384 100` | 3.84 |
| `edithere-wordmark-light.svg` | Dark | `0 0 384 100` | 3.84 |
| `edithere-lockup.svg` | Light | `0 0 484 100` | 4.84 |
| `edithere-lockup-light.svg` | Dark | `0 0 484 100` | 4.84 |

The SVG canvas is transparent. SVG width/height are four times the viewBox dimensions so Canvas renderers load a crisp source for large video titles. Scale proportionally. For video corner branding, use a 28–36 px rendered height; for a title, use a 90–150 px rendered height. A 24 px sample was visually checked for legibility.

`Edit` uses deep navy `#162944` on light backgrounds and white `#F6F9FF` on dark backgrounds. `Here` has one continuous subtle blue gradient (`#4A83EE` → `#3155D9`, or `#8BC4FF` → `#66A0FF`). The cut corners on E, the i dot and t echo the editing nib; rounded bowls and optical spacing preserve a quiet, readable silhouette.

Keep the spelling `EditHere` intact. Do not stretch, apply outlines or add glows. The lockup reuses `assets/icons/helpdesign.svg` without changing its source geometry. At very small sizes use the standalone application icon.

To regenerate assets and previews using the existing local video dependency installation:

```powershell
node assets/brand/render-preview.mjs <preview-output-directory>
```

The renderer uses the repository's existing `@napi-rs/canvas` installation and Segoe UI for preview captions only. Distributed SVGs have no font dependency. Existing project licensing applies.
