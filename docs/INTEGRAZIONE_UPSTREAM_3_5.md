[Italiano](INTEGRAZIONE_UPSTREAM_3_5.md) | [English](UPSTREAM_3_5_INTEGRATION.md)

# Integrazione della versione ufficiale 3.5

La release 3.5.2 integra Red Panda C++ 3.5 ufficiale, commit `06f28b40b001b87374eeae9b59e47422d6a9ca66`, nel fork rmele. L’archivio fornito corrisponde a tutti i 1.929 file tracciati della revisione, ammettendo differenze CRLF/LF. L’integrazione e la correzione del pannello Struttura sono distribuite insieme nei pacchetti installer e portable 3.5.2 del fork. Le release precedenti restano disponibili separatamente.

## Confronto e scelte

La base tecnica del fork era già `cbe084d162987fb90867b12f279af94c7a1d6a38` (3.5 beta 2). Rispetto a quella base restano soltanto otto commit ufficiali e dieci file diversi. Molte funzionalità elencate nelle note ufficiali 3.5 erano quindi già presenti: miglioramenti del parser, ripristino del cursore con annulla/ripeti, esportazione HTML ottimizzata, esclusione delle librerie standard durante il debug e correzioni alla durata del parser non sono nuove aggiunte del fork.

| Area | Scelta di integrazione |
|---|---|
| Disassemblato nella finestra CPU | Adottate le correzioni ufficiali alla selezione con il mouse e a Ctrl+A/C. Conservati sola lettura, colori e traduzioni del fork. |
| Completamento | Combinato il controllo di visibilità ufficiale con la gestione dei callback opzionali del fork. Anche una finestra genitore nascosta deve impedire l’elaborazione di tasti pendenti. |
| Espressioni osservate | Adottata la creazione centralizzata delle watch ufficiale. Virgolette, barre inverse e caratteri di controllo vengono codificati al confine del protocollo GDB/MI per conservare il significato delle espressioni. |
| Opzioni GCC/Clang | Adottato il tipo esplicito del compilatore; `-fno-ms-extensions` limitato a GCC. Conservati warning comuni e opzioni SDCC. |
| Script di compilazione | Importata la migrazione ufficiale del packaging Qt 5 a CMake/Ninja, selezionando Qt 5 con l’opzione effettiva `FORCE_QT5` del fork. La verifica ufficiale resta Windows x64/Qt 6.11.2. |
| Funzioni del fork | Conservati italiano, diagnostici originali e tradotti, snippet, temi/icone, ricerca nelle preferenze, salvataggi atomici, recupero e operazioni asincrone con limiti. Nessuna sostituzione duplicata dove upstream non introduce un equivalente nuovo. |
| Identità/versione | Integrazione pubblicata come versione 3.5.2 del fork, conservando link GitHub, attribuzioni e packaging Windows. La 3.5 ufficiale non retrocede la versione del fork. |

## Verifiche

Le regressioni coprono opzioni specifiche dei compilatori, selezione con il mouse anche in sola lettura, callback del completamento nascosto, selezione/copia nella finestra CPU e integrità del testo, codifica degli argomenti GDB/MI. La suite dedicata al debugger consente una sessione reale GCC/GDB attivabile esplicitamente, con timeout e percorsi degli strumenti specificati. Le suite funzionali mantengono la copertura di errori di persistenza, recupero, snippet e diagnostici. I benchmark confrontano il fork precedente con il codice integrato sullo stesso computer: sono misure, non una promessa di accelerazione generale.

Consulta [Test](TEST_IT.md) per il runner esistente. I risultati delle verifiche dell’integrazione sono riportati di seguito.

### Verifiche dell’integrazione prima del packaging (Windows x64, Qt 6.11.2)

- Superate tutte le **24 suite CTest**, inclusa la sessione reale GCC/GDB sulle watch. Il test del mouse esercita quattro combinazioni sola lettura/modalità colonne attraverso i gestori di produzione, con coordinate coerenti con l’hit-test.
- Profili grafici nativi: **1.584 controlli in inglese e 1.933 in italiano**, zero errori e nessuna verifica saltata. Passate selezione/copia CPU e copia esatta dei diagnostici negli appunti in entrambe le lingue.
- Completati benchmark di editing e I/O su 3.000, 30.000 e 100.000 righe, cinque campioni e contenuti integri. I tempi variano: la prima mediana digitazione/backspace sulle 100.000 righe passa da 99,81 ms a 115,68 ms, con intervalli dei campioni sovrapposti. Queste misure non dimostrano un’accelerazione generale né una regressione statisticamente accertata.
- La configurazione CI Windows attiva il test reale del debugger. Le verifiche di rilascio riguardano il commit esatto indicato nel `TEST_RESULTS.json` allegato; il packaging legacy Qt 5 è esterno al requisito ufficiale per Windows.
- La release 3.5.2 rigenera installer, portable EXE/ZIP e archivio sorgenti. Link e impronte sono in [Distribuzioni Windows](DISTRIBUZIONE_WINDOWS_FORK.md).
