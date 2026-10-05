# Orecchio (plugin)

Plugin strumento VST3/AU + app Standalone, in C++17 con JUCE 8. È il porting del tool web Orecchio, ispirato a noteGRABBER: carichi un sample audio, vedi una mappa delle note su un piano roll e ci costruisci sopra basso, accordi e melodia, poi porti il MIDI nella DAW.

Interfaccia e testi in italiano. Lavorare in italiano con l'utente.

## Build e test
- `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`. JUCE 8.0.4 arriva con FetchContent; `-DJUCE_PATH=...` usa una copia locale.
- Artefatti in `build/Orecchio_artefacts/Release/{VST3,AU,Standalone}`.
- `ctest --test-dir build -C Release` esegue `tests/dsp_test.cpp`. È il test del motore DSP, senza JUCE: demo a 90 BPM con Am–F–C–G, e controlla tempo, tonalità, accordi, basso grave e suggerimenti. Si compila anche a mano: `g++ -std=c++17 -O2 tests/dsp_test.cpp Source/DSP.cpp`.
- `-DORECCHIO_ENGINE_TEST=ON` aggiunge `orecchio_engine_test`, un'app da console che istanzia il processore senza DAW. Simula il trasporto della DAW e quello interno con il loop, e controlla note-on/off, timing al campione, note appese, export MIDI e ripristino dello stato. Salva anche gli screenshot dell'editor (`engine_test_editor_{dark,light}.png`).
- Il VST3 passa pluginval a `--strictness-level 10`, test dell'editor compresi (su Linux con `xvfb-run`).
- CI: `.github/workflows/build.yml` compila Windows, macOS universale e Linux e carica i plugin come artifact.

## Architettura (`Source/`)
- **`DSP.h/.cpp`**: funzioni pure, nessuna dipendenza da JUCE.
  - `resample()`: sinc con finestra di Blackman, verso 22050 Hz.
  - `analyze()`: FFT radix-2 con N=8192 e hop di 1102 campioni (~50 ms), finestra di Hann, frame centrati. Per ogni nota MIDI 28–96 prende il picco nei bin entro ±½ semitono. Sotto `kBassSplit` (MIDI 48) usa una seconda FFT con N=16384.
  - `cleanSpec()`: sottrae frazioni (0.5 / 0.33 / 0.25) delle note −12, −19 e −24 già pulite.
  - `detectKey()`: Krumhansl–Schmuckler.
  - `detectChords()`: triadi per battuta con similarità del coseno, più un bonus per la nota al basso e uno per gli accordi diatonici.
  - `detectTempo()`: flusso spettrale con N=1024 e hop 256, autocorrelazione pesata verso ~115 BPM, poi ricerca fine con un pettine su tempo e fase. Arrotonda al BPM intero se è vicino.
  - `suggestBass()` e `suggestChords()` (rivolti con condotta delle voci), `makeDemo()`.
- **`Model.h`**: `Note {track, midi, start, len}` con start/len in battiti. Il battito 0 è il "primo battito" del sample (`offset`, in secondi).
- **`PluginProcessor`**: stato e audio.
  - Thread dei messaggi: analisi, note, undo (max 200), accordi, stato.
  - Caricamento e analisi girano su un `ThreadPool` e tornano con `callAsync` + `WeakReference`. Un contatore di generazione scarta i risultati vecchi.
  - Thread audio: niente allocazioni né lock bloccanti.
    - Note: il thread dei messaggi le copia in `sharedNotes` sotto `SpinLock`; l'audio le prende con `ScopedTryLock` in un vettore già riservato.
    - Sample: il buffer viene scambiato sotto `sampleLock`; l'audio lo legge con `TryLock`.
    - Anteprima delle note: `AbstractFifo`.
  - Trasporto: se la DAW suona, la posizione viene da `ppqPosition`, mappata nel loop con un modulo. Altrimenti c'è il trasporto interno. Il blocco è diviso in pezzi da 32 campioni per eventi quasi al campione. Al giro del loop gli eventi ripartono esattamente da `loopStart` (`eventsFrom`), altrimenti le note sul primo battito verrebbero saltate.
  - Il sample è agganciato ai battiti (posizione = `offset + beat·60/bpm`). Con un tempo della DAW diverso suona in varispeed.
  - Parametri APVTS: `volSample`, `volBass`, `volChords`, `volMelody`, `loop`, `midiOut`.
  - Uscita MIDI sui canali traccia+1. Il MIDI in ingresso suona sulla traccia attiva.
  - Stato salvato: XML con bpm, offset, loop, `samplePath` o `demo`, note come `t,m,start,len;`, i parametri e il `ValueTree` `UI` (traccia, griglia, lunghezza, contrasto, pulizia, blocco scala, tema, zoom, dimensioni). Al ripristino il sample viene rianalizzato senza toccare il tempo salvato.
- **`PianoRoll`**: un solo componente con tastiera, righello (battute, accordi, loop, cursore) e griglia. Disegna solo la parte visibile.
  - La heatmap è una `Image` frames×69 scalata con `drawImageTransformed`, con soglia al 99,5° percentile e gamma dal contrasto.
  - Modi del mouse: `Move`, `Resize`, `NewNote`, `Ruler`. Durante il trascinamento modifica `work` e chiama `setNotes`; al rilascio chiama `commitEdit(before)`.
- **`PluginEditor`**: tre righe di comandi in `FlexBox`, con larghezza e larghezza minima per ogni elemento; il titolo si nasconde sotto i 1290 px; la larghezza minima della finestra è 1180 px. Contiene anche `MidiDragChip` (trascinamento del MIDI verso la DAW con `performExternalDragDropOfFiles`), il drop di file audio e le scorciatoie (spazio, Ctrl/Cmd+Z, 1/2/3).
- **`Theme.h`**: palette chiara e scura copiata dai token CSS della versione web, più `OrecchioLook`.
- **`Synth.h`**: 32 voci con ADSR. Basso: seno con saturazione. Accordi: dente di sega polyBLEP con passa-basso. Melodia: triangolo.

## Convenzioni
- I testi con accenti passano da `txt()` (`Text.h`): `juce::String(const char*)` accetta solo ASCII.
- Niente allocazioni e lock bloccanti in `processBlock`.
- Commenti in italiano, nomi in inglese.

## Limiti noti / idee
- Varispeed quando il tempo della DAW è diverso da quello del sample. Idea: time-stretch (es. Rubber Band o SoundTouch).
- Il primo battito rilevato non è per forza il primo della battuta. Idea: pulsanti ±1 battito, o rilevazione del downbeat dalla stabilità degli accordi.
- Solo 4/4. Sample fino a 5 minuti. Si salva il percorso del sample, non l'audio.
- Niente selezione multipla, velocity o quantizzazione di gruppo.
- La tonalità può uscire come la relativa maggiore/minore.
- macOS: firma ad-hoc, non notarizzato.
