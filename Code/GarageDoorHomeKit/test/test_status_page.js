const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const page = fs.readFileSync('include/StatusPage.h', 'utf8');
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = {};
function element(id) {
  if (!elements[id]) {
    elements[id] = {
      textContent: '', className: '', innerHTML: '', disabled: false,
      scrollTop: 0, clientHeight: 0, scrollHeight: 0, childNodes: [],
      appendChild(child) { this.childNodes.push(child); },
      removeChild() { this.childNodes.shift(); },
    };
  }
  return elements[id];
}

let state = {
  doorClosed: false, doorState: 'Stopped', doorMoving: false, flashMoving: false,
  obstruction: false,
  controlEnabled: true, controlPending: false, lastChangeAgoS: -1, doorChanges: 0, uptimeMs: 1000,
  epoch: 0, wifi: true, ssid: 'test', rssi: -60, deviceName: 'garagecontroller', ip: '192.0.2.1',
  commissioned: true, bootCount: 1, resetReason: 'test', homekitSetupCode: 'setup-code-must-stay-private',
  freeHeap: 1024, minFreeHeap: 1024, firmware: 'test',
};
let controlError = false;
const context = {
  document: { getElementById: element, createElement: () => ({}) },
  fetch: async (url, options) => {
    if (url === '/api/door/control') {
      if (controlError) return { ok: false, text: async () => 'Save failed' };
      state = { ...state, controlEnabled: options.body === 'enable' };
    }
    return {
      ok: true,
      json: async () => url.startsWith('/api/status') ? state : { latest: 0, entries: [] },
    };
  },
  AbortController, setTimeout, clearTimeout, setInterval: () => {},
  console,
};
vm.runInNewContext(script + '\nglobalThis.testStatus = status; globalThis.testOnline = setOnline;', context);

(async () => {
  await context.testStatus();
  context.testOnline(true);
  assert.strictEqual(element('door').textContent, 'Open');
  assert.strictEqual(element('version').textContent, 'Version: test');
  assert.match(element('grid').innerHTML, /Device name<\/div><div class="v">garagecontroller<div class="sub">192\.0\.2\.1<\/div>/);
  assert.doesNotMatch(element('grid').innerHTML, /HomeKit setup code|setup-code-must-stay-private/);
  assert.match(page, /GarageDoor Status/);
  assert.strictEqual(element('control-state').textContent, 'Stopped');
  assert.strictEqual(element('control-state').className, 'badge status-red');
  assert.strictEqual(element('motion').textContent, 'Not moving');
  assert.strictEqual(element('motion').className, 'badge status-green');
  assert.strictEqual(element('press').textContent, 'Close');
  assert.strictEqual(element('press').disabled, false);
  assert.strictEqual(element('control-toggle').textContent, 'Control enabled: Disable');
  assert.strictEqual(element('control-toggle').className, 'control-enabled');

  state = { ...state, deviceName: '<device>', ip: '<ip>' };
  await context.testStatus();
  assert.match(element('grid').innerHTML, /&lt;device&gt;<div class="sub">&lt;ip&gt;<\/div>/);
  state = { ...state, deviceName: 'garagecontroller', ip: '192.0.2.1' };

  await element('control-toggle').onclick();
  assert.strictEqual(element('press').disabled, true);
  assert.strictEqual(element('control-toggle').textContent, 'Control disabled: Enable');
  assert.strictEqual(element('control-toggle').className, 'control-disabled');
  assert.strictEqual(state.controlEnabled, false);
  await element('control-toggle').onclick();
  assert.strictEqual(element('press').disabled, false);
  assert.strictEqual(state.controlEnabled, true);

  controlError = true;
  await element('control-toggle').onclick();
  assert.strictEqual(element('press').disabled, true);
  assert.match(element('control-error').textContent, /Save failed/);
  controlError = false;
  await context.testStatus();
  state = { ...state, controlPending: true };
  await context.testStatus();
  assert.strictEqual(element('press').disabled, true);
  assert.strictEqual(element('control-toggle').disabled, false);
  state = { ...state, controlPending: false };
  await context.testStatus();

  state = { ...state, doorClosed: true, doorState: 'Closed', doorMoving: true };
  await context.testStatus();
  assert.strictEqual(element('door').textContent, 'Closed');
  assert.strictEqual(element('control-state').textContent, 'Closed');
  assert.strictEqual(element('control-state').className, 'badge status-green');
  assert.strictEqual(element('motion').textContent, 'Not moving');
  assert.strictEqual(element('motion').className, 'badge status-green');
  assert.strictEqual(element('press').textContent, 'Stop');

  state = { ...state, doorClosed: false, doorState: 'Closing', flashMoving: true, obstruction: true };
  await context.testStatus();
  assert.strictEqual(element('door').textContent, 'Open');
  assert.strictEqual(element('control-state').textContent, 'Closing');
  assert.strictEqual(element('control-state').className, 'badge status-transit');
  assert.strictEqual(element('motion').textContent, 'Moving');
  assert.strictEqual(element('motion').className, 'badge status-red');
  assert.strictEqual(element('obstruction').textContent, 'Possible obstruction or reversal');
  assert.strictEqual(element('press').textContent, 'Stop');

  state = { ...state, doorClosed: true, doorMoving: false, flashMoving: false };
  await context.testStatus();
  assert.strictEqual(element('press').textContent, 'Open');

  state = { ...state, doorClosed: false, doorState: 'Open', flashMoving: false };
  await context.testStatus();
  assert.strictEqual(element('control-state').textContent, 'Open');
  assert.strictEqual(element('control-state').className, 'badge status-blue');
  assert.strictEqual(element('motion').textContent, 'Not moving');
  assert.strictEqual(element('motion').className, 'badge status-green');

  state = { ...state, doorState: 'Opening', flashMoving: false };
  await context.testStatus();
  assert.strictEqual(element('motion').textContent, 'Not moving');
  assert.strictEqual(element('motion').className, 'badge status-green');

  state = { ...state, doorState: 'Opening', flashMoving: true };
  await context.testStatus();
  assert.strictEqual(element('motion').textContent, 'Moving');
  assert.strictEqual(element('motion').className, 'badge status-red');

  for (const doorState of ['Opening', 'Moving']) {
    state = { ...state, doorState };
    await context.testStatus();
    assert.strictEqual(element('control-state').textContent, doorState);
    assert.strictEqual(element('control-state').className, 'badge status-transit');
    assert.strictEqual(element('motion').textContent, 'Moving');
    assert.strictEqual(element('motion').className, 'badge status-red');
  }
  console.log('Status page tests passed');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
