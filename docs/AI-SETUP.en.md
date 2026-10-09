# Let AI set up EditHere for you

[简体中文](AI-SETUP.md) · **English**

[Back to the product introduction](../README.md) · [Command-line reference](AGENT-CLI.en.md) · [Companion skill](../skills/edithere/SKILL.md)

**You can use the portable version with the skill as well; there is no need to run the installer or add anything to PATH.** The app handles screenshots and annotations, while the skill guides the AI to find the app, wait for you to submit, and then read the feedback. The two have to be prepared separately.

Give the passage below to an AI agent that can run commands and read and write files on your computer:

```text
Follow https://github.com/Inginnng/EditHere/blob/codex/native/docs/AI-SETUP.en.md to set up EditHere and the edithere skill for my current AI tool. Reuse an existing installation on this machine first; only download and install if there is none. Do not interrupt what I am editing. When you are done, verify the command line and the skill, and tell me how to start using it.
```

If you prefer the portable version, use this passage:

```text
Follow https://github.com/Inginnng/EditHere/blob/codex/native/docs/AI-SETUP.en.md to set up the portable EditHere and the edithere skill for my current AI tool. Look for an existing extracted folder first; if there is none, download the official complete portable ZIP. Do not run the installer, change PATH or set launch at login. Save the full path to the CLI, and tell me how to use it once you have verified it.
```

If a web chat or a remote cloud agent cannot access your computer, it can only offer guidance and cannot complete a local installation from this passage alone. **The source code and release packages are public**: the repository files and releases needed for setup are publicly accessible, and you do not need to request project access separately.

The steps below are for the agent carrying out the setup. Communicate in the user's current language, configure only the current tool, and do not install a copy for other AI tools by default.

## 1. Check the environment and reuse an existing app

- Confirm that you are on the user's own Windows or macOS machine, with the tool permissions this task requires. The Windows release package and the macOS 14+ preview are available; there is no Linux release package yet.
- Check the app directory the user provided this session first, then the `references/local-installation.md` in an existing skill, the current session's `EDITHERE_CLI` (if set), PATH and common installation directories; do not scan the whole disk by default.
- On Windows the default installation directory is `%LOCALAPPDATA%\Programs\EditHere`, and the CLI is `edithere-cli.exe`. In the portable version the CLI sits inside the fully extracted directory and stays together with `EditHere.exe` and its dependency files.
- On macOS the CLI is at `EditHere.app/Contents/MacOS/edithere-cli`, and the usual application directories are `/Applications` or `~/Applications`.
- Once you have found an existing app, run `--version` and `--help` with the full path first to confirm that the commands this task needs are present. Reuse it directly if it works; if an older version really must be upgraded, explain the version difference first and arrange the upgrade after the user has saved and quit normally. Do not terminate the GUI process, overwrite files that are in use, or clear unsaved documents.

`EDITHERE_CLI` is an optional way for the skill to locate the app; it is not an application command-line switch, and it does not require changing a global environment variable. For portable use, the local path recorded below is enough to locate the app across sessions.

## 2. Determine the official version, then download and verify

Download only when there is no usable app on the machine or an upgrade is genuinely needed.

