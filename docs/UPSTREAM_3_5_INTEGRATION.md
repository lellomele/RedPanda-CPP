[Italiano](INTEGRAZIONE_UPSTREAM_3_5.md) | [English](UPSTREAM_3_5_INTEGRATION.md)

# Official 3.5 integration

Release 3.5.2 integrates official Red Panda C++ 3.5, commit `06f28b40b001b87374eeae9b59e47422d6a9ca66`, into the rmele fork. The supplied source archive matches all 1,929 tracked files of this revision, allowing for CRLF/LF differences. The integration and the Structure-panel correction are distributed together in the fork’s 3.5.2 installer and portable packages. Earlier releases remain available separately.

## Comparison and decisions

The fork's technical baseline was already `cbe084d162987fb90867b12f279af94c7a1d6a38` (3.5 beta 2). Only eight upstream commits and ten files differ from that baseline. Most features listed in the official 3.5 changelog were therefore already present: parser improvements, undo/redo caret restoration, HTML export optimization, standard-library debugging skips and parser lifetime fixes are not new fork additions.

| Area | Integration decision |
|---|---|
| CPU disassembly | Adopt upstream mouse-selection and Ctrl+A/C fixes. Keep the editor read-only and preserve the fork's colours and translations. |
| Completion popup | Combine upstream visibility checking with the fork's optional callback handling. Hidden ancestor windows must also suppress pending input. |
| Watch expressions | Adopt upstream centralized watch creation. Escape quotes, backslashes and control characters at the GDB/MI transport boundary so expressions retain their meaning. |
| GCC/Clang options | Adopt explicit compiler type and restrict `-fno-ms-extensions` to GCC; retain common warnings and SDCC options. |
| Build scripts | Import the upstream legacy Qt 5 packaging migration to CMake/Ninja; select Qt 5 through the fork's actual `FORCE_QT5` option. Official validation remains Windows x64/Qt 6.11.2. |
| Fork capabilities | Preserve Italian UI, original-message diagnostics, snippets, themes/icons, searchable preferences, atomic saves, recovery and bounded asynchronous work. No replacement implementation is introduced where upstream has no new equivalent. |
| Identity/version | Publish the integration as fork version 3.5.2, retaining GitHub links, attribution and Windows packaging. Official 3.5 does not downgrade the fork version. |

## Validation

Regression coverage includes compiler-specific options, mouse selection in editable/read-only views, hidden completion callbacks, CPU-dialog select/copy and document integrity, and GDB/MI argument handling. The dedicated debugger suite supports an opt-in real GCC/GDB session, with timeouts and explicit tool paths. Functional suites retain coverage for persistence failures, recovery, snippet and diagnostic behaviour. Editing benchmarks compare the previous fork and integrated code on the same machine; they are measurements, not a claim of a general speedup.

See [Testing](TESTING.md) for the existing suite runner. Integration validation results follow.

### Integration validation before release packaging (Windows x64, Qt 6.11.2)

- All **24 CTest suites passed**, including the real GCC/GDB watch workflow. The mouse regression test exercises four read-only/column-mode combinations through the production handlers with consistent hit-test coordinates.
- Native GUI profiles: **1,584 English checks and 1,933 Italian checks**, zero failures and no skipped checks. CPU select/copy and exact diagnostic clipboard copying passed in both languages.
- Editing/I/O benchmarks completed for 3,000, 30,000 and 100,000 lines, five samples, with content integrity preserved. Timings varied: the first 100,000-line typing/backspace median was 99.81 ms before integration and 115.68 ms afterwards, with overlapping sample ranges. These samples do not establish a general performance improvement or a statistically demonstrated regression.
- The Windows CI configuration enables the real debugger test. Release validation is tied to the exact source commit in the attached `TEST_RESULTS.json`; legacy Qt 5 packaging is outside the official Windows release gate.
- Release 3.5.2 regenerates the installer, portable EXE/ZIP and source archive. Download links and checksums are in [Windows distributions](WINDOWS_DISTRIBUTION.md).
