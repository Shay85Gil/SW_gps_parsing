# NMEA GPS Parser

A production-grade C++17 command-line utility that processes GNSS data from NMEA files, extracts validated GPS coordinates, removes duplicates and jitter, and produces both a structured coordinate table and a Google Maps route URL.

## Building

### Prerequisites

- A C++17-capable compiler (`g++ >= 7`, `clang++ >= 5`, or MinGW `g++` on Windows)
- GNU Make

### Compile

```bash
make            # produces ./nmea_parser (Linux) or nmea_parser.exe (Windows)
make clean      # remove build artefacts
```

The Makefile auto-detects the platform via the `OS` environment variable and adjusts the binary extension and delete command accordingly.

### Compiler flags

| Flag | Purpose |
|------|---------|
| `-std=c++17` | Modern C++ features (structured bindings, `std::optional`, etc.) |
| `-O2` | Release-level optimisation |
| `-Wall -Wextra -Wpedantic` | Strict warnings — treat the codebase as zero-warning |

## Usage

```bash
./nmea_parser <file.nmea> [file2.nmea ...]
```

Multiple files are processed in argument order. The tool reads every line, applies the validation and deduplication pipeline, and prints results to stdout.

### Example

```
$ ./nmea_parser trip.nmea

=== Processing Summary ===
  Total lines read      : 1042
  Checksum failures     : 3
  Not relevant (skipped): 516
  Parse/validation fail : 3
  Valid records parsed  : 520
  After timestamp dedup : 480
  After spatial dedup   : 312

=== Route Points ===
#     Latitude      Longitude     Speed (m/s)
--------------------------------------------
1     32.073021     34.791264     12.345678
2     32.073155     34.791400     11.230000
...

=== Google Maps URL ===
https://www.google.com/maps/dir/32.073021,34.791264/32.073155,34.791400/...
```

## Architecture & Module Structure

The codebase is split into focused modules, each with a single responsibility:

```
main.cpp          ─ Orchestrator: file I/O, counter bookkeeping, summary printing
nmea_parser.h/cpp ─ NMEA checksum verification, sentence classification, $GPRMC extraction
dedup.h/cpp       ─ Last-write-wins temporal dedup and spatial jitter suppression
output.h/cpp      ─ gps_data_t population and Google Maps URL generation
gpsd.h            ─ Provided C header (gps_data_t, gps_fix_t, validity helpers)
```

`main.cpp` includes only the three module headers plus `gpsd.h`; it never touches parsing internals or dedup algorithms directly.

## Processing Pipeline

### Stage 1 — Two-Pass Validation (`nmea_parser`)

Every line read from each input file goes through two sequential validation gates before its data is accepted.

**Pass 1 — NMEA Checksum & Structural Completeness** (`verify_checksum`)

The NMEA 0183 standard specifies an XOR checksum: every byte between `$` and `*` is XOR-folded; the result must equal the two-digit hex value following `*`. This pass uses a tri-state `ChecksumResult` enum to distinguish two failure modes:

- **Incomplete** — the line is structurally malformed (missing `$` prefix, no `*` delimiter, or truncated hex digits). These are counted under **Parse/validation fail** since they represent broken or partial data rather than corruption of an otherwise well-formed sentence.
- **Mismatch** — the sentence has valid structure but the computed checksum does not match the declared value, indicating transmission corruption. These are counted under **Checksum failures**.

**Sentence Classification** (`is_not_relevant`)

Recognised-but-unsupported sentence types (`$GPGSA`, `$GPGGA` and their `$GN` multi-constellation variants) are explicitly classified as **"not relevant"** and reported in their own summary counter rather than being lumped into the parse-failure count.

**Pass 2 — Field Validation** (`parse_gprmc`)

Lines that pass the checksum and are not classified as irrelevant are split on `,` and checked for:

1. **Sentence type** — only `$GPRMC` (and `$GNRMC` for multi-constellation receivers) are processed. All other unknown sentence types fall through to the parse-failure counter.
2. **Minimum field count** — at least 8 fields (indices 0–7) must be present.
3. **Status flag** — field index 2 must be `'A'` (Active). A `'V'` (Void) status means the receiver had no valid fix; those lines are discarded.
4. **Coordinate and hemisphere sanity** — the lat/lon fields must be parseable as doubles and the hemisphere indicators must be single characters (`N`/`S`/`E`/`W`).

### Stage 2 — Coordinate Conversion (`nmea_parser` internal)

NMEA expresses coordinates in `DDMM.MMMM` format (degrees and decimal minutes). The parser extracts the degree portion from the leading digits (everything before the last two integer digits before the decimal point) and converts the remainder from minutes to fractional degrees:

```
decimal_degrees = DD + MM.MMMM / 60.0
```

