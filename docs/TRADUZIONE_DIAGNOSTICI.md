# Diagnostici GCC in italiano con catalogo locale

[English](GCC_DIAGNOSTICS.md) · [Funzionalità](FUNZIONALITA.md) · [README](../README.it.md)

Il catalogo locale comprende **162 regole revisionate** per errori e warning comuni GCC di C/C++, oltre a messaggi del linker supportati. Si attiva quando l'interfaccia effettivamente caricata è italiana. Il cambio di lingua ha effetto dopo il riavvio dell'IDE.

## Lettura e copia dei messaggi

Il pannello Problemi mostra **Descrizione italiana** e una colonna separata **Messaggio originale**. L'originale viene acquisito prima della formattazione dell'etichetta di gravità dell'IDE. I tooltip dell'editor e del pannello usano lo stesso messaggio bilingue; la console completa del compilatore resta invariata.

Il menu contestuale permette di copiare separatamente l'originale o la traduzione. Copia tutto ed esportazione testo/HTML comprendono entrambi in modalità italiana. L'HTML protegge i caratteri speciali di tipi e nomi. Riga e colonna mantengono le coordinate del compilatore; la numerazione visualizzata/esportata parte da 1.

Nomi, tipi, alias, percorsi, forme delle quote, numeri degli argomenti, intervalli e opzioni come `[-fpermissive]` o `[-Wformat=]` restano presenti nella traduzione. Gravità, conteggi e navigazione al sorgente sono invariati. Le spiegazioni scritte dall'utente in static_assert/deprecated vengono conservate.

## Copertura ed esempi

La copertura comprende identificatori e token mancanti, tipi sconosciuti/incompleti, conversioni e return non validi, overload, accesso ai membri, riferimenti, template, limiti degli array, inizializzazione, operatori, switch/case, dichiarazioni inutilizzate, shadowing, formati printf, overflow, shift, valori non inizializzati, deprecazione e durata della memoria.

| Testo originale GCC | Testo italiano |
|---|---|
| `return-statement with no value, in function returning 'int' [-fpermissive]` | `L’istruzione return deve restituire un valore di tipo 'int'. [-fpermissive]` |
| `format '%d' expects argument of type 'int', but argument 2 has type 'char *'` | `Il formato '%d' richiede un argomento di tipo 'int', ma l’argomento 2 è di tipo 'char *'.` |
| `array subscript 4 is above array bounds of 'int [2]'` | `L’indice 4 supera i limiti dell’array di tipo 'int [2]'.` |
| `pointer 'p' used after 'free'` | `Il puntatore 'p' viene usato dopo che la memoria è stata liberata con 'free'.` |
| `wrong number of template arguments (0, should be at least 1)` | `Il template riceve 0 argomenti, ma ne richiede almeno 1.` |

Le regole sono parametrizzate: non traducono soltanto gli identificatori degli esempi. Il corpus comprende **164 diagnostici reali di GCC 16.1.0 da 142 piccoli programmi C/C++**, oltre agli esempi di ogni regola. Alcuni warning richiedono opzioni come `-Wextra`, `-Wconversion`, `-Wshadow` o `-Wformat=2`. La traduzione non abilita warning e non modifica le opzioni del compilatore dell'utente.

## Ripiego e visualizzazione

Il catalogo riconosce messaggi GCC inglesi completi, senza corrispondenze approssimative su frammenti. Formulazioni nuove, compilatori non supportati e messaggi già localizzati non corrispondenti restano invariati. Le note di contesto possono mantenere la lingua emessa da GCC. Le altre lingue dell'interfaccia conservano la visualizzazione precedente.

Catalogo mancante/non valido o messaggio oltre il limite di traduzione comportano il ripiego sull'originale. Non sono attivi Google Translate, API cloud o altri servizi di traduzione online. La traduzione non invia i diagnostici in rete.

Il testo delle righe viene adattato allo sfondo effettivamente disegnato, comprese selezione e righe alternate, con contrasto minimo 4,5:1. La correzione non riscrive lo schema colori salvato dall'utente. Riguarda il testo dei diagnostici e non implica che ogni colore dell'interfaccia rispetti quel rapporto.

## Manutenzione del catalogo

La risorsa sorgente è [`gcc-it.json`](../RedPandaIDE/resources/diagnostics/gcc-it.json). È incorporata nell'eseguibile: modificarla richiede la ricompilazione dell'IDE. Questa versione non importa cataloghi dalle preferenze durante l'esecuzione.

Ogni regola comprende identificatore stabile, espressione ancorata all'intero messaggio, template italiano con `{1}`, `{2}`, ecc. ed esempi. Tutti i valori acquisiti devono essere conservati. La sostituzione dei parametri avviene una volta, evitando di reinterpretare percentuali o parentesi graffe negli identificatori. Le regole vengono validate/compilate una sola volta; ogni riga memorizza la sua traduzione.

Il catalogo è limitato a 512 KiB e il singolo messaggio a 16.384 unità UTF-16 per la traduzione. Le verifiche coprono esempi e diagnostici reali, originali esatti, coerenza fra pannello e tooltip, Unicode, alias, opzioni, messaggi sconosciuti ed esportazione HTML sicura. Consulta il [corpus](../RedPandaIDE/test/gcc_diagnostic_samples.json).
