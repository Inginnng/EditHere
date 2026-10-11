# Install EditHere Web with an AI agent

[简体中文](AI-SETUP.md) · **English**

This guide works before the Skill is installed. The host must provide local files, terminal execution and access to local web pages. Windows, macOS and Linux use the same Web Agent package. A chat interface can install it only when its host provides those capabilities.

## Prompt for the agent

> Follow this guide to install EditHere Web Agent. I will provide the web-agent ZIP download URL or local file and its SHA-256 checksum. Check existing runtimes and Skills first, use Node.js 18+, extract the entire package into a persistent writable user directory, install the edithere-web Skill and verify its page service. Tell me how to annotate the current page. Preserve existing Skills and other services, and report any specific conflict.

## Select the package

Use the ZIP whose name contains **`-web-agent-`**, with `web/install-skill.cjs` inside. The `-web-` package containing only `EditHere.html` supports browser editing without the Agent session service.

Creating a local ZIP does **not** publish a download URL. Use the actual address supplied by the author or listed on a release page; do not assume that the desktop `releases/latest` includes Web assets. A local ZIP works with the same steps below. Distributors should provide its `SHA256SUMS.txt` or exact package checksum.

## Install

1. Check `node --version`: version 18 or newer is required. An absolute path to the host's existing Node.js runtime is also usable. The package does not bundle Node.js and needs no Qt, Python, npm dependencies or desktop EditHere. If Node.js is missing, identify the dependency and configure it for the user's environment.
2. Verify the ZIP's SHA-256 and extract the entire package into a new, persistent, writable user directory. Use PowerShell `Get-FileHash -Algorithm SHA256`, macOS `shasum -a 256` or Linux `sha256sum`; preserve paths with the platform's ZIP extractor. Retain the extracted runtime after installation.
3. From that directory, run:

   ```text
   node web/install-skill.cjs
   ```

   The default destination is `$CODEX_HOME/skills/edithere-web`, or `~/.codex/skills/edithere-web` when unset. For another host, locate its actual Skill directory and pass an absolute destination:

   ```text
   node web/install-skill.cjs --destination <absolute-host-skill-directory/edithere-web>
   ```

   Success returns JSON with `state:"installed"`, the Skill directory, CLI and Node.js paths. The installer checks the runtime and writes its local entry to the Skill's `references/local-installation.md`. It refuses an existing destination. Inspect and reuse a valid existing installation, or install to a user-selected fresh destination; do not delete an existing Skill to bypass the conflict.
4. Start and verify the service from the same extracted directory:

   ```text
   node web/preview.cjs --json
   ```

   Expect `service:"edithere-web-preview"`, `agentBridge:true`, `apiVersion:1`, and the extracted HTML's path, PID and SHA-256. Open its returned `url`. If port 7330 belongs to another installation or service, report its PID/path conflict rather than terminating it automatically. This verifies page access, not a user's feedback submission.
5. Confirm the installed `SKILL.md`, all three reference files and a local entry pointing to the retained runtime. Reload Skills using the host's supported method. If the current chat has not discovered the new Skill, read its installed `SKILL.md` directly for this turn. Report actual installation paths and verification results.

## First annotation

The user can ask to annotate a game homepage with EditHere, or explicitly select `edithere-web`. Following the Skill, capture the actual current screen and choose unique absolute image and new feedback paths:

```text
node web/agent.cjs start --image <absolute-image> --output <new-absolute-feedback-json> --timeout 86400
node web/agent.cjs poll --session <sessionId-returned-by-start>
```

Open the session URL returned by `start` to load the image. The ordinary preview URL is not that session. After the user clicks Feedback, read the same sessionId and proceed only with valid `completed` feedback. Screenshot capture and opening a right panel depend on the host.

Submission writes to the local session service. Continuing automatically after the agent's turn ends requires authorized host scheduling; promise follow-up only after registration succeeds. Without scheduling, ask the user to return to the chat and say “continue” after submitting. Each `wait` lasts at most 55 seconds and is for an explicit request to wait now.

Runtime images, tokens and logs are stored in `web/tests/output/`; exclude them from redistribution. Moving the runtime requires updating the Skill's local entry. See the repository's `web/README.en.md` for capabilities and limitations, and the installed Skill for the annotation workflow.
