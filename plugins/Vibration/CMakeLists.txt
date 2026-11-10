suite_add_plugin(Vibration
    CODE        Vibr
    NAME        "Vibration"
    DESCRIPTION "Lead polish strip: one Euphoria knob over EQ, saturation, multiband and width"
    CATEGORIES  Fx EQ Dynamics
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)
