<p align="center">
  <img src="assets/icons/edithere-256.png" width="104" alt="EditHere icon">
</p>
<h1 align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="assets/brand/edithere-wordmark-light.svg"><img src="assets/brand/edithere-wordmark.svg" width="360" alt="EditHere"></picture></h1>
<p align="center">改这里</p>
<p align="center"><strong>Show AI exactly what you want to change.</strong></p>
<p align="center">Capture a screenshot, add your comments, and rearrange the layout. Give AI your complete visual feedback in one go.</p>
<p align="center">Windows · macOS Preview &nbsp; / &nbsp; Local screen capture and image analysis</p>
<p align="center">
  <a href="#download-and-install">Download</a> ·
  <a href="#set-up-with-ai">Set up with AI</a> ·
  <a href="#features">Features</a> ·
  <a href="#demo-video">Demo video</a> ·
  <a href="docs/AGENT-CLI.md">AI integration (Chinese)</a> ·
  <a href="docs/USER-GUIDE.md">User guide (Chinese)</a> ·
  <a href="CHANGELOG.md">Changelog (Chinese)</a> ·
  <a href="#acknowledgements">Acknowledgements</a>
</p>
<p align="center"><a href="README.md">简体中文</a> · <strong>English</strong></p>

<p align="center">
  <a href="#demo-video">
    <img src="assets/readme/overview.jpg" width="960" alt="EditHere demo: annotate a screenshot, rearrange its layout, and send the feedback to AI">
  </a>
</p>

## Set up with AI

Copy the entire prompt below into an AI tool with access to your local terminal and files, such as Codex or Claude Code. GitHub provides a copy button in the top-right corner of the code block.

```text
Follow https://github.com/Inginnng/EditHere/blob/codex/native/docs/AI-SETUP.md to set up EditHere and the edithere skill for me. Reuse an existing installation or fully extracted ZIP package first; otherwise, install the appropriate version for my operating system. Verify that the CLI works and the skill is in a location recognized by my current AI tool, then explain how to start my first annotation session. Tell me when a system permission prompt requires my action.
```

<details>
<summary>Prefer to use EditHere without installing it? Copy this prompt</summary>

```text
Follow https://github.com/Inginnng/EditHere/blob/codex/native/docs/AI-SETUP.md to set up EditHere for Windows without running an installer and the edithere skill for me. Check for an existing fully extracted package first; I can provide its location if needed. Otherwise, download the official Windows ZIP and extract the entire archive into a suitable user directory. Do not run the installer or change startup settings or PATH. Configure the skill to use the absolute path to edithere-cli.exe, and verify the CLI and skill configuration for my current AI tool. Tell me when a system permission prompt requires my action.
```

</details>

Your AI tool needs permission to use your local terminal and files; a web chat alone cannot install software on your computer. You handle system permission prompts. The source repository and release packages are public, so the AI tool can read the official guide and download the app. See the [AI setup guide (Chinese)](docs/AI-SETUP.md) for the full procedure.

## Why EditHere?

“Move the button in the top-right corner a little to the left.” “Make this card bigger.” “This color is wrong.” When working on an interface with AI, you may know exactly what you want, yet spend a lot of words explaining where a change belongs and how much it should affect.

**EditHere puts that feedback directly on the image.** Mark a region and leave a comment, or drag and resize parts of the image to show the layout you have in mind. Then give AI the original image, annotations, and position and size changes so it can continue editing your project with that context.

Use it for web and app interfaces, game HUDs, charts, and other visuals where you need to show exactly what to change and where.

## Features

