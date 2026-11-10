suite_add_plugin(CauseAndEffect
    CODE        CnEf
    NAME        "Cause and Effect"
    DESCRIPTION "Trance build-up macro: one knob or one trigger drives a whole riser"
    CATEGORIES  Fx Filter
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)
