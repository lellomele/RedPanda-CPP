[Italiano](VERIFICHE_3_5_4.md) | [English](QUALIFICATION_3_5_4.md)

# Verifiche del checkpoint delle fondamenta 3.5.4

Data: 2 novembre 2026. Stato: sviluppo, `alpha.1`, nel ramo
`stabilization/3.5.4`. È un checkpoint della migrazione strutturale, non la
certificazione della release stabile. Consulta il [piano architetturale](PIANO_3_5_4.md).
L'installazione in uso e i pacchetti pubblicati non sono stati sostituiti.

## Configurazione ed esiti

Windows x64, Qt 6.11.2, GCC 16.1.0, Ninja, compilazione Release; configurazione
portabile, Lua, VCS e supporto SDCC abilitati. Le dichiarazioni Qt deprecate
causano un errore di compilazione. Questo checkpoint non qualifica l'esecuzione
NASM/SDCC, altri sistemi operativi o Qt 5.

| Verifica | Esito |
|---|---|
| CTest | 37/37 suite superate, 60,74 secondi |
| Interfaccia Windows nativa, inglese | 1.617 controlli superati, nessuno saltato |
| Interfaccia Windows nativa, italiano | 1.966 controlli superati, nessuno saltato |
| Strumenti esterni reali | Esercitati GCC, Make, GDB e un repository Git locale |
| Coerenza dei sorgenti | 2.672 file identici in due acquisizioni di verifica |

Lo SHA-256 dei manifest identici acquisiti durante e al termine delle verifiche è
`7181ba8c4e72c0c68f5eb6f0e74b5bb27a60ec5a0423ff9bb9846b3e1cae3134`.
Dopo le prove è stata aggiornata la documentazione; i sorgenti di produzione
sono rimasti invariati. I controlli grafici comprendono varianti di temi e
contrasto: il loro numero non misura funzionalità distinte o copertura del codice.

La prima prova grafica ristretta non poteva usare il Cestino nativo e il driver
non chiudeva il relativo dialogo di errore. Ora il driver registra l'errore e
prosegue segnalando una verifica fallita. La prova Windows nativa riuscita ha
usato profili temporanei e processi con limiti; nessun test saltato ha nascosto
l'errore.

## Contratti esercitati

- Acquisizione completa durante digitazione, sostituzione/incolla multiriga,
  Undo/Redo, buffer vuoti, caricamento/ricaricamento fallito, rinomina, «Salva con
  nome» e riapertura; l'acquisizione durante una modifica viene rifiutata.
- I file recovery reali seguono ogni commit del documento; una sostituzione
  fallita conserva l'ultimo file confermato e il retry salva il contenuto completo.
- Le operazioni progetto conservano buffer origine/destinazione e opzioni delle
  unità su annullamento o errore, anche con file chiusi e sole maiuscole/minuscole.
- I conflitti di rinomina esterna conservano unità e buffer distinti, sopravvivono
  al salvataggio/caricamento dei metadati e impediscono l'invio di un worker di
  compilazione finché non vengono risolti.
- «No/Annulla» alla chiusura conserva i buffer modificati; variazioni di
  appartenenza/revisione rifiutano decisioni obsolete. La chiusura progetto ritira
  gli editor del piano anche se manca un'associazione a un'unità.
- I builder semantici terminano ai limiti cumulativi di lavoro/risorse, pubblicano
  valori indipendenti e distinguono risultati parziali. Il lifecycle verifica
  conferme obsolete/duplicate, annullamento, scadenze e commit non ripetibili.

## Misure di editing

Benchmark del solo QSynEdit, cinque campioni per dimensione; mediane in
millisecondi. La colonna editing comprende l'intero batch di 40 coppie
digitazione/backspace.

| Righe | Caricamento documento C++ | 40 coppie editing | Lettura file UTF-8 | Salvataggio atomico |
|---|---:|---:|---:|---:|
| 3.000 | 5,60 | 9,70 | 7,82 | 3,58 |
| 30.000 | 58,01 | 27,96 | 83,18 | 18,82 |
| 100.000 | 181,12 | 76,26 | 292,20 | 52,46 |

