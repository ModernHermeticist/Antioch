suite_add_plugin(Correspondence
    CODE        Crsp
    NAME        "Correspondence"
    DESCRIPTION "Key and chord engine: trance progressions as drag-out MIDI, broadcasts the key to the suite"
    CATEGORIES  Fx Tools
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)
