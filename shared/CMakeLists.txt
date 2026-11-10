# Code shared by every plugin in the suite. Compiled into each plugin separately
# (an INTERFACE library), so each plugin binary stays self-contained.
add_library(suite_shared INTERFACE)
target_sources(suite_shared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/SharedMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KeyBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SectionBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TargetBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CommandRing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FixBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SnapshotBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KickBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SuiteUI.cpp)
target_include_directories(suite_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