Le misure non comprendono il carico complessivo IDE/parser/completamento e non
dimostrano un'accelerazione rispetto alla release precedente.

## Gate ancora aperti prima della release

Pubblicazione di risultati semantici immutabili con dipendenze validate;
migrazione di Struttura, completamento, suggerimenti e refactoring; budget della
memoria complessiva delle istantanee/cache; associazione per identità dei documenti
di progetto e «Salva con nome»; controller per ricollegare file preservando le
opzioni delle unità. Restano le richieste di compilazione immutabili e l'arresto
asincrono coordinato dei servizi. Stress test, prestazioni e artefatti finali
vanno qualificati dopo queste migrazioni. Il precedente episodio da 4–5 GiB non
ha ancora una causa dimostrata: il superamento dei test non ne prova la riproduzione.

## Seconda integrazione: 3 novembre, ancora in sviluppo

La suite corrente ha superato **45 gruppi CTest su 45 in 95,00 secondi**, compresi
il percorso dei file registrati (26 controlli), identità/istantanee dei documenti
(24 controlli), compilazione con dati congelati e alberi di processi. Le prove di
errore/riprova di «Salva con nome» e dell'osservatore della codifica ora usano il
controller reale; le prove del widget isolato verificano che «Salva con nome»
senza registrazione non possa scrivere file. È stata ripristinata la notifica di
errore per le operazioni del controller non riuscite. Questo esito non certifica
le prove separate di salvataggio ancora fallite, descritte sotto.

La successiva prova grafica nativa Windows **non ha superato la qualificazione**:
in inglese e italiano la compilazione ha evidenziato la dipendenza dell'adattatore
autolink dal parser dell'editor. Il completamento disattivato o un'analisi ancora
occupata possono impedire la compilazione. I 22 controlli isolati delle richieste
di compilazione erano riusciti con autolink disattivato e non coprivano questo
collegamento. Serve una lettura indipendente delle dipendenze, con dati congelati,
di proprietà del worker di compilazione registrato. Compilazione/errore/ricompila
del progetto e GDB hanno superato la prova; non certificano la preparazione
immutabile del progetto, che resta da migrare.

Ulteriori prove di salvataggio, in processi isolati con limiti, hanno confermato:

- Un osservatore successivo al commit può modificare il buffer; il vecchio Save
  restituisce true e azzera lo stato modificato, benché disco e buffer differiscano.
- Un osservatore precedente alla scrittura può chiudere l'editor e provocare un
  accesso a memoria non valida (`0xC0000005`). Una prova distinta durante il dialogo
  di errore ha osservato la ripresa dopo la distruzione dell'editor, senza crash
  in quella esecuzione.

Sono rilievi bloccanti per la 3.5.4. Salvataggio ordinario, «Salva con nome» e
conversione della codifica devono usare identità/revisione acquisite, esiti di
commit espliciti e confini sicuri per notifiche e durata degli oggetti, con
regressioni permanenti. Le build di sviluppo non sostituiscono l'installazione
di lavoro dell'utente.

Le letture ora coprono header modificati senza salvarli, file chiusi cambiati
anche ripristinando il timestamp, nuovi header, file assenti/vuoti, cambi di
cartella e radici originali dei documenti con nome relativo. La verifica finale
usa il budget residuo delle sorgenti, non un secondo limite intero indipendente.
Conferma versioni osservate, non un'istantanea atomica del filesystem, l'intento
corrente della GUI o tutta la freschezza semantica. Configurazione mutabile,
pubblicazione/consumatori runtime, limiti aggregati delle generazioni di input e
arresto globale restano aperti. Publisher condiviso e worker di rilascio sono
ancora nuclei isolati.

Le risorse italiane comprendono 101 ulteriori messaggi di documenti/controller e
compilazione. Un audit separato ha individuato 53 sorgenti di messaggi helper che
richiedono ancora localizzazione strutturata nel punto di presentazione: non sono
considerate tradotte dal solo aggiornamento TS. Queste prove non dimostrano un
nuovo miglioramento prestazionale complessivo dell'IDE.

## Autolink indipendente e salvataggio comune dei documenti

