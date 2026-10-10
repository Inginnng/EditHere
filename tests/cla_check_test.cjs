'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { run, loadAgreement, checked, consent } = require('../scripts/cla-check.cjs');

const alice = { id: 10, login: 'alice', type: 'User' };
const bob = { id: 20, login: 'bob', type: 'User' };
const owner = { id: 1, login: 'Inginnng', type: 'User' };
const webFlow = { id: 100, login: 'web-flow', type: 'User' };
const actionBot = { id: 101, login: 'github-actions[bot]', type: 'Bot' };
const checkboxText = '我已阅读并同意 [EditHere 贡献者许可协议 v2](https://github.com/Inginnng/EditHere/blob/main/cla/v2.md)，确认有权提交这些贡献，并授权项目作者按协议进行商业授权及其他许可发布。';
const clone = value => JSON.parse(JSON.stringify(value));
const apiError = status => Object.assign(new Error('Mock API error'), { status });

function fixture(t, options = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-cla-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const config = {
    version: '2', documentPath: 'cla/v2.md', signatureBranch: 'cla-signatures',
    signatureDirectory: 'signatures/v2', statusContext: 'CLA',
    exemptAccounts: ['Inginnng', 'dependabot[bot]', 'github-actions[bot]', 'web-flow'], checkboxText,
  };
  fs.mkdirSync(path.join(root, '.github', 'cla'), { recursive: true });
  fs.mkdirSync(path.join(root, 'cla'));
  fs.writeFileSync(path.join(root, '.github', 'cla', 'config.json'), JSON.stringify(config));
  fs.writeFileSync(path.join(root, 'cla', 'v2.md'), 'Version 2 complete contribution agreement.\n');
  const agreement = loadAgreement(root);
  const pr = {
    id: 201, number: 7, state: 'open', user: clone(options.user || alice), commits: 1,
    head: { sha: 'a'.repeat(40) }, html_url: 'https://github.com/Inginnng/EditHere/pull/7',
    updated_at: '2026-10-10T03:00:00Z', body: `- [x] ${checkboxText}`,
  };
  const state = {
    branch: false, protected: false, defaultBranch: 'main', records: new Map(), statuses: [],
    writes: [], comments: [], branchCreates: [], getCount: 0, commitCalls: 0,
    commits: [{ sha: 'b'.repeat(40), author: clone(pr.user), committer: clone(pr.user) }],
    writeHook: null, getHook: null,
  };
  const github = {
    rest: {
      pulls: {
        async get() {
          state.getCount++;
          if (state.getHook) state.getHook(state.getCount, pr);
          return { data: clone(pr) };
        },
        async listCommits() { throw new Error('Use paginate'); },
      },
      repos: {
        async get() { return { data: { default_branch: state.defaultBranch } }; },
        async getBranch() {
          if (!state.branch) throw apiError(404);
          return { data: { protected: state.protected } };
        },
        async getContent(args) {
          assert.equal(args.ref, 'cla-signatures');
          if (!state.branch || !state.records.has(args.path)) throw apiError(404);
          const value = state.records.get(args.path);
          return { data: { type: 'file', encoding: 'base64', content: Buffer.from(JSON.stringify(value.record)).toString('base64'), sha: value.sha } };
        },
        async createOrUpdateFileContents(args) {
          assert.equal(args.branch, 'cla-signatures');
          state.writes.push(args);
          if (state.writeHook) return state.writeHook(args);
          const existing = state.records.get(args.path);
          if (existing && args.sha !== existing.sha) throw apiError(409);
          if (!existing && args.sha) throw apiError(409);
          state.records.set(args.path, { record: JSON.parse(Buffer.from(args.content, 'base64').toString('utf8')), sha: `file-${state.writes.length}` });
          return { data: {} };
        },
        async createCommitStatus(args) { state.statuses.push(args); return { data: {} }; },
      },
      git: {
        async getRef(args) { assert.equal(args.ref, `heads/${state.defaultBranch}`); return { data: { object: { sha: 'default-head' } } }; },
        async createRef(args) {
          state.branchCreates.push(args);
          assert.equal(args.ref, 'refs/heads/cla-signatures');
          assert.equal(args.sha, 'default-head');
          if (state.branch) throw apiError(422);
          state.branch = true;
          return { data: {} };
        },
      },
      issues: {
        async listComments() { throw new Error('Use paginate'); },
        async createComment(args) { state.comments.push({ id: state.comments.length + 1, user: clone(actionBot), body: args.body }); return { data: {} }; },
        async updateComment(args) { state.comments.find(comment => comment.id === args.comment_id).body = args.body; return { data: {} }; },
      },
    },
    async paginate(method, args) {
      assert.equal(args.per_page, 100);
      if (method === github.rest.pulls.listCommits) { state.commitCalls++; return clone(state.commits); }
      if (method === github.rest.issues.listComments) return clone(state.comments);
      throw new Error('Unexpected paginated endpoint');
    },
  };
  const context = {
    repo: { owner: 'Inginnng', repo: 'EditHere' }, eventName: 'pull_request_target', runId: 5001,
    payload: { action: 'opened', sender: clone(pr.user), pull_request: clone(pr) },
  };
  const core = {
    failed: [], warnings: [], infos: [],
    setFailed(message) { this.failed.push(message); }, warning(message) { this.warnings.push(message); }, info(message) { this.infos.push(message); },
  };
  function sign(user, overrides = {}) {
    state.branch = true;
    const record = {
      schemaVersion: 1, accountId: user.id,
      signatures: [{
        account: { id: user.id, login: user.login },
        agreement: { version: config.version, sha256: agreement.hash, repository: 'Inginnng/EditHere', path: config.documentPath, url: agreement.url, ...overrides },
        pullRequest: { id: pr.id, number: pr.number, url: pr.html_url, headSha: pr.head.sha },
        source: { eventName: 'pull_request_target', action: 'opened', actorId: user.id, eventObjectId: pr.id, eventObjectUrl: pr.html_url },
        signedAt: '2026-10-10T03:00:00Z', acceptedText: checkboxText,
      }],
    };
    state.records.set(`signatures/v2/${user.id}.json`, { record, sha: `old-${user.id}` });
    return record;
  }
  function comment(user, sender = user) {
    context.eventName = 'issue_comment';
    context.payload = {
      action: 'created', sender: clone(sender), issue: { number: pr.number, pull_request: { url: pr.html_url } },
      comment: { id: 900, user: clone(user), body: `- [x] ${checkboxText}`, html_url: `${pr.html_url}#issuecomment-900`, updated_at: pr.updated_at },
    };
  }
  return { root, config, agreement, pr, state, github, context, core, sign, comment,
    execute: () => run({ github, context, core }, root) };
}

