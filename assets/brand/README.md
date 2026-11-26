# EditHere wordmark

The selected identity is **A1: a blue pen-shaped i with an amber cap**. Keep the spelling `EditHere` intact. The independent blue application icon is unchanged.

| Asset | Background | ViewBox | Aspect ratio |
| --- | --- | --- | --- |
| `edithere-wordmark.svg` | Light | `0 0 572 128` | 4.46875 |
| `edithere-wordmark-light.svg` | Dark | `0 0 572 128` | 4.46875 |
| `edithere-lockup.svg` | Light | `0 0 722 128` | 5.640625 |
| `edithere-lockup-light.svg` | Dark | `0 0 722 128` | 5.640625 |

All assets have transparent backgrounds and vector outlines. Intrinsic width/height are four times the viewBox dimensions. Preserve the aspect ratio; do not stretch the lettering. At very small sizes, use the standalone application icon.

The body uses one blue gradient (`#246BD9` → `#3E74EF` → `#4147C9`), with a lighter version for dark backgrounds. The pen cap uses the same amber gradient (`#FFB352` → `#F38A35`) in both themes. The pen has one continuous short tip; do not reintroduce a dark nib or change the cap to red.

The letter outlines are based on **Manrope**, weight 610, with optical spacing and a redesigned i. They are not a newly designed font. The original font is available from [Google Fonts](https://github.com/google/fonts/tree/main/ofl/manrope); its SIL Open Font License is preserved in [Manrope-OFL.txt](Manrope-OFL.txt). No font installation is needed to display these SVGs. The lockup reuses `assets/icons/edithere.svg` without changing that icon's geometry.

The four checked-in SVG files are the source of truth. To create transparent PNG exports and a light/dark preview using the local video dependency installation:

```powershell
node assets/brand/render-preview.mjs <preview-output-directory>
```

The renderer reads the SVGs without rewriting them. It uses the repository's existing `@napi-rs/canvas` installation and Segoe UI only for preview captions.