| Feature | What you can do |
| --- | --- |
| **Capture and annotate** | Start a capture with a global shortcut. Hover to select a region, use the scroll wheel to switch the selection scope, or draw a selection manually. Release the mouse to start editing. |
| **Connect each comment to its location** | Combine point, rectangle, and global annotations. Write comments in the sidebar and use numbered markers to find the corresponding locations on the image. |
| **Rearrange the layout directly** | Enable “Explode” (大爆炸) to drag or resize image regions, or enter exact coordinates and dimensions. You can also annotate the regions you have adjusted. |
| **Hand feedback to AI** | Copy or export JSON. With the companion skill and CLI, AI can open an image, wait for you to explicitly finish annotating, and then receive your feedback to continue editing. |
| **Save and resume** | Export or copy an annotated image, or save the complete project to continue later. Open, drag in, or paste images in common formats. |
| **Work your way** | Zoom and pan freely, undo and redo, switch between light and dark themes, and customize shortcuts and the toolbar. Image region detection runs locally. |

### Point to exactly what you mean

Use a point for a detail, a rectangle for a region, and a global annotation for overall style. Comments and the image stay together, so you do not have to keep searching between chat messages and screenshots.

<img src="assets/readme/annotations.jpg" width="960" alt="Game interface example: mark locations and record feedback in the same editor window">

### Show the layout you want

Use “Explode” (大爆炸) to select and adjust image regions: move a legend out of the way, enlarge a card, or set an exact position and size. Movement records stay alongside annotations, so the feedback conveys both the original location and the intended destination.

<img src="assets/readme/layout.jpg" width="960" alt="Chart example: move a legend while keeping its annotations and position changes">

These adjustments affect regions of the screenshot. Moving a region leaves its original position empty. EditHere does not directly edit webpage source code or automatically fill in the background.

### Give your AI the context it needs

A feedback package contains:

- **The original image:** shows the interface before editing and can optionally be embedded in the JSON.
- **Annotations:** locations, regions, and written comments.
- **Layout changes:** the actual moves and resizes, with the regions recorded before and after each adjustment.

EditHere prepares the feedback. You can send it to AI manually or use the companion skill and command line: AI opens the image, you annotate it in EditHere, and you click **“Finish and return to AI” (完成并返回 AI)**. AI then uses the feedback and your project context to make the changes. EditHere itself does not call a model or modify code.

## Quick start

The screenshots and demo show the Chinese UI. Chinese button and feature names are included below to help you follow along.