test('本人首次勾选创建数字 ID 留档、独立分支和当前 head 的唯一 CLA 状态', async t => {
  const f = fixture(t);
  const result = await f.execute();
  assert.equal(result.state, 'success');
  assert.equal(f.core.failed.length, 0);
  assert.equal(f.state.branchCreates.length, 1);
  assert.equal(f.state.writes.length, 1);
  assert.equal(f.state.writes[0].path, 'signatures/v2/10.json');
  const signature = f.state.records.get('signatures/v2/10.json').record.signatures[0];
  assert.equal(signature.account.id, alice.id);
  assert.equal(signature.agreement.sha256, f.agreement.hash);
  assert.equal(signature.acceptedText, checkboxText);
  assert.equal(signature.source.actorId, alice.id);
  assert.equal(signature.source.workflowRunId, 5001);
  assert.equal(f.state.statuses[0].sha, f.pr.head.sha);
  assert.equal(f.state.statuses[0].context, 'CLA');
  assert.equal(f.state.comments.length, 0);
});

test('维护者替 PR 作者勾选不能创建授权', async t => {
  const f = fixture(t);
  f.context.payload.action = 'edited';
  f.context.payload.changes = { body: { from: '- [ ]' } };
  f.context.payload.sender = owner;
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.writes.length, 0);
  assert.match(f.core.failed.join(''), /@alice/);
  assert.equal(f.state.comments.length, 1);
});

