# Fork changes and code reliability

[Italiano](MODIFICHE_FORK.md) · [User features](FEATURES.md) · [README](../README.md)

This document describes implemented additions and their effect on everyday use. It separates fork changes from upstream development and does not replace Git history.

## Baseline and attribution

| Reference | Revision | Meaning |
|---|---|---|
| Initial RedPanda 3.4 source archive | Official tag `v3.4`, commit `2fff3c2584e99ccd5aafdf897120e00e9c036515` | The original reference for this work |
| Upstream checkout used for implementation | Commit `cbe084d162987fb90867b12f279af94c7a1d6a38`, version `3.5.0-beta.2` | The technical base against which the fork's modifications were applied |

The initial archive and official 3.4 tag identify the same commit. Work on the cloned repository started from the later upstream revision; therefore upstream changes between those revisions are not presented as our additions. The fork's stable 3.5 release builds on that checkout and its subsequent stabilization work. See the [upstream comparison](https://github.com/royqh1979/RedPanda-CPP/compare/2fff3c2584e99ccd5aafdf897120e00e9c036515...cbe084d162987fb90867b12f279af94c7a1d6a38).

Original copyrights, attribution and GPL licensing are retained. The fork attribution is **Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)**. About displays it alongside the original credits with an Italian flag; the application name remains Red Panda C++ in every language.

## Implemented additions

| Area | Fork additions |
|---|---|
| Language and identity | Italian application, utility and Qt-dialog translations; revised menu terminology; language-independent brand and internal identity; copyright and flag in About |
| Appearance | Eight coordinated light, intermediate-dark and dark themes; palette preview/editing and JSON import/export; four coloured New Look-derived icon families; C++/red-panda icon and startup artwork |
| Preferences | Clearer category names, keyword search, pages created on demand, improved sizing and Save/Discard/Cancel navigation |
| Snippets | Search, duplication, enablement and language filters; merge/replace import, export, defaults and placeholders; configurable insertion/completion shortcut; complete library/template persistence |
| Build tools | Selective previewed cleanup to the Recycle Bin; separate custom-Makefile clean command; migration away from the obsolete external deletion tool |
| Diagnostics | A local catalog of 162 GCC patterns in Italian; identical bilingual panel/editor hints, original-message column, independent copying and text/HTML export; contrast correction for issue rows |
| Data protection | Transactional document loading, atomic source saves, validated snippet/theme/recovery data, periodic recovery snapshots and rotating local diagnostics |
| Compatibility and deployment | Qt 6.11.2 deprecated-API updates; preserved Qt 5.15 conditional paths; Windows x64 user installer and portable packages with private Qt dependencies and a distinct core executable |

[Features](FEATURES.md) explains commands and workflows. [GCC diagnostics](GCC_DIAGNOSTICS.md) and [Windows distributions](WINDOWS_DISTRIBUTION.md) cover their specific usage and limitations.

## Robustness and error resilience

The priorities are **correctness and data integrity, recoverable failure handling, then execution speed**. These changes address specific components; they do not claim that every part of the IDE has been rewritten or that every failure is recoverable.

### Preserve valid documents and commit complete writes

Document loading prepares a temporary representation before replacing the open document. A missing, unreadable or invalid input therefore does not clear existing editor text. Source saves prepare a temporary file and commit it atomically; direct overwrite fallback is disabled. Failed saves preserve the original and leave the edited buffer available for another attempt.

Wide-text writes are checked for partial output. Backup files that become shorter do not retain trailing data from older contents. Snippets and new-file templates are committed as one library instead of unrelated writes; previous-session records also use atomic persistence. Other settings check persistence errors, and settings groups are closed even when an operation fails.

### Validate before modifying the active state

Snippet and theme editing uses separate candidate data. Invalid structures, duplicate prefixes, inconsistent placeholders and oversized inputs are rejected before replacing an active collection. Bounded JSON/SVG reads and bounded caches reduce unnecessary resource use and exposure to oversized inputs. Pending preference edits remain under an explicit Save/Discard/Cancel choice.

Artifact cleanup validates paths, protected inputs and the files shown in its preview. Before acting, it checks that the selected output still matches the inspected file. It uses the Recycle Bin and reports individual failures; an unavailable Recycle Bin does not silently become permanent deletion.

