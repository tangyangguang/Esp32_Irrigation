# Two Road Irrigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make two-road operation the default and make manual, schedule, and page logic consistently respect enabled-road availability.

**Architecture:** Keep the existing storage/domain/web boundaries. Add small helpers where the current system needs an effective plan view, so schedule execution and future-facing pages do not duplicate stale-road logic.

**Tech Stack:** ESP32 Arduino, PlatformIO, Esp32Base, C++17-style embedded code, Node.js static validation script.

---

### Task 1: Static Regression Checks

**Files:**
- Modify: `scripts/check-web-structure.mjs`

- [ ] Add checks for `DefaultRoadEnabledMask = 0x03`, settings defaults using `0x03`, plan default road 2 duration, manual API defaults from `SettingsStore::isRoadEnabled`, and plan execution sanitizing disabled roads.
- [ ] Run `node scripts/check-web-structure.mjs` and verify it fails on the current implementation.

### Task 2: Defaults

**Files:**
- Modify: `include/Pins.h`
- Modify: `src/storage/SettingsStore.cpp`
- Modify: `src/storage/PlanStore.cpp`

- [ ] Change road defaults and clamp fallback from one-road to two-road.
- [ ] Change default plan road 2 duration from `0` to `300`.
- [ ] Run `node scripts/check-web-structure.mjs` and continue only after default-related failures are gone.

### Task 3: Effective Plan Execution

**Files:**
- Modify: `src/domain/WateringPlanScheduler.cpp`

- [ ] Compute effective per-road seconds before calling `WateringSession::startManual`.
- [ ] Record and mark skipped when no currently enabled road remains.
- [ ] Use effective seconds in plan-start events for accurate execution facts.

### Task 4: Web Form and Future Plan Semantics

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`

- [ ] Default manual start API to currently enabled roads.
- [ ] Make plan save clear disabled-road seconds even if disabled controls are omitted from the submitted form.
- [ ] Show current effective plan content on recent plan rows.
- [ ] Rename the home alert panel from `异常` to `当前告警`.

### Task 5: Documentation and Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/03_web_validation_checklist.md`

- [ ] Update docs to describe default two-road behavior.
- [ ] Run `node scripts/check-web-structure.mjs`.
- [ ] Run `pio run`.
- [ ] Commit and push.
