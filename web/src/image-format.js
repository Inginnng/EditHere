(function(root,factory){'use strict';const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;if(root)root.EditHereImage=api;})(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const record=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
  const safeName=value=>typeof value==='string'&&value.length>0&&value.length<=255&&!value.startsWith('.')&&!/[\\/:*?"<>|\u0000-\u001f]/.test(value)&&!/[. ]$/.test(value)&&!/^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(value);
  function sha256(bytes){
    const constants=[0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2];
    const state=[0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19],length=Math.ceil((bytes.length+9)/64)*64,data=new Uint8Array(length),words=new Int32Array(64);
    data.set(bytes);data[bytes.length]=128;const view=new DataView(data.buffer),bits=bytes.length*8;view.setUint32(length-8,Math.floor(bits/4294967296));view.setUint32(length-4,bits>>>0);
    const rotate=(value,count)=>(value>>>count)|(value<<(32-count));
    for(let offset=0;offset<length;offset+=64){for(let i=0;i<16;i++)words[i]=view.getInt32(offset+i*4);for(let i=16;i<64;i++){const a=words[i-15],b=words[i-2];words[i]=(words[i-16]+(rotate(a,7)^rotate(a,18)^(a>>>3))+words[i-7]+(rotate(b,17)^rotate(b,19)^(b>>>10)))|0;}let[a,b,c,d,e,f,g,h]=state;for(let i=0;i<64;i++){const one=(h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+constants[i]+words[i])|0,two=((rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c)))|0;h=g;g=f;f=e;e=(d+one)|0;d=c;c=b;b=a;a=(one+two)|0;}for(const[i,value]of[a,b,c,d,e,f,g,h].entries())state[i]=(state[i]+value)|0;}
    return state.map(value=>(value>>>0).toString(16).padStart(8,'0')).join('');
  }
  function dataBytes(value){
    const match=typeof value==='string'&&/^data:(image\/(?:png|jpeg|webp|gif|bmp));base64,([A-Za-z0-9+/]+={0,2})$/.exec(value);
    if(!match||match[2].length%4||match[2].length>96*1024*1024)throw new Error('Invalid image data URL.');
    const binary=typeof atob==='function'?atob(match[2]):Buffer.from(match[2],'base64').toString('binary'),bytes=Uint8Array.from(binary,c=>c.charCodeAt(0));
    const canonical=typeof btoa==='function'?btoa(binary):Buffer.from(bytes).toString('base64');if(canonical!==match[2])throw new Error('Invalid image base64.');
    return{mimeType:match[1],bytes};
  }
  function dimensions(bytes,mimeType){
    const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);let width,height;
    if(mimeType==='image/png'&&bytes.length>=33&&[137,80,78,71,13,10,26,10].every((value,index)=>bytes[index]===value)&&view.getUint32(12)===0x49484452){width=view.getUint32(16);height=view.getUint32(20);}
    else if(mimeType==='image/jpeg'&&bytes.length>=4&&bytes[0]===255&&bytes[1]===216){let offset=2;while(offset+4<=bytes.length){if(bytes[offset++]!==255)throw new Error('Invalid JPEG marker.');while(bytes[offset]===255)offset++;const marker=bytes[offset++];if(marker===217||marker===218)break;if(marker===1||(marker>=208&&marker<=215))continue;const length=view.getUint16(offset);if(length<2||offset+length>bytes.length)throw new Error('Invalid JPEG segment.');if([192,193,194,195,197,198,199,201,202,203,205,206,207].includes(marker)){if(length<8)throw new Error('Invalid JPEG dimensions.');height=view.getUint16(offset+3);width=view.getUint16(offset+5);break;}offset+=length;}}
    else if(mimeType==='image/gif'&&bytes.length>=10&&String.fromCharCode(...bytes.subarray(0,6)).match(/^GIF8[79]a$/)){width=view.getUint16(6,true);height=view.getUint16(8,true);}
    else if(mimeType==='image/bmp'&&bytes.length>=26&&bytes[0]===66&&bytes[1]===77){if(view.getUint32(14,true)===12){width=view.getUint16(18,true);height=view.getUint16(20,true);}else{width=view.getInt32(18,true);height=Math.abs(view.getInt32(22,true));}}
    else if(mimeType==='image/webp'&&bytes.length>=25&&String.fromCharCode(...bytes.subarray(0,4))==='RIFF'&&String.fromCharCode(...bytes.subarray(8,12))==='WEBP'){
      const chunk=String.fromCharCode(...bytes.subarray(12,16));if(chunk==='VP8X'&&bytes.length>=30){width=1+bytes[24]+(bytes[25]<<8)+(bytes[26]<<16);height=1+bytes[27]+(bytes[28]<<8)+(bytes[29]<<16);}else if(chunk==='VP8L'&&bytes[20]===47){const bits=view.getUint32(21,true);width=1+(bits&16383);height=1+((bits>>>14)&16383);}else if(chunk==='VP8 '&&bytes.length>=30&&bytes[23]===157&&bytes[24]===1&&bytes[25]===42){width=view.getUint16(26,true)&16383;height=view.getUint16(28,true)&16383;}}
    if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||width>32767||height>32767||width*height>32000000)throw new Error('Invalid or unsupported image dimensions.');
    return{width,height};
  }
  function inspect(dataUrl){const{bytes,mimeType}=dataBytes(dataUrl);return{bytes,mimeType,...dimensions(bytes,mimeType),sha256:sha256(bytes)};}
  function descriptor(dataUrl,mode='file',file='feedback.png',options={}){
    if(typeof mode==='boolean')mode=mode?'embedded':'none';if(!['file','embedded','none'].includes(mode))throw new Error('Invalid image mode.');
    const info=inspect(dataUrl),value={mode,mimeType:info.mimeType,width:info.width,height:info.height,devicePixelRatio:options.devicePixelRatio??null,sha256:info.sha256};
    if(mode==='file')value.file=file;if(mode==='embedded')value.dataUrl=dataUrl;if(options.preview)value.preview=options.preview;validate(value);return value;
  }
  function validate(value){
    if(!record(value))throw new Error('Invalid image descriptor.');const fields=['mode','mimeType','width','height','devicePixelRatio','sha256',...(value.mode==='file'?['file']:value.mode==='embedded'?['dataUrl']:[]),...(Object.hasOwn(value,'preview')?['preview']:[])];
    if(Object.keys(value).sort().join(',')!==fields.sort().join(',')||!['file','embedded','none'].includes(value.mode)||!['image/png','image/jpeg','image/webp','image/gif','image/bmp'].includes(value.mimeType)||!Number.isInteger(value.width)||!Number.isInteger(value.height)||value.width<1||value.height<1||value.width>32767||value.height>32767||value.width*value.height>32000000||!(value.devicePixelRatio===null||Number.isFinite(value.devicePixelRatio)&&value.devicePixelRatio>0)||typeof value.sha256!=='string'||!/^[a-f0-9]{64}$/i.test(value.sha256))throw new Error('Invalid image descriptor fields.');
    if(value.mode==='file'&&(!safeName(value.file)||!({'image/png':/\.png$/i,'image/jpeg':/\.jpe?g$/i,'image/webp':/\.webp$/i,'image/gif':/\.gif$/i,'image/bmp':/\.bmp$/i}[value.mimeType]).test(value.file)))throw new Error('Invalid original image filename.');
    if(Object.hasOwn(value,'preview')&&(!safeName(value.preview)||!/^.+\.annotated\.jpg$/i.test(value.preview)))throw new Error('Invalid annotated preview filename.');
    if(value.mode==='embedded')verify(value,value.dataUrl);return value;
  }
  function verify(value,dataUrl){const info=inspect(dataUrl);if(info.mimeType!==value.mimeType||info.width!==value.width||info.height!==value.height||info.sha256!==value.sha256?.toLowerCase())throw new Error('Original image MIME, dimensions or SHA-256 do not match.');return info;}
  function dataUrlOf(value){return typeof value==='string'?value:record(value)&&value.mode==='embedded'?(validate(value),value.dataUrl):undefined;}
  return{safeName,sha256,dataBytes,dimensions,inspect,descriptor,validate,verify,dataUrlOf};
});
