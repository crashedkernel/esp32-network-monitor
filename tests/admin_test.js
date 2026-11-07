const assert = require('node:assert/strict'),
  fs = require('node:fs'),
  vm = require('node:vm'),
  path = require('node:path');
class El {
  constructor() {
    this.value = '';
    this.children = [];
    this.listeners = {};
    this.disabled = false;
    this.hidden = false;
    this.textContent = ''
  }
  addEventListener(k, f) {
    this.listeners[k] = f
  }
  appendChild(c) {
    this.children.push(c)
  }
}
module.exports = (async () => {
  const els = {},
    get = id => els[id] ??= new El(),
    custom = new El(),
    port = new El();
  for (const [id, value] of Object.entries({
      mode: 'subnet',
      timeZone: 'America/New_York',
      timeFormat: '24',
      timeServer: 'time.nist.gov',
      resetConfirmation: ''
    })) get(id).value = value;
  const document = {
    getElementById: get,
    querySelector: q => q.includes('customTimeServer') ? custom : port,
    createElement: () => new El()
  };
  const context = vm.createContext({
    document,
    Intl,
    Date,
    fetch: async () => ({
      ok: true,
      json: async () => ['UTC', 'Asia/Kuala_Lumpur', 'America/New_York']
    })
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../web/admin.js'), 'utf8'), context);
  await new Promise(r => setImmediate(r));
  assert.equal(els.subnetFields.hidden, false);
  assert.equal(els.rangeFields.hidden, true);
  assert.equal(custom.disabled, true);
  assert.equal(els.timeZone.children.length, 2);
  els.timeServer.value = 'custom';
  els.timeServer.listeners.change();
  assert.equal(custom.disabled, false);
  assert.equal(custom.required, true);
  els.transport.value = 'ssh';
  els.transport.listeners.change.call(els.transport);
  assert.equal(port.value, 22);
  els.resetConfirmation.value = 'reset';
  els.resetConfirmation.listeners.input();
  assert.equal(els.resetButton.disabled, true);
  let prevented = false;
  els.resetForm.listeners.submit({
    preventDefault() {
      prevented = true
    }
  });
  assert.equal(prevented, true);
  els.resetConfirmation.value = 'RESET';
  els.resetConfirmation.listeners.input();
  assert.equal(els.resetButton.disabled, false);
  console.log('Admin checks passed: timezone loading, time-server fields, address selection and factory reset confirmation.');
})();