The sign is applied based on the hemisphere indicator (`S` and `W` are negative).

Speed is transmitted in knots and converted to metres per second (× 0.514444).

### Stage 3 — Deduplication (`dedup`)

**3a. Last-Write-Wins (Temporal Deduplication)** — `dedup_last_write_wins`

All valid records are inserted into a `std::map` keyed by the UTC timestamp string (`HHMMSS.sss`). Because `std::map::operator[]` overwrites on duplicate keys, the *last* record encountered for any given timestamp is the one kept. Using `std::map` also gives us chronological ordering for free.

**3b. Spatial Deduplication (Jitter Suppression)** — `dedup_spatial`

The temporally-deduplicated list is walked linearly. A point is added to the final route only if it differs from the *most recently kept* point by more than `epsilon` degrees (default `kSpatialEpsilon = 1e-5`, approximately 1.1 m at the equator) in either latitude or longitude. This removes GPS "jitter" — tiny oscillations that consumer-grade receivers produce when stationary or moving slowly.

### Stage 4 — Output (`output`)

Each surviving point is wrapped in the `gps_data_t` structure via `to_gps_data`, with `GPS_SET_LATLON | GPS_SET_SPEED` flags set and mode `MODE_2D`. The safe getter functions from `gpsd.h` are used to extract values for printing.

`build_google_maps_url` constructs a directions URL by appending `/lat,lon` segments to `https://www.google.com/maps/dir`.

## Design Tradeoffs

### Last-Write-Wins vs. First-Write-Wins vs. Averaging

| Strategy | Pros | Cons |
|----------|------|------|
| **Last-Write-Wins** (chosen) | Simple, deterministic, no extra memory. Later corrections from the receiver are preferred. | Discards earlier data that might be more accurate in rare edge cases. |
| First-Write-Wins | Favours the earliest reading. | Later, potentially corrected, readings are lost. |
| Averaging | Smooths out noise. | Blurs genuine position changes that happen within the same timestamp. Adds complexity. |

**Rationale:** GNSS receivers often re-emit corrected data for the same epoch once more satellites are acquired. Last-write-wins naturally prefers these corrections. The simplicity also makes the system easy to reason about and debug.

### Spatial Epsilon Choice

The default epsilon of `1e-5` degrees (~1.1 m) was chosen as a balance:

- **Too small** (e.g. `1e-7`): jitter is not suppressed; the route has hundreds of redundant near-identical points.
- **Too large** (e.g. `1e-3`, ~111 m): genuine low-speed manoeuvres (parking lots, intersections) are collapsed.
- **1e-5**: removes stationary jitter while preserving walking-speed movement and above.

The constant `kSpatialEpsilon` in `dedup.h` can be adjusted for different use cases (e.g. increase it for coarse fleet tracking, decrease it for high-precision survey data).

### Proprietary Parsing vs. Library

An external NMEA library (e.g. `libnmea`, `minmea`) would reduce code but add a dependency. Since the task targets a single sentence type (`$GPRMC`) with well-defined field positions, a purpose-built parser is small, auditable, and has zero external dependencies beyond the C++ standard library and the provided `gpsd.h`.

## File Overview

| File | Role |
|------|------|
| `gpsd.h` | Provided C header — defines `gps_data_t`, `gps_fix_t`, validity masks, and safe getters. |
| `nmea_parser.h` | Public API for checksum verification, sentence classification, and `$GPRMC` parsing. Defines `NmeaRecord` and `ChecksumResult`. |
| `nmea_parser.cpp` | Implementation of the above plus internal helpers (`split`, `nmea_to_decimal`). |
| `dedup.h` | Public API for temporal and spatial deduplication. Defines `kSpatialEpsilon`. |
| `dedup.cpp` | Implementation of `dedup_last_write_wins` and `dedup_spatial`. |
| `output.h` | Public API for `gps_data_t` population and Google Maps URL generation. |
| `output.cpp` | Implementation of `to_gps_data` and `build_google_maps_url`. |
| `main.cpp` | Thin orchestrator — file I/O, counter bookkeeping, summary and table printing. |
| `Makefile` | Build system with Linux/Windows support. |

## Error Handling

- **Unopenable files** — a warning is printed to stderr; processing continues with remaining files.
- **Incomplete lines** — structurally malformed lines (missing `$`, `*`, or hex digits) are counted under "Parse/validation fail".
- **Corrupted lines** — well-formed lines whose checksum doesn't match are counted under "Checksum failures".
- **Empty result set** — a message is printed; the tool exits with code 0.
- **No crashes** — all string-to-number conversions are wrapped in `try/catch`; hemisphere and field-count checks prevent out-of-bounds access.

## Full API Reference

