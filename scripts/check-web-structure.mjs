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

console.log('check-web-structure passed');
