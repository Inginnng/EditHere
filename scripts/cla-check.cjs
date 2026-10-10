'use strict';

// This module is loaded only from the trusted base/default branch checkout.
// Never load a script, configuration file, or agreement from a pull request head.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const COMMENT_MARKER = '<!-- edithere-cla-check:v2 -->';
const MAX_COMMITS = 250; // GitHub's pull-request listCommits endpoint limit.
const STATUS_CONTEXT = 'CLA';

class CheckError extends Error {}
function fail(message) { throw new CheckError(message); }
function accountId(value) { return Number.isSafeInteger(value) && value > 0; }
function safePath(value) {
  return typeof value === 'string' && /^[a-zA-Z0-9_.\/-]+$/.test(value)
    && !value.startsWith('/') && value.split('/').every(part => part && part !== '.' && part !== '..');
}
function loadAgreement(root = path.resolve(__dirname, '..')) {
  const config = JSON.parse(fs.readFileSync(path.join(root, '.github', 'cla', 'config.json'), 'utf8'));
  if (typeof config.version !== 'string' || !config.version
    || !safePath(config.documentPath) || !safePath(config.signatureDirectory)
    || typeof config.signatureBranch !== 'string' || !safePath(config.signatureBranch)
    || config.signatureBranch.includes('..') || config.signatureBranch.endsWith('.lock')
    || config.statusContext !== 'CLA'
    || !Array.isArray(config.exemptAccounts) || config.exemptAccounts.some(login => typeof login !== 'string' || !login)
    || typeof config.checkboxText !== 'string' || !config.checkboxText || /[\r\n]/.test(config.checkboxText)) {
    fail('CLA 配置无效，需由维护者修复可信分支中的配置。');
  }
  const link = config.checkboxText.match(/\]\((https:\/\/github\.com\/[^\s)]+)\)/);
  if (!link) fail('CLA 确认项必须链接到 GitHub 上的固定版本协议。');
  const document = fs.readFileSync(path.join(root, config.documentPath));
  return { config, url: link[1], hash: crypto.createHash('sha256').update(document).digest('hex') };
}

