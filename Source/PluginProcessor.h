#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DSP.h"
#include "Model.h"
#include "Synth.h"

#include <atomic>
#include <memory>
#include <vector>

class OrecchioProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster
{
public:
    OrecchioProcessor();
    ~OrecchioProcessor() override;

    // --- AudioProcessor ---------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Analisi (thread dei messaggi) ------------------------------------------------------
    struct Analysis
    {
        dsp::Spectrogram raw, clean;
        dsp::Key key;
        dsp::Tempo tempo;
        double duration = 0;
        bool valid = false;
    };

    void loadSampleFile (const juce::File& file, bool useDetectedTempo = true);
    void loadDemo (bool useDetectedTempo = true);
    const Analysis& getAnalysis() const noexcept { return analysis; }
    const std::vector<dsp::Chord>& getChords() const noexcept { return chords; }
    bool isBusy() const noexcept { return busy; }
    juce::String getStatus() const { return status; }
    void setStatus (const juce::String& s) { status = s; sendChangeMessage(); }
    juce::String getSampleName() const { return sampleName; }
    juce::AudioFormatManager& getFormatManager() { return formats; }

    // --- Note e modifiche (thread dei messaggi) ---------------------------------------------
    const std::vector<orecchio::Note>& getNotes() const noexcept { return notes; }
    void setNotes (std::vector<orecchio::Note> newNotes);           // aggiornamento "live" durante un trascinamento
    void commitEdit (const std::vector<orecchio::Note>& before);    // salva lo stato precedente per Annulla
    bool undo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    void suggestBass();
    void suggestChords();
    void clearTrack (int track);

    // --- Tempo e griglia --------------------------------------------------------------------
    double getBpm() const noexcept { return bpm.load(); }
    double getOffset() const noexcept { return offset.load(); }
    void setBpm (double newBpm);
    void setOffset (double seconds);
    void applyDetectedTempo();
    int getBeatsPerBar() const noexcept { return 4; }
    double getProjectBeats() const noexcept { return projectBeats.load(); }

    // --- Trasporto --------------------------------------------------------------------------
    void togglePlay();
    bool isPlaying() const noexcept { return internalPlaying.load() || hostPlaying.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }
    double getHostBpm() const noexcept { return hostBpm.load(); }
    double getPlayheadBeat() const noexcept { return playheadBeat.load(); }
    void setPlayheadBeat (double beat);
    void setLoopRange (double startBeat, double endBeat);
    double getLoopStart() const noexcept { return loopStart.load(); }
    double getLoopEnd() const noexcept { return loopEnd.load(); }

    void previewNote (int track, int midi);
    void setActiveTrack (int t) { activeTrack = juce::jlimit (0, orecchio::kNumTracks - 1, t); }

    // --- MIDI --------------------------------------------------------------------------------
    juce::MemoryBlock makeMidiFile (int onlyTrack = -1) const; // -1 = tutte le tracce
    juce::File writeTempMidiForDrag() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::ValueTree ui { "UI" }; // preferenze dell'interfaccia, salvate col progetto

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void startAnalysis (std::function<bool (juce::AudioBuffer<float>&, double&, juce::String&)> loader,
                        juce::String name, juce::String path, bool demo, bool useDetectedTempo);
    void recomputeChords();
    void updateProjectBeats();
    void publishNotes();
    void allNotesOff (juce::MidiBuffer& midi, int samplePos);
    void processEvents (double b0, double b1, juce::MidiBuffer& midi, int samplePos, bool midiOut);

    juce::AudioFormatManager formats;
    juce::ThreadPool pool { 1 };
    int loadGeneration = 0;

    // stato lato messaggi
    Analysis analysis;
    std::vector<dsp::Chord> chords;
    std::vector<orecchio::Note> notes;
    std::vector<std::vector<orecchio::Note>> undoStack;
    juce::String status { "Carica un sample o prova la demo" }, sampleName, samplePath;
    bool sampleIsDemo = false, busy = false;

    // condiviso con il thread audio
    std::atomic<double> bpm { 90.0 }, offset { 0.0 }, projectBeats { 16.0 };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 16.0 }, playheadBeat { 0.0 }, hostBpm { 0.0 };
    std::atomic<double> seekBeat { 0.0 };
    std::atomic<bool> seekPending { false }, internalPlaying { false }, hostPlaying { false };
    std::atomic<int> activeTrack { orecchio::kMelody };

    juce::SpinLock notesLock;
    std::vector<orecchio::Note> sharedNotes;     // protetto da notesLock
    std::atomic<bool> notesDirty { false };

    juce::SpinLock sampleLock;
    std::unique_ptr<juce::AudioBuffer<float>> sample; // protetto da sampleLock
    double sampleFileRate = 44100.0;

    struct Preview { int track, midi; };
    juce::AbstractFifo previewFifo { 64 };
    std::array<Preview, 64> previewBuf {};

    // solo thread audio
    orecchio::TrackSynth synth;
    std::vector<orecchio::Note> audioNotes;
    struct Active { int track, midi; double end; };
    std::array<Active, 256> active {};
    int numActive = 0;
    double currentBeat = 0.0, lastHostBeat = -1.0, rate = 44100.0;
    bool wasPlaying = false;

    std::atomic<float>* volSample = nullptr;
    std::atomic<float>* volTrack[orecchio::kNumTracks] {};
    std::atomic<float>* loopParam = nullptr;
    std::atomic<float>* midiOutParam = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE (OrecchioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrecchioProcessor)
};
