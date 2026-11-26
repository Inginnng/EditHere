#!/usr/bin/env node
/**
 * Unified EditHere CLI router (`cli/edithere.mjs`).
 * Converges Desktop CLI, Web Agent compatibility, DSH inbox hot-add,
 * one-command skill setup (`init`), diagnostics (`doctor`), project conversion,
 * and unified stdio MCP (`mcp`).
 */
import { APP_VERSION } from './version.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { formatDoctorReport, runDoctor } from './doctor.mjs';
import { runInit } from './init.mjs';
import { manifestToUnifiedProject, unifiedProjectToManifest } from './project-format.mjs';
import { closeWarmBrowser, listDesktopWindows, queryStatus, runBlockingAnnotate, runFastCapture } from './router.mjs';
import { runStdioServer } from './unified-mcp.mjs';


export function usageText() {
  return [
    `EditHere Unified CLI v${APP_VERSION}`,
    '',
    'Commands:',
    '  doctor [--json]                                       Diagnose Desktop CLI, Node, Web runtime, Playwright & AI hosts',
    '  init [--host auto|<agent-id>|all]                       Install/update skill and output MCP config',
    '       [--destination ABS] [--update] [--force] [--dry-run] [--json]',
    '  status [--mode auto|desktop|web]                      Probe active transport without launching GUI',
    '  windows                                               List visible desktop windows excluding EditHere',
    '  capture [--source interactive|screen|window|web]      Fast single-call screen, window, or web capture',
    '          [--window TXT | --window-id ID | --url URL] [--output PATH] [--annotate]',
    '  annotate <input> --output <new.json>                  Open image/video/manifest via Desktop CLI or Web fallback',
    '       [--mode auto|desktop|web] [--page-context TXT] [--timeout SEC] [--inbox DIR]',
    '  convert to-unified <manifest.json> --output <proj.json> Convert image-set manifest into edithere-project-2',
    '  convert to-manifest <proj.json> --out-dir <dir>       Extract edithere-project-2 or web project into manifest + images',
    '  mcp                                                   Start unified stdio MCP server',
    '  --version | --help                                    Show version or usage',
  ].join('\n');
}

function parseFlags(argv) {
  const positionals = [];
  const flags = {};
  const BOOLEAN_FLAGS = new Set(['--json', '--force', '--update', '--dry-run', '--help', '-h', '--version', '-v', '--screen', '--full-page', '--annotate']);
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (!token.startsWith('-')) {
      positionals.push(token);
      continue;
    }
    if (BOOLEAN_FLAGS.has(token)) {
      flags[token.replace(/^-+/, '')] = true;
      continue;
    }
    if (i + 1 >= argv.length) {
      throw new Error(`Missing value for option: ${token}`);
    }
    flags[token.replace(/^--/, '')] = argv[i + 1];
    i += 1;
  }
  return { positionals, flags };
}

