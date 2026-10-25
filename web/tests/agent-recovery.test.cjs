'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {AgentSessions} = require('../agent-session.cjs');
const image = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==';

test('CLI reads a completed native receipt from disk with no server credential or live service', () => {
  const parent = path.join(__dirname,'output/agent-recovery');fs.mkdirSync(parent,{recursive:true});
  const directory=fs.mkdtempSync(path.join(parent,'run-'));
  const store=new AgentSessions({directory});
  const session=store.create({image,inputPath:path.join(directory,'source.png'),output:path.join(directory,'feedback.json')});
  const feedback={annotationSpace:'result',image,objects:[{source:null,movements:[],annotations:['启动按钮更醒目']}]};
  const receipt=store.submit(new URL(session.url).searchParams.get('agent'),feedback);
  fs.unlinkSync(path.join(directory,'server-key'));
  const metadata=path.join(directory,session.sessionId+'.json'), before=fs.readFileSync(metadata);
  for(const command of ['poll','wait']){
    const result=JSON.parse(execFileSync(process.execPath,[path.join(__dirname,'../agent.cjs'),command,'--session',session.sessionId,...(command==='wait'?['--wait','0']:[])],{env:{...process.env,EDITHERE_WEB_DATA_DIR:directory},windowsHide:true,encoding:'utf8'}));
    assert.equal(result.state,'completed');assert.equal(result.sha256,receipt.sha256);
    assert.deepEqual(result.feedback,receipt.feedback);
  }
  assert(!fs.existsSync(path.join(directory,'server-key')),'read-only commands must not start a server or rotate credentials');
  assert.deepEqual(fs.readFileSync(metadata),before,'reading receipt does not rewrite stored metadata');
});
