// MCP Apps transport follows the official ext-apps server helpers and Cowart's
// stdio/resource/tool pattern. The host owns process lifetime; no HTTP listener
// or detached process is needed for the embedded editor.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {McpServer} from '@modelcontextprotocol/sdk/server/mcp.js';
import {StdioServerTransport} from '@modelcontextprotocol/sdk/server/stdio.js';
import {registerAppTool, registerAppResource, getUiCapability, RESOURCE_MIME_TYPE} from '@modelcontextprotocol/ext-apps/server';
import {z} from 'zod';
import sessionModule from '../agent-session.cjs';

const {AgentSessions, MAX_BODY, imageData, defaultSessionDirectory, readSessionStatus} = sessionModule;
const directory = typeof EDITHERE_MCP_DIR !== 'undefined' ? EDITHERE_MCP_DIR : path.dirname(fileURLToPath(import.meta.url));
export const UI_URI = 'ui://edithere/annotation.html';
const sessionIdSchema = z.string().regex(/^[a-f0-9]{32}$/);
const tokenSchema = z.string().regex(/^[a-f0-9]{64}$/);
const INLINE_IMAGE_LIMIT = 8 * 1024 * 1024;
const IMAGE_CHUNK_SIZE = 256 * 1024;

function publicReceipt(status) {
  return {sessionId:status.sessionId, state:status.state, name:status.name, outputPath:status.output, expiresAt:status.expiresAt,
    ...(status.state === 'completed' ? {completedAt:status.completedAt, sha256:status.sha256, feedback:status.feedback} : {})};
}
function result(value, meta) {
  return {content:[{type:'text', text:JSON.stringify(value)}], structuredContent:value, ...(meta ? {_meta:{edithere:meta}} : {})};
}
function errorResult(error) { return {isError:true, content:[{type:'text', text:error.message || String(error)}]}; }
function guarded(callback) {
  return async (args, extra) => {
    try {
      extra.signal.throwIfAborted();
      return await callback(args, extra);
    } catch (error) { return errorResult(error); }
  };
}
function imageInput(imagePath) {
  if (typeof imagePath !== 'string' || !path.isAbsolute(imagePath)) throw new Error('imagePath must be an absolute local image path.');
  const inputPath = path.resolve(imagePath), stat = fs.statSync(inputPath);
  if (!stat.isFile() || stat.size > 64 * 1024 * 1024) throw new Error('Input must be a local image file no larger than 64 MiB.');
  const mime = {'.png':'png','.jpg':'jpeg','.jpeg':'jpeg','.webp':'webp','.gif':'gif','.bmp':'bmp'}[path.extname(inputPath).toLowerCase()];
  if (!mime) throw new Error('Supported input images: PNG, JPEG, WebP, GIF and BMP.');
  return {inputPath, image:imageData('data:image/' + mime + ';base64,' + fs.readFileSync(inputPath).toString('base64'))};
}
function fallback(imagePath, outputPath, name) {
  return {offlineHtmlPath:path.resolve(directory, '../EditHere.html'), command:process.execPath,
    args:[path.resolve(directory, '../agent.cjs'), 'start', '--image', imagePath, '--output', outputPath, ...(name ? ['--name', name] : [])],
    instruction:'This host has not advertised MCP Apps support. Use the offline HTML for manual export, or run the compatibility command and open the returned URL. Do not claim the embedded panel opened.'};
}