test('作者仅修改标题不能承认已经被他人勾选的正文', async t => {
  const f = fixture(t);
  f.context.payload.action = 'edited';
  f.context.payload.changes = { title: { from: 'Old title' } };
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.writes.length, 0);
});

test('作者本人编辑并提交勾选正文可签署', async t => {
  const f = fixture(t);
  f.context.payload.action = 'edited';
  f.context.payload.changes = { body: { from: `- [ ] ${checkboxText}` } };
  assert.equal((await f.execute()).state, 'success');
});

test('维护者预先代勾后作者只改其他正文，不构成本人勾选授权', async t => {
  const f = fixture(t);
  f.context.payload.action = 'edited';
  f.context.payload.changes = { body: { from: `Original description\n\n- [x] ${checkboxText}` } };
  f.context.payload.pull_request.body = `New description\n\n- [x] ${checkboxText}`;
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.writes.length, 0);
});

test('旧正文 HTML 或围栏隐藏同文已勾行，去掉包装不会误签为新授权', async t => {
  for (const previous of [
    `<div>无关说明</div>\n\n- [x] ${checkboxText}`,
    `\`\`\`markdown\n- [x] ${checkboxText}\n\`\`\``,
    `<!--\n- [x] ${checkboxText}\n-->`,
  ]) {
    const f = fixture(t);
    f.context.payload.action = 'edited';
    f.context.payload.changes = { body: { from: previous } };
    f.context.payload.pull_request.body = `修改后的说明\n\n- [x] ${checkboxText}`;
    assert.equal((await f.execute()).state, 'failure', previous);
    assert.equal(f.state.writes.length, 0);
  }
});

test('编辑事件缺失旧正文字符串，不能创建授权', async t => {
  const f = fixture(t);
  f.context.payload.action = 'edited';
  for (const from of [undefined, null, 1, {}]) {
    f.context.payload.changes = { body: { from } };
    assert.equal((await f.execute()).state, 'failure');
  }
  assert.equal(f.state.writes.length, 0);
});

test('synchronize/reopened/ready_for_review 不从已有勾选推定新授权', async t => {
  for (const action of ['synchronize', 'reopened', 'ready_for_review']) {
    const f = fixture(t);
    f.context.payload.action = action;
    assert.equal((await f.execute()).state, 'failure', action);
    assert.equal(f.state.writes.length, 0);
  }
});

test('相同版本和文档哈希的旧记录可复用，账户改名不要求重新签署', async t => {
  const f = fixture(t);
  f.sign(alice);
  f.pr.user.login = 'alice-renamed';
  f.state.commits[0].author.login = 'alice-renamed';
  f.state.commits[0].committer.login = 'alice-renamed';
  f.context.payload.action = 'synchronize';
  assert.equal((await f.execute()).state, 'success');
  assert.equal(f.state.writes.length, 0);
});

test('版本或文档哈希不同不能复用，重新明确授权会追加历史', async t => {
  const f = fixture(t);
  f.sign(alice, { sha256: '0'.repeat(64), version: 'old' });
  f.context.payload.action = 'synchronize';
  assert.equal((await f.execute()).state, 'failure');
  f.context.payload.action = 'opened';
  assert.equal((await f.execute()).state, 'success');
  assert.equal(f.state.records.get('signatures/v2/10.json').record.signatures.length, 2);
});

