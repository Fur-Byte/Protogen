// Exercise the actual editor handlers. Browser walkthrough evidence is recorded
// separately in docs/validation/P6_BROWSER_WALKTHROUGH.md.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const root = path.resolve(__dirname, '../..');
const sample = JSON.parse(fs.readFileSync(path.join(root, 'tools/helmet/timelines/three-channel.json')));
const elements = new Map();
const calls = [];
const downloads = [];

function element(id) {
  if (!elements.has(id)) {
    elements.set(id, {
      value: '', checked: false, disabled: false, children: [], listeners: {},
      append(child) { this.children.push(child); },
      replaceChildren(...children) { this.children = children; },
      setAttribute() {},
      addEventListener(name, handler) { this.listeners[name] = handler; },
      click() { if (this.href) downloads.push(this.download); return this.onclick?.(); },
      getContext() { return {fillRect() {}}; },
    });
  }
  return elements.get(id);
}

const context = vm.createContext({
  document: {getElementById: element, createElement: () => element(Symbol())},
  fetch: async (url, options) => {
    const body = options ? JSON.parse(options.body) : undefined;
    calls.push({url, body});
    if (url.endsWith('/sample')) return {ok:true, json:async () => sample};
    if (url.endsWith('/compile')) {
      return {ok:true, json:async () => ({document:body.document, asset_bytes:336,
        json:JSON.stringify(body.document), header:'// test response from compiler'})};
    }
    return {ok:true, json:async () => ({
      frames: body.positions_ms.map(at => ({at_ms:at, frame:'00'.repeat(112), hidden:false})),
      progress:{started:body.document.events.length, rejected:0, skipped:0},
      metrics:{seek_ms:1},
    })};
  },
  setTimeout() { return 1; }, clearTimeout() {},
  URL: {createObjectURL() { return 'blob:test'; }, revokeObjectURL() {}},
  Blob, console,
});
const control = name => element('timeline-' + name);
async function flush() {
  for (let i=0; i<5; ++i) await new Promise(resolve => setImmediate(resolve));
}
async function input(name, value) {
  control(name).value = value;
  assert.equal(typeof control(name).oninput, 'function', name + ' must update during editing');
  control(name).oninput({target:control(name)});
  await flush();
}
async function main() {
  vm.runInContext(fs.readFileSync(path.join(root, 'tools/helmet/timeline-editor.js'), 'utf8'), context);
  await flush();
  control('step').click();
  await flush();
  control('add').click();
  await flush();
  for (const [name, value] of [['region','right'], ['from','-1'], ['to','2'], ['priority','5'],
                              ['easing','out'], ['id','p6-browser-walkthrough'], ['name','Browser walkthrough']]) {
    await input(name, value);
  }
  await control('validate').click();
  const compiled = calls.filter(call => call.url.endsWith('/compile')).at(-1).body.document;
  assert.equal(compiled.id, 'p6-browser-walkthrough');
  assert.equal(compiled.name, 'Browser walkthrough');
  assert.deepEqual(compiled.events.at(-1), {
    at_ms:16, duration_ms:160, region:'right', channel:'x', from:-1, to:2, easing:'out', priority:5,
  });
  await control('json').click();
  assert.equal(downloads.at(-1), 'p6-browser-walkthrough.json');
  await control('header').click();
  assert.equal(downloads.at(-1), 'TimelineAsset.h');

  await input('scrub', '5000');
  await input('scrub', '1000');
  await input('scrub', '5000');
  const seeks = calls.filter(call => call.url.endsWith('/seek')).slice(-3);
  assert.deepEqual(seeks.map(call => call.body.positions_ms), [[5000],[1000],[5000]]);

  const html = fs.readFileSync(path.join(root, 'tools/helmet/index.html'), 'utf8');
  assert(html.indexOf('<main') < html.indexOf('id="timeline"'));
  assert(html.indexOf('id="timeline"') < html.indexOf('<footer>'));
  assert(html.indexOf('<footer>') < html.indexOf('</main>'));
  console.log('PASS: timeline creation, input edits, export payloads, backward seek requests, and container placement');
}
main().catch(error => { console.error(error); process.exitCode=1; });