export function createEditHereMcpServer({dataDirectory = defaultSessionDirectory(), htmlPath = path.resolve(directory, '../EditHere.mcp.html')} = {}) {
  const sessions = new AgentSessions({directory:dataDirectory, credentials:false});
  const server = new McpServer({name:'edithere-web', version:'0.1.0'}, {maxToolInputElements:200000});
  const uiMeta = {prefersBorder:false, csp:{connectDomains:[], resourceDomains:[], frameDomains:[]}};
  registerAppResource(server, 'EditHere annotation editor', UI_URI, {
    description:'Self-contained EditHere editor. Original branding, native feedback JSON and persistent drafts.',
    _meta:{ui:uiMeta}
  }, async () => ({contents:[{uri:UI_URI, mimeType:RESOURCE_MIME_TYPE, text:fs.readFileSync(htmlPath, 'utf8'), _meta:{ui:uiMeta}}]}));

  registerAppTool(server, 'edithere_web_open', {
    title:'Open EditHere', description:'Open a local screenshot in EditHere for the user to annotate and move components. Pass an absolute imagePath and optional unique outputPath, or only a previously returned sessionId to resume its persisted draft/receipt. Wait for user feedback; do not invent edits or repeatedly poll. A host without MCP Apps receives an explicit compatibility fallback.',
    inputSchema:{imagePath:z.string().optional(), outputPath:z.string().optional(), name:z.string().min(1).max(200).optional(), sessionId:sessionIdSchema.optional()},
    annotations:{readOnlyHint:false, destructiveHint:false, idempotentHint:false, openWorldHint:false},
    _meta:{ui:{resourceUri:UI_URI, visibility:['model']}}
  }, guarded(async args => {
    const uiSupport = getUiCapability(server.server.getClientCapabilities())?.mimeTypes?.includes(RESOURCE_MIME_TYPE) ? 'supported' : 'unsupported';
    let session;
    if (args.sessionId) {
      if (args.imagePath !== undefined || args.outputPath !== undefined || args.name !== undefined) throw new Error('To resume, pass only sessionId.');
      session = sessions.byId(args.sessionId);
    } else {
      const input = imageInput(args.imagePath);
      const outputPath = args.outputPath ?? path.join(path.dirname(input.inputPath), '.edithere', 'feedback-' + crypto.randomUUID() + '.json');
      if (!path.isAbsolute(outputPath)) throw new Error('outputPath must be absolute.');
      if (uiSupport === 'unsupported') return result({state:'unsupported', uiSupport, fallback:fallback(input.inputPath, path.resolve(outputPath), args.name)});
      const created = sessions.create({...input, output:outputPath, name:args.name, timeoutSeconds:86400});
      session = sessions.byId(created.sessionId);
    }
    const receipt = publicReceipt(session.state === 'completed' ? readSessionStatus({directory:dataDirectory, sessionId:session.id}) : sessions.status(session));
    const meta = {sessionId:session.id, token:session.token, name:session.name, state:session.state,
      ...(session.draft ? {draft:session.draft} : {}), ...(session.state === 'completed' ? {receipt} : {})};
    // SDK stdio clients commonly cap one incoming message at 10 MiB. A draft
    // never repeats its immutable image; larger images cross the same official
    // tool bridge in bounded private chunks instead of increasing host limits.
    if (session.image.length < INLINE_IMAGE_LIMIT && Buffer.byteLength(JSON.stringify(meta)) + session.image.length < INLINE_IMAGE_LIMIT) meta.image = session.image;
    else meta.imageInfo = {length:session.image.length};
    return result({...receipt, uiSupport, ...(uiSupport === 'unsupported' ? {fallback:fallback(session.inputPath, session.output, session.name)} : {})}, meta);
  }));

  server.registerTool('edithere_web_read', {
    title:'Read saved EditHere feedback', description:'Read a persisted EditHere session and completed native feedback without an HTTP service. The receipt omits image encoding; the complete original-image JSON is at outputPath. Use after the user submits feedback or asks to continue.',
    inputSchema:{sessionId:sessionIdSchema}, annotations:{readOnlyHint:true, destructiveHint:false, idempotentHint:true, openWorldHint:false}
  }, guarded(async ({sessionId}) => result(publicReceipt(readSessionStatus({directory:dataDirectory, sessionId})))));

  function authorized(sessionId, token) {
    const session = sessions.byId(sessionId);
    if (session.token !== token) throw new Error('This token does not belong to the session.');
    return session;
  }
  function appTool(name, title, inputSchema, callback) {
    registerAppTool(server, name, {title, inputSchema, annotations:{readOnlyHint:false, destructiveHint:false, idempotentHint:true, openWorldHint:false},
      _meta:{ui:{resourceUri:UI_URI, visibility:['app']}}}, guarded(callback));
  }
  appTool('edithere_web_image', 'Read EditHere image chunk', {sessionId:sessionIdSchema, token:tokenSchema, offset:z.number().int().min(0).default(0)}, async ({sessionId,token,offset}) => {
    const session = authorized(sessionId, token);
    if (offset > session.image.length) throw new Error('Image chunk offset is outside this session\'s image.');
    const chunk = session.image.slice(offset, offset + IMAGE_CHUNK_SIZE);
    return {...result({offset, nextOffset:offset + chunk.length, totalLength:session.image.length}), _meta:{edithereImageChunk:chunk}};
  });
  appTool('edithere_web_submit', 'Save EditHere feedback', {sessionId:sessionIdSchema, token:tokenSchema, feedback:z.unknown()}, async ({sessionId,token,feedback}) => {
    const session = authorized(sessionId, token);
    // The immutable original belongs to the authorized session. Apps upload
    // only changes; callers that explicitly supply another image are rejected
    // by the unchanged strict native validator below.
    const complete = feedback && typeof feedback === 'object' && !Array.isArray(feedback) && !Object.hasOwn(feedback, 'image') ? {...feedback, image:session.image} : feedback;
    return result(publicReceipt(sessions.submit(token, complete)));
  });
  appTool('edithere_web_draft', 'Save EditHere draft', {sessionId:sessionIdSchema, token:tokenSchema, project:z.unknown()}, async ({sessionId,token,project}) => {
    const session = authorized(sessionId, token), document = project?.document;
    const complete = document && typeof document === 'object' && !Array.isArray(document) && !Object.hasOwn(document, 'image') ? {...project, document:{...document, image:session.image}} : project;
    return result(sessions.saveDraft(token, complete));
  });
  appTool('edithere_web_cancel', 'Cancel EditHere session', {sessionId:sessionIdSchema, token:tokenSchema}, async ({sessionId,token}) => {
    return result(publicReceipt(sessions.cancel(authorized(sessionId, token))));
  });
  return {server, sessions};
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const {server} = createEditHereMcpServer();
  const transport = new StdioServerTransport(process.stdin, process.stdout, {maxBufferSize:MAX_BODY + 1024 * 1024});
  server.connect(transport).catch(error => { console.error(error.message); process.exitCode = 1; });
  // Ending a host connection is respected. Durable files survive; this process
  // never starts a replacement or tries to keep an interrupted agent running.
  process.stdin.once('end', () => { server.close().catch(error => console.error(error.message)); });
}
