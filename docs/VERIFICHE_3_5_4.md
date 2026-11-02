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
