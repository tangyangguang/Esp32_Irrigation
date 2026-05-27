# Two Road Irrigation Design

## Goal

The irrigation application uses two roads as the default business model. A one-road installation is represented by explicitly disabling one road in irrigation settings, not by treating the product as a one-road system.

## Business Boundaries

- New defaults and cleared settings enable road 1 and road 2.
- Plan defaults include both roads with the same default duration.
- Manual watering defaults to all currently enabled roads.
- Disabled roads are hardware/business unavailable: they cannot be manually started and must not be executed by schedules.
- Schedule execution uses the effective plan content: configured plan duration intersected with currently enabled roads.
- If a scheduled plan has no effective road at trigger time, it is recorded as skipped and marked handled for that date.
- Future-facing pages display effective current execution content. Historical records remain factual snapshots and are not reinterpreted through current settings.

## Page Semantics

- Home shows current state, current watering state, manual operation for both roads, and current alert state.
- Recent plans shows today, tomorrow, and the day after tomorrow in one table. It is for future control, especially skipping upcoming executions.
- Historical records show completed watering sessions only.
- Plan configuration edits schedule definitions for both roads.
- Irrigation settings controls road availability and per-road calibration/configuration.

## Validation

- Static checks must reject one-road defaults and hard-coded manual API defaults.
- PlatformIO build must pass after implementation.
