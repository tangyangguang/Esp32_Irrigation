import fs from 'node:fs';

function read(file) {
  return fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '';
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

const pins = read('include/Pins.h');
const zoneTypes = read('src/domain/ZoneTypes.h');
const valveController = read('src/domain/ValveController.cpp');
const zoneConfigStore = read('src/storage/ZoneConfigStore.cpp');
const zoneConfigStoreHeader = read('src/storage/ZoneConfigStore.h');
const flowAlertStore = read('src/storage/FlowAlertStore.cpp');
const flowAlertStoreHeader = read('src/storage/FlowAlertStore.h');
const flowConfigStore = read('src/storage/FlowConfigStore.cpp');
const flowConfigStoreHeader = read('src/storage/FlowConfigStore.h');
const planStore = read('src/storage/PlanStore.cpp');
const systemConfigStore = read('src/storage/SystemConfigStore.cpp');
const zoneManager = read('src/domain/ZoneManager.cpp');
const zoneScheduler = read('src/domain/ZoneScheduler.cpp');
const zoneRuntime = read('src/domain/Zone.cpp');
const localControl = read('src/domain/LocalControl.cpp');
const safetyManager = read('src/domain/SafetyManager.cpp');
const flowCalibration = read('src/domain/FlowCalibration.cpp');
const displayService = read('src/domain/DisplayService.cpp');
const irrigationWeb = read('src/web/IrrigationWeb.cpp');
const platformio = read('platformio.ini');

assert(pins.includes('MaxFlowMeters = 2'), 'hardware model should expose two flow meters');
assert(pins.includes('MaxZones = 6'), 'hardware model should expose six zones');
assert(
  pins.includes('Valve5 = 4') && pins.includes('Valve6 = 5'),
  'hardware model should use fixed GPIO4/GPIO5 for valve 5 and 6',
);
assert(
  pins.includes('DefaultZoneEnabledMask = 0x03'),
  'default hardware enable mask should enable zone 1 and 2',
);
assert(!pins.includes('MaxRoads'), 'hardware model must not expose legacy MaxRoads');
assert(!pins.includes('DefaultRoadEnabledMask'), 'hardware model must not expose legacy DefaultRoadEnabledMask');
assert(!pins.includes('StartOkButton'), 'Pins.h must not keep legacy local button aliases');
assert(!pins.includes('Road1UpButton'), 'Pins.h must not keep legacy road-specific button aliases');

assert(
  zoneTypes.includes('MaxFlowMeters = IrrigationPins::MaxFlowMeters'),
  'domain constants should import MaxFlowMeters from Pins.h',
);
assert(
  zoneTypes.includes('MaxZones = IrrigationPins::MaxZones'),
  'domain constants should import MaxZones from Pins.h',
);
assert(
  zoneTypes.includes('ScheduleQueueCapacity = 12'),
  'domain constants should define the planned schedule queue capacity',
);
assert(
  zoneTypes.includes('TotalPlanSlots = MaxZones * MaxPlansPerZone'),
  'plan slots should derive from MaxZones and MaxPlansPerZone',
);

assert(
  valveController.includes('IrrigationPins::Valve5') &&
    valveController.includes('IrrigationPins::Valve6') &&
    valveController.includes('Irrigation::MaxZones'),
  'valve controller should be sized for six fixed valve outputs',
);
assert(valveController.includes('bool setZone(uint8_t zoneId'), 'valve controller should expose Zone naming');
assert(!valveController.includes('setRoad') && !valveController.includes('road='), 'valve controller must not keep Road naming');

assert(zoneTypes.includes('enum class ParameterSource'), 'domain should define ParameterSource');
assert(zoneTypes.includes('enum class FlowFaultAction'), 'domain should define FlowFaultAction');
assert(
  zoneTypes.includes('struct FlowMeterCalibrationProfile') &&
    zoneTypes.includes('kUlPerMinPerHz') &&
    zoneTypes.includes('offsetMilliHz') &&
    zoneTypes.includes('minValidFreqMilliHz'),
  'domain should use K+Offset flow meter calibration profiles',
);
assert(
  zoneTypes.includes('struct ZoneFlowBaselineProfile') &&
    zoneTypes.includes('learnedFlowMlPerMin') &&
    zoneTypes.includes('lowFlowPermille') &&
    zoneTypes.includes('noPulseTimeoutSec'),
  'domain should use per-zone flow baseline profiles',
);
assert(
  zoneTypes.includes('struct FlowMeterConfig') &&
    zoneTypes.includes('activeCalibration') &&
    zoneTypes.includes('pendingCalibration') &&
    zoneTypes.includes('rollbackCalibration'),
  'domain should model flow meter config separately from zone config',
);
assert(
  zoneTypes.includes('struct ZoneConfig') &&
    zoneTypes.includes('uint8_t flowId') &&
    zoneTypes.includes('activeBaseline') &&
    zoneTypes.includes('pendingBaseline') &&
    zoneTypes.includes('rollbackBaseline'),
  'zone config should assign each zone to Flow 1 or Flow 2 and hold its baseline',
);
for (const legacy of [
  'FlowParameters',
  'startupPulseLimit',
  'startupEstimatedMl',
  'stablePulsePerLiter',
  'flowPin',
  'candidateFlow',
  'previousFlow',
  'startTimeoutSec',
  'flowNoPulseTimeoutSec',
  'suppressError',
]) {
  assert(!zoneTypes.includes(legacy), `domain types must not keep legacy field ${legacy}`);
  assert(!zoneConfigStoreHeader.includes(legacy), `zone config API must not expose legacy field ${legacy}`);
}

assert(flowConfigStoreHeader.includes('namespace FlowConfigStore'), 'FlowConfigStore header should exist');
assert(flowConfigStore.includes('irr_flow_v1'), 'flow config should use clean namespace irr_flow_v1');
assert(flowConfigStore.includes('kUlPerMinPerHz = 244897'), 'default K should match calibrated fixed-point value');
assert(flowConfigStore.includes('offsetMilliHz = 0'), 'default offset should be zero');
assert(flowConfigStore.includes('minValidFreqMilliHz = 500'), 'default minimum valid frequency should be 0.5 Hz');
assert(flowConfigStore.includes('warningFreqMilliHz = 4000'), 'default warning frequency should be 4 Hz');

assert(zoneConfigStore.includes('irr_zone_v1'), 'zone config should use clean namespace irr_zone_v1');
assert(zoneConfigStore.includes('zoneId <= 2'), 'zone defaults should enable only zone 1 and 2');
assert(zoneConfigStore.includes('config.flowId = 1'), 'zone defaults should assign all zones to Flow 1');
assert(zoneConfigStore.includes('lowFlowPermille = 100'), 'zone baseline default low flow threshold should be 10%');
assert(zoneConfigStore.includes('highFlowPermille = 3000'), 'zone baseline default high flow threshold should be 300%');
assert(zoneConfigStore.includes('flowFaultConfirmSec = 15'), 'zone baseline default fault confirmation should be 15 seconds');
assert(zoneConfigStore.includes('noPulseTimeoutSec = 10'), 'zone baseline default no pulse timeout should be 10 seconds');
assert(zoneConfigStore.includes('StoredZoneConfig') && zoneConfigStore.includes('CONFIG_BLOB_MAX_LEN'), 'zone config store should assert blob size');
assert(zoneConfigStoreHeader.includes('savePendingBaseline'), 'zone config store should expose pending baseline saves');
assert(zoneConfigStoreHeader.includes('applyPendingBaseline'), 'zone config store should expose pending baseline apply');
assert(zoneConfigStoreHeader.includes('restoreRollbackBaseline'), 'zone config store should expose rollback baseline restore');
assert(zoneConfigStore.includes('config.pendingBaseline') && zoneConfigStore.includes('config.rollbackBaseline'), 'zone config store should persist pending and rollback baselines');

assert(flowAlertStoreHeader.includes('namespace FlowAlertStore'), 'FlowAlertStore header should exist');
assert(flowAlertStore.includes('irr_falert_v1'), 'FlowAlertStore should use clean namespace irr_falert_v1');
assert(flowAlertStore.includes('FlowAlertStore::FlowAlert flows[Irrigation::MaxFlowMeters]'), 'Flow alerts should be stored per Flow');
assert(flowAlertStore.includes('setIdleLeak'), 'FlowAlertStore should support Flow-level idle leak alerts');
assert(!flowAlertStore.includes('zone:'), 'FlowAlertStore should not store Flow alerts as Zone faults');

assert(systemConfigStore.includes('irr_sys_v1'), 'system config should use clean namespace irr_sys_v1');
assert(!systemConfigStore.includes('kKeyBlob'), 'system config should not keep a canonical blob key');
assert(!systemConfigStore.includes('setPod(kNamespace'), 'system config should be scalar-only');
for (const legacy of [
  'durationPresets',
  'idleLeakDetectionEnabled',
  'calibrationSampleTarget',
  'calibrationMaxCaptureMin',
  'calibrationDetailCaptureSec',
  'calibrationDetailPulseLimit',
  'flowRateWindowSec',
  'flowChartIntervalSec',
  'flowChartHistoryMin',
]) {
  assert(!zoneTypes.includes(legacy), `SystemConfig must not keep legacy field ${legacy}`);
  assert(!systemConfigStore.includes(legacy), `SystemConfigStore must not keep legacy field ${legacy}`);
}
assert(systemConfigStore.includes('queuedPlanMaxDelaySec'), 'system config should include queued plan max delay');
assert(systemConfigStore.includes('idleLeakWindowSec'), 'system config should include idle leak window');
assert(systemConfigStore.includes('idleLeakPulseThreshold'), 'system config should include idle leak pulse threshold');

assert(planStore.includes('irr_plan_v1'), 'plan config should use clean namespace irr_plan_v1');
assert(planStore.includes('planKey') && planStore.includes('"p%u_%u"'), 'plan config should save each Zone/slot under a separate key');
assert(planStore.includes('kKeyNext') && planStore.includes('setInt(kNamespace, kKeyNext'), 'plan config should persist next plan id as a scalar');
assert(!planStore.includes('StoredPlansBlob'), 'plan config must not keep one large legacy blob');
assert(!planStore.includes('kKeyBlob'), 'plan config must not keep a canonical blob key');

assert(zoneManager.includes('bool flowBusy(uint8_t flowId)'), 'ZoneManager should own Flow-level busy checks');
assert(zoneManager.includes('zone.config().flowId == flowId'), 'Flow busy checks should use ZoneConfig.flowId');
assert(zoneManager.includes('FlowAlertStore::idleLeakActive(config.flowId)'), 'blockedReason should reject zones under a Flow leak alert');
assert(zoneManager.includes('BusinessEventLog::appendFlowIdleLeakDetected'), 'idle leak detection should record Flow-level events');
assert(!zoneManager.includes('BusinessEventLog::appendLeakDetected(zoneId'), 'idle leak detection should not record Flow leaks as Zone leaks');
assert(zoneManager.includes('bool startPlan('), 'ZoneManager should expose a unified plan start entrypoint');
assert(zoneManager.includes('return strcmp(ZoneManager::blockedReason(zoneId), "none") == 0'), 'Zone starts should reject same-Flow concurrency through blockedReason');
assert(zoneManager.includes('return "flow_busy"'), 'blockedReason should expose same-Flow busy rejection');
assert(zoneScheduler.includes('queuedPlanMaxDelaySec'), 'scheduler should honor queuedPlanMaxDelaySec');
assert(zoneScheduler.includes('ZoneManager::startPlan'), 'scheduler should start plans through ZoneManager');
assert(!zoneScheduler.includes('zone.start('), 'scheduler must not bypass Flow mutual exclusion');
assert(zoneRuntime.includes('learnedFlowMlPerMin > 0'), 'zone runtime should evaluate learned flow baselines');
assert(zoneRuntime.includes('flowFaultConfirmSec'), 'zone runtime should require continuous flow fault confirmation');
assert(zoneRuntime.includes('FLOW_LOW_STOPPED'), 'zone runtime should stop on confirmed low flow when configured');
assert(zoneRuntime.includes('FLOW_HIGH_STOPPED'), 'zone runtime should stop on confirmed high flow when configured');

assert(localControl.includes('kConfirmWindowMs = 5000'), 'local control should use a five-second same-button confirmation window');
assert(localControl.includes('IrrigationPins::ButtonPrevZone'), 'local control should use the previous-zone button');
assert(localControl.includes('IrrigationPins::ButtonNextZone'), 'local control should use the next-zone button');
assert(localControl.includes('IrrigationPins::ButtonSelect'), 'local control should use the select/start-stop button');
assert(localControl.includes('IrrigationPins::ButtonStopAll'), 'local control should use the Stop All button');
assert(localControl.includes('IrrigationPins::ButtonInfo'), 'local control should support the optional info button');
assert(localControl.includes('Irrigation::MaxZones'), 'local control should scan all configured zones');
assert(!localControl.includes('kLocalButtonZoneMax'), 'local control must not cap local operation to Zone 1/2');
assert(!localControl.includes('Road1UpButton') && !localControl.includes('Road2DownButton'), 'local control must not use legacy road-specific buttons');
assert(!safetyManager.includes('ZoneManager::startManual'), 'SafetyManager should not own local zone operations');
assert(!safetyManager.includes('Road1UpButton') && !safetyManager.includes('Road2DownButton'), 'SafetyManager must not special-case Zone 1/2');

assert(flowCalibration.includes('actualMl * 60000ULL') || flowCalibration.includes('totalActualMl * 60000ULL'), 'calibration should compute K from measured volume and pulse count');
assert(flowCalibration.includes('FlowConfigStore::savePendingCalibration'), 'calibration should save pending Flow calibration, not Zone parameters');
assert(!flowCalibration.includes('ZoneConfigStore::saveCandidate'), 'calibration must not save legacy Zone flow candidates');

assert(displayService.includes('Wire.begin(IrrigationPins::I2cSda, IrrigationPins::I2cScl)'), 'display service should use configured I2C pins');
assert(displayService.includes('LocalControl::selectedZoneId()'), 'display service should show the locally selected zone');
assert(displayService.includes('ZoneManager::blockedReason'), 'display service should show blockedReason');
assert(displayService.includes('fitLine'), 'display service should constrain LCD1602 line length');
assert(!displayService.includes('FlowConfigStore::set'), 'display service must not edit flow calibration');
assert(!displayService.includes('ZoneConfigStore::set'), 'display service must not edit zone config');

assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/irrigation/zones"'), 'web should expose a Zone config page');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/irrigation/flows"'), 'web should expose a Flow config page');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/irrigation/calibration"'), 'web should expose a calibration page');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/irrigation/settings"'), 'web should expose a system settings page');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/irrigation/plans"'), 'web should expose a plan page');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/irrigation/records"'), 'web should expose a records page');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/irrigation/events"'), 'web should expose an events page');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/api/v1/plans"'), 'web should expose plan JSON API');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/api/v1/records"'), 'web should expose records JSON API');
assert(irrigationWeb.includes('Esp32BaseWeb::addRoute("/api/v1/events"'), 'web should expose business events JSON API');
assert(irrigationWeb.includes('for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId)'), 'web pages should scan all configured zones');
assert(irrigationWeb.includes('if (!config.enabled)') && irrigationWeb.includes('continue;'), 'home page should hide disabled zones');
assert(irrigationWeb.includes('enabledPresent'), 'web forms should be able to submit unchecked enabled boxes');
assert(irrigationWeb.includes('flowHasEnabledZones(flowId)'), 'web should reject disabling a Flow while enabled Zones still use it');
assert(irrigationWeb.includes('ZoneManager::isZoneBusy(zoneId)'), 'web should reject editing a running Zone');
assert(irrigationWeb.includes('FlowConfigStore::savePendingCalibration'), 'web should save manual K+Offset edits as pending calibration');
assert(irrigationWeb.includes('FlowConfigStore::applyPendingCalibration'), 'web should expose pending calibration apply');
assert(irrigationWeb.includes('FlowConfigStore::restoreRollbackCalibration'), 'web should expose calibration rollback restore');
assert(irrigationWeb.includes('ZoneConfigStore::savePendingBaseline'), 'web should save manual Zone baselines as pending');
assert(irrigationWeb.includes('ZoneConfigStore::applyPendingBaseline'), 'web should expose pending Zone baseline apply');
assert(irrigationWeb.includes('ZoneConfigStore::restoreRollbackBaseline'), 'web should expose Zone baseline rollback restore');
assert(irrigationWeb.includes('ZoneManager::isFlowBusy(flowId)'), 'web should reject Flow calibration apply while that Flow is running');
assert(irrigationWeb.includes('queuedPlanMaxDelaySec'), 'web settings should expose queued plan max delay');
assert(!irrigationWeb.includes('kLocalButtonZoneMax'), 'web must not inherit local Zone 1/2 cap');
assert(platformio.includes('ESP32BASE_WEB_MAX_ROUTES=64'), 'route capacity should cover the expanded irrigation API surface');

console.log('check-web-structure passed');
