# Home Zone Card Overview Design

## Goal

Redesign the irrigation home page zone status area from a table into adaptive zone cards. The page must show only enabled zones, keep each enabled zone in its own stable visual area, and preserve the important operational information currently available on the home page.

This is a UI/layout change for the application Web page. It does not change valve control, flow sampling, scheduling, storage, or public API semantics.

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

The home page keeps the existing global metric panel, then replaces the zone table with an adaptive card grid.

Only enabled zones are rendered on the home page. Disabled zones remain visible and configurable on the zone management page, but do not take space on the home page.

Grid behavior:

- 1 enabled zone: one card is centered and constrained to a readable maximum width.
- 2 enabled zones: two columns on desktop, one column on mobile.
- 3 enabled zones: two columns on desktop, with the third card centered on the second row; one column on mobile.
- 4 enabled zones: two-by-two grid on desktop, one column on mobile.

Each zone card uses the same internal structure and fixed positions so the layout does not jump when a zone starts, stops, or enters an error state.

## Zone Card Content

Each enabled zone card always displays:

- Header: zone name and state tag.
- Task line: current task label or idle text.
- Main metrics: remaining time, current flow, estimated volume.
- Flow chart area: fixed height and fixed position.
- Footer: runtime detail on the left and the relevant action on the right.

State-specific behavior:

- Idle: task line shows no running task; flow and remaining time show `-`; chart shows an empty baseline.
- Starting/running: task line shows manual or plan watering; remaining time, flow, estimated volume, and chart update from polling.
- Error: state tag uses danger styling and remains clickable to open the existing error detail dialog; task line shows the error reason.
- Leak alert active: start action is replaced with the existing leak alert blocked text.

## Information Priority

The home page should prioritize field readability in this order:

1. Zone identity and state.
2. Remaining time.
3. Current flow.
4. Estimated volume.
5. Task type and runtime progress.
6. Actions.
7. Flow chart trend.

`elapsedSec` should be used in the footer as `已运行 / 目标`, because it gives better operational context than target duration alone.

`pulses` and `planId` should not be shown by default on the home page. They are diagnostic identifiers and would compete with higher-value operational information. They can remain available through API responses and records/events pages.

## Data Flow

The existing status polling remains the primary update mechanism.

- `/api/v1/status` updates card state, metrics, task text, error state, and actions.
- `/api/v1/flow/history?zoneId=N` updates the chart for enabled zones that are starting/running.

The chart region stays visible for idle and error cards, but the history request can remain limited to running zones to avoid unnecessary network and rendering work.

If the enabled zone set changes while the page is open, the existing structural reload behavior remains acceptable. A reload is clearer than trying to patch card additions/removals into a long-lived page.

## Interaction

Existing safety rules remain unchanged:

- Start opens the existing manual watering dialog.
- Stop uses POST and confirmation.
- Clear error uses POST and the existing error-specific confirmation.
- Stop all remains below the zone card grid and is disabled when nothing is running.
- State-changing forms keep JavaScript `confirm` and POST semantics.

The existing error detail dialog is reused. The error state tag in each card opens the same dialog as the current table implementation.

## Visual Design

The card grid should match the existing Esp32Base Web visual language:

- Use restrained borders, soft backgrounds, and existing tag/button classes where possible.
- Keep border radius at 8px or less.
- Avoid nested decorative cards; metric boxes are compact fixed-format UI elements inside the zone card.
- Keep text compact and readable on mobile.
- Preserve stable chart dimensions so chart loading or empty states do not resize cards.

## Testing

Verification should cover:

- PlatformIO build succeeds.
- Home page renders with 1, 2, 3, and 4 enabled zones.
- Mobile-width layout becomes a single column without overlapping text or controls.
- Running zone polling updates remaining time, flow, volume, and chart.
- Idle zones keep the chart placeholder and do not show stale flow values.
- Error zones show the error reason and can open the existing error dialog.
- Leak alert state blocks start actions as before.
- Stop, stop all, manual start, and clear error still use POST and confirmation.

Hardware behavior is not changed by this design. Any runtime watering/flow validation remains not hardware-verified until tested on the ESP32 device.
