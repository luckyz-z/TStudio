#include "Engine/Studio.h"

namespace ts
{
bool Studio::importMidi(const juce::File& file, int channelId, juce::String& error)
{
    juce::FileInputStream stream(file);
    juce::MidiFile midi;
    if (!stream.openedOk() || !midi.readFrom(stream))
    {
        error = "Could not read " + file.getFileName();
        return false;
    }
    const auto format = midi.getTimeFormat();
    if (format <= 0)
    {
        error = "SMPTE-timed MIDI files are not supported.";
        return false;
    }
    if (project.findChannel(channelId) == nullptr)
    {
        error = "Select a channel first.";
        return false;
    }

    std::vector<Note> notes;
    const auto scale = double(ppq) / format;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence sequence(*midi.getTrack(t));
        sequence.updateMatchedPairs();
        for (const auto* event : sequence)
        {
            if (!event->message.isNoteOn())
                continue;
            const auto start = event->message.getTimeStamp() * scale;
            const auto end = event->noteOffObject != nullptr ? event->noteOffObject->message.getTimeStamp() * scale : start + ticksPerStep;
            notes.push_back({ juce::roundToInt(start), juce::jmax(1, juce::roundToInt(end - start)), event->message.getNoteNumber(), event->message.getFloatVelocity() });
        }
    }
    if (notes.empty())
    {
        error = file.getFileName() + " has no notes.";
        return false;
    }

    project.checkpoint();
    {
        const juce::ScopedLock sl(project.lock);
        project.pattern().notes[channelId] = std::move(notes);
    }
    project.touch();
    return true;
}

bool Studio::exportMidi(const juce::File& file, int channelId, juce::String& error)
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ppq);
    juce::MidiMessageSequence tempoTrack;
    tempoTrack.addEvent(juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000.0 / project.tempo.load())), 0.0);
    tempoTrack.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    midi.addTrack(tempoTrack);

    for (const auto& [id, notes] : project.pattern().notes)
    {
        if ((channelId >= 0 && id != channelId) || notes.empty())
            continue;
        juce::MidiMessageSequence track;
        if (const auto* channel = project.findChannel(id))
            track.addEvent(juce::MidiMessage::textMetaEvent(3, channel->name), 0.0);
        for (const auto& note : notes)
        {
            track.addEvent(juce::MidiMessage::noteOn(1, juce::jlimit(0, 127, note.key), juce::jlimit(0.01f, 1.0f, note.velocity)), double(note.start));
            track.addEvent(juce::MidiMessage::noteOff(1, juce::jlimit(0, 127, note.key)), double(note.end()));
        }
        track.sort();
        track.updateMatchedPairs();
        midi.addTrack(track);
    }

    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk() || !midi.writeTo(stream))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}
}
