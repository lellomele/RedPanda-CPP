# Installazione e distribuzioni Windows

**[Release 3.5.1 stabile](https://github.com/lellomele/RedPanda-CPP/releases/tag/v3.5.1-rmele.1)** — Windows 10/11 x64.

| Pacchetto | Download |
| --- | --- |
| Installer Windows x64 | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.1-rmele.1/RedPandaCPP-3.5.1-rmele-Setup-x64.exe) |
| Portable ZIP | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.1-rmele.1/RedPandaCPP-3.5.1-rmele-Portable-x64.zip) |
| Portable EXE | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.1-rmele.1/RedPandaCPP-3.5.1-rmele-Portable-x64.exe) |
| Sorgenti | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.1-rmele.1/RedPandaCPP-3.5.1-rmele-Sources.zip) |
| Impronte SHA-256 | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.1-rmele.1/SHA256SUMS.txt) |

I nomi dei pacchetti scaricabili usano `rmele`. La rinomina riguarda soltanto gli allegati su GitHub: la cartella di installazione e i crediti all’interno dei pacchetti rimangono quelli indicati sotto.

[English](WINDOWS_DISTRIBUTION.md) · [Funzionalità](FUNZIONALITA.md) · [README](../README.it.md)

## Scelta del pacchetto

| Formato | Installazione | Preferenze e recupero |
|---|---|---|
| Setup x64 EXE | Utente corrente, normalmente in `%LOCALAPPDATA%/Programs/RedPandaCPP-Mele`; voce nel menu Start e collegamento desktop opzionale | Cartella dei dati applicativi nel profilo utente |
| ZIP portabile x64 | Estrarre tutta la cartella in un percorso scrivibile | `app/config` nel pacchetto estratto |
| EXE portabile x64 | Estrae la stessa struttura; nessuna registrazione come applicazione installata o creazione di collegamenti | `app/config` nel pacchetto estratto |

Usa i pacchetti Windows x64 su Windows 10/11. Qt 6.11.2 e le dipendenze di esecuzione sono inclusi. Compilatore (GCC/Clang) e debugger (GDB/LLDB) non sono inclusi: configurali separatamente nelle preferenze dell'IDE.

**Compilatore da scaricare separatamente:** [WinLibs x64 UCRT, GCC 16.1.0, POSIX/SEH](https://github.com/brechtsanders/winlibs_mingw/releases/tag/16.1.0posix-14.0.0-ucrt-r4) — [ZIP](https://github.com/brechtsanders/winlibs_mingw/releases/download/16.1.0posix-14.0.0-ucrt-r4/winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r4.zip) · [7z](https://github.com/brechtsanders/winlibs_mingw/releases/download/16.1.0posix-14.0.0-ucrt-r4/winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r4.7z). Comprende GDB e Make.

> [!TIP]
> **Configura WinLibs in pochi passaggi**
>
> Dopo aver estratto tutto l’archivio:
>
> 1. Nella barra degli strumenti, clicca **Opzioni del compilatore** <img src="assets/compiler-options-button.png" height="28" alt="Compiler Options">.
> 2. Apri la pagina **Compilatore → Toolchain**.
> 3. Clicca l’icona della **cartella** <img src="assets/compiler-folder-button.png" height="28" alt="Find Compiler in the Folder"> per cercare il compilatore nella directory dei binari.
> 4. Seleziona **`mingw64\bin` all’interno della cartella in cui hai estratto WinLibs**; per esempio, `D:\Strumenti\WinLibs\mingw64\bin`.
> 5. Conferma la cartella, scegli il profilo GCC aggiunto se necessario e salva con **OK**.
>
> **Non occorre modificare il PATH di Windows.**

### Avvio e struttura delle cartelle

```text
RedPandaCPP/
  RedPandaIDE.exe             Launcher principale: avvia questo file
  COPYRIGHT.txt
  LICENSE
  LEGGIMI.txt
  licenses/                  Licenze dei componenti di terze parti
  app/
    RedPandaIDE-core.exe      IDE interno
    Qt6*.dll                 Librerie di esecuzione private
    qt.conf
    plugins/
    templates/
    config/                  Creata per preferenze/recupero portabili
```

Mantieni tutta la struttura. Il launcher inoltra gli argomenti e conserva ambiente/cartella di lavoro del chiamante, senza aggiungere al PATH le DLL Qt private. La sua icona è coerente con core e installer. Informazioni comprende **Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)**, la bandiera italiana e i crediti originali del progetto.

