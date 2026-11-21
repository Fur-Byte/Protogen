// Execute the application script. The DOM fixture provides inputs only;
// assertions inspect commands produced by its actual event handlers.
const assert=require('node:assert/strict');
const test=require('node:test');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const html=fs.readFileSync(path.join(__dirname,'../../tools/helmet/index.html'),'utf8');
const studio=fs.readFileSync(path.join(__dirname,'../../tools/helmet/studio.js'),'utf8');
const token = 'A'.repeat(43);
function openStudio() {
  const elements = new Map(), requests = [], timers = [], tabStorage = new Map();
  function element(id) {
    if(!elements.has(id))elements.set(id,{id,type:['natural_blinks','idle_motion'].includes(id)?'checkbox':'number',value:'1',checked:false,
      listeners:{},style:{},attributes:{},append(){},setAttribute(name,value){this.attributes[name]=value;},checkValidity(){return true;},
      addEventListener(type,fn){this.listeners[type]=fn;}});
    return elements.get(id);
  }
  let draws = 0;
  element('face').getContext = () => ({clearRect(){draws++;},strokeRect(){},beginPath(){},arc(){},fill(){}});
  const expressions = () => Array.from(elements.values()).filter(el => el.className === 'expression');
  const context=vm.createContext({document:{getElementById:element,createElement:()=>element(Symbol()),querySelectorAll:selector=>selector === '.expression' ? expressions() : []},
    location:{hash:'#usb='+token,pathname:'/'}, history:{replaceState(state,title,path){assert.equal(path,'/');context.location.hash='';}},
    sessionStorage:{setItem:(key,value)=>tabStorage.set(key,value),getItem:key=>tabStorage.get(key)},
    fetch:(path,options)=>new Promise(resolve=>requests.push({
      path, options, reply(value,ok=true){resolve({ok,json:async()=>value});}
    })),
    setTimeout(fn,ms){timers.push({fn,ms});},console});
  vm.runInContext(studio,context);
  return {context, element, expressions, requests, timers, get draws(){return draws;}};
}
const {context, requests} = openStudio();
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
  const input=html.match(new RegExp('<input\\b[^>]*\\bid="'+field.name+'"[^>]*>'));
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

function snapshot(expression, brightness=1) {
  return {simulation:false, status:{expression, visible:true, sleeping:false, boop:false, features:4, microphone_enabled:true},
    config:{brightness, mouth_mode:1}, display:{frame:'00'.repeat(112)}};
}
const settle = () => new Promise(resolve => setImmediate(resolve));

test('connection, face controls and brightness report follow the accepted state', async () => {
  const {element, expressions, requests} = openStudio();
  const value = snapshot(5, 15);
  value.simulation = true;
  value.config.boop_trigger = 90;
  Object.assign(value.status, {visible:false, sleeping:true, boop:true, boop_reading:90,
    brightness:{configured:15, effective:1, maximum:2, approved:1}});
  requests.find(request => request.path === '/api/state').reply(value);
  await settle();
  assert.equal(element('connection').textContent, 'Desktop simulation');
  assert.equal(element('connection').className, 'tag online');
  assert.equal(element('simulation').hidden, false);
  assert.equal(element('save').textContent, 'Save in simulation');
  assert.equal(element('display-label').textContent, 'Same firmware · simulated hardware');
  assert.equal(element('reaction').textContent, 'Sleeping');
  assert.equal(element('sleep').textContent, 'Wake display');
  assert.equal(element('visor').textContent, 'Show visor');
  assert.equal(element('boop').textContent, 'Release boop');
  assert.deepEqual(expressions().map(el => el.attributes['aria-pressed']),
    ['false', 'false', 'false', 'false', 'false', 'true']);
  assert.match(element('brightness-report').textContent, /Configured 15 · Active 1 · Firmware ceiling 2 · Approved 1\./);

  value.simulation = false;
  value.status.sleeping = false;
  delete value.status.brightness.approved;
  const load = element('load').onclick();
  requests.at(-1).reply(value);
  await load;
  assert.equal(element('connection').textContent, 'USB connected');
  assert.equal(element('simulation').hidden, true);
  assert.equal(element('save').textContent, 'Save to helmet');
  assert.equal(element('display-label').textContent, 'Live helmet frame');
  assert.equal(element('reaction').textContent, 'Visor hidden');
  assert.match(element('brightness-report').textContent, /Firmware ceiling 2\. Set brightness/);
});

test('audio telemetry and controls reflect firmware capabilities and face state', async () => {
  const studio = openStudio();
  const {element, requests} = studio;
  for (const [status, micDisabled, calibrateDisabled] of [
    [{features:0}, true, true],
    [{microphone_enabled:undefined}, true, true],
    [{microphone_enabled:false}, false, true],
    [{sleeping:true}, false, true],
    [{visible:false}, false, true],
    [{boop:true}, false, true],
    [{}, false, false],
  ]) {
    const value = snapshot(0);
    Object.assign(value.status, status);
    const draws = studio.draws;
    const action = element('load').onclick();
    requests.at(-1).reply(value);
    await action;
    assert.equal(element('microphone').disabled, micDisabled, JSON.stringify(status));
    assert.equal(element('calibrate').disabled, calibrateDisabled, JSON.stringify(status));
    assert.equal(studio.draws, draws + 1);
  }
  assert.equal(element('audio-status').textContent, 'Quiet');
  assert.equal(element('brightness-report').textContent, 'This firmware does not report its brightness ceiling.');

  const active = snapshot(0);
  Object.assign(active.status, {features:6, speaking:true, calibrating:true, envelope:300, audio_gate:42,
    audio_drops:3, boop_reading:51, paw_connected:true, sensor_faults:2, spi_hz:4000000});
  const load = element('load').onclick();
  requests.at(-1).reply(active);
  await load;
  assert.equal(element('audio-status').textContent, 'Calibrating…');
  assert.equal(element('microphone').textContent, 'Mute microphone');
  assert.deepEqual([element('envelope').textContent, element('gate').textContent, element('drops').textContent], [300,42,3]);
  assert.equal(element('level').style.width, '100%');
  assert.equal(element('boop-reading').textContent, 'Boop sensor: 51 / 1023');
  assert.equal(element('footer').textContent, 'USB settings · changes stay local · Paw connected · Faults 2 · SPI 4 MHz');
});

