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
  'src/storage/EventStore.h',
  'src/storage/EventStore.cpp',
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
const records = read('src/storage/RecordStore.h') + read('src/storage/RecordStore.cpp');
const web = read('src/web/IrrigationWeb.cpp');
const pins = read('include/Pins.h');
const pio = read('platformio.ini');

assert(pins.includes('MaxRoads = 4'), 'hardware model should expose four fixed zones');
assert(pins.includes('DefaultRoadEnabledMask = 0x03'), 'default hardware enable mask should enable zone 1 and 2');

assert(zoneTypes.includes('NameMaxBytes = 32'), 'names should support at least 10 Chinese characters');
assert(zoneTypes.includes('enum class ZoneState') && zoneTypes.includes('STARTING') && zoneTypes.includes('ERROR'), 'ZoneState should include the final state machine');
assert(zoneTypes.includes('enum class TaskResult') && zoneTypes.includes('FLOW_START_TIMEOUT') && zoneTypes.includes('FLOW_NO_PULSE_TIMEOUT'), 'TaskResult should distinguish flow start and running pulse timeouts');
assert(zoneTypes.includes('enum class PlanObservationStatus') && zoneTypes.includes('MISSED') && zoneTypes.includes('SKIPPED_CONFIG_INVALID'), 'plan observation statuses should include missed and invalid config outcomes');

assert(systemConfig.includes('maxWateringDurationSec'), 'system config should include max watering duration');
assert(systemConfig.includes('scheduleGraceSec'), 'system config should include schedule grace seconds');
assert(systemConfig.includes('durationPresets'), 'system config should include fixed duration presets');
assert(systemConfig.includes('86400'), 'max watering duration upper bound should allow up to 24 hours');
assert(systemConfig.includes('14400'), 'max watering duration default should be 4 hours');

assert(zoneConfig.includes('ZoneConfig') && zoneConfig.includes('startTimeoutSec') && zoneConfig.includes('suppressError'), 'zone config should include timeout and suppressError fields');
assert(zoneConfig.includes('Zone 1') && zoneConfig.includes('Zone 4'), 'zone defaults should name all four zones');
assert(zoneError.includes('leakAlertActive') && zoneError.includes('ZoneError'), 'zone errors and leak alert should be persistent');

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
assert(records.includes('configSnapshot') && records.includes('startTimeoutSec') && records.includes('flowNoPulseTimeoutSec'), 'records should include config snapshot timeout fields');
assert(records.includes('createFixedFile') && read('src/storage/EventStore.cpp').includes('createFixedFile'), 'fixed-capacity binary stores should use Esp32BaseFs::createFixedFile');
assert(!records.includes('calloc') && !read('src/storage/EventStore.cpp').includes('calloc'), 'fixed-capacity binary stores should not allocate full files on heap');

assert(web.includes('/api/v1/zone/start') && web.includes('zoneId'), 'web API should use fixed endpoint plus zoneId parameter');
assert(web.includes('/api/v1/plan/update') && web.includes('planId'), 'plan API should use fixed endpoint plus planId parameter');
assert(!web.includes('/api/v1/water/start'), 'old water start API should be removed');
assert(!web.includes('road_id') && !web.includes('roadId'), 'external API should not expose road identifiers');

for (const marker of ["method='post'", 'method=\"post\"']) {
  let index = web.indexOf(marker);
  while (index !== -1) {
    const snippet = web.slice(index, index + 800);
    assert(snippet.includes('confirm('), `POST form must include browser confirmation near offset ${index}`);
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

assert(pio.includes('-D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL'), 'project should keep Esp32Base full profile');