### Recover work without overwriting its originals

Recovery captures modified documents, writes snapshots in the background and distinguishes active instances through locks. New writes do not overlap. Previously restored snapshots are removed only after a replacement snapshot has been committed successfully. Invalid snapshots are retained for inspection instead of being applied to the editor.

Restored content opens as new, unsaved documents. This lets the user compare and save recovered work while keeping original files intact. Recovery depends on writable storage and a successfully completed snapshot; its interval is not a guarantee against all data loss.

### Cancel recoverable operations and release resources

Known application/file errors at the event boundary cancel the failed operation and are reported after the event stack has unwound. Compiler tasks handle their recognised errors without terminating the interface. This is deliberately limited: unknown exceptions, native faults and corrupted process state are not indiscriminately ignored.

Windows handles and inter-instance messages have more explicit ownership and validation. Startup/shutdown paths avoid accessing a main window that has not been created. Competitive Companion waits for its background thread to finish on stop or failed startup, with an atomic stop flag, before releasing its resources. These measures reduce invalid lifetime use and incomplete shutdown.

### Keep the interface responsive after protecting data

Preference pages are created only when selected. Recovery uses cached text and skips unchanged state, with disk writes outside the interface thread. Theme and diagnostic-colour caches are bounded; diagnostic patterns are compiled once and translations are cached per issue row. These changes reduce repeated work. No overall IDE speedup percentage or comprehensive performance benchmark is claimed.

### Separate application dependencies from user programs

Windows packages keep the actual IDE and its DLLs in `app`. The top-level launcher starts `app/RedPandaIDE-core.exe` without adding those DLL directories to the environment of compiled programs. Normal and portable builds choose different configuration locations.

Setup and the extractor refuse nonempty destinations. The normal uninstaller removes an explicit list of distributed files, removes only empty directories and retains personal files and user-profile preferences. This first packaging version requires uninstalling an installed copy before updating it.

## Operational bounds

| Data or operation | Current bound or requirement |
|---|---|
| Recovery interval | 10–300 seconds |
| Recovery snapshot | At most 256 documents and 128 MiB of serialized data |
| One recovered document | At most 32 × 1024 × 1024 UTF-16 text code units |
| Snippet library | At most 8 MiB and 5,000 snippets, with additional per-field validation |
| Theme JSON | At most 256 KiB |
| Diagnostic catalog/input | Catalog at most 512 KiB; one message at most 16,384 UTF-16 code units for translation |
| Local diagnostic log | Rotation after exceeding 2 MiB; one previous file retained |
| Atomic save | Permission to create and commit a temporary file in the destination directory |
| Cleanup | Recognised outputs, a saved source/project and operating-system Recycle Bin support |

Native crashes, memory corruption, unavailable storage and power loss may require restarting and using the last valid recovery copy. These bounds protect specific operations; they are not general limits on how large a source file the editor can open.

## Compatibility scope

The exercised configuration is Windows x64, GCC 16.1.0, Qt 6.11.2 and CMake/Ninja. Verification covers document failure paths, rejected imports, preference cancellation, recovery, cleanup, language/identity, rendered colours and diagnostic consistency. The GCC corpus contains 164 real diagnostics from 142 small C/C++ programs, in addition to the catalog examples.

Installed and portable layouts were exercised in English and Italian without SDK Qt or compiler directories in the launch PATH; installer extraction and selective uninstallation were checked in isolated folders. Conditional Qt 5.15 code paths and Linux/macOS resources remain, but this fork has not been fully validated on those combinations. Version 3.5.0 is the stable Windows release of this fork; upstream releases retain their own versioning and validation.

## Editing and persistence stabilization

Opening validates codecs, Unicode units and UTF-32 scalars before replacing a document. Failed loads retain the previous text, line identities, encoding and newline settings; incomplete editor instances are destroyed. Saving rejects conversions that would silently lose characters. BOM, CR/LF/CRLF and final-newline presence are retained; generic UTF-16/32 BOM formats write little-endian, with explicit BE codecs available.

Save As changes the name, type, watcher and notifications only after a successful atomic commit. Cancelled encoding reloads and failed conversions retain codec, text and undo history. Automatic formatting remains an undoable edit if saving fails. Closed-file symbol renaming now applies its computed contents and saves atomically; project layouts use checked atomic commits too.

