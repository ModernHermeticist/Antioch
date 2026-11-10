suite_add_plugin(Antioch
    CODE        Anth
    NAME        "Antioch"
    DESCRIPTION "Space bundle: harmonic echo, euphoric reverb and throw delay"
    CATEGORIES  Fx Delay Reverb
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)
