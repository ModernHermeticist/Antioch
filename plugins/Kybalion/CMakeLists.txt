suite_add_plugin(Kybalion
    CODE        Kybl
    NAME        "Kybalion"
    DESCRIPTION "Trance bus: 16-step gate (Rhythm) and tempo-synced or sidechain ducking (Polarity)"
    CATEGORIES  Fx Dynamics
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)
