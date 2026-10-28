# AI provider brand artwork

These five SVG files are an unmodified, renamed subset of [Lobe Icons](https://github.com/lobehub/lobe-icons), copyright (c) 2023 LobeHub, under its MIT License. Only static SVG artwork is vendored; no React, Node or LobeHub runtime is required.

Pinned upstream commit: [`8b7bbc433d38437fa2b0cca58ae896917162b812`](https://github.com/lobehub/lobe-icons/tree/8b7bbc433d38437fa2b0cca58ae896917162b812).

All source paths below are relative to that upstream commit. The SHA-256 hashes describe the exact bytes stored here.

| Local file | Upstream source | SHA-256 |
| --- | --- | --- |
| `openai.svg` | `packages/static-svg/icons/openai.svg` | `a595df6b423920c67a7f8f73c063e4bfb72d415948097b6cac063a2366bb5186` |
| `claude.svg` | `packages/static-svg/icons/claude-color.svg` | `a3101f3047a119aa11825ad9369510f0c472428c8c52d420e31bc62db44a8364` |
| `deepseek.svg` | `packages/static-svg/icons/deepseek-color.svg` | `deba5f98a5c1796e20fcac3149bcd7eb8a32f0bdd04d048819400b1f28bd1439` |
| `glm.svg` | `packages/static-svg/icons/zhipu-color.svg` | `8174e65ff71647d5c705c920c953a82bd723d9e3e42887bdd4ffc9dfe51207a0` |
| `kimi.svg` | `packages/static-svg/icons/kimi-color.svg` | `0bdb95375d7c6e90a1850e91cd36770da2bc98fa33ec2d783f4a8fed027da85f` |

The matching, complete upstream [MIT License](../../../packaging/licenses/lobehub-icons/LICENSE) is distributed as `licenses/lobehub-icons/LICENSE`. Its SHA-256 is `add9d7531d1b21646317a8958e38fc727506fa39d24bdecb44154d943c82753a`.

`app/aiprovidericons.cpp` renders these embedded SVG paths at the requested device scale. Upstream `currentColor` fills follow the light/dark interface foreground; explicit brand colours are preserved. No path geometry is changed. Provider marks identify the selected service and do not imply endorsement; their respective owners retain their trademark rights.
