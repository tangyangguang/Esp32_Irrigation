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
const flowConfigStore = read('src/storage/FlowConfigStore.cpp');
const flowConfigStoreHeader = read('src/storage/FlowConfigStore.h');
const systemConfigStore = read('src/storage/SystemConfigStore.cpp');
const zoneManager = read('src/domain/ZoneManager.cpp');
const zoneScheduler = read('src/domain/ZoneScheduler.cpp');
const zoneRuntime = read('src/domain/Zone.cpp');
const localControl = read('src/domain/LocalControl.cpp');
const safetyManager = read('src/domain/SafetyManager.cpp');
const flowCalibration = read('src/domain/FlowCalibration.cpp');

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

assert(zoneManager.includes('bool flowBusy(uint8_t flowId)'), 'ZoneManager should own Flow-level busy checks');
assert(zoneManager.includes('zone.config().flowId == flowId'), 'Flow busy checks should use ZoneConfig.flowId');
assert(zoneManager.includes('bool startPlan('), 'ZoneManager should expose a unified plan start entrypoint');
assert(zoneManager.includes('!flowBusy(config.flowId)'), 'Zone starts should reject same-Flow concurrency');
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

console.log('check-web-structure passed');
