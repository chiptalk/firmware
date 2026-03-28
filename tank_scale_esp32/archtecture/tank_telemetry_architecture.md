# Tank Telemetry Architecture (Draft)

## Current Implementation Status (Codebase Snapshot)

This document now reflects both the target design and what is already implemented in firmware.

Implemented now (edge):

- RS232 scale parsing (T7E-style frames) with normalized signed readings
- OLED main screen with:
  - status icons (Wi-Fi / internet proxy / scale)
  - comm arrows (TX/RX activity based on HTTP + MQTT activity)
  - top-strip IP display
  - auto-scaled weight font
- Wi-Fi setup AP portal + STA web UI
- NTP sync gate before IoT startup
- AWS IoT fleet provisioning + device MQTT connect
- adaptive telemetry publishing by detected tank state
- heartbeat topic and payload (health + IP + RSSI)
- provisional flow event topic and payload (`fill/discharge` start/end)
- MQTT debug mode (`MQTT_DEBUG_MODE`)
- local RAM outbox buffering for failed/offline publishes (bounded)
- runtime telemetry tuning via device web UI (with restore defaults)
- OLED sleep/wake policy (display-only sleep)

Not yet implemented (edge):

- persistent settings storage (web UI settings currently runtime-only)
- persistent outbox (current queue is RAM-only; lost on reboot)
- claim delivery workflow to device (`tenantId` assignment downlink)
- robust cleaning detection tuning/validation (initial heuristic exists)

## Purpose

This project is a tank data logger that:

- reads tank weight/level from an RS232-connected scale
- shows current tank amount in real time
- sends telemetry to AWS IoT
- feeds a SaaS management widget and reconciliation workflows
- supports a tenant/device claim workflow (device starts unclaimed, then is assigned to a tenant)


## Operational Context (Observed)

- Typical day:
  - fill cycles: ~3 per day
  - discharge cycles: ~1 per day
- Fill duration: ~30 to 60 minutes
- Discharge duration: ~1 to 15 minutes
- Cleaning sessions: every ~1 to 3 days

User expectations:

- real-time current tank amount
- recent cycle visibility (past few days)
- operational punctuality (did fill/discharge happen on schedule?)
- data consistency for reconciliation against other measurements/systems


## Design Goals

- Real-time enough for operators (human-friendly updates)
- Low bandwidth / low cloud cost during idle periods
- Preserve enough data fidelity for cycle analytics and reconciliation
- Reliable device operation with intermittent connectivity
- Clean tenant isolation after claim


## High-Level Architecture

### Edge Device (ESP32)

- Reads RS232 scale continuously
- Filters/normalizes readings
- Detects tank state:
  - `idle`
  - `filling`
  - `discharging`
  - `cleaning`
  - `unknown`
- Publishes to AWS IoT Core using MQTT over TLS
- Starts in `unclaimed` mode
- After claim, publishes under tenant-specific topics
- Provides local configuration UI for telemetry behavior tuning

Current firmware module split (separation of concerns):

- `ScaleReader`: UART + parsing + normalized weight values
- `TankTelemetry`: state machine + parameterized thresholds/intervals
- `AwsIotProvisioner`: provisioning, MQTT session, publish helpers
- `MqttOutbox`: bounded local queue for failed publishes
- `DisplayManager`: OLED rendering + display sleep/wake control
- `WebServerManager`: AP setup + STA settings UI/API (runtime config editing)
- `App`: orchestration only (wires modules together)

### AWS Ingestion

- AWS IoT Core (MQTT broker + auth)
- IoT Rules route telemetry to storage/services:
  - time-series store (recommended: Timestream)
  - summary/metadata store (recommended: DynamoDB)
  - optional Lambda for transformations/enrichment

### SaaS / Widget Layer

- Realtime widget:
  - current amount
  - current state (idle/filling/discharging/cleaning)
  - recent trend
- Historical/cycle view:
  - fill/discharge cycles (last few days)
  - punctuality/regularity indicators
- Reconciliation:
  - cycle summaries + time-series trace for audits


## Telemetry Frequency Strategy (Recommended)

Use adaptive publishing: sample fast locally, publish based on state/activity.

### Local Sampling (device-side)

- Read scale at `1 Hz` (every 1 second)
- Compute rate of change (`delta weight / time`)
- Use transition confirmation windows and minimum delta to avoid noise-triggered state flips

Current edge implementation detail:

