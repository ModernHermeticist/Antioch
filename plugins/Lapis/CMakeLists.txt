suite_add_plugin(Lapis
    CODE        Laps
    NAME        "Lapis"
    DESCRIPTION "Master finisher: auto gain to the loudness target, clipper and true-peak limiter"
    CATEGORIES  Fx Dynamics Mastering
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
    TESTS       Tests.cpp)
