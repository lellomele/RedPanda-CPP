# Test e misure dell’editing

[English](TESTING.md) · [Documentazione](README.md)

## Compilazione ed esecuzione

Usa un compilatore C++17 e un’installazione Qt compatibili, come descritto in [BUILD.md](../BUILD.md). Compila l’applicazione e tutti i target di test: compilare soltanto l’IDE non genera tutti gli eseguibili della suite.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --target RedPandaIDE all-test-targets benchmark-qsynedit --parallel
ctest --test-dir build --output-on-failure --timeout 120 --output-junit functional.xml
```

CTest esegue 16 suite funzionali indipendenti e cinque controlli degli argomenti del runner. Ogni eseguibile Qt accetta anche `--list-suites` e `--suite NOME`, seguiti dalle normali opzioni QtTest. Per esempio:

```sh
build/RedPandaIDE/test-editor --suite failure-paths
build/libs/qsynedit/test-qsynedit --suite document
```

Su Windows gli eseguibili terminano con `.exe`; rendi disponibili le DLL di Qt e del compilatore nell’ambiente di test. I test usano file temporanei e non richiedono modifiche alle preferenze personali dell’IDE. CTest seleziona la piattaforma Qt offscreen. Un’asserzione fallita o un timeout fa fallire la suite interessata.

## Copertura

| Livello | Scenari |
| --- | --- |
| Documenti e archiviazione | UTF-8/16/32, little/big endian, BOM, percorsi Unicode, CR/LF/CRLF, newline finale, input malformati, Unicode incompleto, codifiche con perdita dati, scritture parziali simulate e salvataggi atomici falliti |
| Servizi | Metadati di recupero invalidi, rilettura delle copie, conservazione dello stato precedente, validazione e persistenza degli snippet, corpus deterministico di input malformati, ciclo di vita dei log e gestione di eventi recuperabili |
| Editing | Documenti di sola lettura, Unicode, limiti delle selezioni, Annulla/Ripristina, parentesi e virgolette, snippet e posizioni del cursore, ambiti di ricerca/sostituzione, sostituzioni multilinea e gruppi/lookaround regex |
| Parser e modelli | Letterali incompleti ed escape durante la digitazione, nuova analisi e simboli obsoleti, confini delle direttive include, aggiornamenti rientranti della cronologia di ricerca, indici degli alberi e notifiche dei modelli |
| Applicazione | Interfaccia inglese/italiana, temi, icone, preferenze, contrasto dei diagnostici, messaggi originali/tradotti, output GCC reale, aperture fallite, sostituzione/rinomina su file chiusi ed errori del layout del progetto |
| Distribuzione | Controlli separati di installer/estrattore, argomenti del launcher, dipendenze private, posizione delle preferenze, conservazione dei dati alla disinstallazione e impronte dei pacchetti |

La copertura riguarda scenari concreti: test superati non significano che ogni percorso di esecuzione o configurazione hardware sia stato esercitato. I commit sui sorgenti sono atomici per singolo file. La sostituzione su più file non è una transazione unica: un errore successivo non annulla i file già salvati.

## Controlli dell’applicazione con profili isolati

Il runner seguente registra log, JUnit, risultati JSON e schermate in una nuova cartella dei risultati. Serve Python 3.9+ per questo strumento facoltativo di coordinamento, non per usare l’IDE.

```sh
python tools/run_stabilization.py --build-dir build --report-dir reports/run-1 --ui --benchmark --compiler /path/to/g++
```

Usa una nuova cartella per ogni esecuzione GUI. I controlli nativi Windows richiedono una sessione desktop e il supporto del Cestino. L’argomento del compilatore attiva vere compilazioni GCC e verifica i diagnostici nel pannello dell’IDE; senza di esso restano i test sui messaggi campione. `--ide` può indicare il launcher principale di una distribuzione estratta, per verificare il programma distribuito. Preferisci cartelle di test con spazi e caratteri Unicode.

## Misure ripetibili

```sh
build/libs/qsynedit/benchmark-qsynedit --lines 3000,30000,100000 --samples 5 --edits 40 > editing-performance.json
```

Il JSON contiene campioni, mediana/minimo/massimo, SHA-256 degli input e controlli di integrità. Gli scenari in memoria misurano caricamento, digitazione/backspace locale, ricerca, sostituzione e Annulla/Ripristina in QSynEdit. Gli `io_scenarios` separati misurano vere aperture UTF-8/BOM e salvataggi atomici con commit, con testo/percorsi Unicode e confronto esatto dei byte fuori dal timer.

Le misure usano file locali e cache del sistema operativo già attiva; non svuotano la cache. Il benchmark in memoria esclude rendering visibile, parser completo dell’IDE e compilazioni esterne. Confronta input con le stesse impronte, impostazioni, numero di campioni e condizioni della macchina. I tempi sono osservazioni, non soglie assolute fragili per CTest; gli errori di integrità fanno invece fallire il benchmark.

## Verifica continua

Il workflow `Stabilization gates` compila l’applicazione completa con Git, Lua e SDCC attivi, poi esegue suite funzionali e benchmark su Windows/MinGW/Qt 6 e Linux/Qt 5.15. Log e artefatti riportano le versioni effettive della toolchain. Prima della release si eseguono anche prove dei pacchetti e controlli GUI nativi bilingui sulle distribuzioni Windows. Verifica il commit esatto usato per generare la release: un’esecuzione precedente superata non convalida modifiche successive.

Il requisito per il rilascio è Windows x64 con Qt 6.11.2. Il job Linux/Qt 5 offre una verifica informativa della compatibilità storica e non blocca la release Windows; il risultato effettivo resta visibile. Altre piattaforme e toolchain richiedono una validazione separata.

Su Windows con Git attivo, un ulteriore test nativo del codificatore askpass porta il totale a 22; usa input Unicode sintetici, mai credenziali reali.