## Installazione, aggiornamento e rimozione

Setup ed estrattore portabile richiedono una destinazione vuota, evitando di sovrascrivere un'installazione esistente o file estranei. Per aggiornare una copia installata, chiudi l'IDE e disinstallala prima, quindi esegui il nuovo setup. La disinstallazione rimuove una lista esplicita dei file distribuiti e soltanto le cartelle vuote; mantiene preferenze nel profilo utente e file personali.

Per aggiornare il portabile, chiudi la vecchia copia, conserva `app/config`, estrai il nuovo pacchetto in una cartella vuota e scrivibile e ripristina lì la configurazione. Quando cambi computer trasferisci sempre l'intero pacchetto. Riconfigura i percorsi delle toolchain se compilatore/debugger sono installati in posizioni diverse.

I pacchetti non sono firmati digitalmente. Una scansione antivirus locale riguarda quei file in quel momento e non garantisce il comportamento futuro dei software di sicurezza. Per la configurazione supportata e i limiti operativi, consulta [compatibilità e limiti](MODIFICHE_FORK.md#ambito-della-compatibilità).

## Ricompilazione

Requisiti: CMake/Ninja, GCC MinGW-w64, Qt 6.11.2 (base, SVG, tools e traduzioni), Python 3.9+, NSIS e astyle. I sorgenti del packaging sono in [`platform/windows/fork-packaging`](https://github.com/lellomele/RedPanda-CPP/tree/v3.5.1-rmele.1/platform/windows/fork-packaging).

1. Configura due build dell'IDE: `PORTABLE_CONFIG=oui` per il portabile e `PORTABLE_CONFIG=non` per l'installabile; entrambe usano `FILESYSTEM_LAYOUT=flat` e `MAKE_INTERFACE=mingw32`.
2. Installa ciascuna build in `payload/portable/app` oppure `payload/installed/app`, aggiungi astyle e denomina il backend distribuito `app/RedPandaIDE-core.exe`.
3. Compila `launcher.rc` con windres dalla cartella sorgente del packaging. Compila `launcher.cpp` e l'oggetto delle risorse con `-std=c++17 -O2 -Wall -Wextra -Werror -static -municode -mwindows -lshell32`. Copialo come `payload/*/RedPandaIDE.exe`.
4. Distribuisci Qt con `windeployqt --release --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime --no-patchqt --plugindir <app>/plugins --dir <app> <app>/RedPandaIDE-core.exe`. In `app/qt.conf` imposta `Prefix=.` e `Plugins=plugins`.
5. Esegui `complete-runtime.py` indicando payload, prefisso Qt, prefisso del compilatore e le due directory di build. Il controllo ricorsivo delle importazioni PE deve terminare senza DLL mancanti; includi le licenze corrispondenti dei componenti di terze parti.
6. Genera il manifest di cancellazione con `Delete` per ogni file distribuito e `RMDir` non ricorsivi per le sottocartelle in ordine di profondità inverso. Escludi da entrambi i pacchetti preferenze, cronologia e documenti personali.
7. Compila `distribution.nsi` definendo `PAYLOAD`, `OUTPUT_FILE`, `APP_VERSION`, `APP_ICON` e `DELETE_MANIFEST`. Aggiungi `PORTABLE_PACKAGE` per l'estrattore portabile. Crea anche uno ZIP della cartella portabile e un archivio dei sorgenti corrispondenti del fork.

Verifica nomi di launcher/core, estrazione, avvio senza SDK Qt nel PATH, separazione delle impostazioni e disinstallazione con sorgenti estranei nella destinazione. L'installer supporta `/TESTMODE` per prove isolate di estrazione/disinstallazione senza registrazioni o collegamenti reali. Questa prova non copre ogni scenario di installazione registrata.

Riferimenti: [Distribuzione Qt su Windows](https://doc.qt.io/qt-6/windows-deployment.html), [NSIS](https://nsis.sourceforge.io/), [Pacchetto NSIS MSYS2](https://packages.msys2.org/packages/mingw-w64-x86_64-nsis).
