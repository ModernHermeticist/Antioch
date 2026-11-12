suite_add_plugin(Correspondence
    CODE        Crsp
    NAME        "Correspondence"
    DESCRIPTION "Key and chord engine: trance progressions as drag-out MIDI, broadcasts the key to the suite"
    CATEGORIES  Fx Tools
    SOURCES     Source/PluginProcessor.cpp
                Source/PluginEditor.cpp
                Source/Parameters.cpp
    TESTS       Tests.cpp)

# The pattern library: a text file per role, embedded (PatternData.h)
file(GLOB CORRESPONDENCE_PATTERNS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Patterns/*.txt)
juce_add_binary_data(CorrespondencePatterns HEADER_NAME PatternData.h NAMESPACE PatternData SOURCES ${CORRESPONDENCE_PATTERNS})
set_target_properties(CorrespondencePatterns PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(Correspondence PUBLIC CorrespondencePatterns)
