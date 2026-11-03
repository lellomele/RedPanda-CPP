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

La seconda integrazione associa le unità del progetto ai DocumentId della sessione
e riunisce «Salva con nome» e ricollegamento dei file in un controller dell'interfaccia.
I piani verificano identità, revisione e opzioni attraverso le finestre di dialogo.
Testo e metadati del progetto hanno conferme distinte: un commit parziale viene
segnalato e conserva il buffer corrente. Il confronto nativo Windows distingue
nomi Unicode che Qt considerava equivalenti. Gli spazi di nomi ambigui nelle
cartelle sensibili alle maiuscole vengono rifiutati senza sovrascrivere unità o
documenti; gli alias virtuali non verificabili rimangono conflitti.

Le richieste di analisi acquisiscono cartella di lavoro e codifiche come valori.
Il worker registra versioni effettivamente lette e ricerche di header assenti,
controlla i limiti prima di decodificare o creare le righe e verifica le versioni
osservate alla fine. Il percorso assoluto originale del documento vale anche
durante l'analisi preliminare delle dipendenze. Una dipendenza cambiata comporta
una ricostruzione asincrona conservativa della cache. Questa conferma delle
letture non certifica ancora configurazione mutabile e intento corrente della
GUI: configurazione immutabile, pubblicazione e consumatori restano P3/P4.

Compilazione di file singoli e controllo sintattico in memoria usano valori
congelati. I suggerimenti diagnostici verificano DocumentId e revisione acquisiti,
anche per gli header aperti; il pannello conserva i messaggi originali. Su Windows
l'albero dei processi entra in un Job nativo atomicamente all'avvio: il task
termina solo dopo la conferma del kernel che tutti i discendenti sono usciti.
Restano la preparazione Project/SDCCProject e l'arresto asincrono globale.
Questa garanzia sugli alberi di processi non viene attribuita agli altri sistemi.

Rappresentazione semantica condivisa, contabilità aggregata delle allocazioni e
unico worker di rilascio hanno test isolati, ma non pubblicano ancora i risultati
runtime. Gli oggetti trattenuti restano contabilizzati fino alla distruzione.
Il limite non riguarda la memoria RSS del processo, il motore mutabile o tutte
le generazioni di input accettate. Le chiavi di selezione possono essere ambigue.

Nuove prove dell'audit hanno confermato due blocchi del salvataggio ordinario:
una modifica da una notifica successiva al salvataggio può risultare pulita senza
essere stata scritta; la chiusura dell'editor da una notifica precedente alla
scrittura può causare un accesso a memoria non valida. Salvataggio ordinario,
«Salva con nome» e conversione della codifica devono seguire lo stesso contratto
prima del rilascio. La suite corrente superata non risolve queste prove separate.
Il coordinatore deve inoltre mantenere vivo l'event loop fino alla conferma di
arresto di ogni servizio: deleteLater e coda vuota non bastano.

Adattatore temporaneo: EditorClosePlan confronta attualmente copie del testo nel
controller al momento della chiusura. La verifica passerà a identità/revisione
DocumentStore dopo la qualificazione di tutte le notifiche di modifica.

L'obiettivo è la 3.5.4 pronta al rilascio; questa fase non certifica già risolti
tutti i rilievi dell'audit né attribuisce una causa al precedente blocco con
crescita di memoria a 4–5 GiB.