export async function main(argv = process.argv.slice(2)) {
  if (!argv.length || argv[0] === '--help' || argv[0] === '-h') {
    console.log(usageText());
    return 0;
  }
  if (argv[0] === '--version' || argv[0] === '-v') {
    console.log(`EditHere Unified CLI ${APP_VERSION}`);
    return 0;
  }

  const [command, ...rest] = argv;
  const { positionals, flags } = parseFlags(rest);
  if (flags.help || flags.h) {
    console.log(usageText());
    return 0;
  }

  switch (command) {
    case 'doctor': {
      const report = await runDoctor();
      console.log(flags.json ? JSON.stringify(report, null, 2) : formatDoctorReport(report));
      return report.ok ? 0 : 1;
    }
    case 'init': {
      const receipt = runInit({
        host: flags.host || 'auto',
        destination: flags.destination || null,
        homeDir: flags['home-dir'] || undefined,
        force: Boolean(flags.force),
        update: Boolean(flags.update),
        dryRun: Boolean(flags['dry-run']),
      });
      console.log(JSON.stringify(receipt, null, 2));
      return 0;
    }
    case 'status': {
      const status = queryStatus({ mode: flags.mode || 'auto' });
      console.log(JSON.stringify(status));
      return status.ok ? 0 : 1;
    }
    case 'windows': {
      const res = listDesktopWindows();
      console.log(JSON.stringify(res));
      return res.ok ? 0 : 1;
    }
    case 'capture': {
      const inferredSource = flags.source
        || (flags.url ? 'web' : flags.window || flags['window-id'] ? 'window' : flags.screen ? 'screen' : 'interactive');
      try {
        const res = await runFastCapture({
          source: inferredSource,
          window: flags.window || null,
          windowId: flags['window-id'] || null,
          url: flags.url || null,
          viewport: flags.viewport || undefined,
          dpr: flags.dpr !== undefined ? Number(flags.dpr) : undefined,
          theme: flags.theme || undefined,
          fullPage: Boolean(flags['full-page']),
          selector: flags.selector || null,
          waitSelector: flags['wait-selector'] || null,
          waitMs: flags['wait-ms'] !== undefined ? Number(flags['wait-ms']) : undefined,
          concurrency: flags.concurrency !== undefined ? Number(flags.concurrency) : undefined,
          annotate: Boolean(flags.annotate),
          outputPath: flags.output || null,
          imagePath: flags.image || null,
          pageContext: flags['page-context'] || null,
          timeoutSeconds: flags.timeout !== undefined ? Number(flags.timeout) : 1800,
          mode: flags.mode || 'auto',
        });
        console.log(JSON.stringify(res));
        return res.ok ? 0 : 1;
      } finally {
        await closeWarmBrowser();
      }
    }
    case 'annotate': {
      if (!positionals[0] || !flags.output) {
        throw new Error('Usage: annotate <input> --output <new-feedback.json> [--mode auto|desktop|web] [--page-context TXT] [--timeout SEC] [--inbox DIR]');
      }
      const timeoutSeconds = flags.timeout !== undefined ? Number(flags.timeout) : 1800;
      const receipt = runBlockingAnnotate({
        inputPath: positionals[0],
        outputPath: flags.output,
        mode: flags.mode || 'auto',
        pageContext: flags['page-context'] || null,
        timeoutSeconds,
        inboxDir: flags.inbox || null,
      });
      console.log(JSON.stringify(receipt));
      return receipt.ok ? 0 : 1;
    }
    case 'convert': {
      const direction = positionals[0];
      const inputPath = positionals[1];
      if (!direction || !inputPath) {
        throw new Error('Usage: convert to-unified <manifest.json> --output <project.json> | convert to-manifest <project.json> --out-dir <dir>');
      }
      if (direction === 'to-unified') {
        if (!flags.output) throw new Error('--output is required for convert to-unified.');
        const project = manifestToUnifiedProject(inputPath);
        const absOut = path.resolve(flags.output);
        fs.mkdirSync(path.dirname(absOut), { recursive: true });
        fs.writeFileSync(absOut, JSON.stringify(project, null, 2) + '\n', 'utf8');
        console.log(JSON.stringify({ ok: true, output: absOut, schemaVersion: project.schemaVersion, pageCount: project.pages.length }));
        return 0;
      }
      if (direction === 'to-manifest') {
        const outDir = flags['out-dir'] || flags.output;
        if (!outDir) throw new Error('--out-dir is required for convert to-manifest.');
        const result = unifiedProjectToManifest(inputPath, outDir, { name: flags.name || null });
        console.log(JSON.stringify(result));
        return 0;
      }
      throw new Error(`Unknown convert subcommand "${direction}". Expected to-unified or to-manifest.`);
    }
    case 'mcp': {
      await runStdioServer();
      return 0;
    }
    default:
      throw new Error(`Unknown command "${command}". Run with --help to see available commands.`);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().then((code) => {
    process.exitCode = code;
  }).catch((error) => {
    console.error(JSON.stringify({ ok: false, error: error && error.message ? error.message : String(error) }));
    process.exitCode = 1;
  });
}