Search replacement validates every selected result in a file before editing, handles zero-based positions and actual regex lengths/captures, and groups open-buffer changes into one undo operation. Closed-file writes are atomic. Files commit independently; a later failure does not undo earlier files. Read-only buffers are protected. Search-history and tree models validate indexes, preserve selection and balance resets during reentrant view updates.

Snippet expansion preserves read-only state, source order of cursor/replacement stops and the previous buffer when macro preparation fails. Replacement stops must occupy a single line and cursor stops must remain outside them; unsupported layouts are rejected. Completion popups tolerate missing/cancelled parsers and optional callbacks. Header suggestions keep local, project and system precedence. The tokenizer handles unfinished literals and trailing escapes safely during typing.

QSynEdit rescans folding only when block boundaries or structure change, preserving collapsed blocks and undo/redo. Search/replace uses original regex match context, with multi-line coordinate updates and checked scopes. The diagnostic handler supports reconfiguration and shutdown without losing its predecessor; recovery rejects malformed caret metadata without replacing existing documents.

A [multi-level testing guide](TESTING.md) documents unit/component suites, failure injection, deterministic malformed inputs, bilingual application checks, actual GCC output, distribution checks and editing/file-I/O measurements. Performance measurements describe their workloads and scope; they do not establish a universal speedup for the entire IDE. xmake includes the contrast service and RPM manifests include all supplied raster icons.

A recoverable save failure while closing rejects the close event, restores shutdown flags and keeps unsaved editors available for correction or retry.

The checked text-codec wrappers now share the same Unicode framing and stateless BOM policy under Qt 5 and Qt 6. UTF-32 saves validate scalar boundaries and omit uninitialized encoder tails; truncated final units are rejected. Git integration exposes process exit status, reports nonzero exits and startup failures, and has offline repository regression checks with Unicode paths and commits.

Git commands are available from the Git menu when a configured Git executable and a suitable repository are present. Unavailable commands remain disabled. Restoring tracked working-tree files requires a confirmation with No selected initially; cancelling preserves the files and unsaved editor buffers remain available.

Compiler and runner cancellation uses atomic cross-thread flags and bounded termination followed by forced process stop. Stopping a program no longer waits on a semaphore in the interface thread. Task ownership is retained until the worker finishes, and compiler argument macros are captured before starting the worker instead of reading editor widgets from a background thread. IPC memory and native handles are released on normal completion and failure.

The Windows launcher now carries stable 3.5 version metadata and is rebuilt together with the distribution resources.

Repository initialization checks the actual Git result before generating ignore rules. Existing `.gitignore` files and links are preserved, generated defaults use an atomic checked save, and staging failures are reported. Detached HEAD repositories are recognised. Windows execution preserves the user program’s PATH without adding the IDE’s private runtime directory.

Closing a modified buffer now requires an explicit save or discard decision, including empty modified documents. An unexpected dialog result preserves the buffer. Failed clipboard copying reports a recoverable status message and keeps the selected original diagnostic available for retry.

Generated Windows/MinGW Make recipes now select the absolute system cmd.exe and /d /c explicitly for clean and build. Rebuild therefore removes old objects even when inherited shell settings prefer sh; custom Makefiles and Unix recipes retain their selected shell.

Windows distributions retain the required notices for the actual bundled runtimes, Artistic Style and embedded Lua. Packaging verifies matching ICU/formatter versions, rejects missing required notices and records their SHA-256 values in `licenses/COMPONENTS.json`.

The Windows Git credential helper is installed beside the IDE core, matching the path used by Git integration.

Windows can delay native process creation inside QProcess before the worker can act on a stop request. Cancellation requests remain pending until startup returns; termination of an already running child has bounded waits. Application closure waits up to eight seconds for workers. If they remain active, the close request is rejected and documents stay alive for another attempt.

About and Help > Website now use the fork GitHub repository in every interface language. About separately identifies the inherited Red Panda C++/QSynEdit code and the fork enhancements, with the updated 2026 author/contact line. Original source notices and third-party copyrights remain preserved.

Maintenance release 3.5.1 publishes the fork website and clarified ownership descriptions, updated copyright/contact, bilingual documentation and regenerated Windows distributions.