1. **Install EditHere:** let your AI agent install EditHere for you, using the prompt in [Set up with AI](#set-up-with-ai).

2. **Capture the screen:** say “Use EditHere to annotate the main page for me” in your project, or capture the screen with EditHere's global shortcut.

<p align="center"><img src="assets/readme/game-before.png" width="720" alt="Game interface: the screen before any change"></p>

3. **Annotate** in EditHere:

- **Point annotation:** annotate a single point.
- **Rectangle annotation:** detected rectangles are highlighted; click one to annotate it, or draw a region manually.
- **Global comment:** requirements that apply to the whole project.
- **Explode (大爆炸):** split every detected element, then move or resize them. EditHere records the position before and after each move so AI can see both; the original position is left empty, and your AI fills in what belongs there.

<p align="center"><img src="assets/readme/game-annotating.png" width="720" alt="Game interface: annotating in EditHere"></p>

See the [user guide (Chinese)](docs/USER-GUIDE.md) for more actions, shortcuts, and examples.

4. **Send the feedback to AI**

A single JSON contains:

- **The original image:** shows the interface before editing and can optionally be embedded in the JSON.
- **Annotations:** locations, regions, and written comments.
- **Layout changes:** the actual moves and resizes, with the regions recorded before and after each adjustment.

EditHere prepares the feedback. You can send it to AI manually or use the companion skill and command line: AI opens the image, you annotate it in EditHere, and you click **“Finish and return to AI” (完成并返回 AI)**. AI then uses the feedback and your project context to make the changes. EditHere itself does not call a model or modify code.

For example, attach your exported feedback to this prompt:

> Update the current project using this EditHere feedback. Use the original image to understand the interface, address each item in annotations, and adjust the layout using the positions and dimensions in changes. Only modify what is explicitly requested. Explain any ambiguity before proceeding.

<p align="center"><img src="assets/readme/game-after.png" width="720" alt="Game interface: the screen after the annotated changes"></p>

### Use it in an AI workflow

After installing EditHere or fully extracting the ZIP package, copy [`skills/edithere`](skills/edithere/SKILL.md) from this repository into the skills directory used by Codex or Claude Code, and tell the AI where to find the CLI. You can also use the [AI setup prompt](#set-up-with-ai) above to configure it. The skill instructions are currently in Chinese. You can then start a session with a request like this:

> Use EditHere to let me annotate this interface. Wait until I finish, then update the current project based on my feedback.

AI opens the image with `edithere-cli annotate` and waits for you to decide when you are finished. Cancellation or a timeout does not turn unsubmitted edits into change requests. The CLI can also export feedback from existing projects for your own scripts or agents.

See [AI integration and the command line (Chinese)](docs/AGENT-CLI.md) for full setup instructions, commands, and examples.

## Demo video

**The clip below walks through the whole flow: screen capture, annotations, layout adjustments, and an agent starting an annotation session and receiving feedback.** It features original instrumental music and **Mandarin narration**, with the Chinese UI shown on screen.

<p align="center"><img src="assets/readme/demo.gif" width="640" alt="EditHere demo: capture, annotate, rearrange the layout, and hand the feedback to AI"></p>

The animation above is a compressed preview; download the [full promo video (MP4)](https://github.com/Inginnng/EditHere/releases/download/v0.9.4/EditHere-0.9.4-promo.mp4) from Releases, or see [all downloads](https://github.com/Inginnng/EditHere/releases/latest).

## Download and install

| Platform | Download | How to use |
| --- | --- | --- |
| **Windows x64 · Recommended** | [Download the EXE installer](https://github.com/Inginnng/EditHere/releases/download/v0.9.4/EditHere-0.9.4-win-x64-setup.exe) | Installs for the current user without administrator privileges. Includes a Start menu entry, an uninstaller, and project file associations. |
| **Windows x64 · No installation required** | [Download ZIP — no installation required](https://github.com/Inginnng/EditHere/releases/download/v0.9.4/EditHere-0.9.4-win-x64.zip) | Extract the entire archive and run `EditHere.exe`. Keep the DLLs and plugin folders alongside it. |
| **macOS · Apple Silicon / Intel** | [Download the universal DMG](https://github.com/Inginnng/EditHere/releases/download/v0.9.4/EditHere-0.9.4-macos-universal.dmg) | Open the DMG and drag `EditHere.app` to the “Applications” shortcut inside. Grant Screen Recording permission before taking your first screenshot. |

The Windows installer defaults to `%LOCALAPPDATA%\Programs\EditHere`. On the components page, you can choose whether to launch at login, add the CLI to PATH, and create a desktop shortcut. Launch at login and PATH are selected by default on a new installation; upgrades preserve the existing startup registration state. Reopen your terminal and AI tools after installation so they can pick up the updated PATH. Uninstalling preserves your settings and projects.

The Windows installer is **not yet code-signed**. Download it from this repository's Releases and verify the [application package SHA-256 checksums](https://github.com/Inginnng/EditHere/releases/download/v0.9.4/SHA256SUMS.txt).

The minimum Windows build target is Windows 10 1809+; development and testing take place on Windows 11. The ZIP package does not require a separate installation of Qt, Python, Node, or .NET. macOS requires **14+** and has passed builds and automated tests, but **remains a preview without acceptance testing on a physical Mac or Apple notarization**.

The source repository and release packages are public and available to browse and download. See [Releases](https://github.com/Inginnng/EditHere/releases) for all versions.

<details>
<summary>Upgrading from an older version</summary>

Quit the old version, then run the installer or fully extract the new ZIP package. Projects now use the `.edithere` extension and no longer support legacy `.helpdesign` projects; export old projects as JSON feedback in the previous version before upgrading if needed. If launch at login reports an old path, keep the startup option selected and save the settings to refresh the path. If Windows has disabled the startup entry, re-enable it in the system's Startup Apps settings. Saving a project once in the new app registers the `.edithere` file association. Older versions' automatic update checks may not recognize the new repository URL; use the download links above for your first upgrade after the rename.

</details>

## FAQ

**Can it detect every control and all image content?** No. It combines element boundaries exposed by the system with local image analysis to find candidate regions. This is not OCR or semantic recognition. Draw a selection manually when the suggested region does not fit.

**Are screenshots uploaded automatically?** Screen capture, image analysis, and editing happen locally. Update checks do not upload your screenshots. If you send feedback yourself or let an integrated AI tool read it, any upload depends on how that tool operates. EditHere connects to GitHub when you check for updates manually or enable update checks at startup.

**Can I use a different AI tool?** Feedback is delivered as JSON and images, without being tied to a model provider. Codex and Claude Code can use the companion skill; other tools can receive feedback through the CLI or manual import. EditHere does not include model calls or model credits.

**Can I use the skill without installing EditHere?** Yes. Fully extract the Windows ZIP, keep the app, CLI, DLLs, and plugin folders together, put the `edithere` skill in a directory recognized by your AI tool, and give the AI the absolute path to `edithere-cli.exe`. You do not need to run the installer or add anything to PATH. Once configured, start with a request such as “Use EditHere to let me annotate this image.”

**Can I save my work and continue later?** Yes. A `.edithere` project preserves the original image, annotations, and editing state. The JSON you copy for AI communicates the change requests from the current session.

## Acknowledgements

Thanks to everyone on the [linux.do](https://linux.do/) forum for their suggestions and feedback.

The project also stands on the shoulders of these projects and people:

- **[Qt 6](https://www.qt.io/)** (The Qt Company and the Qt Project contributors) — the UI, networking and image handling are built on Qt, dynamically linked under the LGPL. Without it, a cross-platform native app would not have come together this quickly.
- **[MinGW-w64](https://www.mingw-w64.org/) and the GCC runtime** — the toolchain behind the Windows build and the runtime components shipped with it.
- **[NSIS](https://nsis.sourceforge.io/) and [aqtinstall](https://github.com/miurahr/aqtinstall)** — the Windows installer and the Qt deployment step in CI; both would have cost far more time to build from scratch.
- **Everyone who reports a problem or suggests an improvement** — most of the fixes in the installer, the annotation flow and the update flow came from your descriptions. Keep them coming via [Issues](https://github.com/Inginnng/EditHere/issues) or email.

License and copyright notices for third-party components live in the [third-party notices (Chinese)](packaging/THIRD-PARTY-NOTICES.md): that page is what the licences require, this section is simply thanks.

## License and commercial collaboration

The original software is licensed under the [MIT License](LICENSE) (SPDX: `MIT`). Provided that the copyright notice and license text are retained, you are free to use, copy, modify, merge, publish, distribute, sublicense, and sell the software — including for commercial purposes, inside closed-source products, and when invoking it from your AI tools and agents. See [licensing details (Chinese)](LICENSING.md).

Beyond the license, priority support, custom development, joint development, and branding cooperation are available from **[inginnng@163.com](mailto:inginnng@163.com)**; see [commercial collaboration (Chinese)](COMMERCIAL-LICENSE.md). These services are **not** a prerequisite for using the software.

The EditHere name and marks are not licensed under the MIT License. Third-party components such as Qt and MinGW remain subject to their own licenses; see [third-party notices (Chinese)](packaging/THIRD-PARTY-NOTICES.md).

## Documentation and feedback

The following documents are currently in Chinese:

[User guide](docs/USER-GUIDE.md) · [AI integration and CLI](docs/AGENT-CLI.md) · [Build and development](docs/DEVELOPMENT.md) · [Changelog](CHANGELOG.md)

[Report a problem or suggest an improvement](https://github.com/Inginnng/EditHere/issues). Please include your operating system and app versions, steps to reproduce the issue, and a screenshot or sample project you are comfortable sharing.