test('检查 PR 作者、所有提交作者及提交者，每个账户分别本人同意', async t => {
  const f = fixture(t);
  f.state.commits[0].committer = bob;
  let result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.deepEqual(result.missing.map(user => user.id), [bob.id]);
  f.comment(bob);
  result = await f.execute();
  assert.equal(result.state, 'success');
  const signature = f.state.records.get('signatures/v2/20.json').record.signatures[0];
  assert.equal(signature.source.eventObjectId, 900);
  assert.equal(signature.source.eventObjectUrl, `${f.pr.html_url}#issuecomment-900`);
  assert.equal(f.state.comments.length, 1);
  assert.match(f.state.comments[0].body, /检查通过/);
});

test('维护者修改他人的评论不能代签署，本人编辑评论可以', async t => {
  const f = fixture(t);
  f.sign(alice);
  f.state.commits[0].author = bob;
  f.comment(bob, owner);
  f.context.payload.action = 'edited';
  f.context.payload.changes = { body: { from: '- [ ]' } };
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.writes.length, 0);
  f.context.payload.sender = bob;
  assert.equal((await f.execute()).state, 'success');
});

test('他人预先替共同作者勾选评论，作者随后改其他文字仍不能授权', async t => {
  const f = fixture(t);
  f.sign(alice);
  f.state.commits[0].author = bob;
  f.comment(bob);
  f.context.payload.action = 'edited';
  f.context.payload.changes = { body: { from: `Old text\n- [x] ${checkboxText}` } };
  f.context.payload.comment.body = `New text\n- [x] ${checkboxText}`;
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.writes.length, 0);
});

test('编辑评论去掉旧 HTML 包装但未改变既有已勾行，不创建授权', async t => {
  const f = fixture(t);
  f.sign(alice);
  f.state.commits[0].author = bob;
  f.comment(bob);
  f.context.payload.action = 'edited';
  f.context.payload.changes = { body: { from: `<div>旧说明</div>\n\n- [x] ${checkboxText}` } };
  f.context.payload.comment.body = `新说明\n\n- [x] ${checkboxText}`;
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.writes.length, 0);
});

test('他人评论或仓库管理员确认不能覆盖多个员工账户', async t => {
  const f = fixture(t);
  f.state.commits[0].author = bob;
  f.comment(owner);
  const result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.deepEqual(result.missing.map(user => user.id), [alice.id, bob.id]);
  assert.equal(f.state.writes.length, 0);
});

test('未知或未关联账户的 author/committer 必须阻止通过，输出不含邮箱', async t => {
  const f = fixture(t);
  f.sign(alice);
  f.state.commits[0].author = null;
  f.state.commits[0].commit = { author: { email: 'private@example.com' } };
  f.context.payload.action = 'synchronize';
  const result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.match(result.problems.join(''), /无法对应/);
  assert.doesNotMatch(JSON.stringify([f.core.failed, f.state.comments, f.state.statuses]), /private@example/);
});

test('只豁免明确列出的账户，任意 Bot 不能获得通配豁免', async t => {
  const f = fixture(t, { user: owner });
  f.state.commits[0].committer = webFlow;
  assert.equal((await f.execute()).state, 'success');
  assert.equal(f.state.writes.length, 0);
  f.state.commits[0].committer = { id: 200, login: 'random-bot[bot]', type: 'Bot' };
  const result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.deepEqual(result.missing.map(user => user.id), [200]);
});

test('Co-authored-by 未被 REST 作者列表覆盖时必须人工核验且不泄露邮箱', async t => {
  const f = fixture(t);
  f.sign(alice);
  f.context.payload.action = 'synchronize';
  f.state.commits[0].commit = { message: 'Implement feature\n\nCo-authored-by: Another Author <private-coauthor@example.com>' };
  const result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.equal(result.missing.length, 0);
  assert.match(result.problems.join(''), /Co-authored-by.*逐人核验/);
  assert.doesNotMatch(JSON.stringify([f.core.failed, f.state.comments, f.state.statuses]), /private-coauthor@example/);
});

test('GitHub PR endpoint 超过 250 个提交不能声称全部账户已核验', async t => {
  const f = fixture(t, { user: owner });
  f.pr.commits = 251;
  const result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.equal(f.state.commitCalls, 0);
  assert.match(result.problems.join(''), /250.*人工核验/);
});