Complete reference for every public symbol exposed by the project headers. Internal (`static`) helpers in `.cpp` files are implementation details and not documented here.

---

### `gpsd.h` — GPS Data Structures (provided)

C-linkage header (`extern "C"`) defining the canonical data container that the rest of the project populates.

#### Types

| Type | Definition | Purpose |
|------|-----------|---------|
| `gps_mask_t` | `uint64_t` | Bitmask indicating which fields in `gps_data_t` carry valid data. |
| `fix_mode_t` | `enum { MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 }` | Receiver fix quality. `MODE_2D` = lat/lon valid, `MODE_3D` = lat/lon/alt valid. |
| `gps_fix_t` | `struct` | A single navigation fix. |
| `gps_data_t` | `struct` | Top-level container wrapping a fix, its validity mask, and receiver status. |

#### `gps_fix_t` fields

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `time` | `double` | seconds (Unix epoch) | Timestamp of the fix. |
| `latitude` | `double` | degrees (WGS84) | +N / −S. |
| `longitude` | `double` | degrees (WGS84) | +E / −W. |
| `speed` | `double` | m/s | Speed over ground. |
| `mode` | `fix_mode_t` | — | Fix type (`MODE_NO_FIX`, `MODE_2D`, `MODE_3D`). |

#### `gps_data_t` fields

| Field | Type | Description |
|-------|------|-------------|
| `set` | `gps_mask_t` | Bitwise OR of `GPS_SET_*` flags. |
| `fix` | `gps_fix_t` | Most recent navigation fix. |
| `status` | `int` | Implementation-defined receiver status. |

#### Validity flag constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `GPS_SET_NONE` | `0` | No fields valid. |
| `GPS_SET_LATLON` | `1 << 0` | `fix.latitude` and `fix.longitude` are valid. |
| `GPS_SET_SPEED` | `1 << 1` | `fix.speed` is valid. |
| `GPS_SET_TIME` | `1 << 2` | `fix.time` is valid. |
| `GPS_SET_MODE` | `1 << 3` | `fix.mode` is valid. |

#### Inline helper functions

**`int gps_has_latlon(const gps_data_t *d)`**

Returns non-zero if `d` is non-null, `GPS_SET_LATLON` is set, and `fix.mode >= MODE_2D`.

**`int gps_has_speed(const gps_data_t *d)`**

Returns non-zero if `d` is non-null, `GPS_SET_SPEED` is set, and `fix.mode >= MODE_2D`.

**`int gps_get_latlon(const gps_data_t *d, double *lat, double *lon)`**

Safe getter. Writes latitude and longitude through the output pointers. Returns `0` on success, `-1` if the position is unavailable or any pointer is null.

**`int gps_get_speed_mps(const gps_data_t *d, double *speed_mps)`**

Safe getter. Writes speed (m/s) through the output pointer. Returns `0` on success, `-1` if speed is unavailable or the pointer is null.

**`double gps_mps_to_kmh(double mps)`**

Converts metres/second to kilometres/hour (`mps * 3.6`).

**`double gps_mps_to_knots(double mps)`**

Converts metres/second to knots (`mps * 1.9438444924406048`).

---

### `nmea_parser.h` — Parsing & Validation

Declared in `nmea_parser.h`, implemented in `nmea_parser.cpp`.

#### `NmeaRecord` (struct)

Intermediate record produced by a successful `$GPRMC` parse. Carries the minimum data needed for deduplication and output.

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `timestamp` | `std::string` | `HHMMSS.sss` | UTC time extracted from the sentence. Used as the dedup key. |
| `latitude` | `double` | decimal degrees | +N / −S, already converted from NMEA `DDMM.MMMM`. |
| `longitude` | `double` | decimal degrees | +E / −W, already converted from NMEA `DDDMM.MMMM`. |
| `speed_mps` | `double` | m/s | Speed over ground, converted from knots. |

#### `ChecksumResult` (enum class)

Tri-state return type for `verify_checksum`.

| Enumerator | Meaning |
|------------|---------|
| `kOk` | Well-formed sentence; computed checksum matches the declared value. |
| `kIncomplete` | Structurally incomplete — missing `$`, `*`, or parseable hex digits. |
| `kMismatch` | Structurally valid but the XOR checksum does not match. |

#### `ChecksumResult verify_checksum(const std::string& sentence)`

**Pass 1** of the validation pipeline. Computes the XOR of every byte between `$` and `*` and compares it to the two-digit hex value after `*`.

| Parameter | Description |
|-----------|-------------|
| `sentence` | A raw line from the NMEA file (trailing `\r` should already be stripped). |

| Return value | Condition |
|--------------|-----------|
| `kOk` | Checksum matches. |
| `kIncomplete` | Line lacks `$` prefix, `*` delimiter, or two hex digits after `*`. |
| `kMismatch` | Structure is valid but the computed and declared checksums differ. |

