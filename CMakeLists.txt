cmake_minimum_required(VERSION 3.22)

# --- 1. SETUP LIBRARY NAME ---
get_filename_component(MODULE_DIR_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
string(TOUPPER "${MODULE_DIR_NAME}" MODULE_NAME_UPPER)
project(${MODULE_NAME_UPPER} C)

set(LIB_NAME "${MODULE_NAME_UPPER}_lib")
message(STATUS "Configuring Module: ${LIB_NAME}")

# --- 2. SOURCE MANAGEMENT ---
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c")

if(SRC_FILES)
    add_library(${LIB_NAME} STATIC ${SRC_FILES})
    target_include_directories(${LIB_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Inc")
else()
    add_library(${LIB_NAME} INTERFACE)
    target_include_directories(${LIB_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Inc")
endif()

# --- 3. DEPENDENCY MANAGEMENT ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake")

    # A. INTERNAL: Apply naming convention (Driver_GPIO -> DRIVER_GPIO_lib)
    foreach(DEP_FOLDER ${INTERNAL_DEPS})
        string(TOUPPER "${DEP_FOLDER}" DEP_UPPER)
        set(DEP_LIB_NAME "${DEP_UPPER}_lib")
        
        message(STATUS "  -> Linking Internal: ${DEP_LIB_NAME}")
        target_link_libraries(${LIB_NAME} PUBLIC ${DEP_LIB_NAME})
    endforeach()

    # B. EXTERNAL: Link exactly as provided (stm32cubemx -> stm32cubemx)
    foreach(EXT_LIB ${EXTERNAL_DEPS})
        message(STATUS "  -> Linking External: ${EXT_LIB}")
        target_link_libraries(${LIB_NAME} PUBLIC ${EXT_LIB})
    endforeach()
endif()