# Flow Calibration Parameter Sets Design

## Goal

Flow calibration must separate parameter generation from parameter activation. A calibration run, manual entry, or copy operation prepares a candidate parameter set, but the active irrigation estimate only changes after the user explicitly applies that candidate.

The design keeps the vocabulary small:

- Current parameters: the parameter set currently used by watering records and runtime volume estimation.
- Candidate parameters: a saved per-zone draft parameter set that can be generated, edited, or copied before activation.
- Previous parameters: the per-zone parameter set that was active immediately before the most recent apply or restore operation.

There is no direct edit path for current parameters.

## Parameter Fields

Each parameter set contains the same runtime fields:

```text
startupPulseLimit     startup-stage pulse count; 0 disables startup compensation
startupEstimatedMl    estimated startup-stage volume in ml
stablePulsePerLiter   stable-stage pulses per liter
```

Validation follows the current zone configuration limits:

```text
startupPulseLimit: 0..10000
startupEstimatedMl: 0..10000 ml
stablePulsePerLiter: 1..10000
```

When `startupPulseLimit` is `0`, `startupEstimatedMl` is treated as `0`.

## Per-Zone State

Each zone independently stores:

```text
current parameter set
optional candidate parameter set
optional previous parameter set
```

Candidate and previous parameter sets are persistent. They survive reboot and power loss.

The persistent storage boundary should be one complete per-zone calibration-parameter record, not scattered independent keys. That record contains the current parameter set, optional candidate slot, optional previous slot, candidate source, and candidate source details. This keeps apply and restore operations from leaving a partially updated state such as current values saved without the matching previous values.

The current parameter set must have exactly one authoritative storage source. Runtime watering snapshots and volume estimation read from that source. Implementation must not maintain a second independent copy of current calibration parameters in another store; if the existing zone configuration record remains the storage vehicle, it should be evolved into the single complete per-zone record rather than duplicated by a separate calibration-current record.

Candidate parameters default to unset. The page must display an unset candidate as "no candidate parameters" rather than silently mirroring the current values.

Previous parameters default to unset. The restore action is hidden or disabled until the zone has a previous parameter set.

## Candidate Sources

Candidate parameters can be created or replaced by exactly three actions:

```text
manual       user edits and saves candidate values
calibration  calibration samples are computed into candidate values
copied       another zone's current parameters are copied into this zone's candidate values
```

The source is stored only for user-facing context. It is not an audit system and does not create extra runtime behavior.

For copied candidates, the source details include the source zone ID. The page can then display context such as "copied from Zone 1 current parameters", and the apply event can include that source context.

All candidate creation actions may replace an existing candidate, but the Web page must clearly confirm that the current candidate will be replaced.

## Apply Flow

Applying a candidate is the only way to change current parameters.

For the target zone:

```text
1. Require a valid candidate parameter set.
2. Reject the operation if the candidate values are identical to the current values.
3. Require the target zone to be idle.
4. Save the current parameter set as previous parameters.
5. Copy the candidate parameter set into current parameters.
6. Keep the candidate parameter set.
7. Reload the zone runtime configuration.
```

Keeping the candidate after apply preserves the calibration context and avoids hiding the values the user just activated.

The system should not persist a separate "applied" flag. The page derives candidate state by comparing the candidate's three parameter values with the current values:

```text
candidate equals current     display as "matches current"
candidate differs from current display as "pending apply"
```

This avoids stale state after restore operations.

When the candidate equals the current parameter set, apply must have no side effects: it must not overwrite previous parameters and must not write a business event. The Web page should disable or hide the apply action in this state; the API should reject the request with a clear error such as `candidate_unchanged`.

## Restore Flow

Restoring previous parameters is also per-zone and requires the target zone to be idle.

The operation swaps current and previous parameters:

```text
current <-> previous
```

This gives the user a simple way to undo a mistaken switch and then switch back again without introducing multi-level history.

## Copy Flow

Copying is deliberately limited:

```text
source: another zone's current parameters
target: this zone's candidate parameters
```

The system must not copy another zone's candidate parameters. It must not directly overwrite the target zone's current parameters.

Copying from the same zone's current parameters is not allowed as a copy action. If the user wants to edit from the current values, the manual candidate form may be prefilled from this zone's current values, but a candidate is created only after the user explicitly saves the manual candidate.

## Calibration Samples

Calibration samples remain a temporary working area. Computing calibration output writes a candidate parameter set for the sampled zone; it does not apply the parameters.

After candidate generation, the candidate is independent from the sample list. Clearing samples does not clear candidate parameters.

Samples from multiple zones must not be mixed in one computation. The UI and API should prevent mixing early: once the sample work area contains samples for one zone, starting calibration for another zone is rejected until samples are cleared. If mixed-zone samples are ever present because of an older state or unexpected path, computation must fail with a clear error rather than silently choosing a subset.

The existing wording "recommended parameters" should be replaced in the user experience with "candidate parameters". "Recommended" may remain only as explanatory text for a candidate source, not as a separate concept.

## Web And API Semantics

The calibration page should show the parameter lifecycle per zone:

```text
Current parameters   read-only, explicitly labeled as in use
Candidate parameters editable, generated by calibration, or copied from another zone's current parameters
Previous parameters  read-only, restorable when available
```

The sample collection area remains part of the calibration page. Its compute action becomes "generate candidate parameters".

Dangerous or state-changing Web actions use POST, authentication, and JavaScript confirmation, consistent with project rules.

API actions should remain action-oriented even if the exact route names are adjusted during implementation:

```text
save manual candidate
copy current parameters from another zone to candidate
apply candidate
restore previous
```

## Event Log

Business event logs should record durable runtime-affecting changes:

```text
candidate applied
previous parameters restored
```

Each event includes the zone ID, the old three-parameter current values, and the new three-parameter current values. Applying a candidate also includes the candidate source and any source details such as copied-from zone ID.

Candidate edits and copy operations do not need business event entries unless later requirements call for detailed audit logs.

## Testing And Validation

Implementation validation should cover:

- Default state: no candidate and no previous parameters for every zone.
- Manual candidate save validates ranges and persists across restart.
- Calibration compute writes candidate parameters and does not change current parameters.
- Applying candidate updates current parameters, stores previous parameters, preserves candidate, and reloads the zone.
- Applying an unchanged candidate is rejected without overwriting previous parameters or writing an event.
- Restore swaps current and previous parameters.
- Apply and restore are rejected when the target zone is running.
- Cross-zone copy only accepts another zone's current parameters and writes the target candidate.
- Copied candidates retain copied-from zone ID as source detail.
- Starting calibration for a different zone is rejected while samples for another zone exist.
- Mixed-zone calibration samples are rejected defensively if they ever exist.
- Clearing samples does not clear candidate parameters.
- PlatformIO build passes.

Hardware behavior that depends on real valves, sensors, or flow pulses remains unverified until tested on the ESP32 device.
