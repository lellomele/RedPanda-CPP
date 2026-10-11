# Funzionalità e operazioni

[English](FEATURES.md) · [README](../README.it.md) · [Modifiche e affidabilità](MODIFICHE_FORK.md)

Questa guida descrive le aggiunte implementate nel fork, usando RedPanda 3.4 come riferimento iniziale per l'utente. Restano disponibili le funzionalità upstream di editing, progetti, compilazione e debug. Le revisioni esatte sono indicate nella [distinzione delle baseline](MODIFICHE_FORK.md#baseline-e-attribuzione).

## Aspetto, lingue e identità

Inglese e italiano sono selezionabili in **Preferenze > Interfaccia e area di lavoro > Aspetto**, insieme alle lingue già presenti. Riavvia dopo un cambio di lingua. I menu italiani usano verbi di azione e terminologia familiare negli IDE; termini consolidati come Debug, Breakpoint, Watch, Snippet, Template, Assembly e Commit rimangono riconoscibili. L'interfaccia italiana è disponibile, ma la revisione linguistica di tutte le finestre secondarie non è completa.

Il nome dell'applicazione è **Red Panda C++ in ogni lingua**. La nuova icona a colori combina panda rosso, C++ e parentesi graffe. Compare nell'eseguibile, nella finestra, nel comando Informazioni e nelle risorse desktop. La finestra Informazioni mostra un'immagine ampia, i crediti originali e **© 2006 Ing. prof. Raffaele Mele**, con una bandiera italiana alta quanto i caratteri del copyright. Una breve schermata iniziale mostra la stessa immagine durante la preparazione dell'ambiente; un timer o un clic la chiude.

### Temi coordinati

Otto temi aggiuntivi abbinano palette dell'interfaccia e schemi di evidenziazione del codice:

| Nome italiano | Nome inglese | Aspetto |
|---|---|---|
| Alba | Dawn | Chiaro, accenti caldi |
| Carta | Paper | Chiaro, palette essenziale |
| Nebbia | Mist | Chiaro, contrasto morbido |
| Ardesia | Slate | Palette scura intermedia |
| Crepuscolo | Dusk | Palette scura intermedia |
| Nord | Nord | Scuro, toni freddi |
| Mezzanotte | Midnight | Scuro, sfondi profondi |
| Foresta | Forest | Scuro, toni verdi |

Visualizza l'anteprima, modifica la palette nell'editor dei colori e importa o esporta temi JSON. Tema dell'interfaccia e schema dell'editor sono coordinati, mentre le preferenze dei colori del codice restano configurabili. I temi storici sono mantenuti.

### Famiglie di icone a colori

In Aspetto sono disponibili quattro famiglie aggiuntive: **Fluent Color, Adwaita Color, Breeze Color e Cupertino Color**. Ciascuna comprende 148 SVG per comandi, file, simboli e indicatori dell'editor. Derivano dalle icone upstream New Look, con palette ispirate a Windows/Fluent, GNOME, KDE e macOS. Mantengono gli accenti cromatici e adattano i primi piani neutri agli sfondi chiari e scuri. Le famiglie storiche restano disponibili.

Il repository conserva i sorgenti grafici e i formati per la distribuzione: ICO Windows, PNG/SVG Linux e ICNS macOS.

## Preferenze ricercabili

Le pagine sono raggruppate in categorie più chiare: **Interfaccia e area di lavoro**, **Editor e snippet**, **Compilazione e strumenti**, **Esecuzione e test**, insieme agli altri gruppi disponibili.

Premi **Ctrl+F**, cerca il nome di una pagina o una parola chiave e seleziona un risultato. Il filtro mantiene aperta la pagina corrente. Quando lasci impostazioni modificate, scegli **Salva**, **Scarta** oppure **Annulla**. Le pagine vengono create quando servono, anziché tutte insieme; dimensionamento delle pagine e delle colonne riduce le etichette tagliate.

## Raccolta e inserimento degli snippet

In **Preferenze > Editor e snippet > Snippet** puoi cercare per nome, prefisso, descrizione o codice. Sono disponibili creazione, duplicazione, abilitazione/disabilitazione, assegnazione del linguaggio, importazione con unione o sostituzione, esportazione e ripristino dei predefiniti. Sono supportati C, C++, header comuni e assembly. La raccolta comprende 23 esempi predefiniti e i template dei nuovi file.

Le preferenze lavorano su una copia indipendente della raccolta. Le modifiche diventano attive quando vengono applicate; importazioni non valide o rifiutate conservano la raccolta corrente.

Usa **Codice > Inserisci o completa Snippet**, inizialmente **Ctrl+Alt+Spazio**:

1. Digita un prefisso esatto, oppure un prefisso parziale che identifica un solo snippet abilitato.
2. Invoca il comando per espanderlo. Quando serve scegliere, il popup elenca gli snippet del linguaggio corrente.
3. Premi **Tab** per passare fra testi modificabili e posizioni del cursore.

Il comando funziona anche con il completamento generale disabilitato. Rispetta snippet disabilitati ed editor in sola lettura; ogni espansione costituisce una sola operazione annullabile. Sono supportati prefissi con punteggiatura, come `for(;;)`. Modifica la combinazione nella pagina **Scorciatoie** delle preferenze.

