# Red Panda C++

[English](README.md) · [Funzionalità](docs/FUNZIONALITA.md) · [Modifiche del fork](docs/MODIFICHE_FORK.md)

<img src="docs/assets/redpanda-cpp.png" width="160" alt="Red Panda C++">

Un IDE per C/C++ e assembly GNU, con integrazione dei compilatori, navigazione del codice e debug. Questo fork aggiunge un'interfaccia italiana configurabile, temi chiari e scuri coordinati, snippet più completi, preferenze ricercabili, diagnostici GCC tradotti localmente e maggiore protezione dei documenti durante salvataggio e recupero.

Basato su [RedPanda C++ di Roy Qu e contributori](https://github.com/royqh1979/RedPanda-CPP). Il riferimento iniziale 3.4 e la revisione upstream usata per questo fork sono identificati nelle [Modifiche del fork](docs/MODIFICHE_FORK.md).

## Installazione

**[Release beta v3.5.0-beta.2-rmele.1](https://github.com/lellomele/RedPanda-CPP/releases/tag/v3.5.0-beta.2-rmele.1)** — Windows 10/11 x64.

| Pacchetto | Download |
| --- | --- |
| Installer Windows x64 | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-rmele.1/RedPandaCPP-3.5-beta2-rmele-Setup-x64.exe) |
| Portable ZIP | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-rmele.1/RedPandaCPP-3.5-beta2-rmele-Portable-x64.zip) |
| Portable EXE | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-rmele.1/RedPandaCPP-3.5-beta2-rmele-Portable-x64.exe) |
| Sorgenti | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-rmele.1/RedPandaCPP-3.5-beta2-rmele-Sources.zip) |
| Impronte SHA-256 | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-rmele.1/SHA256SUMS.txt) |

I pacchetti Windows x64 sono disponibili in tre formati:

- **Setup EXE:** installa per l'utente corrente e aggiunge una voce al menu Start; il collegamento sul desktop è opzionale.
- **ZIP portabile:** estrarre tutta la cartella in un percorso scrivibile.
- **EXE portabile autoestraente:** estrae la stessa struttura senza registrare un'applicazione installata.

Avvia **`RedPandaIDE.exe` nella cartella principale**. L'IDE interno è `app/RedPandaIDE-core.exe`: conserva l'intera cartella `app`. Qt e le dipendenze necessarie all'avvio sono inclusi. Configura separatamente **GCC/Clang** e **GDB/LLDB** per compilare e fare debug dei tuoi programmi.

Setup ed estrattore portabile richiedono una destinazione vuota. Per aggiornare una copia installata, disinstallala prima: le preferenze personali vengono conservate. Per aggiornare il portabile, conserva `app/config` e ripristinala in una copia appena estratta. I pacchetti non sono firmati digitalmente. Consulta [Installazione e distribuzioni Windows](docs/DISTRIBUZIONE_WINDOWS_FORK.md).

## Uso

Apri un sorgente o progetto, configura un compilatore in **Strumenti > Preferenze**, quindi compila, esegui o avvia il debug.

- **Aspetto:** scegli lingua, tema coordinato e famiglia di icone. Riavvia dopo un cambio di lingua.
- **Preferenze:** usa **Ctrl+F** per trovare una pagina; le modifiche pendenti possono essere salvate, scartate o mantenute annullando la navigazione.
- **Snippet:** gestisci e filtra gli snippet, importa/esporta raccolte ed espandi un prefisso con **Ctrl+Alt+Spazio**. La scorciatoia è configurabile; Tab passa fra i segnaposto.
- **Diagnostici GCC:** con l'interfaccia italiana, pannello Problemi e tooltip condividono la traduzione locale e conservano l'originale. I messaggi non riconosciuti restano invariati.
- **Pulisci artefatti di compilazione:** visualizza gli output riconosciuti e scegli quali spostare nel Cestino. I Makefile personalizzati hanno un comando `clean` separato.
- **Recupero e resilienza:** abilita copie periodiche di recupero e accedi alla relativa cartella o al registro diagnostico.

Consulta [Funzionalità e operazioni](docs/FUNZIONALITA.md) per temi disponibili, sintassi degli snippet, copertura dei diagnostici e comportamento della pulizia.

## Configurazione e limiti

Le build installabili conservano le impostazioni nel profilo utente; i pacchetti portabili usano `app/config`. Il recupero apre il testo in nuovi documenti non salvati, senza sovrascrivere gli originali. I salvataggi atomici richiedono il permesso di creare un file temporaneo nella cartella di destinazione.

Il recupero è una misura di protezione: crash nativi, corruzione della memoria, problemi di archiviazione e interruzioni di corrente possono richiedere un riavvio e l'ultima copia valida. Salva regolarmente il lavoro importante. La pulizia copre gli output riconosciuti e richiede il Cestino del sistema operativo. La traduzione italiana usa un catalogo finito, che non comprende ogni messaggio del compilatore. Consulta [Robustezza, resilienza e limiti operativi](docs/MODIFICHE_FORK.md#robustezza-e-resilienza-agli-errori).

## Compilazione dai sorgenti

Requisiti: C++17, CMake 3.19+, Qt 6.8+ oppure Qt 5.15, con compilatore e Qt della stessa architettura. Moduli Qt: Core, Concurrent, Gui, Widgets, Network, PrintSupport, Svg, Xml, Test e LinguistTools. Le distribuzioni Windows usano Qt 6.11.2 e MinGW-w64. Altre combinazioni di piattaforma e toolchain richiedono verifica per questo fork.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --parallel
cmake --build build --target all-test-targets --parallel
ctest --test-dir build --output-on-failure
```

Consulta [BUILD.md](BUILD.md) per le procedure upstream e [Packaging Windows](docs/DISTRIBUZIONE_WINDOWS_FORK.md#ricompilazione) per installer e struttura portabile del fork.

Consulta [Test e misure dell’editing](docs/TEST_IT.md) per le verifiche con profili isolati e i benchmark ripetibili.

## Licenza e copyright

GNU GPL versione 3 o successiva: [LICENSE](LICENSE), [FORK_NOTICE.txt](FORK_NOTICE.txt). Gli autori e contributori originali conservano i propri copyright; i componenti di terze parti mantengono le rispettive licenze.

Attribuzione del fork: **© 2006 Ing. prof. Raffaele Mele**, presente anche nella finestra Informazioni accanto ai crediti originali.