- state updates are rate-based and parameterized
- state changes require both:
  - persistence time (`flowConfirmMs` / `idleConfirmMs`)
  - minimum accumulated delta (`flowMinDelta`)
- this prevents `0.1 kg` jitter from flipping state on large tanks

### MQTT Publish Frequency (adaptive)

- `idle` / steady state:
  - every `30s` (can relax to `60s` later)
- `filling`:
  - every `5s`
- `discharging`:
  - every `1-2s`
- `cleaning` / noisy state:
  - every `2-5s`
- immediate publish on:
  - state change
  - threshold crossing
  - alarm condition
- heartbeat (device health):
  - every `60s`

Current firmware defaults (runtime-tunable):

- `idle`: `30000 ms`
- `filling`: `5000 ms`
- `discharging`: `2000 ms`
- `unknown`: `5000 ms`
- heartbeat: `60000 ms`

Why:

- Filling is slow enough that 5s updates are operationally real-time
- Discharge is fast; 1-2s preserves useful dynamics
- Idle dominates time, so 30-60s saves bandwidth and cost


## Message Types (Recommended)

### 1. Telemetry (current readings)

Purpose: realtime display + time-series history

Suggested payload fields:

- `deviceId`
- `ts` (device epoch seconds)
- `seq` (monotonic counter)
- `weight`
- `state` (`idle|filling|discharging|cleaning|unknown`)
- `quality` (optional confidence/health score)
- `rssi` (optional)

Currently implemented telemetry payload fields:

- `deviceId`
- `ts` (device epoch)
- `weight` (string, display-style signed value)
- `state`
- `seq`
- `scaleActive`

### 2. Event (cycle transitions / summaries)

Purpose: punctuality analysis + reconciliation + alerts

Suggested event types:

- `fill_start`
- `fill_end`
- `discharge_start`
- `discharge_end`
- `cleaning_start`
- `cleaning_end`

Suggested fields:

- `deviceId`
- `ts`
- `eventType`
- `cycleId`
- `startWeight` / `endWeight` (for end events)
- `durationSec` (for end events)
- `deltaWeight`
- `avgRate` / `peakRate` (optional)
- `confidence`

Currently implemented (edge, provisional):

- `fill_start`
- `fill_end`
- `discharge_start`
- `discharge_end`

Current event payload fields:

- `deviceId`
- `ts`
- `seq`
- `provisional` (`true`)
- `cycleId`
- `state`
- `eventType`
- `weight`

End-event fields (currently included on `_end` events):

- `startTs`
- `startWeight`
- `endWeight`
- `deltaWeight`
- `durationSec`

Current design decision:

- edge events are emitted only on **confirmed state transitions**
- events are provisional and intended to be validated/adjusted server-side
- `cleaning` events are not emitted yet (state exists, heuristic still being tuned)

### 3. Heartbeat

Purpose: device observability and support

Suggested fields:

- `deviceId`
- `ts`
- `uptimeSec`
- `wifiConnected`
- `ip`
- `mqttConnected`
- `firmwareVersion`
- `freeHeap`
- `rssi`

Currently implemented heartbeat payload fields:

- `deviceId`
- `ts`
- `seq`
- `uptimeSec`
- `ip`
- `wifiConnected`
- `mqttConnected`
- `state`
- `rssi`


## Topic Model (Tenant Claim Aware)

### Before Claim (Unclaimed)

Device publishes to unclaimed topics so backend can discover and onboard it.

- `unclaimed/<deviceId>/telemetry`
- `unclaimed/<deviceId>/event`
- `unclaimed/<deviceId>/heartbeat`

### After Claim (Tenant Scoped)

Device publishes to tenant-scoped topics after tenant assignment.

- `t/<tenantId>/d/<deviceId>/telemetry`
- `t/<tenantId>/d/<deviceId>/event`
- `t/<tenantId>/d/<deviceId>/heartbeat`

Notes:

- Device should continue to include `deviceId` in payload (useful for auditing and debugging)
- Topic migration from unclaimed to claimed should be explicit and logged


## Tenant Device Claim Workflow (End-to-End)

### A. Device First Boot / Provisioning

1. Device connects to Wi-Fi
2. Device syncs time (NTP)
3. Device fleet-provisions with AWS IoT (claim cert -> device cert)
4. Device connects to AWS IoT using device cert
5. Device publishes to `unclaimed/<deviceId>/...`

