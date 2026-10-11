# Local Italian GCC diagnostics

[Italiano](TRADUZIONE_DIAGNOSTICI.md) · [Features](FEATURES.md) · [README](../README.md)

The local catalog contains **162 reviewed rules** for common GCC C/C++ errors, warnings and supported linker messages. It is active when the loaded interface is Italian. Changing interface language takes effect after restarting the IDE.

## Read and copy messages

The Issues panel has an **Italian description** column and a separate **Original message** column. The original is captured before the IDE formats its severity label. Editor and panel tooltips use the same bilingual message; the complete compiler console remains unchanged.

Use the context menu to copy the original or the Italian translation separately. Copy All and text/HTML exports include both in Italian mode. HTML exports escape special characters in types and names. Line and column numbers retain the compiler's coordinates; displayed/exported positions use numbering from 1.

Names, types, aliases, paths, quote styles, argument numbers, ranges and options such as `[-fpermissive]` or `[-Wformat=]` survive translation. The compiler's severity, issue counts and source navigation are unchanged. User-written static_assert/deprecated explanations are retained.

## Coverage and examples

Coverage includes missing identifiers and tokens, unknown/incomplete types, invalid conversions and returns, overloads, access control, references, templates, array bounds, initialization, operators, switch/case, unused declarations, shadowing, printf formats, overflow, shifts, uninitialized values, deprecation and memory-lifetime warnings.

| Original GCC wording | Italian wording |
|---|---|
| `return-statement with no value, in function returning 'int' [-fpermissive]` | `L’istruzione return deve restituire un valore di tipo 'int'. [-fpermissive]` |
| `format '%d' expects argument of type 'int', but argument 2 has type 'char *'` | `Il formato '%d' richiede un argomento di tipo 'int', ma l’argomento 2 è di tipo 'char *'.` |
| `array subscript 4 is above array bounds of 'int [2]'` | `L’indice 4 supera i limiti dell’array di tipo 'int [2]'.` |
| `pointer 'p' used after 'free'` | `Il puntatore 'p' viene usato dopo che la memoria è stata liberata con 'free'.` |
| `wrong number of template arguments (0, should be at least 1)` | `Il template riceve 0 argomenti, ma ne richiede almeno 1.` |

These are parameterised patterns, not translations limited to the example identifiers. The corpus contains **164 real GCC 16.1.0 diagnostics from 142 small C/C++ programs**, plus examples for every rule. Some warnings require options such as `-Wextra`, `-Wconversion`, `-Wshadow` or `-Wformat=2`. Translation does not enable warnings or change the user's compiler options.

## Fallback and display

The catalog matches complete English GCC messages, not approximate fragments. New wording, unsupported compiler families and already-localised messages that do not match remain unchanged. Context notes may remain in the language emitted by GCC. Other interface languages retain their existing display.

A missing/invalid catalog or a message beyond the translation size limit falls back to the original. No Google Translate, cloud translation API or other online service is enabled. Translation sends no diagnostic text over the network.

Issue-row text is adjusted against the background actually drawn, including selection and alternating rows, with a minimum 4.5:1 contrast ratio. The adjustment does not rewrite the user's saved colour scheme. This is specific to issue text, not a claim that every colour in the interface meets that ratio.

## Catalog maintenance

The source resource is [`gcc-it.json`](https://github.com/lellomele/RedPanda-CPP/blob/v3.5.0-rmele.1/RedPandaIDE/resources/diagnostics/gcc-it.json). It is embedded in the executable; editing it requires rebuilding the IDE. This version does not import runtime catalogs from preferences.

Each rule has a stable identifier, an expression anchored to the complete message, an Italian template with `{1}`, `{2}`, etc., and examples. Every captured value must be preserved. Parameters are substituted once so percent signs or braces in identifiers are not interpreted as replacement instructions. The rules are validated/compiled once; each issue row caches its translation.

Catalog input is limited to 512 KiB and one translated message to 16,384 UTF-16 code units. Tests cover the catalog examples and real messages, exact originals, matching panel/editor hints, Unicode, aliases, options, unknown-message fallback and HTML-safe export. See [the corpus](https://github.com/lellomele/RedPanda-CPP/blob/v3.5.0-rmele.1/RedPandaIDE/test/gcc_diagnostic_samples.json).
