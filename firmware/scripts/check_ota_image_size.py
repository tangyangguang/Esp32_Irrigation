import csv
from pathlib import Path

Import("env")


# The production IOT image retains the existing N4 partition table so OTA
# upgrades preserve LittleFS and user configuration. Its MQTT/TLS stack leaves
# just over 8% with the non-secret release fixture, so 8% is the explicit floor.
MINIMUM_REMAINING_PERCENT = 8.0


def _parse_size(value):
    return int(str(value).strip(), 0)


def _partition_path():
    configured = env.GetProjectOption("board_build.partitions")
    path = Path(str(configured))
    if not path.is_absolute():
        path = Path(env.subst("$PROJECT_DIR")) / path
    return path.resolve()


def _ota_slots(path):
    slots = []
    with path.open("r", encoding="utf-8", newline="") as source:
        rows = csv.reader(line for line in source if not line.lstrip().startswith("#"))
        for row in rows:
            if len(row) < 5:
                continue
            name, partition_type, subtype, _, size = (item.strip() for item in row[:5])
            if partition_type == "app" and subtype.startswith("ota_"):
                slots.append((name, _parse_size(size)))
    if not slots:
        raise RuntimeError("partition table has no OTA app slots: %s" % path)
    return slots


def _check_image_size(target, source, env):
    del source
    image_path = Path(str(target[0])).resolve()
    partition_path = _partition_path()
    slots = _ota_slots(partition_path)
    slot_capacity = min(size for _, size in slots)
    image_size = image_path.stat().st_size
    remaining = slot_capacity - image_size
    remaining_percent = remaining * 100.0 / slot_capacity
    slot_summary = ", ".join("%s=%d" % item for item in slots)
    print(
        "OTA image size: partition=%s slots=[%s] image=%d remaining=%d (%.2f%%)"
        % (partition_path, slot_summary, image_size, remaining, remaining_percent)
    )
    if remaining < 0:
        print("ERROR: firmware.bin exceeds the smallest OTA app slot")
        env.Exit(1)
    if remaining_percent < MINIMUM_REMAINING_PERCENT:
        print(
            "ERROR: production IOT firmware requires at least %.1f%% OTA slot headroom"
            % MINIMUM_REMAINING_PERCENT
        )
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _check_image_size)
