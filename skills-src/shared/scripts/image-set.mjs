#!/usr/bin/env node
import {createRequire} from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
const {VERSION, readManifest, writeManifest, upsertPage,bundledProject} = createRequire(import.meta.url)('./image-set.cjs');
const [command, file, ...args] = process.argv.slice(2);
try {
  if (!command || command === '--help') {
    console.log('EditHere project image sets\n  node scripts/image-set.mjs create <set.json> --name <project>\n  node scripts/image-set.mjs upsert <set.json> --id <page-id> --image <screenshot> [--name <page-name>]\n  node scripts/image-set.mjs remove <set.json> --id <page-id>\n  node scripts/image-set.mjs list <set.json>\n  node scripts/image-set.mjs bundle <set.json> --output <new-web-project.json>\nOpen the manifest with desktop annotate, Web/MCP projectPath, or DSH start --project.');
  } else {
    if (!file) throw new Error('A manifest path is required.');
    const options = {};
    for (let i = 0; i < args.length; i += 2) {
      if (!/^--(?:name|id|image|output)$/.test(args[i]) || !args[i + 1] || options[args[i]] !== undefined) throw new Error('Unknown, duplicate or missing option: ' + args[i]);
      options[args[i]] = args[i + 1];
    }
    let result;
    if (command === 'create') result = writeManifest(file, {schemaVersion:VERSION, name:options['--name'] || path.basename(file), pages:[]}, {create:true});
    else if (command === 'upsert') {
      if (!options['--image']) throw new Error('--image is required.');
      result = upsertPage(file, {id:options['--id'], name:options['--name'], imagePath:options['--image']});
    } else if (command === 'remove') {
      const value = readManifest(file), index = value.pages.findIndex(page => page.id === options['--id']);
      if (index < 0) throw new Error('Page ID not found.');
      value.pages.splice(index, 1); result = writeManifest(file, value);
    } else if(command==='bundle'){
      if(!options['--output'])throw new Error('--output is required for bundle.');
      const output=path.resolve(options['--output']);result=bundledProject(file);
      fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(result),{flag:'wx'});
      result={output,pageCount:result.pages.length};
    } else if (command === 'list') result = readManifest(file);
    else throw new Error('Unknown command: ' + command);
    console.log(JSON.stringify({ok:true, manifest:path.resolve(file), ...result}));
  }
} catch (error) { console.error(JSON.stringify({ok:false, error:error.message})); process.exitCode = 1; }
