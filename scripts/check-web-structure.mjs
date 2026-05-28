import fs from 'node:fs';

const web = fs.readFileSync('src/web/IrrigationWeb.cpp', 'utf8');
const main = fs.readFileSync('src/main.cpp', 'utf8');
const pins = fs.readFileSync('include/Pins.h', 'utf8');
const settings = fs.readFileSync('src/storage/SettingsStore.cpp', 'utf8');
const plans = fs.readFileSync('src/storage/PlanStore.cpp', 'utf8');
const planHeader = fs.readFileSync('src/storage/PlanStore.h', 'utf8');
const scheduler = fs.readFileSync('src/domain/WateringPlanScheduler.cpp', 'utf8');
const session = fs.readFileSync('src/domain/WateringSession.h', 'utf8') + fs.readFileSync('src/domain/WateringSession.cpp', 'utf8');
const records = fs.readFileSync('src/storage/RecordStore.h', 'utf8');

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

assert(main.includes('Esp32BaseWeb::setDeviceName("首页")'), 'home label should be 首页');
assert(web.includes('Esp32BaseWeb::setHeadExtraCallback(writeCss)'), 'business CSS should be injected through Esp32Base head callback');
assert(main.includes('Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION)'), 'system navigation should use Esp32Base default compact footer section');
assert(!main.includes('Esp32BaseWeb::setBuiltinLabel'), 'system footer labels should remain Esp32Base defaults');
const pio = fs.readFileSync('platformio.ini', 'utf8');
assert(pio.includes('-D ESP32BASE_ENABLE_APP_CONFIG=1'), 'App Config should be enabled so Esp32Base footer includes App Config');
assert(pio.includes('-D ESP32BASE_APP_CONFIG_MAX_GROUPS=1'), 'App Config group capacity should be explicit');
assert(pio.includes('-D ESP32BASE_APP_CONFIG_MAX_FIELDS=1'), 'App Config field capacity should be explicit');
assert(!web.includes('.footerbar'), 'business CSS must not override Esp32Base footer navigation');
assert(!web.includes('body>nav'), 'business CSS/script must not override Esp32Base navigation');
assert(!web.includes('max-width:none'), 'business CSS must not override Esp32Base body/page width model');
assert(!web.includes('addPage("/irrigation/manual"'), 'manual page should not be registered as a separate page');
assert(web.includes('addPage("/irrigation/settings", "灌溉设置"'), 'settings nav should be 灌溉设置');
assert(!web.includes('addPage("/irrigation/data", "记录"'), 'records nav should be renamed to 历史记录');
assert(!web.includes('handleManualPage'), 'manual page handler should be removed after merging into home');
assert(!web.includes('redirectTo("/irrigation/manual")'), 'manual actions should return to home');
assert(!web.includes('<h2>系统事件</h2>'), 'records page should not render system events');
assert(!web.includes('/api/v1/maintenance/factory-reset'), 'business web should not expose maintenance factory reset route');
assert(!web.includes('writeRecentPanel("昨日"'), 'recent plans should not render yesterday as a separate panel');
assert(!web.includes('writeRecentPanel("今日"'), 'recent plans should use a single table instead of per-day panels');
assert(web.includes('<th>日期</th><th>时间</th><th>计划</th>'), 'recent plans should render a single date/time table');
assert(pins.includes('MaxRoads = 4'), 'hardware model should expose four fixed roads');
assert(pins.includes('DefaultRoadEnabledMask = 0x03'), 'default road mask should enable both roads');
assert(pins.includes('Valve3 = 16') && pins.includes('Valve4 = 27'), 'road 3/4 valve PWM pins should be fixed');
assert(pins.includes('Flow3 = 36') && pins.includes('Flow4 = 39'), 'road 3/4 flow input pins should be fixed');
assert(pins.includes('ValveHoldDutyPercent = 70') && pins.includes('ValvePullInMs = 5000'), 'fixed PWM hold defaults should be documented in pins');
assert(!settings.includes('0x01,\n    SettingsStore::MODE_SIMULTANEOUS'), 'settings defaults should not keep one-road mask');
assert(!settings.includes('MODE_SEQUENTIAL'), 'sequential mode should be removed from settings');
assert(settings.includes('return mask == 0 ? IrrigationPins::DefaultRoadEnabledMask : mask'), 'invalid road mask should fall back to the two-road default mask');
assert(planHeader.includes('MaxPlansPerRoad = 6'), 'plans should be six slots per road');
assert(planHeader.includes('TotalPlans = IrrigationPins::MaxRoads * MaxPlansPerRoad'), 'total plans should derive from road count and per-road slots');
assert(planHeader.includes('roadId') && planHeader.includes('slotIndex'), 'plan items should be bound to a single road slot');
assert(!planHeader.includes('roadSec[2]'), 'plan model should not store two-road durations');
assert(!plans.includes('plan.roadSec'), 'plan storage should not use two-road duration fields');
assert(!web.includes('bool useR1 = true;\n    bool useR2 = false;'), 'manual start API should not hard-code road 1 only by default');
assert(!session.includes('SessionState'), 'watering runtime should be road-level tasks, not a global session state');
assert(session.includes('startRoadTask'), 'watering runtime should expose road-level task start');
assert(records.includes('startSource') && records.includes('stopSource') && records.includes('result'), 'records should store objective start/stop sources and watering result');
assert(scheduler.includes('TotalPlans') && scheduler.includes('startRoadTask'), 'scheduler should iterate per-road plan slots and start one road task');
assert(web.includes('当前告警'), 'home alert panel should use current alert terminology');

const navOrder = [
  'addPage("/irrigation", "首页"',
  'addPage("/irrigation/plans", "近期计划"',
  'addPage("/irrigation/data", "历史记录"',
  'addPage("/irrigation/plan-config", "计划配置"',
  'addPage("/irrigation/settings", "灌溉设置"',
];
let lastIndex = -1;
for (const marker of navOrder) {
  const index = web.indexOf(marker);
  assert(index > lastIndex, `navigation marker missing or out of order: ${marker}`);
  lastIndex = index;
}
