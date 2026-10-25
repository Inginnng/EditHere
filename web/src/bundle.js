(function(root,factory){'use strict';const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;if(root)root.EditHereBundle=api;})(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const table=Uint32Array.from({length:256},(_,n)=>{for(let bit=0;bit<8;bit++)n=n&1?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
  function crc32(bytes){let crc=0xffffffff;for(const byte of bytes)crc=table[(crc^byte)&255]^(crc>>>8);return(crc^0xffffffff)>>>0;}
  function zip(files){
    const entries=[],parts=[],encoder=new TextEncoder();let offset=0;
    for(const[name,data]of files){const filename=encoder.encode(name),bytes=typeof data==='string'?encoder.encode(data):data;if(!filename.length||filename.length>65535||bytes.length>160*1024*1024)throw new Error('Feedback bundle entry is too large.');const header=new Uint8Array(30+filename.length),view=new DataView(header.buffer),crc=crc32(bytes);view.setUint32(0,0x04034b50,true);view.setUint16(4,20,true);view.setUint16(6,0x800,true);view.setUint32(14,crc,true);view.setUint32(18,bytes.length,true);view.setUint32(22,bytes.length,true);view.setUint16(26,filename.length,true);header.set(filename,30);parts.push(header,bytes);entries.push({filename,bytes,crc,offset});offset+=header.length+bytes.length;}
    const directoryOffset=offset;
    for(const entry of entries){const header=new Uint8Array(46+entry.filename.length),view=new DataView(header.buffer);view.setUint32(0,0x02014b50,true);view.setUint16(4,20,true);view.setUint16(6,20,true);view.setUint16(8,0x800,true);view.setUint32(16,entry.crc,true);view.setUint32(20,entry.bytes.length,true);view.setUint32(24,entry.bytes.length,true);view.setUint16(28,entry.filename.length,true);view.setUint32(42,entry.offset,true);header.set(entry.filename,46);parts.push(header);offset+=header.length;}
    const end=new Uint8Array(22),view=new DataView(end.buffer);view.setUint32(0,0x06054b50,true);view.setUint16(8,entries.length,true);view.setUint16(10,entries.length,true);view.setUint32(12,offset-directoryOffset,true);view.setUint32(16,directoryOffset,true);parts.push(end);return parts;
  }
  // Only uncompressed archives produced by this editor are accepted. No path
  // is ever resolved or fetched: names are matched to user-supplied files.
  function unzip(bytes){
    if(bytes.length>160*1024*1024)throw new Error('Feedback bundle is too large.');const files=new Map(),view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),decoder=new TextDecoder();let offset=0;
    while(offset+4<=bytes.length&&view.getUint32(offset,true)===0x04034b50){if(offset+30>bytes.length)throw new Error('Invalid feedback ZIP.');const flags=view.getUint16(offset+6,true),method=view.getUint16(offset+8,true),crc=view.getUint32(offset+14,true),length=view.getUint32(offset+18,true),original=view.getUint32(offset+22,true),nameLength=view.getUint16(offset+26,true),extra=view.getUint16(offset+28,true),start=offset+30+nameLength+extra;if(flags&~0x800||method!==0||length!==original||start+length>bytes.length)throw new Error('Open a ZIP downloaded by EditHere, or extract and select its JSON and images.');const name=decoder.decode(bytes.subarray(offset+30,offset+30+nameLength));if(!name||/[\\/\u0000]/.test(name)||files.has(name))throw new Error('Invalid or duplicate feedback ZIP filename.');const data=bytes.slice(start,start+length);if(crc32(data)!==crc)throw new Error('Feedback ZIP checksum does not match.');files.set(name,data);offset=start+length;}
    if(!files.size||offset+4>bytes.length||view.getUint32(offset,true)!==0x02014b50)throw new Error('Invalid feedback ZIP.');return files;
  }
  return{zip,unzip,crc32};
});
