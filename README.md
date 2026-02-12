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

## Architecture & Processing Pipeline

### Stage 1 — Two-Pass Validation

Every line read from each input file goes through two sequential validation gates before its data is accepted.

**Pass 1 — NMEA Checksum & Structural Completeness**

The NMEA 0183 standard specifies an XOR checksum: every byte between `$` and `*` is XOR-folded; the result must equal the two-digit hex value following `*`. This pass uses a tri-state result to distinguish two failure modes:

- **Incomplete** — the line is structurally malformed (missing `$` prefix, no `*` delimiter, or truncated hex digits). These are counted under **Parse/validation fail** since they represent broken or partial data rather than corruption of an otherwise well-formed sentence.
- **Mismatch** — the sentence has valid structure but the computed checksum does not match the declared value, indicating transmission corruption. These are counted under **Checksum failures**.

**Pass 2 — Field Validation**

Lines that pass the checksum are split on `,` and checked for:

1. **Sentence type** — only `$GPRMC` (and `$GNRMC` for multi-constellation receivers) are processed. Recognised-but-unsupported sentence types (`$GPGSA`, `$GPGGA` and their `$GN` multi-constellation variants) are explicitly classified as **"not relevant"** and reported in their own summary counter rather than being lumped into the parse-failure count. All other unknown sentence types fall through to the parse-failure counter.
2. **Minimum field count** — at least 8 fields (indices 0–7) must be present.
3. **Status flag** — field index 2 must be `'A'` (Active). A `'V'` (Void) status means the receiver had no valid fix; those lines are discarded.
4. **Coordinate and hemisphere sanity** — the lat/lon fields must be parseable as doubles and the hemisphere indicators must be single characters (`N`/`S`/`E`/`W`).

### Stage 2 — Coordinate Conversion

NMEA expresses coordinates in `DDMM.MMMM` format (degrees and decimal minutes). The parser extracts the degree portion from the leading digits (everything before the last two integer digits before the decimal point) and converts the remainder from minutes to fractional degrees:

```
decimal_degrees = DD + MM.MMMM / 60.0
```

The sign is applied based on the hemisphere indicator (`S` and `W` are negative).

Speed is transmitted in knots and converted to metres per second (× 0.514444).

### Stage 3 — Deduplication

**3a. Last-Write-Wins (Temporal Deduplication)**

All valid records are inserted into a `std::map` keyed by the UTC timestamp string (`HHMMSS.sss`). Because `std::map::operator[]` overwrites on duplicate keys, the *last* record encountered for any given timestamp is the one kept. Using `std::map` also gives us chronological ordering for free.

**3b. Spatial Deduplication (Jitter Suppression)**

The temporally-deduplicated list is walked linearly. A point is added to the final route only if it differs from the *most recently kept* point by more than `epsilon` degrees (default `1e-5`, approximately 1.1 m at the equator) in either latitude or longitude. This removes GPS "jitter" — tiny oscillations that consumer-grade receivers produce when stationary or moving slowly.

### Stage 4 — Output

Each surviving point is wrapped in the `gps_data_t` structure defined in `gpsd.h`, with `GPS_SET_LATLON | GPS_SET_SPEED` flags set and mode `MODE_2D`. The safe getter functions from the header are used to extract values for printing.

A Google Maps directions URL is built by appending `/lat,lon` segments to `https://www.google.com/maps/dir`.

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

The constant `kSpatialEpsilon` at the top of `main.cpp` can be adjusted for different use cases (e.g. increase it for coarse fleet tracking, decrease it for high-precision survey data).

### Proprietary Parsing vs. Library

An external NMEA library (e.g. `libnmea`, `minmea`) would reduce code but add a dependency. Since the task targets a single sentence type (`$GPRMC`) with well-defined field positions, a purpose-built parser is small, auditable, and has zero external dependencies beyond the C++ standard library and the provided `gpsd.h`.

## File Overview

| File | Role |
|------|------|
| `gpsd.h` | Provided header — defines `gps_data_t`, `gps_fix_t`, validity masks, and safe getters. |
| `main.cpp` | All parsing, validation, deduplication, and output logic. |
| `Makefile` | Build system with Linux/Windows support. |

## Error Handling

- **Unopenable files** — a warning is printed to stderr; processing continues with remaining files.
- **Incomplete lines** — structurally malformed lines (missing `$`, `*`, or hex digits) are counted under "Parse/validation fail".
- **Corrupted lines** — well-formed lines whose checksum doesn't match are counted under "Checksum failures".
- **Empty result set** — a message is printed; the tool exits with code 0.
- **No crashes** — all string-to-number conversions are wrapped in `try/catch`; hemisphere and field-count checks prevent out-of-bounds access.
