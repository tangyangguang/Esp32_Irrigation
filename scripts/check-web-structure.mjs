import fs from 'node:fs';
import path from 'node:path';

function read(file) {
  return fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '';
}

function exists(file) {
  return fs.existsSync(file);
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

const requiredFiles = [
  'src/domain/ZoneTypes.h',
  'src/domain/ZoneTaskRunner.h',
  'src/domain/ZoneTaskRunner.cpp',
  'src/domain/Zone.h',
  'src/domain/Zone.cpp',
  'src/domain/ZoneManager.h',
  'src/domain/ZoneManager.cpp',
  'src/domain/ZoneScheduler.h',
  'src/domain/ZoneScheduler.cpp',
  'src/domain/BusinessEventLog.h',
  'src/domain/BusinessEventLog.cpp',
  'src/domain/FlowCalibration.h',
  'src/domain/FlowCalibration.cpp',
  'src/domain/PlanExecutionTracker.h',
  'src/domain/PlanExecutionTracker.cpp',
  'src/storage/SystemConfigStore.h',
  'src/storage/SystemConfigStore.cpp',
  'src/storage/ZoneConfigStore.h',
  'src/storage/ZoneConfigStore.cpp',
  'src/storage/ZoneErrorStore.h',
  'src/storage/ZoneErrorStore.cpp',
  'src/storage/PlanStore.h',
  'src/storage/PlanStore.cpp',
  'src/storage/ScheduleSkipStore.h',
  'src/storage/ScheduleSkipStore.cpp',
  'src/storage/RecordStore.h',
  'src/storage/RecordStore.cpp',
  'src/web/IrrigationWeb.h',
  'src/web/IrrigationWeb.cpp',
  'src/app/IrrigationApp.cpp',
];

for (const file of requiredFiles) {
  assert(exists(file), `required new architecture file missing: ${file}`);
}

const forbiddenFiles = [
  'src/domain/WateringSession.h',
  'src/domain/WateringSession.cpp',
  'src/domain/WateringPlanScheduler.h',
  'src/domain/WateringPlanScheduler.cpp',
  'src/domain/LeakMonitor.h',
  'src/domain/LeakMonitor.cpp',
  'src/storage/SettingsStore.h',
  'src/storage/SettingsStore.cpp',
  'src/storage/PlanSkipStore.h',
  'src/storage/PlanSkipStore.cpp',
  'src/storage/PlanResultStore.h',
  'src/storage/PlanResultStore.cpp',
  'src/storage/EventStore.h',
  'src/storage/EventStore.cpp',
];

for (const file of forbiddenFiles) {
  assert(!exists(file), `old business module must be removed: ${file}`);
}

const sourceFiles = [
  ...fs.readdirSync('src/domain').map((name) => path.join('src/domain', name)),
  ...fs.readdirSync('src/storage').map((name) => path.join('src/storage', name)),
  ...fs.readdirSync('src/web').map((name) => path.join('src/web', name)),
  'src/app/IrrigationApp.cpp',
  'src/main.cpp',
].filter((file) => /\.(h|cpp)$/.test(file));

const allSource = sourceFiles.map((file) => read(file)).join('\n');
const zoneTypes = read('src/domain/ZoneTypes.h');
const systemConfig = read('src/storage/SystemConfigStore.h') + read('src/storage/SystemConfigStore.cpp');
const zoneConfig = read('src/storage/ZoneConfigStore.h') + read('src/storage/ZoneConfigStore.cpp');
const zoneError = read('src/storage/ZoneErrorStore.h') + read('src/storage/ZoneErrorStore.cpp');
const plans = read('src/storage/PlanStore.h') + read('src/storage/PlanStore.cpp');
const skips = read('src/storage/ScheduleSkipStore.h') + read('src/storage/ScheduleSkipStore.cpp');
const scheduler = read('src/domain/ZoneScheduler.h') + read('src/domain/ZoneScheduler.cpp');
const flowMeter = read('src/domain/FlowMeter.h') + read('src/domain/FlowMeter.cpp');
const flowCalibration = read('src/domain/FlowCalibration.h') + read('src/domain/FlowCalibration.cpp');
const records = read('src/storage/RecordStore.h') + read('src/storage/RecordStore.cpp');
const businessEvents = read('src/domain/BusinessEventLog.h') + read('src/domain/BusinessEventLog.cpp');
const web = read('src/web/IrrigationWeb.cpp');
const main = read('src/main.cpp');
const pins = read('include/Pins.h');
const pio = read('platformio.ini');
const calibrationDoc = read('docs/road-management/04-flow-calibration.md');
const roadReadme = read('docs/road-management/README.md');

assert(pins.includes('MaxRoads = 4'), 'hardware model should expose four fixed zones');
assert(pins.includes('DefaultRoadEnabledMask = 0x03'), 'default hardware enable mask should enable zone 1 and 2');

assert(zoneTypes.includes('NameMaxBytes = 32'), 'names should support at least 10 Chinese characters');
assert(zoneTypes.includes('enum class ZoneState') && zoneTypes.includes('STARTING') && zoneTypes.includes('ERROR'), 'ZoneState should include the final state machine');
assert(zoneTypes.includes('enum class TaskResult') && zoneTypes.includes('FLOW_START_TIMEOUT') && zoneTypes.includes('FLOW_NO_PULSE_TIMEOUT'), 'TaskResult should distinguish flow start and running pulse timeouts');
assert(zoneTypes.includes('enum class PlanObservationStatus') && zoneTypes.includes('MISSED') && zoneTypes.includes('SKIPPED_CONFIG_INVALID'), 'plan observation statuses should include missed and invalid config outcomes');

assert(systemConfig.includes('maxWateringDurationSec'), 'system config should include max watering duration');
assert(systemConfig.includes('scheduleGraceSec'), 'system config should include schedule grace seconds');
assert(systemConfig.includes('durationPresets'), 'system config should include fixed duration presets');
assert(systemConfig.includes('idleLeakDetectionEnabled'), 'system config should include a user switch for idle leak detection');
assert(systemConfig.includes('calibrationSampleTarget') && systemConfig.includes('calibrationMaxCaptureMin'), 'system config should include calibration sample target and max capture minutes');
assert(systemConfig.includes('calibrationDetailCaptureSec') && systemConfig.includes('calibrationDetailPulseLimit'), 'system config should include calibration detail capture bounds');
assert(systemConfig.includes('config.calibrationSampleTarget = 5;') && systemConfig.includes('config.calibrationDetailCaptureSec = 20;'), 'flow calibration defaults should use 5-sample capacity and 20 seconds of detail capture');
assert(systemConfig.includes('86400'), 'max watering duration upper bound should allow up to 24 hours');
assert(systemConfig.includes('14400'), 'max watering duration default should be 4 hours');
assert(systemConfig.includes('submittedMinutesAsSeconds'), 'App Config should accept manual duration values in minutes and store seconds');
assert(systemConfig.includes('kKeyLeakEnabled') && systemConfig.includes('addBool'), 'App Config should expose idle leak detection as a boolean switch');
assert(systemConfig.includes('"单次最长分钟"') && systemConfig.includes('"手动默认分钟"'), 'App Config duration labels should use minutes for user-facing watering durations');
assert(!systemConfig.includes('"单次最长秒"') && !systemConfig.includes('"手动默认秒"'), 'App Config should not expose user-facing watering duration fields in seconds');
assert(systemConfig.includes('"校准样本容量"') && systemConfig.includes('"明细脉冲上限"'), 'App Config should expose flow calibration settings');

assert(zoneConfig.includes('ZoneConfig') && zoneConfig.includes('startTimeoutSec') && zoneConfig.includes('suppressError'), 'zone config should include timeout and suppressError fields');
assert(zoneTypes.includes('startupPulseLimit') && zoneTypes.includes('startupEstimatedMl') && zoneTypes.includes('stablePulsePerLiter'), 'zone config should use two-stage flow estimation fields');
assert(!zoneTypes.includes('calibrationX1000'), 'zone config should not keep the old calibration coefficient');
assert(zoneConfig.includes('startupPulses') && zoneConfig.includes('stablePulses'), 'zone water estimation should split startup and stable pulses');
assert(zoneConfig.includes('Zone 1') && zoneConfig.includes('Zone 4'), 'zone defaults should name all four zones');
assert(zoneConfig.includes('config.suppressError = true;'), 'flow anomalies should default to record-only mode');
assert(zoneError.includes('leakAlertActive') && zoneError.includes('ZoneError'), 'zone errors and leak alert should be persistent');
assert(flowMeter.includes('beginCapture') && flowMeter.includes('endCapture'), 'flow meter should expose raw pulse detail capture for calibration');
assert(flowCalibration.includes('stableWindowMs') && flowCalibration.includes('stableStepMs'), 'flow calibration should use sliding window stability detection');
assert(flowCalibration.includes('computeRecommendation') && flowCalibration.includes('rateVariationPermille'), 'flow calibration should compute recommendations and stability diagnostics');

assert(plans.includes('uint32_t planId'), 'plan id should be uint32_t');
assert(plans.includes('nextPlanId'), 'plan store should persist a non-reused next plan id');
assert(plans.includes('MaxPlansPerZone = 6'), 'plan store should use six fixed slots per zone');
assert(plans.includes('exists') && plans.includes('createdAt'), 'plan definitions should separate slot existence and creation time');
assert(!plans.includes('lastRunYmd'), 'plan definition must not persist lastRunYmd');

assert(skips.includes('Capacity = 128'), 'schedule skip store should have 128 entries');
assert(skips.includes('SkipReason') && skips.includes('WEATHER'), 'schedule skip should store skip reason');
assert(skips.includes('uint32_t planId'), 'schedule skip should be keyed by planId');

assert(scheduler.includes('eligibleFromEpoch'), 'scheduler should gate plans by first trusted time');
assert(scheduler.includes('scheduleGraceSec'), 'scheduler should use schedule grace seconds');
assert(!scheduler.includes('lastRunYmd'), 'scheduler should not use persistent lastRunYmd');

assert(records.includes('planNameSnapshot') && records.includes('startedEpoch') && records.includes('startedUptimeMs'), 'records should be self-contained with plan name, epoch, and uptime');
assert(records.includes('configSnapshot') && zoneTypes.includes('startTimeoutSec') && zoneTypes.includes('flowNoPulseTimeoutSec'), 'records should include config snapshot timeout fields');
assert(records.includes('configSnapshot') && zoneTypes.includes('startupPulseLimit') && zoneTypes.includes('startupEstimatedMl') && zoneTypes.includes('stablePulsePerLiter'), 'records should snapshot two-stage flow estimation fields');
assert(records.includes('createFixedFile'), 'fixed-capacity watering record store should use Esp32BaseFs::createFixedFile');
assert(!records.includes('calloc'), 'fixed-capacity watering record store should not allocate full files on heap');

assert(pio.includes('-D ESP32BASE_ENABLE_APP_EVENTS=1'), 'project should enable Esp32Base App Events');
assert(pio.includes('-D ESP32BASE_APP_EVENT_LOG_CAPACITY=256'), 'project should set a scoped App Events capacity');
assert(pio.includes('-D ESP32BASE_APP_CONFIG_MAX_GROUPS=4'), 'App Config group capacity should cover manual, schedule, safety, and calibration groups');
assert(pio.includes('-D ESP32BASE_APP_CONFIG_MAX_FIELDS=16'), 'App Config capacity should cover all registered irrigation system fields');
assert(businessEvents.includes('Esp32BaseAppEventLog::append'), 'business events should write through Esp32BaseAppEventLog::append');
assert(businessEvents.includes('schedule_skipped') && businessEvents.includes('flow_fault') && businessEvents.includes('leak_detected'), 'business event vocabulary should cover schedule, flow, and leak decisions');
assert(businessEvents.includes('observedPulses') && businessEvents.includes('VALUE3'), 'leak events should record observed pulses, threshold, and detection window');
assert(allSource.includes('Esp32BaseAppEventLog::clear'), 'business clear-records flow should clear Esp32Base App Events');

assert(web.includes('/api/v1/zone/start') && web.includes('zoneId'), 'web API should use fixed endpoint plus zoneId parameter');
assert(web.includes('/irrigation/calibration') && web.includes('handleCalibrationPage'), 'web should include a dedicated flow calibration page');
assert(web.includes('/api/v1/calibration/start') && web.includes('/api/v1/calibration/apply'), 'web API should include flow calibration lifecycle endpoints');
assert(web.includes('calibration-metrics') && web.includes('calibration-workflow') && web.includes('calibration-internal'), 'calibration page should use compact configuration and guided collection sections');
assert(web.includes('calibration-current-params') && web.includes('当前使用参数'), 'calibration page should show currently active per-zone flow parameters');
assert(web.includes('chart-grid') && web.includes('chart-tick') && web.includes('chart-axis-title'), 'calibration sample charts should render grid lines, dense tick labels, and axis titles');
assert(web.includes('writeChartTicks(') && web.includes('writeChartAxisTitle('), 'calibration sample charts should use shared chart helpers for axis labels and ticks');
assert(web.includes('/api/v1/plan/update') && web.includes('planId'), 'plan API should use fixed endpoint plus planId parameter');
assert(web.includes('planform-cycle') && web.includes('cycleday-grid'), 'plan edit form should group cycle days and execution days together');
assert(web.includes('irrPlanRenderDays') && web.includes('replaceChildren'), 'plan edit form should render exactly the visible execution days from cycleDays');
assert(web.includes('Math.min(30') && web.includes('Math.max(1'), 'plan edit form should clamp cycleDays to 1..30 in the browser');
assert(web.includes('if(mask===0)mask=1'), 'plan edit form should keep day 1 selected when cycleDays changes to an empty selection');
assert(!web.includes(" hidden") || !web.includes("data-cycle-day='"), 'plan edit form should not pre-render 30 execution days and hide the inactive ones');
assert(web.includes('addPage("/index", "首页", handleOverviewPage)'), 'business homepage should be registered at stable /index path');
assert(main.includes('Esp32BaseWeb::setHomePath("/index")'), 'root path should redirect to stable /index homepage');
assert(web.includes('查看今天是否还有计划'), 'overview should use user-facing homepage guidance');
assert(web.includes('sendMetricCard("今日计划"'), 'overview metrics should include today plan count');
assert(web.includes('sendMetricCard("异常提醒"') && web.includes('errorCount > 0 ? "danger"'), 'overview error metric should be highlighted when faults exist');
assert(web.includes('writeZoneErrorDialog') && web.includes('irrFaultOpen'), 'overview error status tag should open an in-page fault detail dialog');
assert(web.includes('writeZoneErrorTime') && web.includes('zoneErrorSourceLabel') && web.includes('zoneErrorResultLabel'), 'overview fault dialog should show objective fault time, source, and result');
assert(!web.includes('<th>异常原因</th>'), 'overview zone table should not reserve a permanent fault reason column');
assert(web.includes('zoneErrorClearConfirm(status.errorCode)'), 'clear-error confirmation should include the current fault reason');
assert(web.includes('irrOverviewRefreshMs') && web.includes('30000') && web.includes('1000'), 'overview should poll slowly while idle and every second while watering');
assert(web.includes('/api/v1/status') && web.includes('irrOverviewApplyStatus'), 'overview should refresh from the lightweight status API');
assert(web.includes('irrOverviewLiters') && web.includes("+' L'"), 'overview estimated water should render in liters');
assert(web.includes('writeLitersFromMilliliters(status.estimatedMilliliters)'), 'overview initial estimated water should use liters');
assert(web.includes('irrOverviewRenderState') && web.includes('irrOverviewRenderActions'), 'overview should update zone state/action cells in place');
assert(!web.includes('if(changed){location.reload();return;}'), 'overview status polling should not reload the whole page for normal running state changes');
assert(web.includes('irrManualSend') && web.includes('X-Esp32Base-Ajax') && web.includes('irrOverviewPollNow'), 'manual start should submit asynchronously and refresh status immediately');
assert(web.includes('Esp32BaseWeb::isAjaxRequest()'), 'web-page AJAX actions should return JSON instead of redirecting');
assert(!web.includes('sendMetric("可手动启动"'), 'overview metrics should not duplicate manual start availability count');
assert(!web.includes('beginPanel("手动浇水启动")'), 'manual start should be merged into the zone status panel');
assert(web.includes('irrManualOpen') && web.includes('<dialog id='), 'manual start should open a confirmation dialog from zone status actions');
assert(web.includes("<dialog id='irrManualDialog' class='panel eb-modal"), 'manual start dialog should use Esp32Base modal baseline styling');
assert(web.includes('irrmanual-presets') && web.includes('irrmanual-summary'), 'manual start dialog should use dedicated layout classes for aligned controls');
assert(web.includes('远程启动前请确认现场安全'), 'manual start dialog should include safety guidance for remote users');
assert(web.includes("value='确认启动'"), 'manual start dialog should require an explicit final start submit');
assert(!web.includes("confirm('确认启动手动浇水？')"), 'manual start final submit should not show a third confirmation popup');
assert(web.includes('irrManualPreset(this)') && web.includes('data-min='), 'manual start presets should be visible buttons that fill duration');
assert(!web.includes('<select onchange=\"this.form.durationMin.value=this.value\">'), 'manual start presets should not use a select dropdown');
assert(web.includes('基本信息') && web.includes('流量估算') && web.includes('异常处理'), 'zone edit form should group fields into readable sections');
assert(web.includes('启动阶段脉冲') && web.includes('启动阶段水量') && web.includes('稳定每升脉冲'), 'zone edit form should expose two-stage flow estimation fields');
assert(!web.includes('校准 x1000'), 'zone edit form should not expose the old calibration coefficient');
assert(web.includes('硬件引脚') && web.includes('阀门控制 GPIO') && web.includes('流量计输入 GPIO'), 'zone edit form should show read-only hardware pin diagnostics with user-facing labels');
assert(!web.includes('流量 GPIO'), 'zone pages should not use the ambiguous flow GPIO label');
assert(!web.includes('<th>阀门 GPIO</th>') && !web.includes('<th>流量计输入 GPIO</th>'), 'zone list should not expose hardware pin columns');
assert(web.includes('<th>启动超时</th><th>无脉冲超时</th>'), 'zone list should show user-relevant timeout settings');
assert(!web.includes('/esp32base/app-events.csv') && !web.includes('基础库存储视图'), 'business event page should not show low-level App Events storage links');
assert(!web.includes('<th>ID</th><th>等级</th><th>运行时间'), 'business event page should not show a separate uptime column');
assert(web.includes('writeEventTimeHuman(event)'), 'business event page should show real time, falling back to boot count plus uptime only when real time is unavailable');
assert(web.includes('eventZoneId(event)') && web.includes('writeEventDetailValue') && web.includes('writeEventZoneName'), 'business event rows should explain object, zone, and high-value values clearly');
assert(web.includes('writeShortDateTimeHuman') && !web.includes('%04d-%02d-%02d %02d:%02d:%02d'), 'record and event pages should show month-day time without year');
assert(web.includes('writeDurationMsHumanCompact(record.endedUptimeMs - record.startedUptimeMs)'), 'watering record runtime should use compact human duration');
assert(web.includes('writeLitersFromMilliliters(record.estimatedMilliliters)'), 'watering record estimated water should use liters');
assert(!web.includes('record.estimatedMilliliters);\n    Esp32BaseWeb::sendChunk(" ml'), 'watering record table should not show milliliters');
assert(!web.includes('/api/v1/water/start'), 'old water start API should be removed');
assert(!web.includes('road_id') && !web.includes('roadId'), 'external API should not expose road identifiers');

for (const marker of ["method='post'", 'method=\"post\"']) {
  let index = web.indexOf(marker);
  while (index !== -1) {
    const snippet = web.slice(index, index + 800);
    if (!snippet.includes("action='/api/v1/zone/start'")) {
      assert(snippet.includes('confirm('), `POST form must include browser confirmation near offset ${index}`);
    }
    assert(snippet.includes('once(this)'), `POST form must prevent duplicate submission near offset ${index}`);
    index = web.indexOf(marker, index + marker.length);
  }
}

assert(!allSource.includes('WateringSession'), 'old WateringSession references should be removed');
assert(!allSource.includes('WateringPlanScheduler'), 'old WateringPlanScheduler references should be removed');
assert(!allSource.includes('LeakMonitor'), 'old LeakMonitor references should be removed');
assert(!allSource.includes('PlanResultStore'), 'old PlanResultStore references should be removed');
assert(!allSource.includes('PlanSkipStore'), 'old PlanSkipStore references should be removed');
assert(!allSource.includes('SettingsStore'), 'old SettingsStore references should be removed');
assert(!allSource.includes('EventStore'), 'old application EventStore references should be removed');
assert(!zoneTypes.includes('enum class EventType') && !zoneTypes.includes('enum class EventSource'), 'old application event enums should be removed');

assert(pio.includes('-D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL'), 'project should keep Esp32Base full profile');
assert(calibrationDoc.includes('detailPulseDeltas') && calibrationDoc.includes('滑动窗口') && calibrationDoc.includes('stablePulsePerLiter'), 'flow calibration design doc should describe raw pulse detail and final parameters');
assert(roadReadme.includes('04-flow-calibration.md'), 'road management docs index should link the flow calibration design');
