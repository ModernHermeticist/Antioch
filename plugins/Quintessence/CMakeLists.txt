suite_add_plugin(Quintessence
    CODE        Quin
    NAME        "Quintessence"
    DESCRIPTION "Self-managing EQ and multiband dynamics: moves towards the track's role, checks each move, explains it"
    CATEGORIES  Fx EQ Dynamics
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)
