# Modifiche del fork e affidabilità del codice

[English](FORK_CHANGES.md) · [Funzionalità per l'utente](FUNZIONALITA.md) · [README](../README.it.md)

Questo documento descrive le aggiunte implementate e i loro effetti nell'uso quotidiano. Distingue le modifiche del fork dagli sviluppi upstream e non sostituisce la cronologia Git.

## Baseline e attribuzione

| Riferimento | Revisione | Significato |
|---|---|---|
| Archivio iniziale dei sorgenti RedPanda 3.4 | Tag ufficiale `v3.4`, commit `2fff3c2584e99ccd5aafdf897120e00e9c036515` | Riferimento originale di questo lavoro |
| Checkout upstream usato per l'implementazione | Commit `cbe084d162987fb90867b12f279af94c7a1d6a38`, versione `3.5.0-beta.2` | Base tecnica rispetto alla quale sono state applicate le modifiche del fork |

L'archivio iniziale e il tag ufficiale 3.4 identificano lo stesso commit. Il lavoro sul clone è iniziato dalla revisione upstream successiva: gli sviluppi upstream fra queste revisioni non vengono quindi presentati come nostre aggiunte. La release stabile 3.5 del fork nasce da quel checkout e dal successivo lavoro di stabilizzazione. Consulta il [confronto upstream](https://github.com/royqh1979/RedPanda-CPP/compare/2fff3c2584e99ccd5aafdf897120e00e9c036515...cbe084d162987fb90867b12f279af94c7a1d6a38).

Copyright, attribuzioni e licenza GPL originali sono mantenuti. L'attribuzione del fork è **© 2006 Ing. prof. Raffaele Mele**. La finestra Informazioni la mostra accanto ai crediti originali con una bandiera italiana; il nome dell'applicazione resta Red Panda C++ in ogni lingua.

## Aggiunte implementate

| Area | Aggiunte del fork |
|---|---|
| Lingua e identità | Traduzioni italiane dell'applicazione, delle utilità e dei dialoghi Qt; terminologia dei menu rivista; marchio e identità interna indipendenti dalla lingua; copyright e bandiera in Informazioni |
| Aspetto | Otto temi chiari, intermedi e scuri coordinati; anteprima/modifica delle palette e import/export JSON; quattro famiglie di icone a colori derivate da New Look; icona C++/panda rosso e immagine iniziale |
| Preferenze | Categorie più chiare, ricerca per parole chiave, pagine create su richiesta, dimensionamento migliorato e navigazione con Salva/Scarta/Annulla |
| Snippet | Ricerca, duplicazione, abilitazione e filtri di linguaggio; importazione con unione/sostituzione, esportazione, predefiniti e segnaposto; scorciatoia configurabile per inserimento/completamento; persistenza della raccolta completa e dei template |
| Compilazione | Pulizia selettiva con anteprima e Cestino; comando distinto per il clean di Makefile personalizzati; migrazione dal vecchio strumento esterno di cancellazione |
| Diagnostici | Catalogo locale di 162 formulazioni GCC in italiano; testi bilingui identici fra pannello e tooltip, colonna originale, copie separate ed esportazione testo/HTML; contrasto corretto nelle righe dei diagnostici |
| Protezione dei dati | Caricamento dei documenti prima dell'applicazione, salvataggi atomici, validazione di snippet/temi/dati di recupero, copie periodiche e diagnostica locale con rotazione |
| Compatibilità e distribuzione | Aggiornamenti delle API deprecate Qt 6.11.2; percorsi condizionali Qt 5.15 mantenuti; installer Windows x64 per utente e pacchetti portabili con Qt privato ed eseguibile core distinto |

[Funzionalità](FUNZIONALITA.md) descrive comandi e operazioni. [Diagnostici GCC](TRADUZIONE_DIAGNOSTICI.md) e [Distribuzioni Windows](DISTRIBUZIONE_WINDOWS_FORK.md) approfondiscono utilizzo e limiti specifici.

## Robustezza e resilienza agli errori

Le priorità sono **correttezza e integrità dei dati, gestione recuperabile degli errori, quindi velocità di esecuzione**. Gli interventi riguardano componenti specifici: non implicano la riscrittura dell'intero IDE o la recuperabilità di ogni guasto.

### Conservare documenti validi e completare le scritture

Il caricamento prepara una rappresentazione temporanea prima di sostituire il documento aperto. Un input mancante, illeggibile o non valido non svuota quindi il testo già presente nell'editor. Il salvataggio dei sorgenti prepara un file temporaneo e lo conferma atomicamente; il ripiego sulla sovrascrittura diretta è disabilitato. Un errore conserva l'originale e lascia il testo modificato disponibile per un nuovo tentativo.

Le scritture delle codifiche estese vengono controllate per rilevare output parziali. I backup che si accorciano non conservano dati residui del contenuto precedente. Snippet e template dei nuovi file sono confermati come un'unica raccolta, anziché con scritture indipendenti; anche la registrazione della sessione precedente usa persistenza atomica. Le altre impostazioni controllano gli errori di persistenza e i gruppi delle impostazioni vengono chiusi anche quando un'operazione fallisce.

### Validare prima di modificare lo stato attivo

La modifica di snippet e temi lavora su dati candidati separati. Strutture non valide, prefissi duplicati, segnaposto incoerenti e input troppo grandi vengono rifiutati prima di sostituire una raccolta attiva. Letture JSON/SVG e cache con limiti riducono consumo inutile di risorse e problemi legati a input eccessivi. Le modifiche pendenti nelle preferenze restano sotto il controllo esplicito di Salva/Scarta/Annulla.

La pulizia degli artefatti verifica percorsi, input protetti e file mostrati nell'anteprima. Prima di agire controlla che l'output selezionato corrisponda ancora al file esaminato. Usa il Cestino e segnala gli errori individuali: l'assenza del Cestino non diventa una cancellazione definitiva implicita.

### Recuperare il lavoro senza sovrascrivere gli originali

Il recupero acquisisce i documenti modificati, scrive le istantanee in background e distingue le istanze attive attraverso lock. Le scritture nuove non si sovrappongono. Le istantanee ripristinate vengono rimosse soltanto dopo il completamento di una nuova copia. I dati di recupero non validi vengono conservati per l'ispezione, invece di essere applicati all'editor.

Il contenuto ripristinato si apre come nuovo documento non salvato. L'utente può confrontarlo e salvarlo mantenendo intatti i file originali. Il recupero dipende da una cartella scrivibile e da una copia completata: il suo intervallo non garantisce l'assenza di perdita di dati in ogni condizione.

### Annullare gli errori recuperabili e rilasciare le risorse

Gli errori applicativi e di file riconosciuti al confine degli eventi annullano l'operazione fallita e vengono segnalati dopo lo svolgimento dello stack dell'evento. I task di compilazione gestiscono gli errori riconosciuti senza terminare l'interfaccia. La gestione è mirata: eccezioni sconosciute, guasti nativi e stato del processo corrotto non vengono ignorati indiscriminatamente.

Handle Windows e messaggi fra istanze hanno gestione delle risorse e validazione più esplicite. I percorsi di avvio/uscita evitano l'accesso a una finestra principale non ancora creata. Competitive Companion attende la conclusione del thread di fondo all'arresto o dopo un avvio fallito, con flag di stop atomico, prima di liberare le risorse. Queste misure riducono accessi a oggetti non più validi e chiusure incomplete.

### Mantenere l'interfaccia reattiva dopo aver protetto i dati

Le pagine delle preferenze sono create quando vengono selezionate. Il recupero riutilizza il testo in cache e salta stati invariati, scrivendo su disco fuori dal thread dell'interfaccia. Le cache dei temi e dei colori dei diagnostici hanno limiti; le regole dei diagnostici vengono compilate una sola volta e le traduzioni sono memorizzate per riga. Gli interventi riducono il lavoro ripetuto. Non viene dichiarata una percentuale di accelerazione generale o un benchmark completo dell'IDE.

### Separare le dipendenze dell'applicazione dai programmi dell'utente

I pacchetti Windows conservano IDE e DLL in `app`. Il launcher principale avvia `app/RedPandaIDE-core.exe` senza aggiungere quelle cartelle DLL all'ambiente dei programmi compilati. Build normale e portabile scelgono percorsi distinti per la configurazione.

Setup ed estrattore rifiutano destinazioni non vuote. Il disinstallatore normale rimuove una lista esplicita dei file distribuiti, elimina soltanto cartelle vuote e mantiene file personali e preferenze nel profilo utente. Questa prima versione del packaging richiede la disinstallazione della copia installata prima di un aggiornamento.

## Limiti operativi

| Dati o operazione | Limite o requisito attuale |
|---|---|
| Intervallo del recupero | 10–300 secondi |
| Istantanea di recupero | Al massimo 256 documenti e 128 MiB di dati serializzati |
| Singolo documento recuperato | Al massimo 32 × 1024 × 1024 unità di testo UTF-16 |
| Raccolta di snippet | Al massimo 8 MiB e 5.000 snippet, con ulteriori controlli sui campi |
| Tema JSON | Al massimo 256 KiB |
| Catalogo/input diagnostico | Catalogo fino a 512 KiB; fino a 16.384 unità UTF-16 per tradurre un messaggio |
| Registro diagnostico locale | Rotazione dopo il superamento di 2 MiB; mantenuto un file precedente |
| Salvataggio atomico | Permesso di creare e confermare un file temporaneo nella cartella di destinazione |
| Pulizia | Output riconosciuti, sorgente/progetto salvato e Cestino del sistema operativo |

Crash nativi, corruzione della memoria, archiviazione non disponibile e interruzioni di corrente possono richiedere un riavvio e l'ultima copia di recupero valida. Questi limiti proteggono operazioni specifiche: non sono limiti generali sulla dimensione del sorgente apribile nell'editor.

## Ambito della compatibilità

La combinazione esercitata è Windows x64, GCC 16.1.0, Qt 6.11.2 e CMake/Ninja. Le verifiche coprono errori dei documenti, importazioni rifiutate, annullamento delle preferenze, recupero, pulizia, lingua/identità, colori disegnati e coerenza dei diagnostici. Il corpus GCC comprende 164 diagnostici reali da 142 piccoli programmi C/C++, oltre agli esempi del catalogo.

Le strutture installabile e portabile sono state provate in inglese e italiano senza cartelle dello SDK Qt o dei compilatori nel PATH di avvio; estrazione e disinstallazione selettiva sono state verificate in cartelle isolate. Restano i percorsi condizionali Qt 5.15 e le risorse Linux/macOS, ma queste combinazioni non sono state validate integralmente per il fork. La 3.5.0 è la release stabile Windows di questo fork; le release upstream mantengono la propria numerazione e validazione.

## Stabilizzazione dell’editing e della persistenza

L’apertura valida codifiche, unità Unicode e scalari UTF-32 prima di sostituire un documento. Un errore conserva testo precedente, identità delle righe, codifica e impostazioni dei newline; gli editor incompleti vengono eliminati. Il salvataggio rifiuta conversioni che perderebbero caratteri silenziosamente. Conservati BOM, CR/LF/CRLF e presenza del newline finale; i formati generici UTF-16/32 BOM scrivono little-endian, con codifiche BE esplicite disponibili.

Salva con nome aggiorna nome, tipo, watcher e notifiche soltanto dopo un commit atomico riuscito. L’annullamento della rilettura e la conversione fallita conservano codifica, testo e cronologia Annulla. La formattazione automatica resta una modifica annullabile se il salvataggio fallisce. La rinomina dei simboli nei file chiusi applica ora il contenuto calcolato e salva atomicamente; anche i layout dei progetti usano commit atomici controllati.

La sostituzione valida tutti i risultati selezionati di un file prima di modificarlo, gestisce coordinate da zero e lunghezze/gruppi regex effettivi, e raggruppa i cambiamenti nei buffer aperti in un unico Annulla. Le scritture sui file chiusi sono atomiche. I file vengono salvati separatamente: un errore successivo non annulla quelli già salvati. Protetti i buffer di sola lettura. I modelli della cronologia e dell’albero di ricerca validano gli indici, mantengono la selezione e bilanciano i reset durante gli aggiornamenti rientranti delle viste.

L’espansione degli snippet rispetta la sola lettura, l’ordine delle posizioni del cursore e dei campi sostitutivi, e il buffer precedente se la preparazione dei macro fallisce. I campi sostitutivi devono occupare una riga e le posizioni del cursore devono restare esterne; le strutture non supportate vengono rifiutate. I popup gestiscono parser mancanti/annullati e callback facoltative. Gli header mantengono la precedenza locale, progetto e sistema. Il tokenizer gestisce in sicurezza letterali incompleti ed escape terminali durante la digitazione.

QSynEdit ricalcola il folding soltanto quando cambiano confini o struttura dei blocchi, conservando i blocchi collassati e Annulla/Ripristina. Ricerca e sostituzione usano il contesto originale dei match regex, con aggiornamento delle coordinate multilinea e controllo degli ambiti. Il gestore dei log può essere riconfigurato e disinstallato senza perdere il predecessore; il recupero rifiuta metadati del cursore malformati senza sostituire i documenti precedenti.

Una [guida ai test multilivello](TEST_IT.md) documenta suite unitarie/di componente, errori simulati, input malformati deterministici, verifiche bilingui dell’applicazione, output GCC reale, controlli delle distribuzioni e misure di editing/apertura/salvataggio. Le misure indicano scenari e ambito: non dimostrano un aumento universale di velocità dell’intero IDE. xmake include il servizio di contrasto e i manifest RPM tutte le icone raster distribuite.

Un errore recuperabile di salvataggio durante la chiusura annulla l’evento, ripristina gli stati di arresto e mantiene disponibili gli editor non salvati per correggere il problema o riprovare.

I wrapper controllati delle codifiche applicano ora gli stessi limiti delle unità Unicode e la stessa gestione stateless del BOM con Qt 5 e Qt 6. I salvataggi UTF-32 validano gli scalari ed escludono i byte residui non inizializzati del codificatore; le unità finali troncate vengono rifiutate. L’integrazione Git espone lo stato del processo, segnala uscite non riuscite ed errori di avvio, ed è coperta da prove su repository locali isolati con percorsi Unicode e commit.

I comandi Git sono disponibili nel menu Git quando sono configurati l’eseguibile e un repository adatto. I comandi non applicabili rimangono disabilitati. Il ripristino dei file versionati nella cartella di lavoro richiede una conferma con No preselezionato: annullando, i file restano invariati e i contenuti non salvati negli editor rimangono disponibili.

L’annullamento di compilazione ed esecuzione usa flag atomici tra i thread e arresti dei processi con attese limitate, seguiti dalla terminazione forzata se necessaria. Fermare un programma non attende più un semaforo nel thread dell’interfaccia. La proprietà dei task viene mantenuta fino alla conclusione del worker; le macro degli argomenti del compilatore vengono acquisite prima di avviarlo, senza leggere gli editor da un thread secondario. Memoria IPC e handle nativi vengono rilasciati sia alla conclusione normale sia dopo un errore.

Il launcher Windows riporta ora i metadati della versione stabile 3.5 e viene ricompilato insieme alle risorse della distribuzione.

L’inizializzazione verifica l’esito effettivo di Git prima di generare le regole di esclusione. File e collegamenti `.gitignore` già presenti vengono conservati; le regole predefinite usano un salvataggio atomico controllato e gli errori di staging sono segnalati. I repository con HEAD scollegato vengono riconosciuti. L’esecuzione Windows conserva il PATH dei programmi dell’utente senza aggiungere la cartella delle dipendenze private dell’IDE.

La chiusura di un buffer modificato richiede ora una scelta esplicita di salvataggio o scarto, anche quando il documento è vuoto. Un esito inatteso della finestra conserva il buffer. Se la copia negli appunti fallisce, un messaggio recuperabile nella barra di stato mantiene disponibile il diagnostico originale selezionato per riprovare.

Le ricette Make generate per Windows/MinGW selezionano esplicitamente cmd.exe di sistema con percorso assoluto e /d /c, sia per la pulizia sia per la compilazione. Ricompila tutto elimina quindi i vecchi oggetti anche quando le impostazioni ereditate preferiscono sh; Makefile personalizzati e ricette Unix conservano la shell scelta.

Le distribuzioni Windows conservano i notice richiesti per i runtime effettivamente inclusi, Artistic Style e Lua incorporato. Il packaging verifica le versioni corrispondenti di ICU/formatter, rifiuta notice necessari mancanti e registra le impronte SHA-256 in `licenses/COMPONENTS.json`.

Il helper Windows per le credenziali Git viene installato accanto al core dell’IDE, nel percorso usato dall’integrazione Git.

Windows può ritardare la creazione nativa del processo dentro QProcess, prima che il worker possa applicare una richiesta di arresto. La richiesta resta pendente finché l’avvio non ritorna; l’arresto di un processo già attivo ha attese limitate. La chiusura dell’applicazione attende fino a otto secondi i worker: se restano attivi, la chiusura viene rifiutata e i documenti rimangono disponibili per riprovare.
