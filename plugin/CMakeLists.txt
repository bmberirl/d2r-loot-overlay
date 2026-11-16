cmake_minimum_required(VERSION 3.29)
project(LootDropOverlay VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the DLL carries its own runtime and does not depend on what the game ships.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# SDK pinned to the D2RLoader 1.3.1 era (commit 717f727).
set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(../sdk/PluginSDK sdk EXCLUDE_FROM_ALL)

add_library(LootDropOverlay SHARED
	src/plugin.cpp
	src/drop_feed.cpp
	src/http_server.cpp
	src/item_tables.cpp
	src/ground_scanner.cpp
	src/grail.cpp
	src/filter_store.cpp
	src/settings.cpp
	src/tally.cpp
	src/loot_drop_overlay.rc
)
set_target_properties(LootDropOverlay PROPERTIES OUTPUT_NAME "loot-drop-overlay")
target_link_libraries(LootDropOverlay PRIVATE D2RLPlugin::D2RLPlugin ws2_32)
target_compile_definitions(LootDropOverlay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(LootDropOverlay PRIVATE /W4 /permissive- /utf-8 /EHsc /guard:cf /Zi)
target_link_options(LootDropOverlay PRIVATE /guard:cf /DEBUG /OPT:REF /OPT:ICF)
