// Run from any directory: node Scripts/Test-ClaudeSettings.cjs
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const html = fs.readFileSync(path.join(__dirname, '../Resources/UI/settings_ui.html'), 'utf8');
for (const match of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)) {
  new vm.Script(match[1]);
}

// An old remote catalog must not hide bundled current models; new remote IDs survive.
const calls = {};
const context = vm.createContext({ _fillSelect: (id, models) => { calls[id] = models; } });
const start = html.indexOf('function populateModelDropdowns(');
const end = html.indexOf('function _fillSelect(', start);
assert(start >= 0 && end > start);
vm.runInContext(html.slice(start, end), context);
context.populateModelDropdowns({ claude: ['claude-sonnet-4-6', 'claude-sonnet-5-5', 'future-model'] });
assert.equal(calls['claude-model'][0], 'claude-sonnet-5-5');
assert.equal(calls['claude-model'].filter(id => id === 'claude-sonnet-5-5').length, 1);
assert(calls['claude-model'].includes('future-model'));

context.populateModelDropdowns({});
for (const id of ['claude-sonnet-5-5', 'claude-opus-5-5', 'claude-haiku-5-5', 'claude-fable-5-1']) {
  assert(calls['claude-model'].includes(id));
}

const advanced = html.match(/<details id="api-advanced"[^>]*>([\s\S]*?)<\/details>/);
assert(advanced);
assert(!/<details[^>]*\bopen\b/.test(advanced[0]));
assert(advanced[1].includes('id="custom-params"'));
assert(!advanced[1].includes('required'));
console.log('PASS: JavaScript syntax, Claude catalog with stale/missing remote data, optional advanced settings.');
