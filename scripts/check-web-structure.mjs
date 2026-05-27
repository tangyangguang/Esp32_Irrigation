import fs from 'node:fs';

const web = fs.readFileSync('src/web/IrrigationWeb.cpp', 'utf8');
const main = fs.readFileSync('src/main.cpp', 'utf8');

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

assert(main.includes('Esp32BaseWeb::setDeviceName("首页")'), 'home label should be 首页');
assert(!web.includes('.footerbar'), 'business CSS must not override Esp32Base footer navigation');
assert(!web.includes('addPage("/irrigation/manual"'), 'manual page should not be registered as a separate page');
assert(web.includes('addPage("/irrigation/settings", "灌溉设置"'), 'settings nav should be 灌溉设置');
assert(!web.includes('handleManualPage'), 'manual page handler should be removed after merging into home');
assert(!web.includes('redirectTo("/irrigation/manual")'), 'manual actions should return to home');
assert(!web.includes('<h2>系统事件</h2>'), 'records page should not render system events');
assert(!web.includes('/api/v1/maintenance/factory-reset'), 'business web should not expose maintenance factory reset route');
assert(!web.includes('writeRecentPanel("昨日"'), 'recent plans should not render yesterday as a separate panel');
assert(!web.includes('writeRecentPanel("今日"'), 'recent plans should use a single table instead of per-day panels');
assert(web.includes('<th>日期</th><th>时间</th><th>计划</th>'), 'recent plans should render a single date/time table');
