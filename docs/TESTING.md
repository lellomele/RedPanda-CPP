# Testing and editing measurements

[Italiano](TEST_IT.md) · [Documentation](README.md)

## Build and run

Use a matching C++17 compiler and Qt installation as described in [BUILD.md](../BUILD.md). Build the application and every test target; building the IDE alone does not build all test executables.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --target RedPandaIDE all-test-targets benchmark-qsynedit --parallel
ctest --test-dir build --output-on-failure --timeout 120 --output-junit functional.xml
```

CTest runs 16 independent functional suites and five test-runner argument checks. Each Qt test executable also accepts `--list-suites` and `--suite NAME`, followed by normal QtTest options. For example:

```sh
build/RedPandaIDE/test-editor --suite failure-paths
build/libs/qsynedit/test-qsynedit --suite document
```

On Windows, executable names end in `.exe`; provide Qt and compiler runtime DLLs through the test environment. Tests use temporary files and do not require changing personal IDE preferences. CTest selects Qt's offscreen platform. A failed assertion or a timeout fails the corresponding suite.

## Coverage

| Layer | Scenarios |
| --- | --- |
| Document and storage | UTF-8/16/32, little/big endian, BOM, Unicode paths, CR/LF/CRLF, final newline, malformed input, incomplete Unicode, lossy codecs, injected short writes and atomic-save failure |
| Services | Invalid recovery metadata, snapshot round trips, preservation of previous state, snippet validation and persistence, deterministic malformed-input corpus, logging lifecycle and recoverable event dispatch |
| Editing | Read-only documents, Unicode, selection boundaries, undo/redo, brackets and quotes, snippets and tab stops, search/replace scopes, multi-line replacements and regex captures/lookaround |
| Parser and models | Incomplete literals and escape sequences during typing, re-parsing and stale symbols, include boundaries, reentrant search-history updates, tree indexes and model notification contracts |
| Application | English/Italian interface, themes, icons, preferences, diagnostic contrast, original/translated messages, real GCC output, failed file opening, closed-file replacement/rename and project layout failures |
| Distribution | Separate checks for installer/extractor, launcher arguments, private runtime dependencies, settings locations, uninstall preservation and package checksums |

Coverage is scenario-based; passing tests does not imply that every execution path or hardware configuration has been exercised. Source-file commits are atomic individually. Multi-file replacement is not a transaction across every file: a later failure does not undo earlier committed files.

## Isolated application checks

The following runner records logs, JUnit, JSON results and screenshots in a new report directory. Python 3.9+ is required for this optional orchestration tool, not to run the IDE.

```sh
python tools/run_stabilization.py --build-dir build --report-dir reports/run-1 --ui --benchmark --compiler /path/to/g++
```

Use a fresh report directory for every GUI run. Native Windows checks require a desktop session and Recycle Bin support. The compiler argument enables actual GCC compilation and checks its diagnostics through the IDE panel; without it, the diagnostic fixture tests still run. `--ide` can point to the root launcher of an extracted distribution to verify the packaged application. Prefer test directories with spaces and Unicode characters.

## Repeatable measurements

```sh
build/libs/qsynedit/benchmark-qsynedit --lines 3000,30000,100000 --samples 5 --edits 40 > editing-performance.json
```

The JSON contains raw samples, median/minimum/maximum times, input SHA-256 values and integrity checks. In-memory workloads measure loading, local typing/backspace, searching, replacement and undo/redo in QSynEdit. Separate `io_scenarios` measure actual UTF-8/BOM loading and atomic file saving with commit, including Unicode text/paths and byte-for-byte validation outside the timer.

Measurements use a warm local filesystem and operating-system cache; they do not flush the cache. The in-memory benchmark excludes the visible renderer, the full IDE parser and external compilation. Compare identical input hashes, settings, sample counts and host conditions. Wall-clock timings are observations, not fragile absolute CTest pass/fail thresholds; integrity failures do fail the benchmark.

## Continuous verification

The `Stabilization gates` workflow builds the full application with Git, Lua and SDCC features enabled, then runs the functional suites and benchmarks on Windows/MinGW/Qt 6 and Linux/Qt 5.15. Logs and measurement artifacts identify the actual toolchain versions. Packaging smoke checks and native bilingual GUI checks are additionally run against the Windows distribution before release. Validate the exact source commit used to build the release; an older green run does not validate later changes.

The release requirement is Windows x64 with Qt 6.11.2. The Linux/Qt 5 job is informational legacy coverage and does not block the Windows release; its actual result remains visible. Other platforms and toolchains require separate validation.

On Windows with Git enabled, an additional native askpass encoding test brings the total to 22; it uses synthetic Unicode input, never real credentials.
