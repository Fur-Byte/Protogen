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
      attributes: {},
      setAttribute(name, value) { this.attributes[name] = value; },
      addEventListener(name, handler) { this.listeners[name] = handler; },
      click() { if (this.href) downloads.push(this.download); this.listeners.click?.(); return this.onclick?.(); },
      getContext() { return {fillRect() {}}; },
    });
  }
  return elements.get(id);
}

const context = vm.createContext({
  document: {getElementById: element, createElement: tag => Object.assign(element(Symbol()), {tagName:tag.toUpperCase()})},
  fetch: async (url, options) => {
    const body = options ? JSON.parse(options.body) : undefined;
    calls.push({url, body});
    if (url.endsWith('/sample')) return {ok:true, json:async () => sample};
    if (url.endsWith('/compile')) {
      const document = body.document_json === undefined ? body.document : JSON.parse(body.document_json);
      return {ok:true, json:async () => ({document, asset_bytes:336,
        json:JSON.stringify(document), header:'// test response from compiler'})};
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

async function checkGroupedEvents() {
  const event = (region, channel, at_ms, priority) => ({
    region, channel, at_ms, priority, from:0, to:1, duration_ms:16, easing:'linear',
  });
  const events = [
    event('left', 'x', 0, 3),
    event('full', 'squint', 0, 3),
    event('mouth', 'aperture', 0, 3),
    event('right', 'x', 16, 3),
    event('left', 'lid', 32, 3),
    event('left', 'x', 48, 6),
    event('left', 'x', 48, 2),
  ];
  const file = new Blob([JSON.stringify({...sample, id:'group-order', events})]);
  await control('file').onchange({target:{files:[file]}});
  await flush();
  await input('at', '96'); // Move the first source event after both equal-time events.

  const list = control('events');
  assert.deepEqual(list.children.map(node => [node.tagName, node.textContent]), [
    ['STRONG', 'Left eye · Horizontal'],
    ['BUTTON', '48 ms · 0 → 1 · priority 6'],
    ['BUTTON', '48 ms · 0 → 1 · priority 2'],
    ['BUTTON', '96 ms · 0 → 1 · priority 3'],
    ['STRONG', 'Left eye · Lid closure'],
    ['BUTTON', '32 ms · 0 → 1 · priority 3'],
    ['STRONG', 'Right eye · Horizontal'],
    ['BUTTON', '16 ms · 0 → 1 · priority 3'],
    ['STRONG', 'Mouth · Aperture'],
    ['BUTTON', '0 ms · 0 → 1 · priority 3'],
    ['STRONG', 'Full face · Squint'],
    ['BUTTON', '0 ms · 0 → 1 · priority 3'],
  ]);

  const beforeClick = calls.length;
  list.children[2].click(); // Second equal-time button is the last event in the source.
  await flush();
  assert.deepEqual(calls.slice(beforeClick).map(call => [call.url, call.body.positions_ms]),
    [['/api/timeline/seek', [48]]]);
  assert.equal(control('priority').value, 2);
  const buttons = list.children.filter(node => node.tagName === 'BUTTON');
  assert.ok(buttons.every(button => button.type === 'button'));
  assert.deepEqual(buttons.map(button => button.attributes['aria-pressed']),
    ['false', 'true', 'false', 'false', 'false', 'false', 'false']);

  await input('to', '-2');
  await control('json').click();
  assert.equal(downloads.at(-1), 'group-order.json');
  assert.deepEqual(calls.at(-1).body.document.events, [
    {...events[0], at_ms:96}, ...events.slice(1, -1), {...events.at(-1), to:-2},
  ]);
  for (let i = 0; i < events.length; ++i) control('delete').click();
  await flush();
  assert.deepEqual(list.children, [], 'an empty clip has no headings or buttons');
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
  await checkGroupedEvents();
  console.log('PASS: populated groups, stable display order, original-index editing and click-to-seek');
}
main().catch(error => { console.error(error); process.exitCode=1; });