### B. Backend Detects Unclaimed Device

1. Backend subscribes to `unclaimed/+/telemetry` and `unclaimed/+/heartbeat`
2. SaaS UI shows a list of unclaimed devices
3. Operator selects device and assigns it to a tenant/site/tank

### C. Claim Action

Claim operation should record:

- `tenantId`
- `siteId` (optional)
- `tankId` (optional)
- expected operation schedule (optional, for punctuality analytics)

### D. Device Receives Claim

Recommended approaches (choose one):

- MQTT downlink command (preferred long-term)
- HTTPS polling to backend (simpler if web stack already present)
- Serial/manual provisioning (debug only)

Device stores:

- `tenantId`
- optional metadata (site/tank identifiers)

Current status:

- `tenantId` support exists in topic routing code (`unclaimed` vs `t/<tenant>/d/<device>/...`)
- claim transport/downlink and persistence are not yet implemented
- telemetry/heartbeat/event topics are all implemented in firmware topic helpers

### E. Post-Claim Behavior

1. Device switches publish topics from `unclaimed/...` to `t/<tenantId>/d/<deviceId>/...`
2. Device keeps same cert and same MQTT connection model
3. Backend updates device status to `claimed`

### F. Reclaim / Transfer

Support future workflow:

- clear tenant assignment without reprovisioning AWS cert
- device returns to `unclaimed/...` topics
- keep audit trail of claim history


## AWS Storage Architecture (Recommended)

### 1. Time-Series Storage (Primary): Amazon Timestream

Store telemetry for charts and trend analysis.

Recommended retention strategy:

- high-resolution telemetry: `7-30 days`
- aggregated/downsampled telemetry: `6-24 months` (via scheduled jobs)

Use for:

- live trend chart
- recent history
- rate calculations
- cleaning/fill/discharge pattern analysis

Timestamp model recommendation (important):

- `device_ts`: timestamp sent by the device payload (`ts`)
- `server_received_ts` / `ingest_ts`: timestamp assigned by backend/storage on receipt

Why both:

- `device_ts` represents actual process time near the tank
- `server_received_ts` reveals delay, outage replay, and backlog drain behavior
- both are needed for reconciliation and offline-buffer replay analysis

### 2. Cycle Summary Storage: DynamoDB (or relational DB in SaaS / MySQL)

Store one record per cycle event summary.

Use for:

- punctuality dashboards
- reconciliation reports
- KPI calculations (fills/day, average duration, variance)

Suggested record shape:

- `tenantId`
- `deviceId`
- `cycleId`
- `type` (`fill|discharge|cleaning`)
- `startTs`
- `endTs`
- `durationSec`
- `startWeight`
- `endWeight`
- `deltaWeight`
- `confidence`

If using SaaS MySQL (recommended for reporting/UI joins), use a table name starting with `telemetry_`:

- `telemetry_cycle_summary`

Also add a fast current-state table for widgets:

- `telemetry_latest_state`

Recommendation:

- store cycle summaries in MySQL (`telemetry_cycle_summary`)
- store latest snapshot in MySQL (`telemetry_latest_state`)
- keep high-frequency raw telemetry in Timestream (or short-retention MySQL only if necessary)

### 3. Optional Lambda Enrichment

Use Lambda if needed to:

- normalize units
- enrich with site/tank metadata
- validate payloads
- derive events server-side (if not fully done on device)


## Edge State Detection (Initial Simple Logic)

Use filtered weight + slope with hysteresis:

- `filling`:
  - sustained positive slope above threshold for N seconds
- `discharging`:
  - sustained negative slope below threshold for N seconds
- `idle`:
  - slope near zero for N seconds
- `cleaning`:
  - noisy/oscillatory behavior (high variance, frequent sign changes) or explicit configured mode

Implementation notes:

- add hysteresis to avoid state flapping
- require minimum duration before confirming a cycle
- keep a `cycleId` per detected cycle for event correlation

Current implementation decision:

- edge state is used for responsive UX and adaptive publish timing
- final cycle analytics should still be validated/reprocessed server-side (hybrid model)

This means:

- edge emits operational state quickly (real-time UX)
- backend can confirm/adjust cycle boundaries for reporting and reconciliation


## Reliability / Observability Recommendations

- Add `MQTT_DEBUG_MODE` (already introduced) for diagnostics
- Include publish success/failure logging in debug mode
- Add sequence number (`seq`) to detect gaps/replays
- Buffer a small queue locally when offline
- Heartbeat every 60s with connection health