#### `bool is_not_relevant(const std::string& sentence)`

Classifies known-but-unsupported sentence types so they can be counted separately from parse failures.

| Parameter | Description |
|-----------|-------------|
| `sentence` | A checksum-verified NMEA sentence. |

| Return | Condition |
|--------|-----------|
| `true` | Sentence ID is `$GPGGA`, `$GPGSA`, `$GNGGA`, or `$GNGSA`. |
| `false` | Any other sentence ID. |

#### `bool parse_gprmc(const std::string& sentence, NmeaRecord& out)`

**Pass 2** of the validation pipeline. Extracts position and speed from a `$GPRMC` or `$GNRMC` sentence.

| Parameter | Description |
|-----------|-------------|
| `sentence` | A checksum-verified NMEA sentence (including `*HH` tail). |
| `out` | Output record, populated only on success. |

| Return | Condition |
|--------|-----------|
| `true` | Sentence is a valid, active (`'A'`) GPRMC with parseable coordinates. `out` is filled. |
| `false` | Wrong sentence type, void status, insufficient fields, or unparseable coordinates. `out` is unmodified. |

**Validation checks performed (in order):**

1. Strips `*HH` tail, splits on `,`.
2. Requires `>= 8` fields.
3. Field 0 must be `$GPRMC` or `$GNRMC`.
4. Field 2 (status) must be `'A'`.
5. Field 1 (timestamp) must be non-empty.
6. Fields 4 and 6 (hemisphere) must be single characters.
7. Fields 3/4 and 5/6 must convert to valid decimal-degree coordinates.

---

### `dedup.h` — Deduplication

Declared in `dedup.h`, implemented in `dedup.cpp`.

#### `kSpatialEpsilon` (constant)

```cpp
inline constexpr double kSpatialEpsilon = 1e-5;
```

Default spatial deduplication threshold in decimal degrees. Approximately 1.1 m at the equator. Used as the default `epsilon` argument to `dedup_spatial`.

#### `std::vector<NmeaRecord> dedup_last_write_wins(const std::vector<NmeaRecord>& records)`

Temporal deduplication using the last-write-wins strategy.

| Parameter | Description |
|-----------|-------------|
| `records` | All valid `NmeaRecord`s in file-read order. |

| Return | Description |
|--------|-------------|
| `std::vector<NmeaRecord>` | One record per unique timestamp, sorted chronologically. For duplicate timestamps, the record appearing last in the input wins. |

**Complexity:** O(n log n) — single pass into a `std::map<string, NmeaRecord>`.

#### `std::vector<NmeaRecord> dedup_spatial(const std::vector<NmeaRecord>& records, double epsilon)`

Spatial jitter suppression. Walks the list linearly and keeps a point only if it moved more than `epsilon` degrees from the last kept point.

| Parameter | Description |
|-----------|-------------|
| `records` | Temporally-deduplicated records in chronological order. |
| `epsilon` | Minimum movement threshold in decimal degrees (both axes tested independently). |

| Return | Description |
|--------|-------------|
| `std::vector<NmeaRecord>` | Filtered route with jitter removed. The first point is always kept. |

**Complexity:** O(n) — single linear pass.

---

### `output.h` — Output Generation

Declared in `output.h`, implemented in `output.cpp`.

#### `gps_data_t to_gps_data(const NmeaRecord& r)`

Converts an `NmeaRecord` into the `gps_data_t` structure from `gpsd.h`.

| Parameter | Description |
|-----------|-------------|
| `r` | A validated, deduplicated NMEA record. |

| Return | Description |
|--------|-------------|
| `gps_data_t` | Populated structure with `set = GPS_SET_LATLON \| GPS_SET_SPEED`, `mode = MODE_2D`, `status = 1`. |

**Field mapping:**

| `NmeaRecord` field | `gps_data_t` field |
|---------------------|--------------------|
| `r.latitude` | `fix.latitude` |
| `r.longitude` | `fix.longitude` |
| `r.speed_mps` | `fix.speed` |
| — | `fix.mode = MODE_2D` |
| — | `set = GPS_SET_LATLON \| GPS_SET_SPEED` |
| — | `status = 1` |

#### `std::string build_google_maps_url(const std::vector<NmeaRecord>& route)`

Generates a Google Maps directions URL from an ordered route.

| Parameter | Description |
|-----------|-------------|
| `route` | Final deduplicated route points in order. |

| Return | Description |
|--------|-------------|
| `std::string` | URL of the form `https://www.google.com/maps/dir/lat1,lon1/lat2,lon2/…` with 6 decimal places. Empty string if `route` is empty. |