La sintassi usa `%REPL_BEGIN%...%REPL_END%` per i testi modificabili e `%INSERT%` per una posizione del cursore. Snippet e template dei file vengono salvati insieme in `snippets-library.json`. I file legacy `codesnippets.json` e i template restano leggibili quando la raccolta completa è assente; l'importazione accetta l'array legacy e il formato della raccolta versione 1.

## Diagnostici GCC in italiano e messaggi leggibili

Con l'interfaccia italiana effettivamente caricata, un catalogo locale traduce **162 formulazioni comuni dei diagnostici GCC**: errori e warning C/C++, oltre a messaggi del linker supportati. La copertura comprende nomi/token mancanti, conversioni, template, accesso ai membri, return non validi, formati printf, limiti degli array, overflow, shift e durata della memoria.

Il pannello Problemi mostra **Descrizione italiana** e **Messaggio originale**. I tooltip dell'editor e del pannello condividono lo stesso testo bilingue. Nomi, tipi, alias, numeri degli argomenti, intervalli e opzioni del compilatore vengono conservati. Coordinate, gravità e navigazione mantengono i dati originali del compilatore.

Il menu contestuale permette di copiare separatamente originale e traduzione. Copia tutto ed esportazione testo/HTML mantengono entrambi in modalità italiana. La console del compilatore resta invariata. Altre lingue dell'interfaccia, altre famiglie di compilatori e messaggi non riconosciuti conservano la visualizzazione precedente. Non viene utilizzato un servizio di traduzione online.

Il testo dei diagnostici si adatta allo sfondo effettivo della riga, comprese righe alternate, selezione, finestra inattiva e controlli disabilitati. Conserva i colori configurati quando leggibili e corregge il testo visualizzato quando serve, con contrasto minimo **4,5:1**. Questa correzione non riscrive le preferenze dei temi o dei colori.

Consulta [Traduzione dei diagnostici GCC](TRADUZIONE_DIAGNOSTICI.md) per copertura, esempi e manutenzione del catalogo.

## Pulizia selettiva degli artefatti di compilazione

**Esegui > Pulisci artefatti di compilazione** sostituisce il vecchio comando esterno di rimozione del compilato. Per un sorgente salvato o un progetto mostra gli output riconosciuti: eseguibile o libreria generata, file oggetto, import library delle DLL, risorse compilate e header precompilati.

Seleziona cosa spostare nel Cestino del sistema, poi conferma. Annullare l'anteprima conserva tutti i file; una pulizia avviata può interrompersi fra un file e il successivo. Sono esclusi sorgenti aperti, unità del progetto, configurazioni, input espliciti del linker, link simbolici e output esterni alle cartelle configurate. I file cambiati dopo l'anteprima sono rifiutati. Gli errori vengono segnalati senza terminare l'IDE.

La pulizia è mirata e non esegue scansioni ricorsive per estensione. È disabilitata durante compilazione, esecuzione e debug; richiede il Cestino e non ripiega sulla cancellazione definitiva. Per Makefile personalizzati è disponibile il comando separato **Esegui clean del Makefile**, i cui effetti dipendono dal target `clean`.

## Recupero e diagnostica locale

Abilita il recupero periodico in **Interfaccia e area di lavoro > Recupero e resilienza**. Imposta un intervallo fra 10 e 300 secondi e accedi da quella pagina alla cartella di recupero o al registro diagnostico.

I documenti modificati vengono copiati nelle istantanee di recupero. Dopo una chiusura inattesa, l'IDE propone di ripristinarli come nuovi documenti non salvati. Gli originali sono mantenuti; annullare la proposta lascia disponibili le copie. Le istantanee distinguono le istanze attive e vengono scritte in background, senza sovrapporre le scritture. Il registro ruota a 2 MiB e mantiene un file precedente.

Il recupero affianca i salvataggi normali. Problemi di archiviazione, crash nativi e stato del processo non valido possono comunque richiedere un riavvio: consulta [robustezza e limiti operativi](MODIFICHE_FORK.md#robustezza-e-resilienza-agli-errori).

## Distribuzioni Windows

Installer, ZIP portabile ed estrattore portabile includono Qt e le dipendenze private. Il `RedPandaIDE.exe` principale avvia **`app/RedPandaIDE-core.exe`**. Le preferenze installate risiedono nel profilo utente; quelle portabili e il recupero sono in `app/config`. Compilatore e debugger si configurano separatamente.

Consulta [Distribuzioni Windows](DISTRIBUZIONE_WINDOWS_FORK.md) per installazione, aggiornamento, struttura delle cartelle e sorgenti del packaging.

## Integrazione Git

Configura un eseguibile Git esterno in **Preferenze > Strumenti > Git**. Il menu **Git** offre creazione del repository, aggiunta dei file selezionati allo staging, Commit, Branch, Merge, cronologia, repository remoti, Fetch, Pull e Push. I comandi si abilitano in base all’eseguibile configurato, al repository e alla selezione. Sono riconosciuti anche i repository con HEAD scollegato da un branch.

La creazione conserva un `.gitignore` già presente. Un nuovo file di regole predefinite viene salvato atomicamente dopo l’inizializzazione riuscita. **Ripristina working tree** scarta le modifiche non incluse nello staging dei file versionati su disco soltanto dopo conferma, con No preselezionato. I contenuti non salvati negli editor rimangono disponibili. Gli errori Git mostrano l’output originale dello strumento, per consentire di correggere permessi, configurazione o stato del repository e riprovare.
