'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const site='C:/Users/teodor/Desktop/stem/mpy_drivers/examples/stemkids_site';
const origin='https://stemkids.bg',id='vk-rtc-clock',key='items:'+id,projectId='a360fe00-35d2-4a20-9e4c-0e923ab00900';
const out=path.join(site,'qa/rtc-clock-20261019');fs.mkdirSync(out,{recursive:true});
let cookie='',csrf='';
async function api(action,data,query={}) {
  console.log('API '+action);
  const headers={Origin:origin,Cookie:cookie,Connection:'close'};
  if(data!==undefined){headers['Content-Type']='application/json';headers['X-CSRF-Token']=csrf;}
  const r=await fetch(origin+'/api/?'+new URLSearchParams({action,...query}),{headers,method:data===undefined?'GET':'POST',body:data===undefined?undefined:JSON.stringify(data),signal:AbortSignal.timeout(60000)});
  if(r.headers.get('set-cookie'))cookie=r.headers.get('set-cookie').split(';')[0];
  const text=await r.text();if(!r.ok||!text)throw Error(action+': HTTP '+r.status+'; bytes='+text.length);
  const j=JSON.parse(text);if(j.csrf)csrf=j.csrf;return j;
}
async function backup(label) {
  let f;
  if(process.argv.includes('--reuse-backup')){
    const list=await api('admin-backups');
    const published=fs.statSync(path.join(out,'publication.json')).mtimeMs;
    f=list.items.find(x=>Number(x.name.match(/manual-(\d+)-/)?.[1])>published);
    assert(f,'No checkpoint newer than publication; do not reuse an older backup');
  }else f=await api('admin-backup',{});
  const dir=path.join(site,'.backups/databases',new Date().toISOString().replace(/[:.]/g,'-')+'-rtc-'+label);fs.mkdirSync(dir,{recursive:true});
  const file=path.join(dir,f.name);assert.equal(path.dirname(file),dir);
  console.log('BACKUP '+label+' '+f.bytes+' bytes');
  let last=0;
  await require(path.join(site,'tools/download-checkpoint-http.cjs')).download({url:origin+'/api/?'+new URLSearchParams({action:'admin-backup-download',name:f.name}),origin,cookie,file,bytes:f.bytes,progress:p=>{if(Date.now()-last>15000){last=Date.now();console.log('BACKUP '+p.completed+'/'+p.bytes);}}});
  const db=new(require('node:sqlite').DatabaseSync)(file,{readOnly:true});
  try{assert.equal(db.prepare('PRAGMA integrity_check').get().integrity_check,'ok');assert.equal(db.prepare('PRAGMA foreign_key_check').all().length,0);}finally{db.close();}
  fs.writeFileSync(path.join(dir,'manifest.json'),JSON.stringify({label,source:origin,...f,integrity:'ok'},null,2));console.log('BACKUP OK '+dir);
}
function lessonMarkdown(text) {
  const lines=text.split(/\r?\n/),result=[];
  for(let i=0;i<lines.length;i++) {
    if(i+1<lines.length&&/^[=-]{3,}$/.test(lines[i+1])){result.push('## '+lines[i]);i++;continue;}
    if(lines[i].startsWith('.. code-block::')) {
      result.push('```'+lines[i].split('::')[1].trim());i++;
      while(i+1<lines.length&&(lines[i+1].startsWith('    ')||lines[i+1]==='')){i++;result.push(lines[i].replace(/^    /,''));}
      result.push('```');continue;
    }
    result.push(lines[i].replace(/``/g,'`'));
  }
  return result.join('\n');
}
(async()=>{
  await api('status');const c=JSON.parse(fs.readFileSync(path.join(site,'.deploy/access/administrator.json')));await api('login',{email:c.email,password:c.password});
  try {
    if(process.argv.includes('--finish-backup')){await backup('after');return;}
    let existing;
    try{existing=await api('catalog-view',undefined,{key});}catch(e){if(!e.message.includes('HTTP 404'))throw e;}
    if(existing&&!process.argv.includes('--verify'))throw Error('Record already exists; use --verify, do not overwrite');
    const py=fs.readFileSync(path.join(__dirname,'21_rtc_clock.py'));
    const rst=fs.readFileSync(path.join(__dirname,'21_rtc_clock_bg.rst'));
    const files=[{kind:'code',name:'21_rtc_clock.py',path:'21_rtc_clock.py',mime:'text/x-python',data:py.toString('base64'),size:py.length,caption:'RTC: сверяване и четене с MicroPython'},{kind:'document',name:'RTC_VK_BG.txt',path:'RTC_VK_BG.txt',mime:'text/plain',data:rst.toString('base64'),size:rst.length,caption:'Урок и експеримент с резервно захранване'}];
    if(!process.argv.includes('--verify')) {
      await backup('before');
      const current=await api('team-project',undefined,{id:projectId});const p=current.archive.project;
      assert.equal(p.title,'STEMKIDS · Сайт и платформа');
      fs.writeFileSync(path.join(out,'project-before.json'),JSON.stringify(current,null,2));
      const taskId='T'+(Math.max(...p.lifecycle.tasks.map(t=>Number(t.id.replace(/^T/,''))||0))+1);
      p.lifecycle.tasks.push({id:taskId,title:'Публикувай учебен пример RTC за VK платки',details:'Еднократно сверяване, четене през секунда, VBATT и петминутен опит. Без промени по порта и без флашване.',status:'doing',assignee:'',due:'',requirements:[],dependsOn:[],files:[],evidence:''});
      await api('team-save',{id:projectId,revision:current.revision,project:p,target:current.archive.target});
      const record=require(path.join(site,'catalog-model.js')).normalize({type:'items',id,data:{title:'RTC на VK платки: сверяване и запазване на часа',summary:'Прост MicroPython часовник с отделни функции, кварц 32.768 kHz и опит с пет минути без основно захранване.',target:'VK_RA4M2; други VK RA платки със съвместим RTC фърмуер',kind:'example',boards:['m2'],catalogBoards:['m2'],env:['micropython'],libraries:['machine'],contentMode:'replacement',status:'Пример'},body:lessonMarkdown(rst.toString('utf8'))+'\n\n## Пълен код\n\n```python\n'+py.toString('utf8')+'\n```',files,originalUrl:'',archived:false});
      fs.writeFileSync(path.join(out,'publication.json'),JSON.stringify({key,taskId,record},null,2));
      const saved=await api('catalog-save',{key,revision:0,record});console.log('PUBLISHED '+key+' revision='+saved.revision);
    }
    const live=await api('catalog-record',undefined,{key});
    for(const f of files){const got=live.record.files.find(x=>x.name===f.name);assert(got);assert.equal(got.data,f.data);}
    const publicResponse=await fetch(origin+'/api/?'+new URLSearchParams({action:'catalog-index',compact:'1'}));assert(publicResponse.ok);const publicIndex=await publicResponse.json();assert(publicIndex.items.some(x=>x.key===key));
    const publication=JSON.parse(fs.readFileSync(path.join(out,'publication.json')));
    const current=await api('team-project',undefined,{id:projectId});const p=current.archive.project,t=p.lifecycle.tasks.find(x=>x.id===publication.taskId);assert(t);
    t.status='review';t.evidence='Публикуван учебен пример: '+origin+'/index.html#content/items/'+id+'. Каталогът е видим като гост; Python файлът и урокът са прочетени след вход и съвпадат байт по байт с авторските файлове. Синтаксисът е проверен. Приемането от собственика остава отделно.';
    await api('team-save',{id:projectId,revision:current.revision,project:p,target:current.archive.target});
    await backup('after');
    fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({url:origin+'/index.html#content/items/'+id,taskId:t.id,revision:live.revision,bytesVerified:true,publicIndex:true,sha256:crypto.createHash('sha256').update(py).digest('hex')},null,2));
    console.log('VERIFIED '+origin+'/index.html#content/items/'+id);
  } finally {await api('logout',{});}
})().catch(e=>{console.error(e.message);process.exitCode=1;});
