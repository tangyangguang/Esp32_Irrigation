# Home Zone Card Overview Design

## Goal

Redesign the irrigation home page from a table into a row-based operational dashboard. The page must show only enabled zones, keep every enabled zone in the same information structure, show each zone's current runtime state and today's plan progress at a glance, and add a small global weather forecast display.

This is a Web UI and application data presentation change. It does not change valve control, flow sampling, scheduling execution, or safety behavior.

## Current Context

The current home page shows enabled zones in a table with these per-zone fields:

- Zone name
- State
- Task type
- Target duration
- Remaining duration
- Current flow
- Estimated volume
- Actions

The page already polls `/api/v1/status` and uses `/api/v1/flow/history?zoneId=N` for flow chart history. The status JSON also contains useful fields such as `elapsedSec`, `pulses`, `flowRateReady`, `errorCode`, and `planId`.

## Layout

The home page keeps the existing global metric panel, adds a global weather forecast strip, then replaces the zone table with a row layout.

Only enabled zones are rendered on the home page. Disabled zones remain visible and configurable on the zone management page, but do not take space on the home page.

Every enabled zone is rendered as one full-width row:

- Left side: a larger runtime card for current state, metrics, flow chart, and actions.
- Right side: a smaller plan card for today's plan progress and all of today's plan entries.

This means 1 enabled zone shows 1 row, 2 enabled zones show 2 rows, and so on. The page structure is consistent across the user's common deployments: a long-running 1-zone device and a long-running 2-zone device should feel like the same product with a different number of rows, not two different layouts.

On mobile-width screens, each row stacks vertically with the runtime card above the plan card.

## Zone Card Content

Each enabled zone row has two cards.

The runtime card always displays:

- Header: zone name and state tag.
- Task line: current task label or idle text.
- Main metrics: remaining time, current flow, estimated volume.
- Flow chart area: fixed height and fixed position, with visible axes and grid labels.
- Footer: runtime detail on the left and the relevant action on the right.

The plan card always displays:

- Summary: completed plan count, completed planned minutes / total planned minutes, and remaining planned minutes.
- Plan list: all enabled plans for the zone that are scheduled to run today.
- Per-plan details: start time, planned duration, concise note, and status.

State-specific behavior:

- Idle: task line shows no running task; flow and remaining time show `-`; chart shows an empty baseline.
- Starting/running: task line shows manual or plan watering; remaining time, flow, estimated volume, and chart update from polling.
- Error: state tag uses danger styling and remains clickable to open the existing error detail dialog; task line shows the error reason.
- Leak alert active: start action is replaced with the existing leak alert blocked text.

Plan statuses are intentionally simple:

- `完成`: the plan has already run or been checked and completed for today.
- `运行中`: the plan is currently executing.
- `未完成`: the plan is still pending or was not completed today.

The plan card does not include a separate "next plan" feature block. The plan list is short enough that the next pending item is visible in context, and a separate block would duplicate the list.

## Information Priority

The home page should prioritize field readability in this order:

1. Zone identity and state.
2. Remaining time.
3. Current flow.
4. Estimated volume.
5. Flow chart trend.
6. Task type and runtime progress.
7. Today's plan progress.
8. Actions.

`elapsedSec` should be used in the footer as `已运行 / 目标`, because it gives better operational context than target duration alone.

`pulses` and `planId` should not be shown by default on the home page. They are diagnostic identifiers and would compete with higher-value operational information. They can remain available through API responses and records/events pages.

The plan summary should avoid duplicate information. It should not show both raw plan count and a completed/total count. The planned-minute progress is higher value than raw plan count alone because it answers how much of today's planned watering has actually been completed.

## Weather Forecast

The home page includes a global weather forecast strip between the global metric panel and the zone rows.

Weather content:

- Current weather summary: temperature, condition, 24-hour rain probability, and wind level if available.
- Three-day forecast: today, tomorrow, and the following day, each with temperature range and rain probability.

Weather is display-only in this project. It must not automatically alter watering schedules, skip plans, or change valve behavior. Weather-based automation remains an external-system concern using explicit APIs, not controller-core logic.

If no weather snapshot is available, the strip should show a compact unavailable state such as `暂无天气数据`, without blocking the rest of the home page.

## Data Flow

The existing status polling remains the primary update mechanism.

- `/api/v1/status` updates card state, metrics, task text, error state, and actions.
- `/api/v1/flow/history?zoneId=N` updates the chart for enabled zones that are starting/running.
- Server-rendered plan data provides today's per-zone plan summary and list.
- A stored or externally supplied weather snapshot provides the weather strip.

The chart region stays visible for idle and error cards, but the history request can remain limited to running zones to avoid unnecessary network and rendering work.

If the enabled zone set changes while the page is open, the existing structural reload behavior remains acceptable. A reload is clearer than trying to patch card additions/removals into a long-lived page.

The initial weather implementation should prefer an explicit local snapshot over direct third-party weather API calls from the ESP32. This keeps API keys, provider-specific HTTPS requirements, rate limits, and location handling outside the controller. A later implementation can add a small business API for an external system to publish the weather snapshot, subject to the same POST/auth/escape rules as other business APIs.

## Interaction

Existing safety rules remain unchanged:

- Start opens the existing manual watering dialog.
- Stop uses POST and confirmation.
- Clear error uses POST and the existing error-specific confirmation.
- Stop all remains below the zone rows and is disabled when nothing is running.
- State-changing forms keep JavaScript `confirm` and POST semantics.

The existing error detail dialog is reused. The error state tag in each card opens the same dialog as the current table implementation.

## Visual Design

The row layout should match the existing Esp32Base Web visual language:

- Use restrained borders, soft backgrounds, and existing tag/button classes where possible.
- Keep border radius at 8px or less.
- Avoid nested decorative cards; metric boxes are compact fixed-format UI elements inside the zone card.
- Keep text compact and readable on mobile.
- Preserve stable chart dimensions so chart loading or empty states do not resize cards.
- Avoid overusing bold text in the plan card. The plan card is a compact operational list, not a marketing panel.
- The flow chart should include a clear x-axis, y-axis, grid lines, and useful tick labels. A simple unlabeled sparkline is not enough for this use case because users need to judge whether flow is normal, rising, falling, or absent.

## Testing

Verification should cover:

- PlatformIO build succeeds.
- Home page renders with 1, 2, 3, and 4 enabled zones as 1, 2, 3, and 4 rows.
- Mobile-width layout stacks each row into runtime card then plan card without overlapping text or controls.
- Running zone polling updates remaining time, flow, volume, and chart.
- Idle zones keep the chart placeholder and do not show stale flow values.
- Error zones show the error reason and can open the existing error dialog.
- Plan cards show today's per-zone plan progress, planned-minute progress, remaining planned minutes, and all of today's plan entries.
- Plan statuses are shown as `完成`, `运行中`, or `未完成`.
- Weather strip shows available weather data or a compact unavailable state.
- Weather display does not alter plan execution or valve behavior.
- Leak alert state blocks start actions as before.
- Stop, stop all, manual start, and clear error still use POST and confirmation.

Hardware behavior is not changed by this design. Any runtime watering/flow validation remains not hardware-verified until tested on the ESP32 device.
