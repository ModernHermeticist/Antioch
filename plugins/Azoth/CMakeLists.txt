suite_add_plugin(Azoth
    CODE        Azth
    NAME        "Azoth"
    DESCRIPTION "Automatic transition FX: impacts, crashes, swells, downlifters and snare rolls placed from the Ouroboros map"
    CATEGORIES  Fx Generator
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
    TESTS       Tests.cpp)