Per compilare file C e C++ singoli, l’autolink legge le dipendenze del sorgente su
disco nel worker di compilazione registrato. La richiesta acquisisce nella GUI
i valori del compilatore e delle regole. Il worker rileva percorsi e macro della
toolchain, legge le dipendenze entro limiti espliciti e verifica le versioni che
ha osservato. La compilazione può procedere con l’assistenza alla scrittura
disattivata oppure occupata. Lo scanner segue le regole di inclusione dell’IDE.
Il compilatore effettivo governa preprocessore e compilazione, e processi esterni
possono ancora cambiare i sorgenti tra i due passaggi.

«Salva», «Salva con nome» e la conversione di codifica condividono controller e
serializzatore. La scrittura usa la destinazione stabile e la revisione
acquisita, con sostituzione atomica del file. Modifiche o chiusura prima della
scrittura invalidano l’operazione. Le modifiche successive al commit restano
da salvare e l’esito identifica la revisione scritta su disco. Per «Salva con
nome» il controller distingue il commit del testo da quello dei metadati di
progetto. Le copie di backup con data restano un’operazione separata.

Le prove sul vecchio salvataggio hanno riprodotto anche un cambio di destinazione
durante la notifica precedente alla scrittura, che ha sovrascritto il nuovo file
di prova. La regressione usa ora l’editor registrato e controlla entrambi i file
su disco.
La build di sviluppo ha superato 47 gruppi CTest su 48 in 166,14 secondi. Il
flusso dei file registrati ha superato tutti i 32 controlli, inclusi conversione
fallita, modifica dopo il commit, Undo/Redo alla revisione salvata, cambio di
destinazione e chiusura sincrona prima della scrittura. Le prove del parser hanno
verificato stringhe raw, inclusioni con macro, direttive condizionali e limiti
della scansione. Se lo scanner dell’IDE non risolve un header, il log segnala
che l’autolink viene saltato per quella compilazione e GCC restituisce la
propria diagnostica.

Le prove Windows native hanno superato 1.618 controlli in inglese e 1.967 in
italiano, tutti eseguiti. L’unico gruppo CTest fallito conteneva una prova
separata di esecuzione fuori schermo. Il programma ha prodotto il file atteso,
ma la conclusione è stata osservata a 10,13 secondi contro un limite di otto.
Nelle prove native i tempi sono stati 0,61 e 0,74 secondi. La causa precisa del
ritardo intermittente resta in analisi.

La coda di configurazione del parser ha superato dieci casi isolati.
L’integrazione nell’IDE e il passaggio di Struttura ai dati immutabili restano
da qualificare. Questa build usa Qt 6.11.2 su Windows x64 e ha il suffisso alpha.

## Configurazione e arresto coordinato del parser

L’IDE invia rilevamento del compilatore, applicazione della configurazione,
analisi, invalidazione e reset tramite una sola coda di scrittura per parser.
Ogni analisi mantiene il riferimento alla propria configurazione. Quando
arriva una configurazione più recente, il reset già richiesto viene riproposto
con l’elenco aggiornato delle unità di progetto. La ricerca degli header
acquisisce percorsi immutabili prima di accedere al filesystem. Le letture
residue del motore mutabile sono protette mentre il worker scrive.

In chiusura la finestra mantiene in vita ogni parser, interrompe le nuove
richieste e attende in modo asincrono la conferma del worker registrato.
Il lettore delle sorgenti osserva l’annullamento dell’attività in corso.
L’arresto degli altri servizi e il completamento del ciclo di vita
dell’applicazione restano verifiche necessarie prima della release.

CTest ha superato 49 gruppi su 49 in 146,35 secondi. Le prove del runner
misurano fino a 15 secondi per il primo output del programma di prova e
fino a tre per la conferma dell’uscita successiva. Sono limiti delle prove.
I limiti dell’IDE sono rimasti invariati e il percorso della toolchain
osservato dal programma viene controllato separatamente.
La stessa build ha superato 1.619 controlli Windows nativi in inglese e 1.968
in italiano, tutti eseguiti. In un’esecuzione inglese il programma ha prodotto
il primo output dopo 6,540 secondi e il worker ha concluso un millisecondo
dopo. Il maggior intervallo misurato fra due aggiornamenti della GUI è stato
di 51 ms.
