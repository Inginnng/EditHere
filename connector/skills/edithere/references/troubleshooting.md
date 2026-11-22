# Exit Codes, Version Baselines & Troubleshooting

Quick diagnostic lookup when CLI calls, MCP tools, desktop permissions, or local preview servers hit an error.

## 1. CLI Exit Codes

All `edithere-cli` commands (except `--help` and `--version`) print single-line UTF-8 JSON:

| Exit Code | Meaning / Code | Action |
| --- | --- | --- |
| `0` | Success | Verify `ok === true` in stdout JSON and confirm the unique `--output` file exists and parses cleanly before acting |
| `2` | Invalid arguments / protocol | Verify input image/manifest path is absolute and exists, and options are valid |
| `3` | App / Agent endpoint unavailable | Run `status`; on `agent_endpoint_unavailable`, save user work and ask user to restart cleanly (never kill-9 processes) |
| `4` | `busy` | Unsaved document, active screenshot, or running session exists; ask user to finish or resolve it before retrying |
| `5` | File / I/O error | Check input readability, confirm `--output` does NOT yet exist, and confirm parent dir exists |
| `6` | Cancelled by user | Stop immediately; do not reuse previous feedback or auto-reopen a new round |
| `7` | Timed out | Same as `6`; confirm user did not submit before asking whether to retry (`--timeout` accepts `1`–`86400`s) |
| `8` | `desktop_access_required` | Current execution environment cannot reach the desktop session; see Section 3 |

## 2. Version Baselines

Always run `edithere-cli --version` (offline, sandbox-safe) and compare against `status.version`. If they differ, two installations exist — use the **lower** version to judge capabilities and alert the user without silently switching executables:

| EditHere Version | Supported Capabilities & Caveats |
| --- | --- |
| **0.10.0+** | Images, `.edithere` projects, project galleries, and video annotation (`video-feedback-1`) |
| **0.9.8 – 0.9.9** | Images and `.edithere` projects only (videos fail with `io_error`) |
| **0.9.6 – 0.9.7** | IPC endpoint name mismatch: `status` misreports `running:false` and `annotate` hits `startup_timeout`; upgrade |
| **0.9.0+** | Uses `objects` feedback structure (older versions emit parallel `annotations` + `changes`) |
| **0.8.21+** | `status` returns `running:false` only when desktop endpoints are definitely absent; `running:null` means unknown |

Direct users to <https://github.com/Inginnng/EditHere/releases/latest> when an upgrade is needed.

## 3. Desktop Sessions & Restricted Sandboxes

- On Windows and macOS, `status`, `open`, `capture`, and `annotate` require access to the logged-in desktop session (`--help`, `--version`, and offline `export` do not).
- In sandboxed Agents, request the host's approved desktop-capable execution path from the very first desktop call (e.g., `require_escalated` in Codex `exec_command` with clear justification) instead of spawning an invisible window inside a sandbox. Never alter global sandbox policies or elevate the app binary.
- A sandbox spawn failure before process creation does NOT mean EditHere is stopped.

## 4. Disconnections, Crashes & Service Recovery

- **Dropped connection / lost receipt**: Check **only** the current round's unique `--output` path. Proceed only if the file exists, parses as valid feedback JSON, and the user confirms they clicked **Finish and return to AI**. Never retry blindly or reuse an older round's output.
- **`startup_failed` / `startup_timeout`**: Inspect the `connection` diagnostic field; do not restart the desktop app in a loop.
- **Web Agent (`7330`) & DSH (`4782`)**:
  - If port `7330` is occupied by another workspace, set the same `EDITHERE_WEB_PORT` (`1`–`65535`) on both `web/preview.cjs` and `web/agent.cjs` instead of killing unknown processes.
  - Web submissions persist to disk (`.edithere/sessions/<sessionId>.json` or session dir); even if the background Node server stops between turns, `poll` / `edithere_web_read` still validates saved receipts.
  - For DSH startup failures, check `%USERPROFILE%\.edithere\dsh\logs\server-error.log` or run `node <dsh>/cli.mjs serve`.
