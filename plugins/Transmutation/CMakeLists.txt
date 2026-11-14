suite_add_plugin(Transmutation
    CODE        Trmu
    NAME        "Transmutation"
    DESCRIPTION "Creative moves for each track, chosen and timed by a director that follows the song"
    CATEGORIES  Fx
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
    TESTS       Tests.cpp)

# Azoth lives inside Transmutation now (merged)
suite_add_module(Transmutation Azoth)
