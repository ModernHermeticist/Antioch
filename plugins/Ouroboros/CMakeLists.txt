suite_add_plugin(Ouroboros
    MIDI_INPUT
    CODE        Orbs
    NAME        "Ouroboros"
    DESCRIPTION "Arrangement conductor: tells the suite which section the song is in"
    CATEGORIES  Fx Tools
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
    TESTS       Tests.cpp)
