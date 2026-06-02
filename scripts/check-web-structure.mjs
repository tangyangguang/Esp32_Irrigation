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

function functionBody(source, name) {
  const signature = source.search(new RegExp(`(?:void|bool|uint8_t|uint16_t|uint32_t|const char\\*)\\s+${name}\\s*\\(`));
  assert(signature !== -1, `function missing: ${name}`);
  const open = source.indexOf('{', signature);
  assert(open !== -1, `function body missing: ${name}`);
  let depth = 0;
  for (let i = open; i < source.length; i += 1) {
    if (source[i] === '{') depth += 1;
    if (source[i] === '}') depth -= 1;
    if (depth === 0) {
      return source.slice(open + 1, i);
    }
  }
  throw new Error(`function body unterminated: ${name}`);
}

function assertPostGuard(name) {
  const body = functionBody(web, name);
  assert(body.includes('checkBusinessPost("irrigation.'), `${name} must use the shared POST same-origin guard`);
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
assert(systemConfig.includes('StoredSystemConfig') && systemConfig.includes('kKeyBlob') && systemConfig.includes('setPod(kNamespace, kKeyBlob'),
       'system config API saves should use one versioned POD instead of multi-key writes');
assert(systemConfig.includes('flowRateWindowSec') && systemConfig.includes('flowChartIntervalSec') && systemConfig.includes('flowChartHistoryMin'), 'system config should include flow rate display window, chart interval, and chart history settings');
assert(systemConfig.includes('config.flowRateWindowSec = 5;') && systemConfig.includes('config.flowChartIntervalSec = 5;') && systemConfig.includes('config.flowChartHistoryMin = 10;'), 'flow rate display defaults should use 5s window, 5s chart interval, and 10 minutes of history');
assert(systemConfig.includes('"流速显示"') && systemConfig.includes('"流速窗口秒"') && systemConfig.includes('"图表历史分钟"'), 'App Config should expose flow rate display settings');
assert(systemConfig.includes('86400'), 'max watering duration upper bound should allow up to 24 hours');
assert(systemConfig.includes('14400'), 'max watering duration default should be 4 hours');
assert(systemConfig.includes('submittedMinutesAsSeconds'), 'App Config should accept manual duration values in minutes and store seconds');
assert(systemConfig.includes('kKeyLeakEnabled') && systemConfig.includes('addBool'), 'App Config should expose idle leak detection as a boolean switch');
assert(systemConfig.includes('"单次最长分钟"') && systemConfig.includes('"手动默认分钟"'), 'App Config duration labels should use minutes for user-facing watering durations');
assert(!systemConfig.includes('"单次最长秒"') && !systemConfig.includes('"手动默认秒"'), 'App Config should not expose user-facing watering duration fields in seconds');
assert(systemConfig.includes('"校准样本容量"') && systemConfig.includes('"明细脉冲上限"'), 'App Config should expose flow calibration settings');

assert(zoneConfig.includes('ZoneConfig') && zoneConfig.includes('startTimeoutSec') && zoneConfig.includes('suppressError'), 'zone config should include timeout and suppressError fields');
assert(zoneConfig.includes('validUtf8NoControl(const char* text, size_t maxLen)') &&
       zoneConfig.includes('remaining < 2') &&
       zoneConfig.includes('remaining < 3') &&
       zoneConfig.includes('remaining < 4'),
       'zone name UTF-8 validation must be bounded before reading continuation bytes');
assert(zoneTypes.includes('startupPulseLimit') && zoneTypes.includes('startupEstimatedMl') && zoneTypes.includes('stablePulsePerLiter'), 'zone config should use two-stage flow estimation fields');
assert(!zoneTypes.includes('calibrationX1000'), 'zone config should not keep the old calibration coefficient');
assert(!zoneTypes.includes('FlowParameterSource') && !zoneTypes.includes('sourceZoneId'), 'candidate flow parameters should not persist source metadata');
assert(!zoneConfig.includes('FlowParameterSource') && !zoneConfig.includes('sourceZoneId'), 'zone config storage should save candidate values only');
assert(zoneConfig.includes('startupPulses') && zoneConfig.includes('stablePulses'), 'zone water estimation should split startup and stable pulses');
assert(zoneConfig.includes('Zone 1') && zoneConfig.includes('Zone 4'), 'zone defaults should name all four zones');
assert(zoneConfig.includes('config.suppressError = true;'), 'flow anomalies should default to record-only mode');
assert(zoneError.includes('leakAlertActive') && zoneError.includes('ZoneError'), 'zone errors and leak alert should be persistent');
assert(flowMeter.includes('beginCapture') && flowMeter.includes('endCapture'), 'flow meter should expose raw pulse detail capture for calibration');
assert(flowMeter.includes('configureFlowRate') && flowMeter.includes('flowMillilitersPerMinute') && flowMeter.includes('readFlowHistory'), 'flow meter should expose configurable flow rate and chart history APIs');
assert(flowMeter.includes('MaxFlowHistoryPoints = 360'), 'flow chart history should cap each zone at 360 points');
assert(flowCalibration.includes('stableWindowMs') && flowCalibration.includes('stableStepMs'), 'flow calibration should use sliding window stability detection');
assert(flowCalibration.includes('computeRecommendation') && flowCalibration.includes('rateVariationPermille'), 'flow calibration should compute recommendations and stability diagnostics');
assert(functionBody(web, 'handleCalibrationApplyApi').includes('FlowCalibration::active()'),
       'applying calibration candidates must reject active calibration capture');
assert(functionBody(web, 'handleCalibrationPreviousRestoreApi').includes('FlowCalibration::active()'),
       'restoring previous calibration parameters must reject active calibration capture');
{
  const zoneTick = functionBody(read('src/domain/Zone.cpp'), 'Zone::tick');
  const runningBranch = zoneTick.slice(zoneTick.indexOf('m_state == Irrigation::ZoneState::RUNNING'));
  assert(runningBranch.indexOf('elapsedMs >= durationMs') !== -1 &&
         runningBranch.indexOf('FLOW_NO_PULSE_TIMEOUT') !== -1 &&
         runningBranch.indexOf('elapsedMs >= durationMs') < runningBranch.indexOf('FLOW_NO_PULSE_TIMEOUT'),
         'running zones must finish completed tasks before evaluating no-pulse timeout');
}

assert(plans.includes('uint32_t planId'), 'plan id should be uint32_t');
assert(plans.includes('nextPlanId'), 'plan store should persist a non-reused next plan id');
assert(plans.includes('MaxPlansPerZone = 6'), 'plan store should use six fixed slots per zone');
assert(plans.includes('exists') && plans.includes('createdAt'), 'plan definitions should separate slot existence and creation time');
assert(!plans.includes('lastRunYmd'), 'plan definition must not persist lastRunYmd');
assert(plans.includes('bool currentYmd(uint32_t* out)') &&
       plans.includes('bool validYmd(uint32_t ymd)') &&
       !plans.includes('DefaultCycleStartYmd'),
       'plan store should expose strict date helpers and should not fall back to a fixed default date');
assert(plans.includes('recoverNextPlanId') && plans.includes('saveAllPlansBlob'),
       'plan store should recover plan ids and save plans through a single committed blob');
assert(read('src/domain/PlanExecutionTracker.cpp').includes('Esp32BaseConfig') &&
       read('src/domain/PlanExecutionTracker.cpp').includes('irr_plan_exec'),
       'plan execution tracker should persist handled plan observations outside plan definitions');

assert(skips.includes('Capacity = 128'), 'schedule skip store should have 128 entries');
assert(skips.includes('SkipReason') && skips.includes('WEATHER'), 'schedule skip should store skip reason');
assert(skips.includes('uint32_t planId'), 'schedule skip should be keyed by planId');
assert(skips.includes('PlanStore::validYmd') && !skips.includes('persistAll'),
       'schedule skip store should use strict real-date validation and must not rewrite all 128 slots per update');
assert(skips.includes('pruneBefore(') && skips.includes('removeAt('),
       'schedule skip store should prune expired entries and compact single slots');

assert(scheduler.includes('eligibleFromEpoch'), 'scheduler should gate plans by first trusted time');
assert(scheduler.includes('scheduleGraceSec'), 'scheduler should use schedule grace seconds');
assert(!scheduler.includes('lastRunYmd'), 'scheduler should not use persistent lastRunYmd');
assert(!scheduler.includes('dueEpoch < m_eligibleFromEpoch'), 'scheduler must not silently skip plans still inside the grace window after boot/NTP sync');
assert(!scheduler.includes('markObserved(plan, ymd, minuteOfDay, Irrigation::PlanObservationStatus::MISSED)') &&
       read('src/domain/PlanExecutionTracker.h').includes('markVolatile') &&
       scheduler.includes('appendObservation(plan, Irrigation::PlanObservationStatus::MISSED, dueEpoch);'),
       'scheduler should not persist MISSED observations that may be caused by time jumps');
assert(scheduler.includes('MaintenanceService::factoryResetPending()') && scheduler.includes('SKIPPED_RESET'),
  'scheduler should skip plan starts while factory reset is pending');
assert(scheduler.includes('FlowCalibration::active()') && scheduler.includes('SKIPPED_BUSY'),
  'scheduler should skip plan starts while flow calibration is active');
assert(read('src/domain/PlanExecutionTracker.h').includes('bool mark(') &&
       read('src/domain/PlanExecutionTracker.h').includes('retrySave') &&
       read('src/domain/PlanExecutionTracker.cpp').includes('m_dirty') &&
       !read('src/domain/PlanExecutionTracker.cpp').includes('(void)save()'),
       'plan execution tracker persistence failures must be returned to callers');
assert(scheduler.includes('retrySave') && scheduler.includes('tracker_retry_save_failed'),
       'scheduler should retry dirty plan execution tracker saves');
assert(scheduler.includes('kTrackerFaultEventMinIntervalMs') && scheduler.includes('recordTrackerPersistFailed'),
       'scheduler should throttle repeated plan tracker persistence fault events');
assert(scheduler.includes('appendPlanTrackerPersistFailed'),
       'scheduler should log when plan execution tracker persistence fails');

assert(records.includes('planNameSnapshot') && records.includes('startedEpoch') && records.includes('startedUptimeMs'), 'records should be self-contained with plan name, epoch, and uptime');
assert(records.includes('flowStatsValid') && records.includes('maxFlowMlPerMin') && records.includes('maxFlowFirstAtSec') && records.includes('minFlowMlPerMin') && records.includes('minFlowFirstAtSec'), 'watering records should persist max/min flow and first occurrence times');
assert(records.includes('flowRateWindowSec'), 'watering records should snapshot the flow rate window used for max/min statistics');
assert(records.includes('configSnapshot') && zoneTypes.includes('startTimeoutSec') && zoneTypes.includes('flowNoPulseTimeoutSec'), 'records should include config snapshot timeout fields');
assert(records.includes('configSnapshot') && zoneTypes.includes('startupPulseLimit') && zoneTypes.includes('startupEstimatedMl') && zoneTypes.includes('stablePulsePerLiter'), 'records should snapshot two-stage flow estimation fields');
assert(records.includes('createFixedFile'), 'fixed-capacity watering record store should use Esp32BaseFs::createFixedFile');
assert(!records.includes('calloc'), 'fixed-capacity watering record store should not allocate full files on heap');
assert(records.includes('recoverMetaFromRecords') && records.includes('appendRecordStoreRecovered'),
       'watering record store should recover metadata from committed records on boot');
assert(records.includes('appendRecordMetaSaveFailed'),
       'watering record store should log metadata save failures after record writes');
assert(records.includes('commitMagic') && records.includes('crc32') && records.includes('crc32Record'),
       'watering records should use a commit marker and CRC before recovery accepts a slot');
assert(!records.includes('LegacyWateringRecord') &&
       !records.includes('migrateLegacyStoreFile') &&
       !records.includes('recoverInterruptedMigration') &&
       !records.includes('kMigrationPath') &&
       !records.includes('kMigrationBackupPath') &&
       !records.includes('appendRecordStoreMigrated'),
       'watering record store must not keep legacy format migration code');
assert(read('src/domain/Zone.cpp').includes('appendRecordAppendFailed') &&
       !read('src/domain/Zone.cpp').includes('(void)RecordStore::append(record)'),
       'zone finish should not ignore watering record append failures');
assert(zoneConfig.includes('schemaResetDetected') &&
       plans.includes('schemaResetDetected') &&
       businessEvents.includes('config_schema_reset') &&
       web.includes('水路配置已重置') &&
       web.includes('计划配置已重置'),
       'zone and plan schema mismatch should be visible via business events and Web notices');

assert(pio.includes('-D ESP32BASE_ENABLE_APP_EVENTS=1'), 'project should enable Esp32Base App Events');
assert(pio.includes('-D ESP32BASE_APP_EVENT_LOG_CAPACITY=256'), 'project should set a scoped App Events capacity');
assert(pio.includes('-D ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_WARN'), 'project should explicitly default System Logs / FileLog to WARN');
assert(pio.includes('-D ESP32BASE_APP_CONFIG_MAX_GROUPS=5'), 'App Config group capacity should cover manual, schedule, safety, calibration, and flow display groups');
assert(pio.includes('-D ESP32BASE_APP_CONFIG_MAX_FIELDS=19'), 'App Config capacity should cover all registered irrigation system fields');
assert(businessEvents.includes('Esp32BaseAppEventLog::append'), 'business events should write through Esp32BaseAppEventLog::append');
assert(businessEvents.includes('schedule_skipped') && businessEvents.includes('flow_fault') && businessEvents.includes('leak_detected'), 'business event vocabulary should cover schedule, flow, and leak decisions');
assert(businessEvents.includes('observedPulses') && businessEvents.includes('VALUE3'), 'leak events should record observed pulses, threshold, and detection window');
assert(businessEvents.includes('web_route_fault') && web.includes('appendWebRouteRegistrationFailed'),
       'business web route registration failures should be visible as business events');
assert(!allSource.includes('Esp32BaseAppEventLog::clear'), 'business layer should not clear Esp32Base App Events');

assert(web.includes('/api/v1/zone/start') && web.includes('zoneId'), 'web API should use fixed endpoint plus zoneId parameter');
assert(web.includes('/irrigation/calibration') && web.includes('handleCalibrationPage'), 'web should include a dedicated flow calibration page');
assert(web.includes('/api/v1/calibration/start') && web.includes('/api/v1/calibration/apply'), 'web API should include flow calibration lifecycle endpoints');
assert(web.includes('/api/v1/calibration/status') && web.includes('handleCalibrationStatusApi'), 'web API should include a read-only calibration status endpoint');
assert(web.includes('/api/v1/calibration/candidate') && !web.includes('/api/v1/calibration/candidate/manual'), 'candidate save API should not encode source type in the route');
assert(web.includes('calibration-metrics') && web.includes('calibration-compact-workflow') && web.includes('calibration-internal'), 'calibration page should use compact configuration and guided collection sections');
assert(web.includes('calibration-zone-list') && web.includes('calibration-zone-row') && web.includes('设为当前'), 'calibration page should show one parameter row per zone with set-current actions');
assert(web.includes('writeFlowParameterCompact') && web.includes('启动 ') && web.includes(' · ') && web.includes(' P/L'),
       'calibration page should render flow parameters in compact one-line text');
assert(web.includes('calibration-collect-status') && web.includes('接水状态') &&
       web.includes('calibration-current-params') && web.includes('当前水路参数'),
       'calibration collection area should separate collection status from current zone parameters');
assert(web.includes('calibration-compact-workflow') && web.includes('calibration-inline-form'),
       'calibration collection actions should use compact inline layout');
assert(!web.includes('calibrationProgressSamples'),
       'calibration collection status should not render the sample-count card');
assert(web.includes('calibration-sample-summary') && web.includes('已保存 ') && web.includes('有效 ') && web.includes('容量 '),
       'calibration samples panel should show the sample count as a compact summary');
assert(web.includes('calibration-stage-control') && web.includes('calibration-stage-disabled'),
       'calibration collection action cards should use fixed controls and disabled visual states');
assert(web.includes("action='/api/v1/calibration/stop' onsubmit=\\\"return once(this)&&calibrationSubmit(this)\\\""),
       'calibration stop action should not include a confirmation dialog by design');
assert(web.includes("action='/api/v1/calibration/compute' onsubmit=\\\"return confirm('确认用当前样本生成候选参数？')&&once(this)&&calibrationSubmit(this)\\\""),
       'calibration compute action should include a confirmation dialog');
assert(web.includes("action='/api/v1/calibration/start' onsubmit=\\\"return confirm('确认开始校准出水？')") &&
       web.includes("action='/api/v1/calibration/clear' onsubmit=\\\"return confirm('确认清空当前校准样本？')"),
       'other important calibration POST actions should keep confirmation dialogs');
assert(web.includes("action='/api/v1/calibration/sample' onsubmit=\\\"return once(this)&&calibrationSubmit(this)\\\""),
       'calibration sample save action should not include a confirmation dialog by design');
assert(web.includes('calibrationProgressStart') && web.includes('/api/v1/calibration/status') && web.includes('setInterval(calibrationProgressUpdate,1000)'),
       'calibration page should refresh collection progress from the status API every second');
assert(web.includes('calibrationSubmit(this)') && web.includes('calibrationReplaceSections') && web.includes('DOMParser'),
       'calibration page should submit calibration actions locally and replace page sections without a full reload');
assert(web.includes('readOptionalZoneId') && web.includes('currentFlow') && web.includes('candidateFlow'),
       'calibration status API should support selected zone parameter summaries');
assert(web.includes('calibrationCandidateFill') && web.includes('从其他水路填入') && web.includes('填入表单'), 'candidate editor should support copy-as-input inside the candidate form');
assert(!web.includes('来源：') && !web.includes('flowCandidateSourceLabel') && !web.includes('/api/v1/calibration/candidate/copy-current'), 'calibration page should not expose or persist candidate source tracking');
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
assert(web.includes('/api/v1/flow/history') && web.includes('handleFlowHistoryApi'), 'web API should expose single-zone flow chart history');
assert(web.includes('data-irr-flow') && web.includes('irrOverviewFlow') && web.includes('流速'), 'overview should display current flow rate instead of pulse count');
assert(web.includes('writeWeatherStrip()'), 'overview should render the weather forecast strip');
assert(web.includes('const uint32_t todayYmd = currentYmd()') &&
       web.includes('writeZoneOverviewRow(zoneId, status, todayYmd)'),
       'overview should render enabled-zone rows against one consistent today value');
assert(web.includes('writeTodayPlanCard(zoneId, ymd)'), 'overview zone rows should include today plan cards');
assert(web.includes('irr-zone-row') && web.includes('irr-zone-card') && web.includes('irr-plan-card'), 'overview should use row/card layout classes');
assert(web.includes('data-irr-runtime') && web.includes('data-irr-remaining') && web.includes('data-irr-flow') && web.includes('data-irr-ml'), 'overview card metrics should expose live-update targets');
assert(web.includes('irrFlowChart') && web.includes('flowHistory'), 'overview should render recent per-zone flow chart data');
assert(web.includes('L/min') && web.includes('无流速'), 'overview flow chart should include axes/unit labels and an idle baseline state');
assert(web.includes('collectTodayPlanCompletions') && web.includes('RecordStore::readLatest'), 'today plan progress should use watering records');
assert(web.includes('todayPlanResultCompleted') && web.includes('item.lastResult != Irrigation::TaskResult::NONE'),
       'today plan cards should count only successful watering as completed while still showing non-completed record results');
assert(!web.includes('resetNewDay(ymd)'), 'overview rendering must not reset plan execution tracker state');
assert(!web.includes("<table class='part'><thead><tr><th>水路</th><th>状态</th><th>任务</th><th>目标时长"), 'overview should not render the old water-status table');
assert(web.includes('irrOverviewLiters') && web.includes("+' L'"), 'overview estimated water should render in liters');
assert(web.includes('irrOverviewRenderState') && web.includes('irrOverviewRenderActions'), 'overview should update zone state/action cells in place');
assert(!web.includes('if(changed){location.reload();return;}'), 'overview status polling should not reload the whole page for normal running state changes');
assert(web.includes('irrManualSend') && web.includes('X-Esp32Base-Ajax') && web.includes('irrOverviewPollNow'), 'manual start should submit asynchronously and refresh status immediately');
assert(web.includes('Esp32BaseWeb::isAjaxRequest()'), 'web-page AJAX actions should return JSON instead of redirecting');
for (const name of [
  'handleConfigApi',
  'handleCalibrationStartApi',
  'handleCalibrationStopApi',
  'handleCalibrationSampleApi',
  'handleCalibrationSampleUpdateApi',
  'handleCalibrationComputeApi',
  'handleCalibrationCandidateSaveApi',
  'handleCalibrationApplyApi',
  'handleCalibrationPreviousRestoreApi',
  'handleCalibrationClearApi',
  'handleZoneStartApi',
  'handleZoneStopApi',
  'handleZonesStopAllApi',
  'handleZoneConfigApi',
  'handleZoneClearErrorApi',
  'handlePlanCreateApi',
  'handlePlanEnableApi',
  'handlePlanDisableApi',
  'handlePlanDeleteApi',
  'handlePlanUpdateApi',
]) {
  assertPostGuard(name);
}
assert(functionBody(web, 'handleScheduleSkipApi').includes('checkBusinessPost(skip ? "irrigation.'),
       'schedule skip/unskip POST handler must use the shared POST same-origin guard');
assert(functionBody(web, 'checkBusinessPost').includes('Esp32BaseWeb::checkPostAllowed(context)'),
       'shared POST guard must delegate to Esp32BaseWeb::checkPostAllowed()');
assert(web.includes('rejectFactoryResetPending') && web.includes('"factory_reset_pending"'),
       'mutating business handlers should reject writes while factory reset is pending');
assert(!web.includes('sendMetric("可手动启动"'), 'overview metrics should not duplicate manual start availability count');
assert(!web.includes('beginPanel("手动浇水启动")'), 'manual start should be merged into the zone status panel');
assert(web.includes('irrManualOpen') && web.includes('<dialog id='), 'manual start should open a confirmation dialog from zone status actions');
assert(web.includes("<dialog id='irrManualDialog' class='panel eb-modal"), 'manual start dialog should use Esp32Base modal baseline styling');
assert(web.includes('irrmanual-presets') && web.includes('irrmanual-summary'), 'manual start dialog should use dedicated layout classes for aligned controls');
assert(web.includes('远程启动前请确认现场安全'), 'manual start dialog should include safety guidance for remote users');
assert(web.includes("value='确认启动'"), 'manual start dialog should require an explicit final start submit');
assert(web.includes("confirm('确认启动手动浇水？')"), 'manual start final submit should include browser confirmation');
assert(web.includes('irrManualPreset(this)') && web.includes('data-min='), 'manual start presets should be visible buttons that fill duration');
assert(!web.includes('<select onchange=\"this.form.durationMin.value=this.value\">'), 'manual start presets should not use a select dropdown');
assert(web.includes('基本信息') && web.includes('流量估算') && web.includes('异常处理'), 'zone edit form should group fields into readable sections');
assert(web.includes('启动阶段脉冲') && web.includes('启动阶段水量') && web.includes('稳定脉冲 P/L'), 'zone edit form should expose two-stage flow estimation fields');
assert(!web.includes('校准 x1000'), 'zone edit form should not expose the old calibration coefficient');
assert(web.includes('硬件引脚') && web.includes('阀门控制 GPIO') && web.includes('流量计输入 GPIO'), 'zone edit form should show read-only hardware pin diagnostics with user-facing labels');
assert(!web.includes('流量 GPIO'), 'zone pages should not use the ambiguous flow GPIO label');
assert(!web.includes('<th>阀门 GPIO</th>') && !web.includes('<th>流量计输入 GPIO</th>'), 'zone list should not expose hardware pin columns');
assert(web.includes('<th>启动超时</th><th>无脉冲超时</th>'), 'zone list should show user-relevant timeout settings');
{
  const zoneEdit = functionBody(web, 'handleSettingsPage');
  const styleCall = zoneEdit.indexOf('writeFlowParameterLineStyle()');
  const paramLineCall = zoneEdit.indexOf('writeFlowParameterLine(zone.flow)');
  assert(styleCall !== -1 && paramLineCall !== -1 && styleCall < paramLineCall,
         'zone edit page must load flow parameter line styles before rendering current flow parameters');
}
assert(!web.includes('/esp32base/app-events.csv') && !web.includes('基础库存储视图'), 'business event page should not show low-level App Events storage links');
assert(!web.includes('<th>ID</th><th>等级</th><th>运行时间'), 'business event page should not show a separate uptime column');
assert(web.includes('writeEventTimeHuman(event)'), 'business event page should show real time, falling back to boot count plus uptime only when real time is unavailable');
assert(web.includes('eventZoneId(event)') && web.includes('writeEventDetailValue') && web.includes('writeEventZoneName'), 'business event rows should explain object, zone, and high-value values clearly');
assert(web.includes('writeShortDateTimeHuman') && !web.includes('%04d-%02d-%02d %02d:%02d:%02d'), 'record and event pages should show month-day time without year');
assert(web.includes('writeDurationMsHumanCompact(record.endedUptimeMs - record.startedUptimeMs)'), 'watering record runtime should use compact human duration');
assert(web.includes('writeLitersFromMilliliters(record.estimatedMilliliters)'), 'watering record estimated water should use liters');
assert(web.includes('writeAverageFlowRate(record)') && web.includes('writeRecordPeakFlow'), 'watering record table should show average, max, and min flow diagnostics');
assert(!web.includes('record.estimatedMilliliters);\n    Esp32BaseWeb::sendChunk(" ml'), 'watering record table should not show milliliters');
assert(!web.includes('/api/v1/water/start'), 'old water start API should be removed');
assert(!web.includes('road_id') && !web.includes('roadId'), 'external API should not expose road identifiers');
assert(!web.includes('static_cast<uint32_t>(event.value'), 'event JSON must preserve signed int32 values');
assert(!web.includes('handleFactoryResetApi') && !web.includes('/api/v1/maintenance/factory-reset'),
       'business web must not expose a factory reset API');

for (const marker of ["method='post'", 'method=\"post\"']) {
  let index = web.indexOf(marker);
  while (index !== -1) {
    const snippet = web.slice(index, index + 800);
    const confirmationOptional =
      snippet.includes("action='/api/v1/calibration/stop'") ||
      snippet.includes("action='/api/v1/calibration/sample'");
    assert(confirmationOptional || snippet.includes('confirm('), `POST form must include browser confirmation near offset ${index}`);
    assert(snippet.includes('once(this)'), `POST form must prevent duplicate submission near offset ${index}`);
    index = web.indexOf(marker, index + marker.length);
  }
}

assert(systemConfig.includes('saveFieldStored') &&
       functionBody(read('src/storage/SystemConfigStore.cpp'), 'set').includes('saveFieldStored(config)'),
       'SystemConfigStore::set must keep the App Config field keys synchronized with the blob');
assert(systemConfig.includes('fieldStoredMatches') &&
       systemConfig.includes('saveFieldStored(stored.data)'),
       'SystemConfigStore must repair App Config field keys when loading an existing valid blob');

assert(!allSource.includes('WateringSession'), 'old WateringSession references should be removed');
assert(!allSource.includes('WateringPlanScheduler'), 'old WateringPlanScheduler references should be removed');
assert(!allSource.includes('LeakMonitor'), 'old LeakMonitor references should be removed');
assert(!allSource.includes('PlanResultStore'), 'old PlanResultStore references should be removed');
assert(!allSource.includes('PlanSkipStore'), 'old PlanSkipStore references should be removed');
assert(!allSource.includes('SettingsStore'), 'old SettingsStore references should be removed');
assert(!allSource.includes('EventStore'), 'old application EventStore references should be removed');
assert(!zoneTypes.includes('enum class EventType') && !zoneTypes.includes('enum class EventSource'), 'old application event enums should be removed');
assert(read('src/domain/SafetyManager.cpp').includes('kLocalButtonZoneMax = 2'),
       'local START/OK should be explicitly limited to locally controlled zone 1/2');
assert(!read('src/domain/SafetyManager.cpp').includes('lock button not held') &&
       read('src/domain/SafetyManager.cpp').includes('factory reset requested by gpio0 long press'),
       'GPIO0 long press should be a single-button local maintenance trigger');
assert(read('src/domain/ZoneManager.cpp').includes('FlowCalibration::active()') &&
       read('src/domain/ZoneManager.cpp').includes('FlowCalibration::abort('),
       'manual starts should reject active calibration and stop-all should clear calibration state');

assert(pio.includes('-D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL'), 'project should keep Esp32Base full profile');
assert(pio.includes('-D ESP32BASE_WEB_MAX_ROUTES=43'), 'web route capacity should include weather snapshot API');
assert(calibrationDoc.includes('detailPulseDeltas') && calibrationDoc.includes('滑动窗口') && calibrationDoc.includes('stablePulsePerLiter'), 'flow calibration design doc should describe raw pulse detail and final parameters');
assert(roadReadme.includes('04-flow-calibration.md'), 'road management docs index should link the flow calibration design');
