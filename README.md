# Orecchio — plugin

Orecchio ascolta un sample e ti mostra le note che suonano su un piano roll. Sopra la mappa disegni basso, accordi e melodia, li senti insieme al sample e li porti nella DAW come MIDI.

È la versione plugin del tool web: stesse funzioni, ma gira dentro la DAW come strumento **VST3** (Windows, macOS, Linux) e **AU** (macOS), oppure come app **Standalone**.

## Cosa fa

- **Mappa delle note**: un'analisi FFT del sample mostra, per ogni nota da Mi1 a Do7, quanto suona in ogni momento. Il cursore *Contrasto* e l'opzione *Riduci armonici* separano le note vere dalle loro armoniche. Le note gravi usano una FFT lunga il doppio, così i semitoni sotto il Do3 restano distinti.
- **Tempo, tonalità e accordi**: al caricamento rileva BPM e primo battito, che puoi correggere a mano o con *Tap*. Rileva anche la tonalità, scura le righe fuori scala e scrive sopra ogni battuta l'accordo che riconosce.
- **Tre tracce**: Basso, Accordi e Melodia. Sulla traccia Accordi un clic inserisce la triade diatonica. *Blocca scala* tiene le note sempre in tonalità.
- **Suggerimenti**: *Suggerisci basso* e *Suggerisci accordi* leggono il sample e scrivono un punto di partenza. *Annulla* torna indietro.
- **Dentro la DAW**: segue il trasporto della DAW e suona il sample agganciato ai battiti, con un sintetizzatore semplice per ogni traccia. Manda anche le note all'uscita MIDI: basso sul canale 1, accordi sul 2, melodia sul 3.
- **MIDI**: con *Trascina il MIDI* porti le tre tracce direttamente su una traccia della DAW. *Esporta MIDI…* salva un file con tutte le tracce più un file per traccia.
- **Progetto salvato**: note, tempo, loop, impostazioni e percorso del sample restano nel progetto della DAW.
- Tema chiaro e scuro. Puoi anche trascinare un file audio direttamente sulla finestra.

## Come ottenere il plugin

### Strada facile: GitHub Actions (nessun programma da installare)

1. Crea un repository su GitHub e carica il contenuto di questa cartella, compresa `.github`.
2. Apri la scheda **Actions** e aspetta che il run "Build" finisca, circa 10–15 minuti.
3. In fondo alla pagina del run, sotto **Artifacts**, scarica `Orecchio-Windows`, `Orecchio-macOS` o `Orecchio-Linux`.

### Compilarlo sul tuo computer

Serve **CMake 3.22 o più recente**. JUCE viene scaricato in automatico alla prima configurazione.

**Windows**: installa Visual Studio 2022 (Community va bene) con il carico di lavoro "Sviluppo di applicazioni desktop con C++". Poi, dal "Developer PowerShell" nella cartella del progetto:

```
cmake -B build
cmake --build build --config Release
```

**macOS**: installa gli strumenti a riga di comando di Xcode (`xcode-select --install`) e CMake (per esempio `brew install cmake`). Poi:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```

**Linux**: installa le dipendenze elencate nel file `.github/workflows/build.yml`, poi usa gli stessi comandi di macOS.

I plugin compilati finiscono in `build/Orecchio_artefacts/Release/`, nelle cartelle `VST3`, `AU` (solo macOS) e `Standalone`. Se aggiungi `-DORECCHIO_COPY_PLUGIN=ON` alla prima riga, la build li copia anche nelle cartelle di sistema.

### Dove copiarlo

| Sistema | VST3 | AU |
|---|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` | — |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` |
| Linux | `~/.vst3/` | — |

Su macOS i plugin scaricati da GitHub non sono firmati da uno sviluppatore Apple. Se la DAW li blocca, togli la quarantena dal Terminale:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Orecchio.vst3 ~/Library/Audio/Plug-Ins/Components/Orecchio.component
```

Poi fai rifare la scansione dei plugin alla DAW.

## Come si usa

1. Metti Orecchio su una traccia strumento e aprilo.
2. Carica un sample con *Carica sample…*, trascinandolo sulla finestra, oppure usa *Demo*.
3. Controlla **BPM** e **1° battito**: le linee delle battute devono cadere sugli attacchi. Se il tempo rilevato non torna, correggilo a mano o con *Tap*; *Rileva BPM* rimette quello trovato. Il primo battito trovato è un battito, ma non sempre il primo della battuta. Se gli accordi sembrano sfasati, spostalo di un battito.
4. Disegna. Un clic su uno spazio vuoto aggiunge una nota, trascinando la allunghi. Trascini una nota per spostarla, oppure dal bordo destro per allungarla. Un clic su una nota la cancella, e anche il tasto destro. I tasti 1, 2 e 3 cambiano traccia.
5. Ascolta. Quando la DAW suona, Orecchio la segue; da solo usa *Play* o la barra spaziatrice. Trascina sul righello per scegliere la zona del loop, un clic sul righello sposta il cursore.
6. Porta il MIDI nella DAW con *Trascina il MIDI*, con *Esporta MIDI…* oppure instradando l'uscita MIDI del plugin verso altre tracce, se la tua DAW lo permette.

Vista: Ctrl/Cmd + rotella per lo zoom orizzontale, Alt + rotella per l'altezza delle righe, Maiusc + rotella per scorrere in orizzontale.

Una tastiera MIDI collegata alla traccia suona con il timbro della traccia attiva, utile per provare idee.

## Limiti

- Se il tempo della DAW è diverso da quello del sample, il sample viene accelerato o rallentato per restare a tempo, e cambia anche l'intonazione. La barra di stato lo segnala. Per lavorare, imposta il progetto al BPM del sample.
- Battute in 4/4. Sample fino a 5 minuti.
- Il progetto salva il percorso del sample, non l'audio. Se sposti il file le note restano, ma la mappa va ricaricata.
- La tonalità può uscire come la relativa: un pezzo in La minore può risultare Do maggiore, che ha la stessa scala.
- Su macOS il plugin non è notarizzato da Apple (vedi sopra).
