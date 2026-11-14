suite_add_plugin(Mentalism
    CODE        Mntl
    NAME        "Mentalism"
    DESCRIPTION "Mix assistant: loudness, true peak, stereo and tonal balance against a target or reference track"
    CATEGORIES  Fx Analyzer
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)

# Let reference tracks be MP3 as well as WAV/AIFF/FLAC/Ogg.
target_compile_definitions(Mentalism PUBLIC JUCE_USE_MP3AUDIOFORMAT=1)

# Lapis lives inside Mentalism now (merged)
suite_add_module(Mentalism Lapis)