test('使用分页接口读取完整提交列表，并核对返回总数', async t => {
  const f = fixture(t, { user: owner });
  f.pr.commits = 201;
  f.state.commits = Array.from({ length: 201 }, (_, i) => ({ sha: `commit-${i}`, author: owner, committer: webFlow }));
  f.state.commits[200].author = bob;
  const result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.deepEqual(result.missing.map(user => user.id), [bob.id]);
  f.state.commits.pop();
  const incomplete = await f.execute();
  assert.match(incomplete.problems.join(''), /不完整/);
});

test('未知留档格式阻止通过且不会自动覆盖', async t => {
  const f = fixture(t);
  f.state.branch = true;
  f.state.records.set('signatures/v2/10.json', { sha: 'unknown', record: { signed: true } });
  const result = await f.execute();
  assert.equal(result.state, 'failure');
  assert.match(result.error, /格式未知/);
  assert.equal(f.state.writes.length, 0);
});

test('SHA 并发冲突先读回，已有相同授权时幂等成功', async t => {
  const f = fixture(t);
  f.state.writeHook = async args => {
    f.state.records.set(args.path, { record: JSON.parse(Buffer.from(args.content, 'base64').toString('utf8')), sha: 'other-run' });
    throw apiError(409);
  };
  assert.equal((await f.execute()).state, 'success');
  assert.equal(f.state.writes.length, 1);
  assert.equal(f.state.records.get('signatures/v2/10.json').record.signatures.length, 1);
});

test('并发写入不同历史时重读 SHA 并保留双方历史', async t => {
  const f = fixture(t);
  let attempts = 0;
  f.state.writeHook = async args => {
    attempts++;
    if (attempts === 1) { f.sign(alice, { version: '1' }); throw apiError(409); }
    assert.equal(args.sha, 'old-10');
    f.state.records.set(args.path, { record: JSON.parse(Buffer.from(args.content, 'base64').toString('utf8')), sha: 'resolved' });
    return { data: {} };
  };
  assert.equal((await f.execute()).state, 'success');
  assert.equal(f.state.records.get('signatures/v2/10.json').record.signatures.length, 2);
});

test('保护分支或默认分支不能接收自动授权留档', async t => {
  const f = fixture(t);
  f.state.branch = true;
  f.state.protected = true;
  assert.match((await f.execute()).error, /保护/);
  f.state.protected = false;
  f.state.defaultBranch = 'cla-signatures';
  assert.match((await f.execute()).error, /默认分支/);
  assert.equal(f.state.writes.length, 0);
});

test('检查时 head 改变，旧作者结果不能给新 head 成功状态', async t => {
  const f = fixture(t);
  f.sign(alice);
  f.context.payload.action = 'synchronize';
  const originalSha = f.pr.head.sha;
  f.state.getHook = (count, pr) => { if (count === 2) pr.head.sha = 'c'.repeat(40); };
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.statuses.at(-1).sha, originalSha);
  assert.equal(f.state.statuses.at(-1).state, 'failure');
});

test('协议加载失败先清除旧绿色状态，再向固定 CLA 上下文发布失败', async t => {
  const f = fixture(t);
  f.state.statuses.push({ sha: f.pr.head.sha, context: 'CLA', state: 'success' });
  fs.unlinkSync(path.join(f.root, 'cla', 'v2.md'));
  assert.equal((await f.execute()).state, 'failure');
  assert.deepEqual(f.state.statuses.map(status => status.state), ['success', 'pending', 'failure']);
  assert.ok(f.state.statuses.every(status => status.context === 'CLA' && status.sha === f.pr.head.sha));
  assert.equal(f.state.writes.length, 0);
  assert.equal(f.core.failed.length, 1);
});

