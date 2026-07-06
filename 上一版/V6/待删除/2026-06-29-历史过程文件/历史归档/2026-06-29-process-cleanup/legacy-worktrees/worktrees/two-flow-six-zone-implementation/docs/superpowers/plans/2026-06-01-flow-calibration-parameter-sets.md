# Flow Calibration Parameter Sets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement per-zone current, candidate, and previous flow parameter sets with candidate-only editing, explicit apply, restore, copy-from-other-zone, and calibration-generated candidates.

**Architecture:** Evolve the existing per-zone configuration record into the single authoritative record for current, candidate, and previous flow parameters. `ZoneConfigStore` owns validation, normalization, persistence, and atomic per-zone parameter transitions; `FlowCalibration` generates candidates; `IrrigationWeb` exposes the UI/API flow.

**Tech Stack:** PlatformIO Arduino ESP32, Esp32Base config/app-events/web APIs, C++ POD storage structs, existing chunked HTML rendering.

---

### Task 1: Flow Parameter Model And Store Operations

**Files:**
- Modify: `src/domain/ZoneTypes.h`
- Modify: `src/storage/ZoneConfigStore.h`
- Modify: `src/storage/ZoneConfigStore.cpp`
- Modify: `src/domain/ZoneTaskRunner.cpp`
- Modify: `src/storage/RecordStore.h`

- [ ] Add `Irrigation::FlowParameters`, `FlowParameterSource`, and `FlowCandidateSlot` in `ZoneTypes.h`.
- [ ] Replace `ZoneConfig` and `ZoneConfigSnapshot` loose flow fields with `FlowParameters flow`.
- [ ] Store `candidateFlow` and `previousFlow` in `ZoneConfig`.
- [ ] Add `ZoneConfigStore` APIs: `validateFlowParameters`, `saveManualCandidate`, `saveCalibrationCandidate`, `copyCurrentToCandidate`, `applyCandidate`, `restorePrevious`, `flowParametersEqual`.
- [ ] Normalize `startupEstimatedMl` to `0` when `startupPulseLimit == 0`.
- [ ] Bump stored zone config version and default candidate/previous slots to unset.
- [ ] Update runtime snapshots and volume estimation to read `flow`.
- [ ] Run `pio run`; expected failure before call-site migration is allowed only inside this task, and final task state must compile.

### Task 2: Calibration Service Writes Candidates

**Files:**
- Modify: `src/domain/FlowCalibration.h`
- Modify: `src/domain/FlowCalibration.cpp`

- [ ] Rename user-facing API concept from recommendation apply to candidate generation while keeping diagnostic fields.
- [ ] Make `computeRecommendation` reject mixed-zone valid samples before fitting.
- [ ] Make `start` reject starting a different zone when sample work area already contains samples.
- [ ] Replace `applyRecommendation` with `saveCandidate`, writing `ZoneConfigStore::saveCalibrationCandidate`.
- [ ] Keep sample clear independent from candidate storage.
- [ ] Run `pio run`; expected: pass.

### Task 3: Business Events For Apply And Restore

**Files:**
- Modify: `src/domain/BusinessEventLog.h`
- Modify: `src/domain/BusinessEventLog.cpp`

- [ ] Add event appenders for candidate applied and previous restored.
- [ ] Include zone ID, old parameter values, new parameter values, candidate source, and copied-from source zone where available.
- [ ] Use existing App Event value fields for compact numeric values and event text for full old/new/source detail.
- [ ] Run `pio run`; expected: pass.

### Task 4: Web/API Candidate Workflow

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`
- Modify: `platformio.ini`

- [ ] Remove direct current flow parameter inputs from `/irrigation/zones` and `/api/v1/zone/config`.
- [ ] Show current, candidate, and previous parameter sets per zone on `/irrigation/calibration`.
- [ ] Add manual candidate form with range validation.
- [ ] Add copy-from-other-zone-current form.
- [ ] Change calibration compute action to "生成候选参数"; show diagnostics as candidate diagnostics.
- [ ] Reuse `/api/v1/calibration/apply` to apply a candidate after checking target zone idle.
- [ ] Add restore previous API and route.
- [ ] Add manual candidate and copy candidate API routes.
- [ ] Disable or hide apply when candidate equals current.
- [ ] Reject apply/restore while the target zone is running.
- [ ] Increase `ESP32BASE_WEB_MAX_ROUTES` enough for the new routes.
- [ ] Run `pio run`; expected: pass.

### Task 5: Validation And Cleanup

**Files:**
- Modify as needed only in files touched above.

- [ ] Search for stale `推荐参数`, `applyRecommendation`, and direct current flow parameter form fields.
- [ ] Run `pio run`; expected: pass.
- [ ] Review `docs/03_web_validation_checklist.md` against implemented UI/API behavior.
- [ ] Commit the implementation.

## Self-Review

Spec coverage:
- Current/candidate/previous persistence: Task 1.
- Candidate sources and copied-from zone ID: Tasks 1 and 4.
- Calibration-generated candidate: Task 2.
- Mixed-zone sample rejection: Task 2.
- Apply, restore, unchanged candidate rejection: Tasks 1 and 4.
- Event logging: Task 3.
- Web/API behavior: Task 4.
- Build verification and stale wording cleanup: Task 5.

No placeholders remain. Exact UI text and route wiring are implemented in Task 4 using existing `IrrigationWeb.cpp` helpers.