Current implementation details:

- `MQTT_DEBUG_MODE` is implemented (compile-time flag)
- publish result logging is implemented
- local buffering is implemented as a bounded RAM outbox (`MqttOutbox`)
- outbox drains automatically when MQTT reconnects

Local outbox design (current):

- bounded RAM queue (no disk persistence yet)
- default cap: `40` messages
- hard max supported by module: `64`
- heartbeats are deprioritized:
  - heartbeat queueing is skipped when outbox is `>= 75%` full
- oldest messages are dropped when full (configurable behavior in module)

Current limitation:

- RAM outbox is lost on reboot/power loss
- persistent outbox is still a future enhancement


## Security / Multi-Tenant Notes

- Use AWS IoT fleet provisioning for zero-touch onboarding
- Device policy should support:
  - unclaimed topics before claim
  - tenant-scoped topics after claim
- If policy depends on Thing attributes (for `tenantId`), ensure claim workflow updates Thing attributes in AWS IoT
- Keep tenant assignment changes auditable in SaaS backend

AWS IoT lessons learned from this project:

- Fleet provisioning template must match firmware parameter names
  - firmware sends `DeviceId`
  - template must use `DeviceId` (not `SerialNumber`, unless firmware is changed)
- Device policy is Thing-context-based in this project
  - requires Thing creation/attachment during provisioning
- If policy depends on Thing context (`ThingName`, `IsAttached`, attributes), provisioning template must:
  - create Thing
  - activate certificate
  - attach policy
  - ensure cert/Thing association required by policy model

Heartbeat topic policy note:

- adding a new topic (`.../heartbeat`) requires matching IoT policy permissions
- otherwise MQTT publish may fail and can lead to session drop behavior

Event topic policy note:

- `.../event` also requires explicit publish permission in the device policy
- recommended policy additions (Thing-context model):
  - `unclaimed/${iot:Connection.Thing.ThingName}/event`
  - `t/${iot:Connection.Thing.Attributes[tenantId]}/d/${iot:Connection.Thing.ThingName}/event`


## SaaS MySQL Naming Convention (Proposed)

Use `telemetry_` prefix for telemetry-domain tables to keep them grouped and easy to query/manage.

Examples:

- `telemetry_cycle_summary`
- `telemetry_latest_state`
- `telemetry_device_claim_history` (optional)
- `telemetry_event_log` (optional, short retention or audit)


## Recommended Implementation Phases

### Phase 1 (Current)

- Stable RS232 reading
- OLED status icons
- Wi-Fi + NTP + AWS IoT connectivity
- Unclaimed telemetry publishing
- Fleet provisioning template fixed to create Thing + activate cert + attach policy
- MQTT debug mode + improved connect/publish diagnostics

### Phase 2

- Adaptive publish frequency (state-based)
- Heartbeat topic
- Publish result metrics
- Basic state detection (`idle/filling/discharging`)
- Web UI telemetry parameter tuning (runtime)
- OLED comm indicators (HTTP/MQTT activity)
- OLED sleep/wake policy (display-only)
- RAM outbox local buffering for MQTT failures/offline windows

### Phase 3

- Event/cycle summaries (`fill_start/end`, etc.)
- Timestream + DynamoDB rules
- SaaS widget integration (live + recent cycles)
- Settings persistence (telemetry tuning survives reboot)
- MQTT auto-reconnect/backoff hardening
- Heartbeat + telemetry policy/rule storage integration finalized
- Event policy/rule storage integration finalized

### Phase 4

- Claim downlink workflow (tenant assignment delivered to device)
- Thing attribute sync (`tenantId`, site/tank IDs)
- Reassignment/reclaim workflow
- Persistent outbox (LittleFS/NVS-backed) if outage/reboot retention is required


## Open Questions (to refine next)

- How will tenant claim data reach the device? (MQTT command vs HTTP poll)
- Should the device detect `cleaning` automatically or be configured manually?
- What unit is reported by the RS232 scale (kg/lb/raw string)?
- Do we need local buffering for offline periods (and for how long)?
- Should punctuality be evaluated on-device, in backend, or both?
- Should display sleep settings be user-configurable in the web UI?
- Should the device wake OLED on HTTP access (currently no) or only local activity/reading changes?
- What should be the MQTT reconnect policy (interval/backoff/max attempts)?