test('a successful action draws once and an earlier poll cannot replace it', async () => {
  const studio = openStudio();
  const {element, requests, timers} = studio;
  const poll = requests.find(request => request.path === '/api/state');
  const action = element('defaults').onclick();
  requests.at(-1).reply(snapshot(2, 3));
  await action;
  const actionDraws = studio.draws;
  poll.reply(snapshot(0));
  await settle();

  assert.deepEqual([actionDraws, studio.draws, element('reaction').textContent, element('brightness').value],
    [1, 1, 'Angry', 3]);
  assert.equal(element('microphone').disabled, false);
  assert.equal(element('calibrate').disabled, false);
  assert.deepEqual(timers.map(timer => timer.ms), [100]);
  const next = timers.shift().fn();
  requests.at(-1).reply(snapshot(1));
  await next;
  assert.equal(element('reaction').textContent, 'Surprised');
  const failing = timers.shift().fn();
  requests.at(-1).reply({error:'Helmet offline'}, false);
  await failing;
  assert.equal(element('connection').textContent, 'Disconnected');
  assert.equal(element('message').textContent, 'Helmet offline');
});

test('a rejected Apply keeps edits and its error survives a superseded polling error', async () => {
  const {element, requests, timers} = openStudio();
  requests.find(request => request.path === '/api/state').reply(snapshot(0));
  await settle();
  const polling = timers.shift().fn();
  const poll = requests.at(-1);
  element('brightness').value = '4';
  element('settings').listeners.input({target:element('brightness')});
  element('apply').onclick();
  assert.equal(JSON.parse(requests.at(-1).options.body).config.brightness, 4);
  requests.at(-1).reply({error:'Apply rejected'}, false);
  await settle();
  poll.reply({error:'Old poll failed'}, false);
  await polling;
  assert.equal(element('message').textContent, 'Apply rejected');
  assert.equal(element('connection').textContent, 'USB connected');
  assert.equal(element('apply').disabled, false);
  assert.equal(element('settings').disabled, false);

  const fresh = timers.shift().fn();
  requests.at(-1).reply(snapshot(1));
  await fresh;
  assert.equal(element('reaction').textContent, 'Surprised');
  assert.equal(element('brightness').value, '4');
  const count = requests.length;
  element('save').onclick();
  assert.equal(requests.length, count, 'unapplied edits block Save');
  element('apply').onclick();
  assert.equal(JSON.parse(requests.at(-1).options.body).config.brightness, 4);
  requests.at(-1).reply(snapshot(1, 4));
  await settle();
  assert.equal(requests.length, count + 1, 'Apply sends only the settings request');
  element('save').onclick();
  assert.deepEqual(JSON.parse(requests.at(-1).options.body), {action:'save'});
});

test('a preset selected during Apply stays pending for the next explicit Apply', async () => {
  const {element, requests, timers} = openStudio();
  requests.find(request => request.path === '/api/profiles').reply([
    {schema:2, name:'Edited', config:{mouth_mode:0}}
  ]);
  requests.find(request => request.path === '/api/state').reply(snapshot(0));
  await settle();
  const polling = timers.shift().fn();
  const poll = requests.at(-1);
  element('brightness').value = '3';
  element('settings').listeners.input({target:element('brightness')});
  element('apply').onclick();
  const apply = requests.at(-1);
  element('profiles').value = '0';
  element('use-profile').onclick();
  poll.reply(snapshot(2));
  await polling;
  assert.equal(element('reaction').textContent, 'Happy');
  assert.equal(element('settings').disabled, true);
  apply.reply(snapshot(0, 3));
  await settle();
  assert.equal(element('mouth_mode').value, 0);
  assert.match(element('message').textContent, /Profile ready/);
  const count = requests.length;
  element('save').onclick();
  assert.equal(requests.length, count);
  element('apply').onclick();
  assert.deepEqual(JSON.parse(requests.at(-1).options.body), {action:'set', config:{mouth_mode:0}});
});

test('a failed refresh leaves an acknowledged save visible', async () => {
  const {element, requests, timers} = openStudio();
  requests.find(request => request.path === '/api/state').reply(snapshot(0));
  await settle();
  element('save').onclick();
  requests.at(-1).reply({action_acknowledged:true, refresh_error:'frame unavailable'});
  await settle();
  const confirmation = element('message').textContent;
  assert.match(confirmation, /Settings saved on the helmet.*Status refresh failed/);
  const poll = timers.shift().fn();
  requests.at(-1).reply({error:'USB did not respond'}, false);
  await poll;
  assert.equal(element('connection').textContent, 'Disconnected');
  assert.equal(element('message').textContent, confirmation);
});
