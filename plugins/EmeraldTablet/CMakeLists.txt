suite_add_plugin(EmeraldTablet
    CODE        Emtb
    NAME        "Emerald Tablet"
    DESCRIPTION "Key detector: listens to the mix and hands the key to Correspondence and the suite"
    CATEGORIES  Fx Analyzer
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
    TESTS       Tests.cpp)
