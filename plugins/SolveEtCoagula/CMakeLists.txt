suite_add_plugin(SolveEtCoagula
    CODE        Solv
    NAME        "Solve et Coagula"
    DESCRIPTION "Vocal unmasking: dips the music only where and when the vocal (sidechain) needs room"
    CATEGORIES  Fx EQ Dynamics
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
    TESTS       Tests.cpp)
