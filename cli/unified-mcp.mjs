#!/usr/bin/env node
/**
 * Unified EditHere MCP server (zero dependencies, stdio transport, MCP 2025-06-18).
 * Automatically routes between Desktop `edithere-cli` and Web Agent fallback,
 * exposing `edithere_doctor`, `edithere_status`, `edithere_annotate_start`,
 * `edithere_annotate_poll`, and `edithere_convert_project`.
 */
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';
import { runDoctor } from './doctor.mjs';
import { manifestToUnifiedProject, unifiedProjectToManifest } from './project-format.mjs';
import { pollAsyncSession, queryStatus, shutdownManagedSessions, startAsyncSession } from './router.mjs';

export const SERVER_NAME = 'edithere-unified';
export const SERVER_VERSION = '0.14.0';
export const PROTOCOL_VERSION = '2025-06-18';

export const UNIFIED_TOOLS = [
  {
    name: 'edithere_doctor',
    description: 'Inspect Desktop CLI, Node.js runtime, bundled Web Agent, Playwright availability, and AI host skills.',
    inputSchema: {
      type: 'object',
      properties: {},
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_status',
    description: 'Query active EditHere transport status (auto-selects desktop or web fallback).',
    inputSchema: {
      type: 'object',
      properties: {
        mode: { type: 'string', enum: ['auto', 'desktop', 'web'] },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_annotate_start',
    description: 'Start a non-blocking EditHere annotation session for an image, video, or project manifest.',
    inputSchema: {
      type: 'object',
      properties: {
        imagePath: { type: 'string', description: 'Absolute path to image, video, or project.json manifest.' },
        outputPath: { type: 'string', description: 'Absolute path for the new feedback JSON output.' },
        mode: { type: 'string', enum: ['auto', 'desktop', 'web'] },
        pageContext: { type: 'string' },
        timeoutSeconds: { type: 'number' },
        inboxDir: { type: 'string', description: 'Optional hot-add inbox directory for gallery sessions.' },
      },
      required: ['imagePath', 'outputPath'],
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_annotate_poll',
    description: 'Poll an annotation session started by edithere_annotate_start.',
    inputSchema: {
      type: 'object',
      properties: {
        sessionId: { type: 'string' },
      },
      required: ['sessionId'],
      additionalProperties: false,
    },
  },
  {
    name: 'edithere_convert_project',
    description: 'Convert between edithere-image-set-1 manifests and unified edithere-project-2 bundles.',
    inputSchema: {
      type: 'object',
      properties: {
        direction: { type: 'string', enum: ['to-unified', 'to-manifest'] },
        inputPath: { type: 'string' },
        outputPath: { type: 'string' },
      },
      required: ['direction', 'inputPath', 'outputPath'],
      additionalProperties: false,
    },
  },
];

function jsonContent(payload, isError = false) {
  return {
    content: [{ type: 'text', text: JSON.stringify(payload, null, 2) }],
    isError,
  };
}

export async function callUnifiedTool(name, args = {}) {
  switch (name) {
    case 'edithere_doctor': {
      const report = await runDoctor({ probePort: false });
      return jsonContent(report, !report.ok);
    }
    case 'edithere_status': {
      const status = queryStatus({ mode: args.mode || 'auto' });
      return jsonContent(status, !status.ok);
    }
    case 'edithere_annotate_start': {
      const started = startAsyncSession({
        inputPath: args.imagePath,
        outputPath: args.outputPath,
        mode: args.mode || 'auto',
        pageContext: args.pageContext || null,
        timeoutSeconds: args.timeoutSeconds ?? 1800,
        inboxDir: args.inboxDir || null,
      });
      return jsonContent(started, !started.ok);
    }
    case 'edithere_annotate_poll': {
      const polled = pollAsyncSession(args.sessionId);
      return jsonContent(polled, false);
    }
    case 'edithere_convert_project': {
      if (args.direction === 'to-unified') {
        const project = manifestToUnifiedProject(args.inputPath);
        const absOut = path.resolve(args.outputPath);
        fs.mkdirSync(path.dirname(absOut), { recursive: true });
        fs.writeFileSync(absOut, JSON.stringify(project, null, 2) + '\n', 'utf8');
        return jsonContent({ ok: true, outputPath: absOut, schemaVersion: project.schemaVersion, pageCount: project.pages.length });
      }
      if (args.direction === 'to-manifest') {
        const result = unifiedProjectToManifest(args.inputPath, args.outputPath);
        return jsonContent(result);
      }
      throw new Error(`Unsupported direction: ${String(args.direction)}`);
    }
    default:
      throw Object.assign(new Error(`Unknown tool: ${name}`), { rpcCode: -32602 });
  }
}

/**
 * Handle a single JSON-RPC 2.0 request object and return response object (or null for notifications).
 */
export async function handleRpcMessage(msg) {
  if (!msg || typeof msg !== 'object' || Array.isArray(msg) || msg.jsonrpc !== '2.0' || typeof msg.method !== 'string') {
    return { jsonrpc: '2.0', id: null, error: { code: -32600, message: 'Invalid JSON-RPC request.' } };
  }
  const { id, method, params } = msg;
  if (method === 'notifications/initialized' || id === undefined) {
    return null;
  }

  switch (method) {
    case 'initialize':
      return {
        jsonrpc: '2.0',
        id,
        result: {
          protocolVersion: PROTOCOL_VERSION,
          capabilities: { tools: {} },
          serverInfo: { name: SERVER_NAME, version: SERVER_VERSION },
        },
      };
    case 'ping':
      return { jsonrpc: '2.0', id, result: {} };
    case 'tools/list':
      return { jsonrpc: '2.0', id, result: { tools: UNIFIED_TOOLS } };
    case 'tools/call': {
      const name = params?.name;
      const args = params?.arguments || {};
      try {
        const result = await callUnifiedTool(name, args);
        return { jsonrpc: '2.0', id, result };
      } catch (error) {
        if (error && error.rpcCode) {
          return { jsonrpc: '2.0', id, error: { code: error.rpcCode, message: error.message } };
        }
        return {
          jsonrpc: '2.0',
          id,
          result: { content: [{ type: 'text', text: String(error?.message || error) }], isError: true },
        };
      }
    }
    default:
      return { jsonrpc: '2.0', id, error: { code: -32601, message: `Method not found: ${method}` } };
  }
}

export async function runStdioServer() {
  process.once('exit', shutdownManagedSessions);
  process.once('SIGINT', () => process.exit(0));
  process.once('SIGTERM', () => process.exit(0));

  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  try {
    for await (const line of rl) {
      const trimmed = line.trim();
      if (!trimmed) continue;
      let msg;
      try {
        msg = JSON.parse(trimmed);
      } catch {
        process.stdout.write(JSON.stringify({ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'Invalid JSON.' } }) + '\n');
        continue;
      }
      const response = await handleRpcMessage(msg);
      if (response) {
        process.stdout.write(JSON.stringify(response) + '\n');
      }
    }
  } finally {
    shutdownManagedSessions();
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  runStdioServer().catch((err) => {
    process.stderr.write(`[edithere-unified-mcp] fatal: ${String(err?.stack || err)}\n`);
    process.exit(1);
  });
}
