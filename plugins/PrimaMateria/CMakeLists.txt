suite_add_plugin(PrimaMateria
    CODE        Prmt
    NAME        "Prima Materia"
    DESCRIPTION "Section mixer: per-section level, low cut and high cut for any track, following the Ouroboros map"
    CATEGORIES  Fx Tools
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
    TESTS       Tests.cpp)
