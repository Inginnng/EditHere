# Local Web Entry

This repository skill ships inside the Web Agent package and never embeds developer-local paths.
Relative to this file, the MCP server is `../../../mcp/server.bundle.cjs` and the browser compatibility CLI is `../../../agent.cjs`.

Extract the package to a persistent writable directory and run `node web/install-skill.cjs`. The installer verifies runtime files and populates this reference with absolute paths for local Node.js, the MCP server, MCP config snippet, and `agent.cjs` without editing host configs automatically.

MCP Apps requires Node.js 20+ over host-managed stdio (no HTTP server). The browser compatibility CLI runs on Node.js 18+ via loopback HTTP; standalone `EditHere.html` works completely offline without Node.js.
