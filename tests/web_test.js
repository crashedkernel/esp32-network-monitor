// Run with a Node-compatible runtime; no browser/network dependencies.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
class El {
  constructor() {
    this.children = [];
    this._text = '';
    this.value = '';
    this.hidden = false;
    this.dataset = {};
    this.attributes = {};
    this.className = '';
    this.listeners = {};
    this.parts = {};
    this.classList = {
      toggle: (name, on) => {
        let a = this.className.split(' ').filter(x => x && x !== name);
        if (on) a.push(name);
        this.className = a.join(' ')
      }
    }
  }
  set textContent(v) {
    this._text = String(v);
    this.children = []
  }
  get textContent() {
    return this._text + this.children.map(v => v.textContent).join('')
  }
  appendChild(v) {
    this.children.push(v);
    return v
  }
  replaceChildren() {
    this.children = [];
    this._text = ''
  }
  setAttribute(k, v) {
    this.attributes[k] = v
  }
  addEventListener(k, fn) {
    this.listeners[k] = fn
  }
  querySelector(k) {
    return this.parts[k] ??= new El()
  }
  focus() {}
  scrollIntoView(options) {
    this.scrolled = options
  }
}
async function run() {
  const root = path.resolve(__dirname, '..'),
    els = {},
    buttons = ['ip', 'name', 'hostname', 'mac', 'status', 'connection'].map(key => {
      let b = new El();
      b.dataset.key = key;
      b.parentElement = new El();
      return b
    }),
    ticks = [],
    links = ['online', 'offline', 'new', 'changed', 'all'].map(filter => {
      const link = new El();
      link.dataset.filter = filter;
      return link
    });
  let now = 0,
    posts = 0;
  const sample = {
    firmware: 'glinet-ui-9',
    timeZone: 'America/New_York',
    timeFormat: '24',
    timeServer: 'time.nist.gov',
    source: 'glinet',
    clientMode: true,
    uptimeSeconds: 90061,
    ssid: 'Home <network>',
    interval: 60,
    scanning: false,
    scanRequested: false,
    completed: true,
    clockSynced: true,
    nowEpoch: 1793912400,
    lastScanEpoch: 1793912380,
    age: 20,
    scanToken: 'token',
    online: 2,
    offline: 1,
    newDevices: 1,
    changedDevices: 1,
    error: '',
    devices: [{
      ip: '192.168.2.100',
      name: 'Living room',
      hostname: 'tv.home.arpa',
      mac: 'AA:BB:CC:00:00:01',
      status: 'ONLINE',
      isNew: true,
      connection: '5G'
    }, {
      ip: '192.168.2.2',
      name: 'Office printer',
      hostname: 'printer.home.arpa',
      mac: 'AA:BB:CC:00:00:02',
      status: 'OFFLINE',
      changed: true,
      connection: 'cable'
    }, {
      ip: '192.168.2.10',
      name: '<script>unsafe</script>',
      mac: 'AA:BB:CC:00:00:03',
      status: 'ONLINE',
      connection: '5G'
    }]
  };
  const doc = {
    getElementById: id => els[id] ??= new El(),
    querySelectorAll: q => q === 'th button' ? buttons : q === '[data-filter]' ? links : [],
    createElement: () => new El(),
    createTextNode: t => {
      let e = new El();
      e.textContent = t;
      return e
    }
  };
  doc.getElementById('statusFilter').value = 'all';
  const ctx = vm.createContext({
    document: doc,
    Intl,
    Date,
    window: {
      matchMedia: () => ({
        matches: false
      })
    },
    performance: {
      now: () => now
    },
    encodeURIComponent,
    fetch: async (url, options) => {
      if (url === '/api/scan') {
        posts++;
        assert.match(options.body, /token=token/);
        sample.scanRequested = true;
        return {
          ok: true,
          json: async () => ({
            status: 'requested'
          })
        }
      }
      return {
        ok: true,
        json: async () => sample
      }
    },
    setInterval: fn => ticks.push(fn)
  });
  vm.runInContext(fs.readFileSync(path.join(root, 'web/dashboard.js'), 'utf8'), ctx);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(els.rows.children[0].children[0].textContent, '192.168.2.2');
  assert.match(els.rows.children[0].children[4].textContent, /OFFLINEChanged/);
  assert.equal(els.rows.children[1].children[1].textContent, '<script>unsafe</script>');
  assert.equal(els.countOffline.textContent, '1');
  assert.match(els.clockStatus.textContent, /Time synchronized/);
  assert.equal(els.currentTime.textContent, '16:00:00');
  assert.equal(els.lastTime.textContent, '15:59:40');
  els.search.value = '00:00:02';
  els.search.listeners.input();
  assert.equal(els.rows.children.length, 1);
  assert.match(els.rows.children[0].textContent, /Office printer/);
  els.search.value = 'printer.home.arpa';
  els.search.listeners.input();
  assert.equal(els.rows.children.length, 1);
  assert.equal(els.rows.children[0].children[2].textContent, 'printer.home.arpa');
  assert.equal(els.rows.children[0].children[2].dataset.label, 'Hostname / DNS');
  els.clearSearch.listeners.click();
  buttons[2].listeners.click();
  assert.equal(els.rows.children[0].children[2].textContent, 'Unavailable');
  assert.equal(els.rows.children[1].children[2].textContent, 'printer.home.arpa');
  buttons[0].listeners.click();
  els.search.value = 'offline cable';
  els.search.listeners.input();
  assert.equal(els.rows.children.length, 1);
  els.search.value = 'Living room';
  els.search.listeners.input();
  assert.equal(els.rows.children.length, 1);
  els.search.value = '5g';
  els.search.listeners.input();
  assert.equal(els.rows.children.length, 2);
  assert.match(els.rows.children[0].textContent, /unsafe/);
  assert.equal(els.rows.children[0].children[5].dataset.label, 'Connection');
  els.clearSearch.listeners.click();
  els.statusFilter.value = 'offline';
  els.statusFilter.listeners.change();
  assert.equal(els.rows.children.length, 1);
  assert.match(els.resultCount.textContent, /1 of 3/);
  els.statusFilter.value = 'all';
  els.statusFilter.listeners.change();
  buttons[0].listeners.click();
  assert.equal(els.rows.children[0].children[0].textContent, '192.168.2.100');
  els.search.value = 'no match';
  els.search.listeners.input();
  assert.match(els.rows.textContent, /No matching devices/);
  els.clearSearch.listeners.click();
  // Summary links clear search and select the requested device group.
  els.search.value = 'does not match';
  links[1].listeners.click({
    preventDefault() {}
  });
  assert.equal(els.search.value, '');
  assert.equal(els.statusFilter.value, 'offline');
  assert.equal(els.rows.children.length, 1);
  assert.equal(links[1].attributes['aria-current'], 'true');
  assert.equal(els['device-inventory'].scrolled.block, 'start');
  links[0].listeners.click({
    preventDefault() {}
  });
  assert.equal(els.rows.children.length, 2);
  links[2].listeners.click({
    preventDefault() {}
  });
  assert.match(els.rows.textContent, /Living room/);
  links[3].listeners.click({
    preventDefault() {}
  });
  assert.match(els.rows.textContent, /Office printer/);
  links[4].listeners.click({
    preventDefault() {}
  });
  assert.equal(els.rows.children.length, 3);
  sample.timeZone = 'Asia/Kuala_Lumpur';
  await ticks[0]();
  assert.equal(els.currentTime.textContent, '05:00:00');
  assert.equal(els.lastTime.textContent, '04:59:40');
  assert.match(els.zoneLabel.textContent, /Asia\/Kuala_Lumpur/);
  sample.timeFormat = '12';
  await ticks[0]();
  assert.match(els.currentTime.textContent, /05:00:00.*AM/);
  sample.timeZone = 'America/New_York';
  sample.timeFormat = '24';
  await ticks[0]();
  assert.equal(els.currentTime.textContent, '16:00:00');
  assert.equal(els.systemUptime.textContent, '1d 01:01:01');
  const oldTime = els.currentTime.textContent;
  now = 2000;
  ticks[1]();
  assert.notEqual(els.currentTime.textContent, oldTime);
  assert.equal(els.systemUptime.textContent, '1d 01:01:03');
  await els.scanNow.listeners.click();
  assert.equal(posts, 1);
  assert.equal(els.scanNow.disabled, true);
  assert.equal(els.scanButtonLabel.textContent, 'Scanning...');
  sample.scanRequested = false;
  sample.error = 'Router read failed';
  await ticks[0]();
  assert.equal(els.error.textContent, 'Router read failed');
  assert.equal(els.countOffline.textContent, '1');
  sample.uptimeSeconds = 7;
  sample.clockSynced = false;
  sample.nowEpoch = 0;
  sample.lastScanEpoch = 0;
  await ticks[0]();
  assert.equal(els.currentTime.textContent, '--:--:--');
  assert.equal(els.systemUptime.textContent, '00:00:07');
  assert.match(els.clockStatus.textContent, /Synchronizing/);
  assert.match(els.note.textContent, /GL.iNet client tracker/);
  const themeSource = fs.readFileSync(path.join(root, 'web/theme.js'), 'utf8'),
    store = new Map();

  function themeContext() {
    const button = new El(),
      smaller = new El(),
      larger = new El(),
      reset = new El(),
      label = new El(),
      events = {};
    smaller.dataset.fontStep = '-1';
    larger.dataset.fontStep = '1';
    const groups = {
      '[data-theme-toggle]': [button],
      '[data-font-step]': [smaller, larger],
      '[data-font-label]': [label],
      '[data-font-reset]': [reset]
    };
    const document = {
      documentElement: {
        dataset: {},
        style: {}
      },
      querySelectorAll: k => groups[k] || [],
      addEventListener: (k, fn) => events[k] = fn
    };
    const context = vm.createContext({
      document,
      window: {
        matchMedia: () => ({
          matches: false
        }),
        addEventListener: (k, fn) => events[k] = fn
      },
      localStorage: {
        getItem: k => store.get(k),
        setItem: (k, v) => store.set(k, v)
      }
    });
    vm.runInContext(themeSource, context);
    events.DOMContentLoaded();
    return {
      document,
      button,
      smaller,
      larger,
      reset,
      label,
      events
    };
  }
  const light = themeContext();
  assert.equal(light.document.documentElement.dataset.theme, 'light');
  light.button.listeners.click();
  assert.equal(store.get('nm-theme'), 'dark');
  light.larger.listeners.click();
  assert.equal(light.label.textContent, '110%');
  assert.equal(light.document.documentElement.style.fontSize, '17.6px');
  assert.equal(store.get('nm-font-size'), '110');
  const admin = themeContext();
  assert.equal(admin.document.documentElement.dataset.theme, 'dark');
  assert.equal(admin.button.attributes['aria-pressed'], 'true');
  assert.equal(admin.label.textContent, '110%');
  admin.button.listeners.click();
  assert.equal(store.get('nm-theme'), 'light');
  for (let i = 0; i < 10; i++) admin.larger.listeners.click();
  assert.equal(admin.label.textContent, '150%');
  assert.equal(admin.larger.disabled, true);
  for (let i = 0; i < 10; i++) admin.smaller.listeners.click();
  assert.equal(admin.label.textContent, '80%');
  assert.equal(admin.smaller.disabled, true);
  admin.reset.listeners.click();
  assert.equal(admin.label.textContent, '100%');
  assert.equal(store.get('nm-font-size'), '100');
  admin.events.storage({
    key: 'nm-font-size',
    newValue: '130'
  });
  assert.equal(admin.label.textContent, '130%');
  store.set('nm-font-size', 'invalid');
  assert.equal(themeContext().label.textContent, '100%');
  const html = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
  assert(html.includes('System uptime') && html.includes('data-key="hostname"'));
  assert(!html.includes('Network visibility') && !html.includes('id="firmware"'));
  assert(!els.note.textContent.includes('does not provide device names'));
  console.log('Web checks passed: all-column search, filters, sorting, safe names, configured 12/24-hour clocks/time zones, summary links, manual scan, errors and saved themes/font sizes.');
}
module.exports = run();
