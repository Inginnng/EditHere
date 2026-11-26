# Browser Follow-Up & Handoff

True MCP Apps sessions notify the host via the UI message bridge without needing scheduled polling. Plain browser and `agent.cjs` compatibility sessions default to manual handoff: open the review URL, end your turn, and wait for the user to reply "continue".

## 1. Persist & Read Session Records

Save actual `sessionId`, `url`, `image`, `imageSource`, `node`, `cli`, `output`, `expiresAt`, and `taskScope` under `.edithere/sessions/<sessionId>.json`:

```json
{
  "transport": "http-compatibility",
  "followUpMode": "manual",
  "followUp": null,
  "handling": {"state": "pending", "sha256": null}
}
```

URLs contain access tokens — store locally only, never print full URLs externally or construct shell-evaluated strings from paths.

When the user says "continue", run `poll` once against the original `sessionId` (saved receipts remain readable on disk even if the background Node server exited). Never busy-poll pending sessions; only execute a single bounded `wait` (max 55s) if the user explicitly asks to wait inline.

On `completed`, verify `sessionId`, `output`, SHA-256, and payload; record `received` and clear any scheduled check. Mark `processing` **only when code edits actually begin** (reading feedback or asking clarifying questions does not count). Mark `handled` with SHA on completion, or `needs_review` on partial failure. Never re-run an already-`handled` SHA.

## 2. Optional Scheduled Follow-Up (User Opt-In Only)

Do not create heartbeats or cron tasks unless the user explicitly asks for timed follow-up and the host provides a real scheduler tool. Store the real job ID, query the same `sessionId` at most once per tick (no background sleep loops), stay quiet on `pending`, and clean up the job immediately on `completed`, `cancelled`, `expired`, or persistent error.

Never assume a tab was closed based on blur, hidden panel, tab switch, or a single read failure unless the host exposes a complete, exact tab inventory.

## 3. Stopping & Cleanup

When the user asks to stop waiting or cancel, remove/pause the scheduled follow-up cleanly through the host's scheduler API without asking them to reopen the tab. Only call `cancel` on `agent.cjs` when the user asks to cancel the EditHere session itself. Never spawn replacement processes to bypass user cancellation.
