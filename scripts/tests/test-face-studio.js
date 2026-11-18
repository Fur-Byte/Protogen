// Execute the real inline application. The DOM fixture provides inputs only;
// assertions inspect commands produced by its actual event handlers.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const html=fs.readFileSync(path.join(__dirname,'../../tools/helmet/index.html'),'utf8');
const elements=new Map();
function element(id) {
  if(!elements.has(id))elements.set(id,{id,type:['natural_blinks','idle_motion'].includes(id)?'checkbox':'number',value:'1',checked:false,
    listeners:{},style:{},append(){},setAttribute(){},checkValidity(){return true;},
    addEventListener(type,fn){this.listeners[type]=fn;}});
  return elements.get(id);
}
const token = 'A'.repeat(43);
const requests = [];
const tabStorage = new Map();
const context=vm.createContext({document:{getElementById:element,createElement:()=>element(Symbol()),querySelectorAll:()=>[]},
  location:{hash:'#usb='+token,pathname:'/'}, history:{replaceState(state,title,path){assert.equal(path,'/');context.location.hash='';}},
  sessionStorage:{setItem:(key,value)=>tabStorage.set(key,value),getItem:key=>tabStorage.get(key)},
  fetch:(path,options)=>{requests.push({path,options});return new Promise(()=>{});},setTimeout(){},console});
vm.runInContext(html.split('<script>')[1].split('</script>')[0],context);
vm.runInContext("api('/api/action', {action:'load'})",context);
assert.equal(context.location.hash,'');
assert.equal(vm.runInContext('usbCredential()',context),token,'refresh keeps this tab authorized');
for(const {path,options} of requests){
  assert.equal(path.includes(token),false);
  assert.equal(options.headers.Authorization,'Bearer '+token);
  assert.equal(options.redirect,'error');
}
const metadata=JSON.parse(fs.readFileSync(path.join(__dirname,'../../tools/helmet/config-metadata.json'),'utf8'));
assert.deepEqual(Array.from(vm.runInContext('keys',context)),metadata.fields.map(field=>field.name));
for(const field of metadata.fields){
  const input=html.match(new RegExp('<input id="'+field.name+'"[^>]*>'));
  if(input&&input[0].includes('type="number"')){
    assert.equal(Number(input[0].match(/min="(\d+)"/)[1]),field.minimum,field.name);
    assert.equal(Number(input[0].match(/max="(\d+)"/)[1]),field.maximum,field.name);
  }else{
    assert.match(html,new RegExp('id="'+field.name+'"'),field.name);
  }
}
vm.runInContext(`state={config:Object.fromEntries(keys.map(key=>[key,1]))};
  state.config.brightness=15;fill(state.config);
  action=payload=>{globalThis.captured=payload};
  $('mouth_mode').value='0';$('settings').listeners.input({target:$('mouth_mode')});$('apply').onclick();`,context);
assert.equal(Object.hasOwn(context.captured.config,'brightness'),false,'editing another control must not reapprove a stored high brightness');
vm.runInContext(`$('settings').listeners.input({target:$('brightness')});$('apply').onclick();`,context);
assert.equal(context.captured.config.brightness,15,'explicit brightness edit, including the same value, authorizes that request');
vm.runInContext(`useProfile({schema:2,config:{mouth_mode:1}});$('apply').onclick();`,context);
assert.equal(Object.hasOwn(context.captured.config,'brightness'),false,'a profile without brightness must preserve approval');
vm.runInContext(`useProfile({schema:2,config:{brightness:3}});$('apply').onclick();`,context);
assert.equal(context.captured.config.brightness,3,'selecting a profile containing brightness is an explicit request');
vm.runInContext(`globalThis.exported=collect();`,context);
assert.equal(context.exported.brightness,3,'profile exports must keep the requested value');
console.log('PASS: Face Studio unrelated edits preserve brightness approval; explicit edits/profiles authorize brightness');