test('配置错误或改写 statusContext 不能把旧 CLA 成功状态遗留下来', async t => {
  const f = fixture(t);
  fs.writeFileSync(path.join(f.root, '.github', 'cla', 'config.json'), JSON.stringify({ ...f.config, statusContext: 'Other' }));
  assert.equal((await f.execute()).state, 'failure');
  assert.deepEqual(f.state.statuses.map(status => status.state), ['pending', 'failure']);
  assert.ok(f.state.statuses.every(status => status.context === 'CLA'));
});

test('首次 PR API 读取失败，利用 GitHub PR 事件的 head 撤销旧成功状态', async t => {
  const f = fixture(t);
  f.github.rest.pulls.get = async () => { throw apiError(503); };
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.statuses.at(-1).state, 'failure');
  assert.equal(f.state.statuses.at(-1).sha, f.context.payload.pull_request.head.sha);
  assert.equal(f.state.statuses.at(-1).context, 'CLA');
});

test('评论事件 API 读取失败无 head 时不能猜测状态，工作流失败并明确限制', async t => {
  const f = fixture(t);
  f.comment(alice);
  f.github.rest.pulls.get = async () => { throw apiError(503); };
  assert.equal((await f.execute()).state, 'failure');
  assert.equal(f.state.statuses.length, 0);
  assert.equal(f.core.failed.length, 1);
  assert.match(f.core.warnings.join(''), /不能撤销.*已有的 CLA 状态/);
});

test('重复未签署事件只更新同一条机器人提示；用户复制标记不能劫持提示', async t => {
  const f = fixture(t);
  f.context.payload.pull_request.body = `- [ ] ${checkboxText}`;
  f.state.comments.push({ id: 111, user: alice, body: '<!-- edithere-cla-check:v2 -->\nUser text' });
  await f.execute();
  await f.execute();
  assert.equal(f.state.comments.length, 2);
  assert.equal(f.state.comments[0].body, '<!-- edithere-cla-check:v2 -->\nUser text');
});

test('普通 issue 评论及已关闭 PR 不写状态、不写授权', async t => {
  const f = fixture(t);
  f.context.eventName = 'issue_comment';
  f.context.payload = { issue: { number: 7 }, action: 'created' };
  assert.equal((await f.execute()).state, 'skipped');
  assert.equal(f.state.getCount, 0);
  f.comment(alice);
  f.pr.state = 'closed';
  assert.equal((await f.execute()).state, 'skipped');
  assert.equal(f.state.writes.length, 0);
  assert.equal(f.state.statuses.length, 0);
});

test('只认可固定完整文句，不认可默认未勾、引用、代码示例和 HTML 注释', () => {
  assert.equal(checked(`- [x] ${checkboxText}`, checkboxText), true);
  assert.equal(checked(`- [X] ${checkboxText}`, checkboxText), true);
  for (const body of [
    `- [ ] ${checkboxText}`, `> - [x] ${checkboxText}`, `    - [x] ${checkboxText}`,
    `\`\`\`md\n- [x] ${checkboxText}\n\`\`\``, `~~~md\n- [x] ${checkboxText}\n~~~`,
    `\`\`\`md\n\`\`\`stillcode\n- [x] ${checkboxText}\n\`\`\``,
    `\`\`\`md\n    \`\`\`\n- [x] ${checkboxText}\n\`\`\``,
    `<pre>\n- [x] ${checkboxText}\n</pre>`, `<div>\n- [x] ${checkboxText}\n</div>`,
    `<pre\n- [x] ${checkboxText}`,
    `<!-- - [x] ${checkboxText} -->`, `<!--\n- [x] ${checkboxText}`, `- [x] ${checkboxText.replace('/main/', '/fork/')}`,
  ]) assert.equal(checked(body, checkboxText), false, body);
});

test('授权依据事件快照；后来其他账户替作者勾选的当前正文不算本人确认', async t => {
  const f = fixture(t);
  f.context.payload.pull_request.body = `- [ ] ${checkboxText}`;
  assert.equal(consent(f.context, f.pr, f.agreement), null);
  assert.equal((await f.execute()).state, 'failure');
});