function checked(body, text) {
  if (typeof body !== 'string') return false;
  // Be conservative about raw HTML: a visible-looking Markdown line inside
  // <pre>, <div>, or another HTML element does not represent a rendered checkbox.
  if (/<\/?[a-zA-Z][a-zA-Z0-9:-]*(?:\s[^<>]*|\/?)>/.test(body)
    || /(?:^|\n) {0,3}<\/?[a-zA-Z][a-zA-Z0-9-]*(?=\s|>|\/?>|$)/.test(body)) return false;
  // A quotation, fenced example, or HTML comment is not an affirmative checkbox.
  const clean = body.replace(/<!--[\s\S]*?(?:-->|$)/g, '');
  let fence = null;
  for (const raw of clean.split(/\r?\n/)) {
    const line = raw.trim();
    const boundary = raw.match(/^ {0,3}(`{3,}|~{3,})(.*)$/);
    if (boundary) {
      if (!fence) fence = { char: boundary[1][0], length: boundary[1].length };
      else if (boundary[1][0] === fence.char && boundary[1].length >= fence.length
        && boundary[2].trim() === '') fence = null;
      continue;
    }
    if (!fence && !/^ {4}|\t/.test(raw) && (line === `- [x] ${text}` || line === `- [X] ${text}`)) return true;
  }
  return false;
}

function containsCheckedLine(body, text) {
  // For an edit, even a checked line hidden by HTML or a code fence means the
  // actor did not submit a fresh unconfirmed-to-checked transition. Do not use
  // the rendering filter here; it is intentionally stricter for prior text.
  return typeof body === 'string' && body.split(/\r?\n/).some(raw =>
    raw.trim() === `- [x] ${text}` || raw.trim() === `- [X] ${text}`);
}

function consent(context, pr, agreement) {
  const payload = context.payload;
  const sender = payload.sender;
  if (!sender || !accountId(sender.id)) return null;
  if (payload.action === 'edited') {
    const previous = payload.changes?.body?.from;
    // The signing actor must submit the transition from unconfirmed to checked.
    // Merely editing text after someone else checked the box is not consent.
    if (typeof previous !== 'string' || containsCheckedLine(previous, agreement.config.checkboxText)) return null;
  }
  let source;
  let body;
  let user;
  if (context.eventName === 'pull_request_target' && ['opened', 'edited'].includes(payload.action)) {
    user = payload.pull_request?.user;
    if (sender.id !== pr.user?.id || user?.id !== pr.user?.id) return null;
    body = payload.pull_request.body;
    source = { id: payload.pull_request.id, url: payload.pull_request.html_url, updatedAt: payload.pull_request.updated_at };
  } else if (context.eventName === 'issue_comment' && ['created', 'edited'].includes(payload.action)) {
    user = payload.comment?.user;
    if (sender.id !== user?.id) return null;
    body = payload.comment.body;
    source = { id: payload.comment.id, url: payload.comment.html_url, updatedAt: payload.comment.updated_at };
  } else return null;
  if (!validAccount(user) || user.type !== 'User' || !checked(body, agreement.config.checkboxText)) return null;
  return {
    user,
    source: {
      eventName: context.eventName, action: payload.action,
      actorId: sender.id, eventObjectId: source.id, eventObjectUrl: source.url,
      eventUpdatedAt: source.updatedAt || null,
      workflowRunId: context.runId || null,
      workflowRunAttempt: Number(process.env.GITHUB_RUN_ATTEMPT) || 1,
    },
  };
}

function validAccount(user) {
  return !!user && accountId(user.id) && typeof user.login === 'string'
    && /^[a-zA-Z0-9_-]+(?:\[bot\])?$/.test(user.login) && ['User', 'Bot'].includes(user.type);
}
function contributors(pr, commits, exemptAccounts) {
  const users = new Map();
  const problems = [];
  const exemptions = new Set(exemptAccounts.map(login => login.toLowerCase()));
  function add(user, label) {
    if (!validAccount(user)) { problems.push(`${label}无法对应到可验证的 GitHub 账户`); return; }
    if (!exemptions.has(user.login.toLowerCase())) users.set(user.id, user);
  }
  add(pr.user, 'PR 作者');
  for (const commit of commits) {
    const label = `提交 ${String(commit.sha || '').slice(0, 12)} 的`;
    // REST author/committer fields do not enumerate Co-authored-by identities.
    // Do not guess an account from a trailer email or silently waive consent.
    if (/^\s*co-authored-by\s*:/im.test(commit.commit?.message || '')) {
      problems.push(`提交 ${String(commit.sha || '').slice(0, 12)} 含 Co-authored-by 共同作者声明，需维护者逐人核验并取得授权`);
    }
    add(commit.author, `${label}作者`);
    add(commit.committer, `${label}提交者`);
  }
  return { users: [...users.values()], problems: [...new Set(problems)] };
}

function validRecord(record, id) {
  return !!record && record.schemaVersion === 1 && record.accountId === id
    && Array.isArray(record.signatures) && record.signatures.length > 0
    && record.signatures.every(signature => signature && signature.account?.id === id
      && typeof signature.account.login === 'string'
      && typeof signature.agreement?.version === 'string'
      && /^[a-f0-9]{64}$/.test(signature.agreement?.sha256 || '')
      && typeof signature.agreement.repository === 'string'
      && typeof signature.agreement.path === 'string'
      && typeof signature.agreement.url === 'string'
      && typeof signature.acceptedText === 'string'
      && typeof signature.signedAt === 'string'
      && signature.source?.actorId === id
      && ['pull_request_target', 'issue_comment'].includes(signature.source.eventName)
      && Number.isSafeInteger(signature.pullRequest?.number));
}
function hasSignature(record, user, agreement, repository) {
  return validRecord(record, user.id) && record.signatures.some(signature =>
    signature.agreement.version === agreement.config.version
    && signature.agreement.sha256 === agreement.hash
    && signature.agreement.repository === repository
    && signature.agreement.path === agreement.config.documentPath
    && signature.agreement.url === agreement.url
    && signature.acceptedText === agreement.config.checkboxText);
}
async function readRecord(github, repo, agreement, id) {
  const filePath = `${agreement.config.signatureDirectory}/${id}.json`;
  try {
    const { data } = await github.rest.repos.getContent({ ...repo, path: filePath, ref: agreement.config.signatureBranch });
    if (Array.isArray(data) || data.type !== 'file' || data.encoding !== 'base64' || typeof data.content !== 'string') {
      fail(`账户 ${id} 的 CLA 留档不是有效文件，需由维护者检查。`);
    }
    let record;
    try { record = JSON.parse(Buffer.from(data.content, 'base64').toString('utf8')); }
    catch { fail(`账户 ${id} 的 CLA 留档无法解析，需由维护者检查。`); }
    if (!validRecord(record, id)) fail(`账户 ${id} 的 CLA 留档格式未知，需由维护者检查，不能自动覆盖。`);
    return { record, sha: data.sha, path: filePath };
  } catch (error) {
    if (error.status === 404) return { record: null, sha: undefined, path: filePath };
    throw error;
  }
}
async function ensureBranch(github, repo, name) {
  const { data: repository } = await github.rest.repos.get(repo);
  if (name === repository.default_branch) fail('CLA 留档分支不能使用仓库默认分支。');
  try {
    const { data: branch } = await github.rest.repos.getBranch({ ...repo, branch: name });
    if (branch.protected) fail('CLA 留档分支受到保护，需维护者设置独立的可写留档分支。');
    return;
  } catch (error) { if (error.status !== 404) throw error; }
  const { data: ref } = await github.rest.git.getRef({ ...repo, ref: `heads/${repository.default_branch}` });
  try { await github.rest.git.createRef({ ...repo, ref: `refs/heads/${name}`, sha: ref.object.sha }); }
  catch (error) {
    if (![409, 422].includes(error.status)) throw error;
    // Another run may have created the branch while this run was starting.
    const { data: branch } = await github.rest.repos.getBranch({ ...repo, branch: name });
    if (branch.protected) fail('CLA 留档分支受到保护，无法写入授权记录。');
  }
}
async function saveConsent(github, context, pr, agreement, accepted) {
  const repo = context.repo;
  const repository = `${repo.owner}/${repo.repo}`;
  await ensureBranch(github, repo, agreement.config.signatureBranch);
  const signature = {
    account: { id: accepted.user.id, login: accepted.user.login },
    agreement: { version: agreement.config.version, sha256: agreement.hash, repository, path: agreement.config.documentPath, url: agreement.url },
    pullRequest: { id: pr.id, number: pr.number, url: pr.html_url, headSha: pr.head.sha },
    source: accepted.source,
    signedAt: new Date().toISOString(),
    acceptedText: agreement.config.checkboxText,
  };
  for (let attempt = 0; attempt < 3; attempt++) {
    const existing = await readRecord(github, repo, agreement, accepted.user.id);
    if (hasSignature(existing.record, accepted.user, agreement, repository)) return existing.record;
    const record = existing.record || { schemaVersion: 1, accountId: accepted.user.id, signatures: [] };
    record.signatures.push(signature);
    try {
      await github.rest.repos.createOrUpdateFileContents({
        ...repo, branch: agreement.config.signatureBranch, path: existing.path,
        message: `Record CLA v${agreement.config.version} consent for account ${accepted.user.id}`,
        content: Buffer.from(`${JSON.stringify(record, null, 2)}\n`).toString('base64'),
        ...(existing.sha ? { sha: existing.sha } : {}),
      });
      return record;
    } catch (error) {
      if (![409, 422].includes(error.status)) throw error;
      // Read again before retrying: a concurrent run may already have saved it.
    }
  }
  const latest = await readRecord(github, repo, agreement, accepted.user.id);
  if (hasSignature(latest.record, accepted.user, agreement, repository)) return latest.record;
  fail('CLA 授权留档发生并发冲突，请重新运行检查。');
}
async function updateComment(github, repo, pr, agreement, state, missing, problems) {
  const comments = await github.paginate(github.rest.issues.listComments, { ...repo, issue_number: pr.number, per_page: 100 });
  const existing = comments.find(comment => comment.user?.login === 'github-actions[bot]'
    && comment.user.type === 'Bot' && comment.body?.startsWith(COMMENT_MARKER));
  if (state === 'success' && !existing) return;
  const body = state === 'success'
    ? `${COMMENT_MARKER}\n当前 PR 的全部可验证贡献账户已完成本版本贡献协议确认。CLA 检查通过。`
    : `${COMMENT_MARKER}\nCLA 检查尚未通过。\n\n${[
      missing.length ? `尚需本人确认：${missing.map(user => `@${user.login}`).join('、')}。` : '',
      ...problems,
      missing.length ? 'PR 发起人可勾选 PR 描述中的确认项；其他贡献者可复制下方确认项到自己的 PR 评论，勾选后发布。每个账户对同一协议版本及内容只需确认一次。维护者代勾选无效。' : '',
      missing.length ? `- [ ] ${agreement.config.checkboxText}` : '',
      problems.length ? '无法识别的提交账户、Co-authored-by 共同作者或超出 API 上限的提交，需要维护者先核实贡献者，不能自动豁免。' : '',
    ].filter(Boolean).join('\n\n')}`;
  if (existing && existing.body !== body) {
    await github.rest.issues.updateComment({ ...repo, comment_id: existing.id, body });
  } else if (!existing) await github.rest.issues.createComment({ ...repo, issue_number: pr.number, body });
}
async function publishStatus(github, context, pr, state, description) {
  await github.rest.repos.createCommitStatus({
    ...context.repo, sha: pr.head.sha, state, context: STATUS_CONTEXT,
    description: description.slice(0, 140),
    target_url: context.runId ? `https://github.com/${context.repo.owner}/${context.repo.repo}/actions/runs/${context.runId}` : pr.html_url,
  });
}

async function run({ github, context, core }, trustedRoot = path.resolve(__dirname, '..')) {
  let agreement;
  let pr;
  try {
    const number = context.eventName === 'issue_comment'
      ? (context.payload.issue?.pull_request ? context.payload.issue.number : null)
      : context.payload.pull_request?.number;
    if (!Number.isSafeInteger(number) || number <= 0) return { state: 'skipped' };
    try {
      ({ data: pr } = await github.rest.pulls.get({ ...context.repo, pull_number: number }));
    } catch (error) {
      // PR events provide a trusted GitHub head snapshot even if a fresh API
      // read fails. Comment events do not include a head SHA; never guess one.
      if (context.eventName === 'pull_request_target'
        && /^(?:[a-f0-9]{40}|[a-f0-9]{64})$/.test(context.payload.pull_request?.head?.sha || '')) {
        pr = context.payload.pull_request;
      }
      throw error;
    }
    if (pr.state !== 'open') return { state: 'skipped' };
    // Clear a previously green result before reading configuration or the
    // agreement, so malformed trusted files cannot leave CLA falsely green.
    await publishStatus(github, context, pr, 'pending', '正在核验贡献者授权。');
    // The optional root is for local tests. It must never come from event data.
    agreement = loadAgreement(trustedRoot);
    const accepted = consent(context, pr, agreement);
    const cache = new Map();
    const problems = [];
    let commits = [];
    if (!Number.isSafeInteger(pr.commits) || pr.commits < 0) {
      problems.push('GitHub 未提供有效的提交总数，无法完成贡献者核验。');
    } else if (pr.commits > MAX_COMMITS) {
      problems.push(`此 PR 有 ${pr.commits} 个提交，超过 GitHub API 的 ${MAX_COMMITS} 个上限，需维护者人工核验全部贡献者或拆分 PR。`);
    } else {
      commits = await github.paginate(github.rest.pulls.listCommits, { ...context.repo, pull_number: number, per_page: 100 });
      if (commits.length !== pr.commits) problems.push('GitHub 返回的提交列表不完整，无法确认全部贡献者。');
    }
    const collected = contributors(pr, commits, agreement.config.exemptAccounts);
    problems.push(...collected.problems);
    if (accepted && collected.users.some(user => user.id === accepted.user.id)) {
      cache.set(accepted.user.id, await saveConsent(github, context, pr, agreement, accepted));
    }
    const missing = [];
    const repository = `${context.repo.owner}/${context.repo.repo}`;
    for (const user of collected.users) {
      let record = cache.get(user.id);
      if (!record) ({ record } = await readRecord(github, context.repo, agreement, user.id));
      if (!hasSignature(record, user, agreement, repository)) missing.push(user);
    }
    const { data: current } = await github.rest.pulls.get({ ...context.repo, pull_number: number });
    if (current.head.sha !== pr.head.sha || current.user?.id !== pr.user?.id || current.commits !== pr.commits) {
      // Never apply an earlier author list to a new PR head.
      await publishStatus(github, context, pr, 'failure', 'PR 已有新提交，请等待最新 CLA 检查。');
      core.setFailed('检查过程中 PR 已更新，请等待最新提交的 CLA 检查。');
      return { state: 'failure', missing, problems: ['PR 在检查过程中更新'] };
    }
    const state = missing.length || problems.length ? 'failure' : 'success';
    await publishStatus(github, context, pr, state,
      state === 'success' ? '全部贡献者已确认本版本贡献协议。' : '需贡献者本人确认协议，或维护者核验无法识别的提交。');
    await updateComment(github, context.repo, pr, agreement, state, missing, problems);
    if (state === 'failure') core.setFailed([...problems, missing.length ? `尚需确认贡献协议：${missing.map(user => `@${user.login}`).join('、')}` : ''].filter(Boolean).join('\n'));
    else core.info('CLA 检查通过：全部可验证贡献账户已确认当前协议版本及内容。');
    return { state, missing, problems };
  } catch (error) {
    // Do not print event bodies, raw commit identities, email addresses, or API payloads.
    const message = error instanceof CheckError ? error.message
      : `CLA 检查未完成（${Number.isInteger(error.status) ? `GitHub API ${error.status}` : '配置、文件或 API 读取失败'}），需维护者检查工作流。`;
    if (pr) {
      try { await publishStatus(github, context, pr, 'failure', 'CLA 检查未完成，需维护者检查工作流。'); }
      catch { core.warning('无法发布 CLA 状态；本次工作流将保持失败。'); }
    } else core.warning('无法取得 PR head，不能撤销该 head 上已有的 CLA 状态；本次工作流将保持失败。');
    core.setFailed(message);
    return { state: 'failure', error: message };
  }
}

module.exports = { run, loadAgreement, checked, consent, contributors, validRecord, hasSignature };
