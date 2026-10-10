# Istruzioni del progetto

## Priorità per sviluppo e revisione

Applicare sempre questo ordine, richiesto dall’utente:

1. **Robustezza:** correttezza, integrità dei dati e coerenza dello stato prima di ogni altra ottimizzazione. Validare gli input, controllare gli esiti delle operazioni, mantenere salvataggi atomici e gestire correttamente proprietà, durata degli oggetti e concorrenza.
2. **Resilienza agli errori:** progettare percorsi di errore recuperabili. Un errore recuperabile deve annullare l’operazione interessata, conservare documenti e stato valido e consentire di proseguire o riprovare. Fornire diagnostica comprensibile, timeout e limiti appropriati; evitare blocchi permanenti del thread dell’interfaccia. Non ignorare errori né proseguire con stato incoerente per simulare un recupero. Quando continuare non è sicuro, privilegiare la conservazione del lavoro e un riavvio controllato.
3. **Velocità di esecuzione:** cercare un’interfaccia reattiva e un uso contenuto di CPU, memoria e I/O, dopo aver garantito i due punti precedenti. Misurare le ottimizzazioni quando rilevante; preferire elaborazione differita, cache con limiti e lavoro asincrono. Non sacrificare validazione, persistenza affidabile o possibilità di recupero per migliorare le prestazioni.

Queste priorità valgono per nuove funzionalità, correzioni, refactoring e revisione del codice. Le verifiche pertinenti devono coprire anche gli errori prevedibili, l’annullamento e la conservazione dei dati, oltre al funzionamento normale. Documentare i limiti operativi materiali.

## Icona dell’applicazione

Conservare l’icona colorata coerente con panda e C++, i suoi sorgenti e i formati necessari alla distribuzione. Integrarla nell’eseguibile e nelle finestre; su Linux includere le risorse del launcher. Il nome Red Panda C++ resta indipendente dalla lingua dell’interfaccia.

## README per GitHub

Mantenere il README essenziale e rivolto agli utilizzatori: descrizione, installazione, uso, requisiti, limiti operativi rilevanti, compilazione e copyright quando pertinenti. Non inserire resoconti della conversazione, riferimenti allo sviluppo tramite AI, controlli interni o cronologia delle versioni. Conservare le informazioni necessarie all’uso. La preferenza non richiede di cancellare cronologia Git o release.

## Documentazione delle modifiche

Aggiornare `docs/MODIFICHE_FORK.md` con le modifiche implementate rispetto alla baseline, mantenendola separata dal README e dalla cronologia Git.
