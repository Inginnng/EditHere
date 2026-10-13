'use strict';
/*
 * 「直接安装 Web 包」校验：只从下载/解压后的 Web 包出发，不读源码仓库、不读其他构建产物。
 *
 * 用法: node scripts/web-package-check.cjs <解压目录 | ZIP 路径> [测试图片]
 *
 * 两种包自动识别：
 *   - web-agent 包（`EditHere-web-agent.zip`）：根目录放许可、AI-SETUP 与 build-info.json，
 *     运行时在 web/ 下。校验结构、包内自洽（MCP 清单哈希）、技能安装与 MCP stdio 链路。
 *   - offline 包（`EditHere-web.zip`）：根目录只有 EditHere.html 与许可。
 *     校验它是真正免 Node.js 的单文件包，且不含 MCP 运行时。
 *
 * MCP 链路需要一个真实图片文件；找不到图片时该段标记为 SKIP 而不是失败，
 * 结构、自洽与安装检查仍然生效。
 */
const { spawn, spawnSync } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const NODE = process.execPath;
const results = [];

function check(name, ok, detail) {
  results.push({ name, ok: Boolean(ok) });
  const extra = detail === undefined ? '' : '  [' + String(detail).replace(/\s+/g, ' ').slice(0, 240) + ']';
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${extra}`);
}

function skip(name, detail) {
  results.push({ name, ok: true, skipped: true });
  console.log(`SKIP  ${name}${detail ? '  [' + detail + ']' : ''}`);
}

function sha256(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

/** MCP build-info.json 记录的是行尾归一化（utf8-lf）后的哈希，包内校验用同一算法。 */
function mcpHash(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n'), 'utf8').digest('hex');
}

function unzip(archive) {
  const target = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-web-unzip-'));
  const python = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
  const extracted = spawnSync(python, ['-m', 'zipfile', '-e', archive, target], { encoding: 'utf8' });
  if (extracted.status !== 0) {
    fs.rmSync(target, { recursive: true, force: true });
    throw new Error('解压失败，请先手动解压再传入目录：' + String(extracted.stderr || extracted.error).trim());
  }
  return target;
}

function locateImage() {
  const candidates = [process.argv[3], process.env.EDITHERE_TEST_IMAGE,
                      path.resolve(__dirname, '..', '..', 'EditHere宣传', '原图', '游戏修改前.png'),
                      'C:/Windows/Web/Wallpaper/Windows/img0.jpg'];
  return candidates.find((candidate) => candidate && fs.existsSync(candidate)) || null;
}

function runScenario(pkg, workDir, capabilities, openArgs, timeoutMs = 60000, resourceUri) {
  return new Promise((resolve) => {
    const child = spawn(NODE, [path.join(pkg, 'web/mcp/server.bundle.cjs')], {
      stdio: ['pipe', 'pipe', 'pipe'],
      env: { ...process.env, EDITHERE_WEB_DATA_DIR: path.join(workDir, 'sessions') }
    });
    let buf = '';
    let stderr = '';
    const pending = new Map();
    child.stdout.on('data', (data) => {
      buf += data.toString('utf8');
      let index;
      while ((index = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, index).trim();
        buf = buf.slice(index + 1);
        if (!line) continue;
        try {
          const message = JSON.parse(line);
          if (message.id !== undefined && pending.has(message.id)) {
            pending.get(message.id)(message);
            pending.delete(message.id);
          }
        } catch { console.log('  <非 JSON 输出>', line.slice(0, 200)); }
      }
    });
    child.stderr.on('data', (data) => { stderr += data.toString(); });
    const send = (object) => child.stdin.write(JSON.stringify(object) + '\n');
    const wait = (id) => new Promise((done) => pending.set(id, done));
    const finish = (result) => { try { child.kill(); } catch {} resolve({ ...result, stderr }); };
    const timer = setTimeout(() => finish({ timeout: true }), timeoutMs);
    (async () => {
      try {
        const initWait = wait(1);
        send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {
          protocolVersion: '2025-06-18', capabilities, clientInfo: { name: 'edithere-web-package-check', version: '1.0.0' } } });
        const init = await initWait;
        send({ jsonrpc: '2.0', method: 'notifications/initialized' });
        const listWait = wait(2);
        send({ jsonrpc: '2.0', id: 2, method: 'tools/list', params: {} });
        const tools = await listWait;
        const openWait = wait(3);
        send({ jsonrpc: '2.0', id: 3, method: 'tools/call', params: { name: 'edithere_web_open', arguments: openArgs } });
        const call = await openWait;
        let resource = null;
        if (resourceUri) {
          const readWait = wait(4);
          send({ jsonrpc: '2.0', id: 4, method: 'resources/read', params: { uri: resourceUri } });
          resource = await readWait;
        }
        clearTimeout(timer);
        finish({ init, tools, call, resource });
      } catch (error) {
        clearTimeout(timer);
        finish({ error: String(error) });
      }
    })();
  });
}

/** 内嵌图标集随包发出时，其许可必须一起发出；未内嵌的版本不做要求。
 *  0.11.1 起 web 构建从 assets/ui-icons 取图标，构建清单里会出现这些源文件。 */
function checkIconLicense(pkg, required) {
  const license = path.join(pkg, 'licenses/ui-icons/LICENSE');
  if (fs.existsSync(license)) {
    check('共享图标许可非空', fs.statSync(license).size > 0, fs.statSync(license).size + ' 字节');
  } else if (required) {
    check('共享图标许可随包发出', false, '构建清单含 assets/ui-icons 源文件');
  } else {
    skip('共享图标许可（本版 web 构建未内嵌共享图标集）');
  }
}

function manifestIconSources(pkg) {
  try {
    const manifest = JSON.parse(fs.readFileSync(path.join(pkg, 'web/mcp/build-info.json'), 'utf8'));
    return Object.keys(manifest.sources || {}).some((name) => name.startsWith('assets/ui-icons/'));
  } catch { return false; }
}

function checkCommon(pkg) {
  const files = ['LICENSE', 'LICENSING.md', 'NOTICE', 'TRADEMARK-POLICY.md', 'COMMERCIAL-LICENSE.md',
                 'packaging/THIRD-PARTY-NOTICES.md', 'build-info.json', 'README.md'];
  for (const relative of files) check(`包内存在 ${relative}`, fs.existsSync(path.join(pkg, relative)));

  let stamp = null;
  try { stamp = JSON.parse(fs.readFileSync(path.join(pkg, 'build-info.json'), 'utf8')); } catch {}
  check('build-info.json 可解析且记录版本、来源与渠道',
        Boolean(stamp && stamp.productSourceVersion && stamp.sourceCommit && stamp.webRevision && stamp.channel),
        stamp ? `version=${stamp.productSourceVersion} commit=${stamp.sourceCommit} channel=${stamp.channel} dirty=${stamp.sourceDirty}` : '解析失败');
  if (!stamp) return null;
  check('渠道标记与 published 一致',
        (stamp.channel === 'release') === (stamp.published === true),
        `channel=${stamp.channel} published=${stamp.published}`);
  // 发布包不能自称本地预览，反之亦然：README 末段随渠道改写。
  const readme = fs.readFileSync(path.join(pkg, 'README.md'), 'utf8');
  const saysPreview = /本地预览/.test(readme);
  check(`README 渠道口径与 channel=${stamp.channel} 一致`,
        stamp.channel === 'release' ? !saysPreview : saysPreview, saysPreview ? 'README 含「本地预览」' : 'README 无「本地预览」');
  return stamp;
}

function checkOffline(pkg) {
  const html = path.join(pkg, 'EditHere.html');
  check('离线包在根目录提供 EditHere.html', fs.existsSync(html));
  if (fs.existsSync(html)) {
    const size = fs.statSync(html).size;
    const source = fs.readFileSync(html, 'utf8');
    check('离线包 EditHere.html 为完整单文件', size > 100000 && /<html/i.test(source), `${size} 字节`);
    check('离线包不含外部脚本或样式引用',
          !/<(?:script|link)[^>]+(?:src|href)=["']https?:/i.test(source.replace(/<link[^>]+rel=["']icon["'][^>]*>/gi, '')));
    check('离线包 EditHere.html 含内嵌图标与版本号', /data:image\/svg\+xml;base64,/.test(source) && /\d+\.\d+\.\d+/.test(source));
  }
  check('离线包不含 MCP 运行时', !fs.existsSync(path.join(pkg, 'web/mcp/server.bundle.cjs')) && !fs.existsSync(path.join(pkg, 'web/EditHere.mcp.html')));
  check('离线包不含 AI-SETUP 指南', !fs.existsSync(path.join(pkg, 'AI-SETUP.md')));
  const readme = fs.readFileSync(path.join(pkg, 'README.md'), 'utf8');
  check('离线包 README 说明无需 Node.js', /无需\s*Node\.js/.test(readme) && /web-agent/.test(readme));
  checkIconLicense(pkg, false);
}

function checkAgent(pkg, stamp) {
  checkIconLicense(pkg, manifestIconSources(pkg));
  for (const relative of ['AI-SETUP.md', 'AI-SETUP.en.md',
                          'web/EditHere.html', 'web/EditHere.mcp.html', 'web/MCP-THIRD-PARTY-NOTICES.md',
                          'web/install-skill.cjs', 'web/agent.cjs', 'web/agent-session.cjs',
                          'web/preview.cjs', 'web/preview-server.cjs', 'web/src/feedback.js', 'web/src/layout.js',
                          'web/mcp/server.bundle.cjs', 'web/mcp/mcp.json', 'web/mcp/build-info.json',
                          'web/skills/edithere-web/SKILL.md', 'web/skills/edithere-web/agents/openai.yaml',
                          'web/skills/edithere-web/references/capture.md', 'web/skills/edithere-web/references/follow-up.md'])
    check(`包内存在 ${relative}`, fs.existsSync(path.join(pkg, relative)));
  // 随包发出的是通用说明；本机绝对路径由安装器写入，包内不得夹带开发者路径。
  const placeholder = fs.readFileSync(path.join(pkg, 'web/skills/edithere-web/references/local-installation.md'), 'utf8');
  check('包内 local-installation.md 为通用说明且不含开发者绝对路径',
        /install-skill\.cjs/.test(placeholder) && !/[A-Za-z]:[\\/](?:Users|Project|home)\b/.test(placeholder),
        placeholder.split('\n').slice(0, 3).join(' / '));

  // 包内自洽：MCP 清单里的三个产物必须齐全且哈希一致（行尾归一化，与打包校验同一算法）。
  let manifest = null;
  try { manifest = JSON.parse(fs.readFileSync(path.join(pkg, 'web/mcp/build-info.json'), 'utf8')); } catch {}
  if (!manifest) {
    check('web/mcp/build-info.json 可解析', false, '解析失败');
  } else {
    check('web/mcp/build-info.json 记录 schema 与哈希编码',
          manifest.schemaVersion === 1 && manifest.hashEncoding === 'utf8-lf',
          `schemaVersion=${manifest.schemaVersion} hashEncoding=${manifest.hashEncoding}`);
    const artifacts = Object.entries(manifest.artifacts || {});
    const broken = artifacts.filter(([relative, digest]) => {
      const file = path.join(pkg, relative);
      return !fs.existsSync(file) || mcpHash(file) !== digest;
    }).map(([relative]) => relative);
    check('MCP 三个产物齐全且哈希与清单一致',
          artifacts.length === 3 && broken.length === 0, broken.join(', ') || `${artifacts.length} 个产物`);
    let present = 0;
    let matched = 0;
    for (const [relative, digest] of Object.entries(manifest.sources || {})) {
      const file = path.join(pkg, relative);
      if (!fs.existsSync(file)) continue;
      present += 1;
      if (mcpHash(file) === digest) matched += 1;
    }
    check('包内可校验的 MCP 源文件与清单一致', present >= 3 && matched === present, `${matched}/${present} 一致`);
  }

  // 安装技能：install-skill.cjs 只接受绝对目标目录，且拒绝覆盖已有目录。
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-web-install-'));
  const skillDir = path.join(work, 'skills', 'edithere-web');
  const installed = spawnSync(NODE, [path.join(pkg, 'web/install-skill.cjs'), '--destination', skillDir], { encoding: 'utf8' });
  let report = null;
  try { report = JSON.parse((installed.stdout || '').trim().split('\n').pop()); } catch {}
  check('install-skill.cjs 退出码 0', installed.status === 0, (installed.stderr || '').trim());
  check('安装结果 state=installed', Boolean(report && report.state === 'installed'), (installed.stdout || '').trim().slice(0, 200));
  for (const relative of ['SKILL.md', 'agents/openai.yaml', 'references/capture.md', 'references/follow-up.md',
                          'references/local-installation.md'])
    check(`技能文件已安装 ${relative}`, fs.existsSync(path.join(skillDir, relative)));
  const bundle = path.join(pkg, 'web/mcp/server.bundle.cjs');
  if (report && report.mcpServer) {
    check('MCP 配置指向包内绝对路径', path.resolve(report.mcpServer) === bundle, report.mcpServer);
    check('MCP 配置 command 为当前 Node 绝对路径', report.mcpConfig?.mcpServers?.['edithere-web']?.command === NODE);
    const reference = fs.readFileSync(path.join(skillDir, 'references/local-installation.md'), 'utf8');
    check('local-installation.md 记录了本机绝对路径', reference.includes(bundle));
  }
  const again = spawnSync(NODE, [path.join(pkg, 'web/install-skill.cjs'), '--destination', skillDir], { encoding: 'utf8' });
  check('重复安装被拒绝（不覆盖已有技能）',
        again.status !== 0 && /already exists/.test(again.stderr || ''), (again.stderr || '').trim().slice(0, 160));

  return { work, skillDir, bundle };
}

async function checkMcp(pkg, work, image) {
  if (!image) {
    skip('MCP stdio 链路（未找到测试图片，跳过）', '可用第二个参数或 EDITHERE_TEST_IMAGE 指定');
    return;
  }
  const scenario1 = await runScenario(pkg, work, {}, { imagePath: image, name: 'package-check-no-ui' });
  const text1 = JSON.stringify(scenario1.call?.result || scenario1.call || '');
  const names = (scenario1.tools?.result?.tools || []).map((tool) => tool.name);
  check('tools/list 含 edithere_web_open / edithere_web_read',
        names.includes('edithere_web_open') && names.includes('edithere_web_read'), names.join(', '));
  check('场景1（宿主未声明 UI 能力）返回 unsupported', /"uiSupport":"unsupported"|unsupported/.test(text1));
  check('场景1 提供普通浏览器兼容入口 fallback', /fallback/.test(text1));
  check('场景1 不创建空会话', !/"sessionId"/.test(text1));
  if (scenario1.timeout) check('场景1 未超时', false, scenario1.stderr);

  const openTool = (scenario1.tools?.result?.tools || []).find((tool) => tool.name === 'edithere_web_open');
  const resourceUri = openTool?._meta?.ui?.resourceUri;
  check('edithere_web_open 声明 ui.resourceUri', Boolean(resourceUri), resourceUri);

  const outputPath = path.join(work, 'feedback.json');
  const scenario2 = await runScenario(pkg, work,
    { extensions: { 'io.modelcontextprotocol/ui': { mimeTypes: ['text/html;profile=mcp-app'] } } },
    { imagePath: image, name: 'package-check-with-ui', outputPath }, 60000, resourceUri);
  const text2 = JSON.stringify(scenario2.call?.result || scenario2.call || '');
  check('场景2（宿主声明 UI 能力）返回 supported', /"uiSupport":"supported"|supported/.test(text2));
  check('场景2 返回 sessionId', /"sessionId"/.test(text2));
  if (scenario2.timeout) check('场景2 未超时', false, scenario2.stderr);
  const resourceText = scenario2.resource?.result?.contents?.[0]?.text || '';
  check('ui:// 资源可读回内嵌编辑器 HTML',
        resourceText.length > 100000 && /<html/i.test(resourceText), `${resourceText.length} 字节`);
  check('内嵌 HTML 不含外部脚本或样式地址',
        !/https?:\/\/(?!127\.0\.0\.1)[^\s"']*\.(?:js|css)/i.test(resourceText.replace(/data:[^\s"']+/g, '')));
}

(async () => {
  const input = process.argv[2];
  if (!input) {
    console.error('用法: node scripts/web-package-check.cjs <解压目录 | ZIP 路径> [测试图片]');
    process.exitCode = 2;
    return;
  }
  const owned = input.toLowerCase().endsWith('.zip') ? unzip(path.resolve(input)) : null;
  const pkg = owned || path.resolve(input);
  const image = locateImage();
  console.log(`包目录: ${pkg}${owned ? '（由 ' + path.basename(input) + ' 解压）' : ''}`);
  console.log(`测试图片: ${image || '（未找到）'}`);
  console.log('='.repeat(72));

  let work = null;
  try {
    const isAgent = fs.existsSync(path.join(pkg, 'web/mcp/server.bundle.cjs'));
    const isOffline = fs.existsSync(path.join(pkg, 'EditHere.html'));
    check('包结构可识别（offline 或 web-agent）', isAgent || isOffline,
          isAgent ? 'web-agent' : isOffline ? 'offline' : '既不是 EditHere-web.zip 也不是 EditHere-web-agent.zip');
    if (!isAgent && !isOffline) throw new Error('无法识别包类型');
    const stamp = checkCommon(pkg);
    if (isAgent) {
      const installed = checkAgent(pkg, stamp);
      work = installed.work;
      await checkMcp(pkg, work, image);
    } else {
      checkOffline(pkg);
      skip('MCP stdio 链路（离线包不含 MCP）');
    }
  } catch (error) {
    check('校验流程执行完成', false, error && error.message ? error.message : String(error));
  }

  console.log('='.repeat(72));
  const failed = results.filter((entry) => !entry.ok);
  const skipped = results.filter((entry) => entry.skipped).length;
  console.log(`结果：${results.length - failed.length}/${results.length} 通过`
    + (skipped ? `，跳过 ${skipped}` : '')
    + (failed.length ? '，失败：' + failed.map((entry) => entry.name).join('；') : ''));
  if (work) fs.rmSync(work, { recursive: true, force: true });
  if (owned) fs.rmSync(owned, { recursive: true, force: true });
  process.exitCode = failed.length ? 1 : 0;
})().catch((error) => { console.error(error); process.exitCode = 1; });
