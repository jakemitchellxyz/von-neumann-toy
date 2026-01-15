cmake_minimum_required(VERSION 3.16)
project(vnt LANGUAGES C CXX)

# Require C++17 for std::filesystem
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for IDE/linter support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "") # works

include(FetchContent)

find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(TIFF REQUIRED)
find_package(GDAL CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# Find stb (header-only image loading library)
find_path(STB_INCLUDE_DIRS "stb_image.h" HINTS ${CMAKE_PREFIX_PATH}/include)

# ==================================
# Find SuperNOVAS (optional, for additional astrometry features)
# ==================================
find_package(supernovas CONFIG QUIET)
if(TARGET supernovas::core)
    set(SUPERNOVAS_TARGET supernovas::core)
    set(HAS_SUPERNOVAS TRUE)
    message(STATUS "Found SuperNOVAS (supernovas::core)")
elseif(TARGET supernovas::supernovas)
    set(SUPERNOVAS_TARGET supernovas::supernovas)
    set(HAS_SUPERNOVAS TRUE)
    message(STATUS "Found SuperNOVAS (supernovas::supernovas)")
elseif(TARGET supernovas)
    set(SUPERNOVAS_TARGET supernovas)
    set(HAS_SUPERNOVAS TRUE)
    message(STATUS "Found SuperNOVAS")
else()
    set(HAS_SUPERNOVAS FALSE)
    message(STATUS "SuperNOVAS not found - astrometry features will use fallbacks")
endif()

# ==================================
# Find or Download CSPICE
# ==================================
find_package(cspice CONFIG QUIET)

if(NOT TARGET cspice AND NOT TARGET cspice::cspice)
    # Try to find the library in standard locations
    find_library(CSPICE_LIBRARY
        NAMES cspice cspice.a libcspice cspice.lib
        HINTS
        ${CMAKE_PREFIX_PATH}
        ${CMAKE_SOURCE_DIR}/../external/cspice/lib
        $ENV{CSPICE_DIR}/lib
        PATH_SUFFIXES lib
    )
    find_path(CSPICE_INCLUDE_DIR
        NAMES SpiceUsr.h
        HINTS
        ${CMAKE_PREFIX_PATH}
        ${CMAKE_SOURCE_DIR}/../external/cspice/include
        $ENV{CSPICE_DIR}/include
        PATH_SUFFIXES include include/cspice
    )

    if(CSPICE_LIBRARY AND CSPICE_INCLUDE_DIR)
        message(STATUS "Found CSPICE library: ${CSPICE_LIBRARY}")
        message(STATUS "Found CSPICE include: ${CSPICE_INCLUDE_DIR}")
        add_library(cspice_imported UNKNOWN IMPORTED)
        set_target_properties(cspice_imported PROPERTIES
            IMPORTED_LOCATION "${CSPICE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CSPICE_INCLUDE_DIR}"
        )
        set(CSPICE_TARGET cspice_imported)
    else()
        # Download CSPICE from NASA/NAIF
        message(STATUS "CSPICE not found locally, downloading from NASA/NAIF...")

        set(CSPICE_DIR "${CMAKE_BINARY_DIR}/_deps/cspice")

        if(WIN32)
            set(CSPICE_URL "https://naif.jpl.nasa.gov/pub/naif/toolkit/C/PC_Windows_VisualC_64bit/packages/cspice.zip")
            set(CSPICE_ARCHIVE "${CSPICE_DIR}/cspice.zip")
        elseif(APPLE)
            set(CSPICE_URL "https://naif.jpl.nasa.gov/pub/naif/toolkit/C/MacIntel_OSX_AppleC_64bit/packages/cspice.tar.Z")
            set(CSPICE_ARCHIVE "${CSPICE_DIR}/cspice.tar.Z")
        else()
            set(CSPICE_URL "https://naif.jpl.nasa.gov/pub/naif/toolkit/C/PC_Linux_GCC_64bit/packages/cspice.tar.Z")
            set(CSPICE_ARCHIVE "${CSPICE_DIR}/cspice.tar.Z")
        endif()

        if(NOT EXISTS "${CSPICE_DIR}/cspice/lib")
            file(MAKE_DIRECTORY ${CSPICE_DIR})

            if(NOT EXISTS ${CSPICE_ARCHIVE})
                message(STATUS "Downloading CSPICE from ${CSPICE_URL}")
                file(DOWNLOAD ${CSPICE_URL} ${CSPICE_ARCHIVE}
                    SHOW_PROGRESS
                    STATUS DOWNLOAD_STATUS
                )
                list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
                if(NOT STATUS_CODE EQUAL 0)
                    message(FATAL_ERROR "Failed to download CSPICE. Please download manually from:\n${CSPICE_URL}")
                endif()
            endif()

            message(STATUS "Extracting CSPICE...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf ${CSPICE_ARCHIVE}
                WORKING_DIRECTORY ${CSPICE_DIR}
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract CSPICE archive")
            endif()
        endif()

        # Set paths to downloaded CSPICE
        set(CSPICE_INCLUDE_DIR "${CSPICE_DIR}/cspice/include")
        if(WIN32)
            set(CSPICE_LIBRARY "${CSPICE_DIR}/cspice/lib/cspice.lib")
        else()
            set(CSPICE_LIBRARY "${CSPICE_DIR}/cspice/lib/cspice.a")
        endif()

        if(EXISTS ${CSPICE_LIBRARY})
            message(STATUS "Using downloaded CSPICE: ${CSPICE_LIBRARY}")
            add_library(cspice_imported STATIC IMPORTED)
            set_target_properties(cspice_imported PROPERTIES
                IMPORTED_LOCATION "${CSPICE_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${CSPICE_INCLUDE_DIR}"
            )
            set(CSPICE_TARGET cspice_imported)
        else()
            message(FATAL_ERROR
                "CSPICE library not found after download.\n"
                "Please download manually from: https://naif.jpl.nasa.gov/naif/toolkit_C.html\n"
                "And set CSPICE_DIR environment variable to the extracted location."
            )
        endif()
    endif()
else()
    if(TARGET cspice::cspice)
        set(CSPICE_TARGET cspice::cspice)
    else()
        set(CSPICE_TARGET cspice)
    endif()
    message(STATUS "Found CSPICE via find_package")
endif()

find_package(json5-parser CONFIG QUIET)
find_package(json5_parser CONFIG QUIET)
find_package(OpenXLSX CONFIG QUIET)

# ==================================
# Protobuf Code Generation (must be before add_executable)
# ==================================
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/materials/earth/economy")
set(PROTO_FILE "${PROTO_DIR}/cities.proto")
get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h")

# Generate protobuf C++ code
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND protobuf::protoc
    ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
    --proto_path=${PROTO_DIR}
    ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating C++ code from ${PROTO_FILE}"
)

add_executable(vnt
    entrypoint.cpp
    concerns/constants.cpp
    concerns/camera-controller.cpp
    concerns/stars-dynamic-skybox.cpp
    concerns/constellation-loader.cpp
    concerns/ui-overlay.cpp
    concerns/ui-icons.cpp
    concerns/ui-primitives.cpp
    concerns/ui-controls.cpp
    concerns/font-rendering.cpp
    concerns/spice-ephemeris.cpp
    concerns/gravity-grid.cpp
    concerns/settings.cpp
    concerns/solar-lighting.cpp
    types/celestial-body.cpp
    types/lagrange-point.cpp
    types/magnetic-field.cpp
    # Earth Material Implementation
    materials/earth/setup.cpp
    materials/earth/draw.cpp
    materials/earth/initialization/atmosphere-shader.cpp
    materials/earth/initialization/earth-surface.cpp
    materials/earth/preprocessing/color-from-blue-marble.cpp
    materials/earth/preprocessing/normal-and-height-from-elevation.cpp
    materials/earth/preprocessing/specular-from-modis-reflectance.cpp
    materials/earth/preprocessing/night-lights-from-viirs.cpp
    materials/earth/preprocessing/atmosphere-transmittance-lut.cpp
    materials/earth/preprocessing/water-scattering-lut.cpp
    materials/earth/masks/ice-from-color.cpp
    materials/earth/masks/landmass-from-color.cpp
    materials/earth/economy/earth-economy.cpp
    materials/earth/economy/economy-renderer-setup.cpp
    materials/earth/economy/economy-renderer-draw.cpp
    materials/earth/helpers/coordinate-conversion.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    # Helper modules
    materials/helpers/gl.cpp
    materials/helpers/noise.cpp
    materials/helpers/julian.cpp
)

# Include stb headers if found
if(STB_INCLUDE_DIRS)
    target_include_directories(vnt PRIVATE ${STB_INCLUDE_DIRS})
    message(STATUS "Found stb headers at: ${STB_INCLUDE_DIRS}")
endif()

# Link libraries
target_link_libraries(vnt PRIVATE glfw OpenGL::GL glm::glm ${CSPICE_TARGET} TIFF::TIFF GDAL::GDAL protobuf::libprotobuf)
target_compile_definitions(vnt PRIVATE HAS_CSPICE HAS_PROTOBUF)
target_include_directories(vnt PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Link SuperNOVAS if available
if(HAS_SUPERNOVAS)
    target_link_libraries(vnt PRIVATE ${SUPERNOVAS_TARGET})
    target_compile_definitions(vnt PRIVATE HAS_SUPERNOVAS)
endif()

# Suppress LIBCMT conflict warning (CSPICE uses static CRT)
if(MSVC)
    target_link_options(vnt PRIVATE /NODEFAULTLIB:LIBCMT)
endif()

# Link json5-parser if found
if(TARGET json5-parser::json5-parser)
    target_link_libraries(vnt PRIVATE json5-parser::json5-parser)
    target_compile_definitions(vnt PRIVATE HAS_JSON5_PARSER)
elseif(TARGET json5_parser::json5_parser)
    target_link_libraries(vnt PRIVATE json5_parser::json5_parser)
    target_compile_definitions(vnt PRIVATE HAS_JSON5_PARSER)
elseif(TARGET json5-parser)
    target_link_libraries(vnt PRIVATE json5-parser)
    target_compile_definitions(vnt PRIVATE HAS_JSON5_PARSER)
endif()

# Link OpenXLSX if found
if(TARGET OpenXLSX::OpenXLSX)
    target_link_libraries(vnt PRIVATE OpenXLSX::OpenXLSX)
    target_compile_definitions(vnt PRIVATE HAS_OPENXLSX)
    message(STATUS "Found OpenXLSX - Excel file support enabled")
elseif(TARGET OpenXLSX)
    target_link_libraries(vnt PRIVATE OpenXLSX)
    target_compile_definitions(vnt PRIVATE HAS_OPENXLSX)
    message(STATUS "Found OpenXLSX (non-namespaced) - Excel file support enabled")
else()
    message(STATUS "OpenXLSX not found - Excel file support disabled")
    # Don't define HAS_OPENXLSX, so code will skip OpenXLSX usage
endif()

# Copy defaults folder to build output directory (only missing files)
set(DEFAULTS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../defaults")

# Create a CMake script to conditionally copy files
set(COPY_SCRIPT "${CMAKE_BINARY_DIR}/copy_defaults.cmake")
file(WRITE ${COPY_SCRIPT}
    "# Conditional copy script - only copy files that don't exist
set(SOURCE_DIR \"${DEFAULTS_SOURCE_DIR}\")
set(DEST_DIR \"\${DEFAULTS_DEST_DIR}\")

if(NOT DEST_DIR)
    message(FATAL_ERROR \"DEFAULTS_DEST_DIR variable not set\")
endif()

# Get all files recursively from source directory
file(GLOB_RECURSE SOURCE_FILES RELATIVE \"\${SOURCE_DIR}\" \"\${SOURCE_DIR}/*\")

set(COPIED_COUNT 0)
set(SKIPPED_COUNT 0)

# Process each file
foreach(RELATIVE_FILE \${SOURCE_FILES})
    set(SRC_FILE \"\${SOURCE_DIR}/\${RELATIVE_FILE}\")
    set(DST_FILE \"\${DEST_DIR}/\${RELATIVE_FILE}\")
    
    # Check if destination file exists
    if(NOT EXISTS \"\${DST_FILE}\")
        # Get directory of destination file
        get_filename_component(DST_DIR \"\${DST_FILE}\" DIRECTORY)
        
        # Create destination directory if it doesn't exist
        if(NOT EXISTS \"\${DST_DIR}\")
            file(MAKE_DIRECTORY \"\${DST_DIR}\")
        endif()
        
        # Copy the file
        file(COPY \"\${SRC_FILE}\" DESTINATION \"\${DST_DIR}\")
        math(EXPR COPIED_COUNT \"\${COPIED_COUNT} + 1\")
    else()
        math(EXPR SKIPPED_COUNT \"\${SKIPPED_COUNT} + 1\")
    endif()
endforeach()

if(COPIED_COUNT GREATER 0)
    message(STATUS \"Defaults copy: \${COPIED_COUNT} files copied, \${SKIPPED_COUNT} files skipped (already exist)\")
endif()
")

add_custom_command(TARGET vnt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DDEFAULTS_DEST_DIR="$<TARGET_FILE_DIR:vnt>/defaults" -P ${COPY_SCRIPT}
    COMMENT "Copying defaults folder (only missing files) to output directory"
)
