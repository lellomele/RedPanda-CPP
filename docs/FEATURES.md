# Features and workflows

[Italiano](FUNZIONALITA.md) · [README](../README.md) · [Fork changes and reliability](FORK_CHANGES.md)

This guide describes additions implemented in the fork, using RedPanda 3.4 as the initial user reference. Existing upstream editing, project, compiler and debugger features remain available. See [the baseline distinction](FORK_CHANGES.md#baseline-and-attribution) for the exact revisions.

## Appearance, languages and identity

English and Italian can be selected in **Options > Interface and workspace > Appearance**, alongside existing interface languages. Restart after changing language. Italian menu wording uses action verbs and familiar IDE terminology; established terms such as Debug, Breakpoint, Watch, Snippet, Template, Assembly and Commit remain recognisable. The Italian interface is available, but the linguistic review of every secondary dialog is not complete.

The application is called **Red Panda C++ in every language**. Its new coloured icon combines the red panda, C++ and braces. It appears in the executable, window, About action and desktop resources. About includes a large illustration, original credits and **© 2006 Ing. prof. Raffaele Mele**, with an Italian flag sized to the copyright characters. A brief startup screen shows the same artwork while the workspace is being prepared; a timer or click closes it.

### Coordinated themes

Eight additional themes pair interface palettes with syntax-highlighting schemes:

| English name | Italian name | Appearance |
|---|---|---|
| Dawn | Alba | Light, warm accents |
| Paper | Carta | Light, restrained palette |
| Mist | Nebbia | Light, soft contrast |
| Slate | Ardesia | Intermediate dark palette |
| Dusk | Crepuscolo | Intermediate dark palette |
| Nord | Nord | Dark, cool tones |
| Midnight | Mezzanotte | Dark, deeper backgrounds |
| Forest | Foresta | Dark, green tones |

Preview a theme, adjust its palette through the colour editor and import or export JSON themes. Interface themes and editor schemes are coordinated, while editor colour preferences remain configurable. Historical themes are retained.

### Coloured icon families

Appearance offers four additional families: **Fluent Color, Adwaita Color, Breeze Color and Cupertino Color**. Each contains 148 SVG assets covering commands, files, symbols and editor indicators. They derive from upstream New Look artwork, with Windows/Fluent, GNOME, KDE and macOS-inspired palette choices. They retain colour accents and adapt neutral foregrounds to light or dark backgrounds. Historical icon families remain available.

The source artwork and distribution formats are retained in the repository, including Windows ICO, Linux PNG/SVG and macOS ICNS resources.

## Searchable preferences

The preference dialog groups pages under clearer categories: **Interface and workspace**, **Editor and snippets**, **Build and toolchains**, and **Run and testing**, alongside other available groups.

Press **Ctrl+F**, enter a page name or keyword, and select a matching page. Filtering keeps the current page open. When moving away from edited settings, choose **Save**, **Discard** or **Cancel**. Pages are created when needed rather than all at once; page and column sizing reduces clipped labels.

## Snippet library and insertion

Use **Options > Editor and snippets > Snippet** to search by name, prefix, description or code. You can create, duplicate, enable or disable snippets, assign a language, import with merge or replacement, export a library and restore defaults. C, C++, shared headers and assembly are supported. The collection includes 23 default examples and the new-file templates.

Preferences edit an independent copy of the library. Changes become active when applied; invalid or rejected imports leave the current collection intact.

Use **Code > Insert or Complete Snippet**, initially **Ctrl+Alt+Space**:

1. Type an exact prefix, or a partial prefix that identifies one enabled snippet.
2. Invoke the command to expand it. If a choice is needed, the popup lists snippets for the current language.
3. Press **Tab** to move through editable placeholders and cursor stops.

The command works even when general code completion is disabled. Disabled snippets and read-only editors are respected, and an expansion is a single undoable operation. Prefixes containing punctuation, such as `for(;;)`, are supported. Change the binding in the **Shortcuts** preference page.

Snippet syntax uses `%REPL_BEGIN%...%REPL_END%` for editable text and `%INSERT%` for a cursor stop. The library is saved together with file templates in `snippets-library.json`. Legacy `codesnippets.json` and template files remain readable when the complete library is absent; imports accept the legacy array and version 1 library format.

## Italian GCC diagnostics and readable issue rows

When the loaded interface is Italian, a local catalog translates **162 common GCC diagnostic patterns**, including C/C++ errors, warnings and supported linker messages. Coverage includes missing names/tokens, type conversions, templates, access control, invalid returns, printf formats, array bounds, overflow, shifts and memory-lifetime warnings.

The Issues panel shows **Italian description** and **Original message**. Editor and panel tooltips share the same bilingual text. Names, types, aliases, argument numbers, ranges and compiler options are preserved. Source coordinates, severity and navigation still use the compiler's original data.

The context menu offers separate copying of the original and Italian translation. Copy All and text/HTML exports retain both in Italian mode. The compiler console remains unchanged. Other interface languages, other compiler families and unmatched messages retain their existing wording. No online translation service is used.

Issue text adapts to its actual row background, including alternating rows, selection, inactive windows and disabled controls. It retains configured colours when readable and adjusts the displayed text when needed to provide a minimum **4.5:1** contrast ratio. Saved theme and colour settings are not rewritten by this correction.

See [GCC diagnostic translation](GCC_DIAGNOSTICS.md) for coverage, examples and catalog maintenance.

## Selective build-artifact cleanup

**Execute > Clean Build Artifacts** replaces the old external compiled-file deletion command. For a saved source or project, it previews recognised outputs: executable or generated library, object files, DLL import libraries, compiled resources and precompiled headers.

Select what to move to the operating-system Recycle Bin, then confirm. Cancelling the preview preserves every file; an in-progress cleanup can stop between files. Open source files, project units, configuration, explicit linker inputs, symbolic links and outputs outside the configured folders are excluded. Files changed after the preview are rejected. Failures are reported without terminating the IDE.

This is a targeted cleanup, not a recursive scan for file extensions. It is disabled while compiling, running or debugging, requires Recycle Bin support and does not fall back to permanent deletion. Custom Makefiles use the separate **Run Makefile Clean** command; its effects depend on the Makefile's `clean` target.

## Recovery and local diagnostics

Enable periodic recovery in **Interface and workspace > Recovery and resilience**. Set an interval from 10 to 300 seconds and open the recovery folder or local diagnostic log from that page.

Modified documents are copied into recovery snapshots. Following an unexpected exit, the IDE offers to restore them as new, unsaved documents. Original files are retained; cancel the recovery prompt to leave the copies available. Snapshots distinguish active instances and are written in the background without overlapping writers. The diagnostic log rotates at 2 MiB and retains one previous file.

Recovery supplements normal saves. Storage errors, native crashes and invalid process state can still require restarting; see [robustness and operational limits](FORK_CHANGES.md#robustness-and-error-resilience).

## Windows distributions

Installer, portable ZIP and portable extractor include Qt and private runtime dependencies. The main `RedPandaIDE.exe` launches **`app/RedPandaIDE-core.exe`**. Installed preferences reside in the user profile; portable preferences and recovery reside in `app/config`. The compiler/debugger are configured separately.

See [Windows distributions](WINDOWS_DISTRIBUTION.md) for installation, updating, layouts and source packaging.

## Git integration

Configure an external Git executable under **Options > Tools > Git**. The **Git** menu provides repository creation, staging selected files, Commit, branches, Merge, history, remotes, Fetch, Pull and Push. Commands are enabled according to the configured executable, repository and selection. Detached HEAD repositories are recognised too.

Repository creation keeps an existing `.gitignore` unchanged. A new default ignore file is saved atomically after successful initialization. **Restore Working Tree** discards unstaged changes to tracked disk files only after confirmation; No is selected by default. Unsaved editor buffers remain available. Git command failures show the original tool output so that permissions, configuration or repository state can be corrected before retrying.
