[Italiano](PIANO_3_5_4.md) | [English](ARCHITECTURE_3_5_4.md)

# Revisione strutturale verso la 3.5.4

Stato: implementazione e verifica in corso. Configurazione di riferimento:
Windows / Qt 6.11.2. Le build di sviluppo non aggiornano l'installazione d'uso.

## Disegno concordato

Un monolite modulare con un proprietario riconoscibile per ogni stato mutabile,
richieste a messaggi e risultati immutabili identificati da documento, revisione,
configurazione e attività. QSynEdit e il comportamento linguistico del parser
vengono conservati; vengono rivisti i loro contratti di integrazione.

- La GUI possiede documenti e contesto attivo; i worker non attraversano widget,
  impostazioni globali o progetti mutabili.
- Un lavoro riceve una copia coerente degli input. Il motore di analisi ha un
  solo writer; reset, annullamento e riconfigurazione sono operazioni distinte.
- Operazioni su file e documenti distinguono successo, annullamento, errore,
  conflitto e commit già avvenuto con aggiornamenti successivi ancora pendenti.
- Nessun lease del parser attraversa dialoghi o cicli di eventi. La GUI non
  attende indefinitamente un servizio.
- I servizi sono registrati per la chiusura indipendentemente dal pannello
  selezionato. Documenti e controller restano vivi fino alla conferma di arresto.
- Persistenza e recupero controllano scrittura/commit e limiti aggregati prima
  di costruire payload grandi. Un candidato fallito conserva l'ultimo confermato.

## Pacchetti e criteri di completamento

| Pacchetto | Risultato richiesto |
|---|---|
| P0 | Checkpoint riproducibile delle modifiche precedenti, conservate nel commit `3898f098` e in un archivio separato |
| P1 | Operazioni progetto, persistenza e decisioni di chiusura con esiti espliciti; test di annullamento e guasto che preservano dati/stato |
| P2 | DocumentStore e copie immutabili degli input: nessun callback del parser verso widget nel worker |
| P3 | Coordinamento dell'analisi, invalidazione per revisione/dipendenze e risultati semantici immutabili |
| P4 | Migrazione di Struttura, completion, hint e refactoring; contesto coerente e modifiche come unità Undo |
| P5 | Configurazioni di compilazione immutabili e ciclo di vita asincrono coordinato dei servizi |
| P6 | Stress, errori iniettati, misure di editing e qualificazione della stessa sorgente/configurazione dei pacchetti |

I passi sono collegati da questa visione; gli adattatori vengono rimossi quando
il percorso completo è migrato. Non si considera conclusa la revisione perché
sono stati introdotti nuovi tipi sopra tutte le vecchie API.

## Prima integrazione e limiti ancora aperti

Sono in integrazione gli esiti delle operazioni progetto, la scrittura atomica
dei metadati, le decisioni di chiusura senza pulizia anticipata dei buffer, la
distruzione del progetto nel thread proprietario, il preflight del recupero e
gli input immutabili del parser. Il checkpoint delle fondamenta ha superato 37
suite CTest e i controlli grafici nativi Windows in inglese (1.617) e italiano
(1.966), inclusi GCC/Make/GDB reali e Git locale, senza controlli saltati.
Questi esiti qualificano i percorsi esercitati, non l'intera migrazione.

La revisione dei contratti copre anche i confini di modifica/rinomina del
documento, l'invalidazione della cache recovery, la rimozione annullata dal
progetto, la rinomina Windows con sole maiuscole/minuscole e i conflitti di
rinomina esterna persistiti. La compilazione rifiuta i conflitti di ricollocazione
prima di avviare un worker. Consulta il [resoconto delle verifiche](VERIFICHE_3_5_4.md).

Il limite nuovo del testo per una copia di recupero è aggregato: 32 Mi caratteri,
oltre ai limiti di 256 documenti e 128 MiB serializzati. Il rifiuto del candidato
non sostituisce l'ultima copia confermata; la lettura delle copie precedenti
mantiene compatibilità con i limiti già supportati.

Gli input immutabili P2, da soli, non garantiscono la freschezza dell'indice
semantico: restano P3 la revisione delle dipendenze e dei risultati. Il rilascio
degli oggetti nel thread proprietario richiede che il coordinatore mantenga vivo
l'event loop fino all'arresto dei servizi: deleteLater non sostituisce quel gate.

Il builder semantico, i valori della configurazione di compilazione, lo scrittore
controllato dei file generati e il nucleo lifecycle hanno test integrati ma non
sostituiscono ancora i rispettivi adattatori runtime. Prima della pubblicazione
dei risultati servono budget complessivi della cache e condivisione dei sottografi
fra file: un limite per file non è un limite di memoria dell'applicazione.
Le chiavi di selezione possono avere più corrispondenze; non sono identità
univoche. Restano inoltre l'associazione per identità dei documenti di progetto,
la riconciliazione di «Salva con nome» e il controller grafico per ricollegare
un file in conflitto. Sono lavori aperti, non garanzie già implementate.

Adattatore temporaneo: EditorClosePlan confronta attualmente copie del testo nel
controller al momento della chiusura. La verifica passerà a identità/revisione
DocumentStore dopo la qualificazione di tutte le notifiche di modifica.

L'obiettivo è la 3.5.4 pronta al rilascio; questa fase non certifica già risolti
tutti i rilievi dell'audit né attribuisce una causa al precedente blocco con
crescita di memoria a 4–5 GiB.