1. From the [Releases](https://github.com/Inginnng/EditHere/releases) metadata of the official `Inginnng/EditHere` repository, choose the release that suits the current platform, and read that release's notes, the actual asset names and `SHA256SUMS.txt`. Do not guess file names or download URLs from old documents. Release asset names carry no version number, so you can fetch the latest version from a fixed address: `https://github.com/Inginnng/EditHere/releases/latest/download/EditHere-win-x64-setup.exe` (installer), `EditHere-win-x64.zip` (portable), `EditHere-macos-universal.dmg`, `SHA256SUMS.txt`.
2. You can read release information from the official public page or the GitHub API. If the GitHub CLI is configured on the machine, use `gh release view --repo Inginnng/EditHere --json tagName,assets,body` to read the latest release information, then download using the tag and exact asset names you get back; there is no need to configure extra credentials for downloading a public app.
3. If you get a 401/403/404 or the download fails, check the actual URL, whether the asset exists, API rate limiting and network state; for a signed-in tool, also check its authentication state. Report the specific blocker, do not ask for an access token, do not guess a mirror, and do not save web error content as an installer and then run it. You can keep reusing a complete app that already exists on the machine.
4. Download the app and the `SHA256SUMS.txt` for the same version, and compare the matching entry with SHA-256. On Windows use `Get-FileHash -Algorithm SHA256 -LiteralPath <file path>`; on macOS use `shasum -a 256 <file path>`. If the entry is missing or does not match, stop using that file and investigate why.

The app package is verified with `SHA256SUMS.txt`; `SHA256SUMS-A1.txt` covers the promotional video and brand assets and cannot replace the app-package verification. A checksum confirms file integrity; it is not the same as a publisher's digital signature.

The current source version is **0.10.4**; when downloading from GitHub, go by the actual version and assets of the official release. The Windows installer is unsigned; the macOS build is not yet notarised, and acceptance on a real Mac has not been completed. If the system blocks it, explain the actual prompt and leave the decision to the user; do not turn off Gatekeeper, SIP or system security protections to complete the setup.

## 3. Prepare the app according to the user's choice

### Windows: standard install

For a new installation use the official per-user installer; administrator rights are not required. Unless the user asks for launch at login or adding to PATH, this AI-driven setup uses `/STARTUP=0 /ADDPATH=0` and afterwards uses the full CLI path.

Once you have confirmed the download and verification above are complete, you can run this in PowerShell:

```powershell
# $InstallerPath is the absolute path to the verified installer.
$InstallProcess = Start-Process -FilePath $InstallerPath -ArgumentList '/S', '/STARTUP=0', '/ADDPATH=0' -WindowStyle Hidden -Wait -PassThru
if ($InstallProcess.ExitCode -ne 0) {
    throw "EditHere installation failed, exit code: $($InstallProcess.ExitCode)"
}
$EditHereCli = Join-Path $env:LOCALAPPDATA 'Programs\EditHere\edithere-cli.exe'
```

`/S` must be uppercase. When reusing an existing installation, do not run this example again and do not change the user's startup or PATH preferences; if a custom directory is needed, follow the arguments that version of the installer actually supports.

### Windows: portable use

Download the complete Windows portable ZIP and extract it to a directory the user can write to and intends to keep long term, such as a `Tools\EditHere` directory of the user's choosing. Keep all dependencies; do not copy only `edithere-cli.exe`. Record the actual path after extraction, do not run the installer, and do not change PATH or launch-at-login settings.

### macOS: app bundle

Download the official DMG for the matching platform, mount it, and copy the complete `EditHere.app` to a stable directory, preferring an application directory the user has chosen. Do not extract only the CLI, and do not run long term from the temporary mounted volume. Use the full path to the CLI inside the final `.app`; verifying the CLI does not require taking a screenshot or requesting Screen Recording permission. The `skills/edithere` in the DMG sits at the root of the mounted volume, so copying the `.app` does not install the skill — you still need to do the next step.

## 4. Install or update the skill for the current tool

The skill bundled in the app package may predate the path-location fix. Prefer to read the current version from the official repository's `codex/native` branch: first resolve it to a single explicit commit, then fetch the **entire `skills/edithere` directory** from that commit, including `SKILL.md`, reference files and other companion files. Do not mix files from different commits. If a trusted local clone exists, you may also use the same verified version of the files.

First identify the skills directory your tool actually discovers and any existing `edithere`, then choose a location:

| Current tool | Configuration location |
| --- | --- |
| Codex | For a new installation, use `~/.agents/skills/edithere` following the current official convention. If your tool already loads this skill from `~/.codex/skills/edithere` or a custom `CODEX_HOME/skills/edithere`, compare and update it in place to avoid installing a duplicate name. |
| Claude Code | Default `~/.claude/skills/edithere`; respect the custom configuration root the tool actually uses. |
| Other agents | Use only a skill directory or configuration entry point that the tool explicitly supports. If it does not support automatic skill discovery, explain that the CLI can be called manually as described in this guide and the command-line documentation, and do not claim the skill is installed. |

Reference: [Codex skills documentation](https://learn.chatgpt.com/docs/build-skills) · [Claude Code skills documentation](https://code.claude.com/docs/en/skills).

If the target directory already contains files, compare them first and preserve the user's local changes; do not blindly overwrite recursively, and do not create a second skill with the same name. The complete skill directory is readable operating instructions, so review its contents before installing; execution must still respect the permission boundaries of the current tool and the user.

In the **installed skill directory**, create or update `references/local-installation.md` so the next session can find an app in a portable or custom directory. The values below must be replaced with the actual results of your checks:

```markdown
# Local EditHere configuration

- CLI full path: C:\Users\example\Tools\EditHere\edithere-cli.exe
- App directory: C:\Users\example\Tools\EditHere
- CLI version: the actual --version output
- Usage: portable / per-user install / macOS app bundle
- Skill source: Inginnng/EditHere, the actual commit
```

Record the actual full path on macOS as well. This file records only local paths and versions, never writes credentials such as tokens, and **is not committed back to the project repository**; keep it and review it when you update the skill.

## 5. Verify and hand over

Run `--version` and `--help` with the full path, and check the exit code and output. A Windows example:

```powershell
& $EditHereCli --version
if ($LASTEXITCODE -ne 0) { throw 'The EditHere CLI version check failed.' }
& $EditHereCli --help
if ($LASTEXITCODE -ne 0) { throw 'The EditHere CLI help check failed.' }
```

If further verification is needed, run an offline `export` with a test image in a separate temporary directory, writing output to a new path that does not yet exist. Installation acceptance does not automatically call `annotate`, `capture` or `open`, and does not open or replace a document the user is editing. For desktop invocation permissions and error handling, see the [command-line reference](AGENT-CLI.en.md#desktop-access-for-windows-agents).

Check whether your tool has discovered `edithere`. If it only discovers it after a reload or a new session, say explicitly “the files are configured, but a reload is still needed”, and do not claim the skill is loaded just because the copy succeeded. The CLI being executable, the skill files being configured, and the current session being able to trigger it are three states to confirm separately.

When you are done, tell the user: the app's full path, the version, whether it was installed or used portably, the skill path and its load state, and any real blockers that remain. Tell the user to type the following when they next need an annotation:

> Use EditHere to let me annotate this interface. When I am done, update it based on my feedback.

Only when the user subsequently asks for an annotation should you start the corresponding session as described in the skill and wait for the user to click “Finish and return to AI” (完成并返回 AI).
