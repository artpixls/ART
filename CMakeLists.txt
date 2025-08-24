cmake_minimum_required(VERSION 3.9)
if(APPLE)
    cmake_policy(SET CMP0025 NEW)
    cmake_policy(SET CMP0037 NEW)
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Enabling ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

project(ART)

# The default target is Release:
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "One of: None Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

string(TOUPPER ${CMAKE_BUILD_TYPE} UPPER_CMAKE_BUILD_TYPE)

# Set required C and C++ standards and check GCC version:
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9")
    message(FATAL_ERROR "Building ART requires using GCC version 4.9 or higher!")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "10.0" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.2")
    message(STATUS "WARNING: gcc ${CMAKE_CXX_COMPILER_VERSION} is known to miscompile ART when using -ftree-loop-vectorize, forcing the option to be off")    
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-tree-loop-vectorize")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-tree-loop-vectorize")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "11.0")
    message(STATUS "WARNING: gcc ${CMAKE_CXX_COMPILER_VERSION} is known to miscompile ART when using --ffp-contract=fast, forcing the option to be off")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffp-contract=off")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffp-contract=off")
endif()

# We might want to build using the old C++ ABI, even when using a new GCC version:
if(USE_OLD_CXX_ABI)
    add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)
endif()

if(UPPER_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
    add_definitions(-D_DEBUG)
else()
    add_definitions(-DNDEBUG)
    add_definitions(-D_DNDEBUG)
endif()

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

# Cache name suffix examples: "" = ~/.config/ART, "5" = ~/.config/ART-5, "_testing" = ~/.config/ART_testing
# Use "" for stable releases and "5-dev" for anything else.
set(CACHE_NAME_SUFFIX "" CACHE STRING "ART's cache folder suffix")

# By default we don't use a specific processor target, so PROC_TARGET_NUMBER is set to 0.
# Specify other values to optimize for specific processor architecture as listed in ProcessorTargets.cmake:
set(PROC_TARGET_NUMBER 0 CACHE STRING "Selected target processor from the list above (taken from ProcessorTargets.cmake)")

# Set special compilation flags for rtengine which get added to CMAKE_CXX_FLAGS:
# Some Linux distros build with -O2 instead of -O3. We explicitly enable auto vectorization by using -ftree-vectorize
set(RTENGINE_CXX_FLAGS "-ftree-vectorize" CACHE STRING "Special compilation flags for RTEngine")

# Loads the ProcessorTargets list:
include(ProcessorTargets.cmake)
set(PROC_LABEL "undefined" CACHE STRING "Target processor label, unused if PROC_TARGET_NUMBER = 0 or 2")
set(PROC_FLAGS "" CACHE STRING "Target processor related build/link flags")
if((NOT (PROC_TARGET_NUMBER EQUAL 0)) AND (NOT (PROC_TARGET_NUMBER EQUAL 2)))
    set(PROC_LABEL ${PROC_TARGET_${PROC_TARGET_NUMBER}_LABEL})
endif()
if(NOT(PROC_TARGET_NUMBER EQUAL 0))
    set(PROC_FLAGS ${PROC_TARGET_${PROC_TARGET_NUMBER}_FLAGS})
endif()
if(UNIX AND PROC_LABEL STREQUAL "undefined")
    execute_process(COMMAND uname -p OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE cpu)
    if("${cpu}" STREQUAL "unknown")
        set(_proc_label "${CMAKE_SYSTEM_PROCESSOR}")
    else()
        set(_proc_label "${cpu}")
    endif()
    string(REGEX REPLACE "[^a-zA-Z0-9]" "_" PROC_LABEL "${_proc_label}")
endif()

# If PROC_FORCED_LABEL exists, its value is loaded in PROC_LABEL to override the one from ProcessorTargets:
if(DEFINED PROC_FORCED_LABEL)
    set(PROC_LABEL ${PROC_FORCED_LABEL})
endif()

# Add the proc flags to the build flags:
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PROC_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${PROC_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${PROC_FLAGS}")

# Stop compilation on typos such as std:swap (missing colon will be detected as unused label):
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=unused-label")

# Special treatment for x87 and x86-32 SSE (see GitHub issue #4324)
include(FindX87Math)
if(HAVE_X87_MATH)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffloat-store")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffloat-store")
endif()
if(HAVE_X86_SSE_MATH)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 -mfpmath=sse")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2 -mfpmath=sse")
endif()

# Do net set math errno, as we never check its value.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-math-errno")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-math-errno")

# On i386 Linux we can fix unaligned SSE malloc (see GitHub issue #4432)
include(FindUnalignedMalloc)

if(WIN32)
    # Add additional paths. Look in the MinGW path first, then in the Gtkmm path.
    # If you wish to build some dependent libraries, you have to install them in MinGW to use them:
    set(CMAKE_PREFIX_PATH $ENV{MINGW_BASEPATH} $ENV{GTKMM_BASEPATH} CACHE STRING "Additional search paths")

    # increase the stack size
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--stack,4194304")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--stack,4194304")
endif()

if(APPLE)
    if(CMAKE_CXX_COMPILER MATCHES "g\\+\\+-mp-4.[5-8]" OR CMAKE_CXX_COMPILER_ARG1 MATCHES "g\\+\\+-mp-4.[5-8]")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /usr/lib/libstdc++.6.dylib")
        message(STATUS "CMAKE_CXX_COMPILER is MacPorts GCC.\n   CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
    endif()

    # Set minimum system version
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -headerpad_max_install_names")
endif()

option(BUILD_SHARED "Build with shared libraries" OFF)
option(WITH_BENCHMARK "Build with benchmark code" OFF)
option(WITH_LTO "Build with link-time optimizations" OFF)
option(WITH_SAN "Build with run-time sanitizer" OFF)
option(WITH_PROF "Build with profiling instrumentation" OFF)
option(WITH_SYSTEM_KLT "Build using system KLT library" OFF)
option(OPTION_OMP "Build with OpenMP support" ON)
option(ENABLE_MIMALLOC "Use the mimalloc library if available" ON)
option(ENABLE_LIBRAW "Use libraw for decoding" ON)
option(ENABLE_OCIO "Use OpenColorIOv2 for LUT application" ON)
option(ENABLE_CTL "Enable support for the ACES Color Transformation Language" OFF)

option(MACOS_LEGACY_BUNDLE "Use legacy method for building a macOS bundle using the tools/osx/macos_bundle.sh script" OFF)

option(OSX_DEV_BUILD "DEPRECATED - Generate macOS development builds" OFF)

option(OSX_NIGHTLY "DEPRECATED - Generate a generically-named zip (only when MACOS_LEGACY_BUNDLE is OFF)" OFF)

option(OSX_UNIVERSAL "DEPRECATED - Generate a universal app (only when MACOS_LEGACY_BUNDLE is OFF)" OFF)

if(MACOS_LEGACY_BUNDLE)
    set(OSX_DEV_BUILD 1 FORCE)
else()
    set(OSX_DEV_BUILD 0 FORCE)
endif()

if(APPLE AND OSX_DEV_BUILD)
    set(APPLE_NEW_BUNDLE 1)
else()
    set(APPLE_NEW_BUNDLE 0)
endif()

# Set installation directories:
if(WIN32 OR APPLE)
    if(BUILD_BUNDLE)
        message(
            STATUS
                "You have set BUILD_BUNDLE=ON but this is not necessary - the option is forced to ON for Windows and macOS."
        )
    endif()
    set(BUILD_BUNDLE ON FORCE)
endif()

if(NOT DEFINED BUNDLE_BASE_INSTALL_DIR)
    set(BUNDLE_BASE_INSTALL_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
    if(APPLE_NEW_BUNDLE)
        set(BUNDLE_BASE_INSTALL_DIR "${BUNDLE_BASE_INSTALL_DIR}/ART.app")
    elseif(APPLE)
        set(BUNDLE_BASE_INSTALL_DIR "${PROJECT_SOURCE_DIR}/build")
    endif()
endif()

if(BUILD_BUNDLE)
    set(CMAKE_INSTALL_PREFIX "${BUNDLE_BASE_INSTALL_DIR}")
    if(APPLE_NEW_BUNDLE)
        set(BINDIR "Contents/MacOS")
    elseif(APPLE)
        set(BINDIR "${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/MacOS")
    else()
        set(BINDIR .)
    endif()
endif()

if(NOT DEFINED BINDIR)
    set(BINDIR "${CMAKE_INSTALL_PREFIX}/bin")
endif()

if(NOT DEFINED DATADIR)
    if(BUILD_BUNDLE)
        if(APPLE_NEW_BUNDLE)
            set(DATADIR "Contents/Resources")
        elseif(APPLE)
            set(DATADIR "/Applications/ART.app/Contents/Resources/share")
        else()
            set(DATADIR .)
        endif()
    else()
        set(DATADIR "${CMAKE_INSTALL_PREFIX}/share/ART")
    endif()
endif()

if(NOT DEFINED LIBDIR)
    if(BUILD_BUNDLE)
        if(APPLE_NEW_BUNDLE)
            set(LIBDIR "Contents/Frameworks")
        elseif(APPLE)
            set(LIBDIR "${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/Frameworks")
        else()
            set(LIBDIR .)
        endif()
    else()
        # Respect CMAKE_INSTALL_LIBDIR if set
        if(DEFINED CMAKE_INSTALL_LIBDIR)
            if(IS_ABSOLUTE "${LIBDIR}")
                set(LIBDIR "${CMAKE_INSTALL_LIBDIR}")
            else()
                set(LIBDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
            endif()
        else()
            set(LIBDIR "${CMAKE_INSTALL_PREFIX}/lib")
        endif()
    endif()
endif()

if(NOT DEFINED DOCDIR)
    if(BUILD_BUNDLE)
        if(APPLE_NEW_BUNDLE)
            set(DOCDIR "${DATADIR}/doc")
        elseif(APPLE)
            set(DOCDIR "/Applications/ART.app/Contents/Resources")
        else()
            set(DOCDIR "${DATADIR}/share/doc")
        endif()
    else()
        set(DOCDIR "${CMAKE_INSTALL_PREFIX}/share/doc/ART")
    endif()
endif()

if(NOT DEFINED CREDITSDIR)
    if(BUILD_BUNDLE)
        if(APPLE AND NOT APPLE_NEW_BUNDLE)
            set(CREDITSDIR "/Applications/ART.app/Contents/Resources")
        else()
            set(CREDITSDIR "${DATADIR}")
        endif()
    else()
        set(CREDITSDIR "${CMAKE_INSTALL_PREFIX}/share/doc/ART")
    endif()
endif()

if(NOT DEFINED LICENCEDIR)
    if(BUILD_BUNDLE)
        set(LICENCEDIR "${CREDITSDIR}")
    else()
        set(LICENCEDIR "${CMAKE_INSTALL_PREFIX}/share/doc/ART")
    endif()
endif()

if(NOT DEFINED DESKTOPDIR)
    if(UNIX)
        if(BUILD_BUNDLE)
            if(APPLE)
                set(DESKTOPDIR "${DATADIR}/applications")
            else()
                set(DESKTOPDIR "${DATADIR}/share/applications")
            endif()
        else()
            set(DESKTOPDIR "${CMAKE_INSTALL_PREFIX}/share/applications")
        endif()
    endif()
endif()

if(NOT DEFINED ICONSDIR)
    if(UNIX)
        if(BUILD_BUNDLE)
            if(APPLE)
                set(ICONSDIR "${DATADIR}/icons")
            else()
                set(ICONSDIR "${DATADIR}/share/icons")
            endif()
        else()
            set(ICONSDIR "${CMAKE_INSTALL_PREFIX}/share/icons")
        endif()
    endif()
endif()

if(NOT DEFINED APPDATADIR)
    if(UNIX)
        if(BUILD_BUNDLE)
            if(APPLE)
                set(APPDATADIR "${DATADIR}/metainfo")
            else()
                set(APPDATADIR "${DATADIR}/share/metainfo")
            endif()
        else()
            set(APPDATADIR "${CMAKE_INSTALL_PREFIX}/share/metainfo")
        endif()
    endif()
endif()

if(APPLE AND NOT APPLE_NEW_BUNDLE)
    set(LENSFUNDBDIR "${LENSFUNDBDIR}")
elseif(DEFINED LENSFUNDBDIR AND NOT IS_ABSOLUTE "${LENSFUNDBDIR}")
    set(LENSFUNDBDIR "${DATADIR}/${LENSFUNDBDIR}")
endif()

if(APPLE)
    if("${CODESIGNID}")
        set(CODESIGNID
            "${CODESIGNID}"
            CACHE STRING "Codesigning Identity")
    endif()
    if("${NOTARY}")
        set(NOTARY
            "${NOTARY}"
            CACHE STRING "Notarization Identity")
    endif()
    if("${LOCAL_PREFIX}")
        set(LOCAL_PREFIX
            "${LOCAL_PREFIX}"
            CACHE STRING "macos/gtk parent directory ie /usr/local or /opt/local or /opt/homebrew (for arm64)")
    else()
        set(LOCAL_PREFIX
            /usr/local
            CACHE STRING "macos/gtk parent directory ie /usr/local (default) or /opt/local (macports) or /opt/homebrew (for arm64)")
    endif()
    if("${FANCY_DMG}")
        set(FANCY_DMG
            ON
            CACHE BOOL
                  "Use the andreyvit/create-dmg script to make a fancy .dmg")
    endif()
endif()

# On macOS: merge the app via a specific url to generate a universal bundle with both x86_64 and arm64
if(OSX_UNIVERSAL)
    if(NOT OSX_UNIVERSAL_URL)
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
            set(OSX_UNIVERSAL_URL "ART_macOS_x86_64_latest.zip" CACHE STRING "URL of x86_64 app for lipo")
        else()
            set(OSX_UNIVERSAL_URL "ART_macOS_arm64_latest.zip" CACHE STRING "URL of arm64 app for lipo")
        endif()
    endif()
endif()

# Enforce absolute paths for non-bundle builds:
if(NOT BUILD_BUNDLE)
    foreach(path BINDIR DATADIR LIBDIR DOCDIR CREDITSDIR LICENCEDIR)
        if(NOT (IS_ABSOLUTE "${${path}}"))
            message(FATAL_ERROR "The ${path} path has to be absolute when using -DBUILD_BUNDLE=OFF")
        endif()
    endforeach()
endif()

# Check for libraries:
find_package(PkgConfig)
if(WIN32)
pkg_check_modules (GTK     REQUIRED gtk+-3.0>=3.22.24)
pkg_check_modules (GTKMM   REQUIRED gtkmm-3.0>=3.22)
else()
pkg_check_modules (GTK     REQUIRED gtk+-3.0>=3.16)
pkg_check_modules (GTKMM   REQUIRED gtkmm-3.0>=3.16)
endif()

if(GTK_VERSION VERSION_GREATER "3.24.1" AND GTK_VERSION VERSION_LESS "3.24.7")
    if(GTK_VERSION VERSION_EQUAL "3.24.5")
        set(CERTAINTY "known to")
    else()
        set(CERTAINTY "likely to")
    endif()
    message(WARNING "\nWarning! You are using GTK+ version " ${GTK_VERSION} " which is " ${CERTAINTY} " have an issue where combobox menu scroll-arrows are missing when a Gtk::ComboBox list does not fit vertically on the screen. As a result, users of your build will not be able to select items in the following comboboxes: Processing Profiles, Film Simulation, and the camera and lens profiles in Profiled Lens Correction.\nIt is recommended that you either downgrade GTK+ to <= 3.24.1 or upgrade to >= 3.24.7.")
endif()

pkg_check_modules (GLIB2   REQUIRED glib-2.0>=2.44)
pkg_check_modules (GLIBMM  REQUIRED glibmm-2.4>=2.44)
pkg_check_modules (CAIROMM REQUIRED cairomm-1.0)
pkg_check_modules (GIO     REQUIRED gio-2.0>=2.44)
pkg_check_modules (GIOMM   REQUIRED giomm-2.4>=2.44)
pkg_check_modules (GTHREAD REQUIRED gthread-2.0>=2.44)
pkg_check_modules (GOBJECT REQUIRED gobject-2.0>=2.44)
pkg_check_modules (SIGC    REQUIRED sigc++-2.0>=2.3.1)
pkg_check_modules (LENSFUN REQUIRED lensfun>=0.2)
pkg_check_modules (RSVG    REQUIRED librsvg-2.0>=2.40)

# Require exiv2 >= 0.24 to make sure everything we need is available
#find_package(Exiv2 0.24 REQUIRED)
pkg_check_modules(EXIV2 REQUIRED exiv2>=0.24)
#include_directories(SYSTEM ${Exiv2_INCLUDE_DIRS})
#list(APPEND LIBS ${EXIV2_LIBRARIES})
add_definitions(${EXIV2_DEFINITIONS})
set(_exiv2_libs ${EXIV2_LIBRARIES})
set(EXIV2_LIBRARIES "")
foreach(l ${_exiv2_libs})
    set(_el "_el-NOTFOUND")
    if(EXIV2_LIBRARY_DIRS)
        message(STATUS "searching for library ${l} in ${EXIV2_LIBRARY_DIRS}")
        find_library(_el ${l} PATHS ${EXIV2_LIBRARY_DIRS} NO_DEFAULT_PATH)
        if(NOT _el)
            message(STATUS  "  NOT FOUND, searching again in default path")
            find_library(_el ${l})
        endif()
    else()
        message(STATUS "searching for library ${l}")
        find_library(_el ${l})# PATHS ${EXIV2_LIBRARY_DIRS})
    endif()
    message(STATUS "  result: ${_el}")
    set(EXIV2_LIBRARIES ${EXIV2_LIBRARIES} ${_el})
endforeach()

if(WIN32)
    add_definitions(-DWIN32)
    add_definitions(-D_WIN32)
    if(MINGW)
        add_definitions(-D__MINGW32__)
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_definitions(-DWINVER=0x0501)
    endif()
    set(EXTRA_LIB "-lws2_32 -lshlwapi")
endif()

pkg_check_modules(LCMS REQUIRED lcms2>=2.6)
pkg_check_modules(EXPAT REQUIRED expat>=2.1)
pkg_check_modules(FFTW3F REQUIRED fftw3f)
pkg_check_modules(TIFF REQUIRED libtiff-4>=4.0.4)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
if(WITH_SYSTEM_KLT)
    find_package(KLT REQUIRED)
endif()

#Set the appropriate FFTW flags on macOS
if(APPLE AND OPTION_OMP)
        set(EXTRA_LIB "-L${LOCAL_PREFIX}/lib -lfftw3f_omp -lfftw3f -lm")
endif()

# Check for libcanberra-gtk3 (sound events on Linux):
if(UNIX AND(NOT APPLE))
    pkg_check_modules(CANBERRA-GTK REQUIRED libcanberra-gtk3)
endif()

if(WITH_LTO)
    # Using LTO with older versions of binutils requires setting extra flags
    set(BINUTILS_VERSION_MININUM "2.29")
    execute_process(COMMAND ar --version OUTPUT_VARIABLE BINUTILS_VERSION_DETECTED)
    string(REGEX REPLACE ".* ([0-9.]+)\n.*" "\\1" BINUTILS_VERSION_DETECTED "${BINUTILS_VERSION_DETECTED}")
    if("${BINUTILS_VERSION_DETECTED}" VERSION_LESS "${BINUTILS_VERSION_MININUM}")
        if(APPLE)
            set(CMAKE_AR "ar")
            set(CMAKE_RANLIB "ranlib")
        else()
            set(CMAKE_AR "/usr/bin/gcc-ar")
            set(CMAKE_RANLIB "/usr/bin/gcc-ranlib")
        endif()
        message(STATUS "Binutils version detected as less than " ${BINUTILS_VERSION_MININUM} " - setting CMake parameters to enable LTO linking:\n    CMAKE_AR=\"" ${CMAKE_AR} "\"\n    CMAKE_RANLIB=\"" ${CMAKE_RANLIB} "\"")
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=auto")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=auto")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=auto")
endif()

if(WITH_SAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=${WITH_SAN}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=${WITH_SAN}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${WITH_SAN}")
endif()

if(WITH_PROF)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wuninitialized -Wno-deprecated-declarations -Wno-unused-result")
if(OPTION_OMP)
    find_package(OpenMP)
    if(OPENMP_FOUND)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS} -Werror=unknown-pragmas")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-vla-extension")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-vla-cxx-extension")
endif()


# check for libfftw3f_omp
include(CheckCSourceCompiles)
if(OPENMP_FOUND)
    set(CMAKE_REQUIRED_INCLUDES ${FFTW3F_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LIBRARIES)
    foreach(l ${FFTW3F_LIBRARIES})
        set(_f "_f-NOTFOUND")
        find_library(_f ${l} PATHS ${FFTW3F_LIBRARY_DIRS})
        set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} ${_f})
    endforeach()
    check_c_source_compiles(
"#include <fftw3.h>
int main()
{
    fftwf_init_threads();
    fftwf_plan_with_nthreads(1);
    return 0;
}" _fftw3f_multithread)
    if(_fftw3f_multithread)
        add_definitions(-DRT_FFTW3F_OMP)
    else()
        find_library(fftw3f_omp fftw3f_omp PATHS ${FFTW3F_LIBRARY_DIRS})
        if(fftw3f_omp)
            add_definitions(-DRT_FFTW3F_OMP)
            set(FFTW3F_LIBRARIES ${FFTW3F_LIBRARIES} ${fftw3f_omp})
        endif()
    endif()
endif()


# check whether the used version of lensfun has lfDatabase::LoadDirectory
set(_lf_libs ${LENSFUN_LIBRARIES})
set(LENSFUN_LIBRARIES "")
foreach(l ${_lf_libs})
    set(_el "_el-NOTFOUND")
    if(LENSFUN_LIBRARY_DIRS)
        message(STATUS "searching for library ${l} in ${LENSFUN_LIBRARY_DIRS}")
        find_library(_el ${l} PATHS ${LENSFUN_LIBRARY_DIRS} NO_DEFAULT_PATH)
    else()
        message(STATUS "searching for library ${l}")
        find_library(_el ${l})# PATHS ${LENSFUN_LIBRARY_DIRS})
    endif()
    message(STATUS "  result: ${_el}")
    set(LENSFUN_LIBRARIES ${LENSFUN_LIBRARIES} ${_el})
endforeach()
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${LENSFUN_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${LENSFUN_LIBRARIES})
# foreach(l ${LENSFUN_LIBRARIES})
#     if(LENSFUN_LIBRARY_DIRS)
#         # the NO_DEFAULT_PATH is to make sure we find the lensfun version we
#         # want, and not the system's one (e.g. if we have a custom version
#         # installed in a non-standard location)
#         set(_l "_l-NOTFOUND")
#         find_library(_l ${l} PATHS ${LENSFUN_LIBRARY_DIRS} NO_DEFAULT_PATH)
#     else()
#         # LENSFUN_LIBRARY_DIRS can be empty if lensfun is installed in the
#         # default path. In this case, adding NO_DEFAULT_PATH would make
#         # find_library fail...
#         set(_l "_l-NOTFOUND")
#         find_library(_l ${l})
#     endif()
#     set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} ${_l})
# endforeach()
check_cxx_source_compiles(
    "#include <lensfun.h>
int main()
{
    lfDatabase *db = 0;
    bool b = db->LoadDirectory(0);
    return 0;
}" LENSFUN_HAS_LOAD_DIRECTORY)


# check whether we can use the fast float plugin of LCMS2
set(ART_LCMS_VERSION_INFO "${LCMS_VERSION}")
set(CMAKE_REQUIRED_INCLUDES ${LCMS_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES)
foreach(l ${LCMS_LIBRARIES})
    set(_f "_f-NOTFOUND")
    find_library(_f ${l} PATHS ${LCMS_LIBRARY_DIRS} NO_DEFAULT_PATH)
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} ${_f})
endforeach()
check_c_source_compiles(
"#include <lcms2.h>
#include <lcms2_fast_float.h>
#if LCMS2_FAST_FLOAT_VERSION < 1500
#  error \"fast float too old\"
#endif

int main()
{
    cmsPlugin(cmsFastFloatExtensions());
    return 0;
}" _lcms2_fast_float)
if(_lcms2_fast_float)
    add_definitions(-DART_LCMS2_FAST_FLOAT)
    message(STATUS "using LCMS2 fast float plugin")
    set(ART_LCMS_VERSION_INFO "${ART_LCMS_VERSION_INFO}-fastfloat")
endif()

set(ART_MIMALLOC_VERSION_INFO "N/A")
if(ENABLE_MIMALLOC AND NOT APPLE)
    set(HAS_MIMALLOC)
    if(NOT WITH_SAN)
        find_package(mimalloc)
        if(mimalloc_FOUND)
            if ("${mimalloc_VERSION}" VERSION_LESS 1.4)
                message(FATAL_ERROR "found mimalloc ${mimalloc_VERSION}, but at least 1.4 is required")
            else()
                message(STATUS "using mimalloc library ${mimalloc_VERSION}")
                set(HAS_MIMALLOC 1)
                add_definitions(-DWITH_MIMALLOC)
                set(ART_MIMALLOC_VERSION_INFO "V${mimalloc_VERSION}")
            endif()
        else()
            message(FATAL_ERROR "ART requires the mimalloc library. Please install it (see https://microsoft.github.io/mimalloc/)")
        endif()
    endif()
endif()

if(ENABLE_LIBRAW)
    pkg_check_modules(LIBRAW libraw_r>=0.21)
    if(LIBRAW_FOUND)
        message(STATUS "using libraw library ${LIBRAW_VERSION}")
        add_definitions(-DART_USE_LIBRAW)
    else()
        message(STATUS "libraw not found")
    endif()
endif()

if(ENABLE_OCIO)
    pkg_check_modules(OCIO OpenColorIO>=2.0.0)
    if(OCIO_FOUND)
        message(STATUS "using OpenColorIO library ${OCIO_VERSION}")
        add_definitions(-DART_USE_OCIO)
    else()
        message(STATUS "OpenColorIO not found")
    endif()
endif()

if(ENABLE_CTL)
    find_path(CTL_INCLUDE_DIR NAMES "CtlInterpeter.h" PATH_SUFFIXES "CTL")
    pkg_check_modules(OPENEXR OpenEXR>=3)
    #find_path(_openexr_half OpenEXR/half.h)
    if(CTL_LIBRARY_DIR)
        find_library(_ctl_lib IlmCtl PATHS "${CTL_LIBRARY_DIR}")
        find_library(_ctl_math_lib IlmCtlMath PATHS "${CTL_LIBRARY_DIR}")
        find_library(_ctl_simd_lib IlmCtlSimd PATHS "${CTL_LIBRARY_DIR}")
    else()
        find_library(_ctl_lib IlmCtl)
        find_library(_ctl_math_lib IlmCtlMath)
        find_library(_ctl_simd_lib IlmCtlSimd)
    endif()
    if(NOT OPENEXR_FOUND)
        pkg_check_modules(OPENEXR IlmBase)
    endif()
    if(NOT (OPENEXR_FOUND AND CTL_INCLUDE_DIR AND _ctl_lib AND _ctl_math_lib AND _ctl_simd_lib))
        message(STATUS "CTL interpreter not found")
    else()
        set(CTL_INCLUDE_DIRS)
        list(APPEND CTL_INCLUDE_DIRS "${CTL_INCLUDE_DIR}" ${OPENEXR_INCLUDE_DIRS})
        message(STATUS "CTL lib found with headers at ${CTL_INCLUDE_DIR}")
        set(CTL_LIBRARIES)
        set(CTL_LIBRARY_DIRS ${OPENEXR_LIBRARY_DIRS})
        list(APPEND CTL_LIBRARIES "${_ctl_simd_lib}" "${_ctl_math_lib}" "${_ctl_lib}")
        list(APPEND CTL_LIBRARIES ${OPENEXR_LIBRARIES})
        add_definitions(-DART_USE_CTL)
        set(CTL_FOUND 1)
        file(STRINGS "${CTL_INCLUDE_DIR}/CtlVersion.h"
            _ctl_version_line REGEX "#define +CURRENT_CTL_VERSION[ \t]+")
        string(REGEX REPLACE "#define +CURRENT_CTL_VERSION[ \t]+" "version "
            _ctl_version_string "${_ctl_version_line}")
        string(STRIP "${_ctl_version_string}" ART_CTL_VERSION_INFO)
        message(STATUS "Found CTL interpreter, ${ART_CTL_VERSION_INFO}")
    endif()
else()
    set(ART_CTL_VERSION_INFO "N/A")
endif()


# Find out whether we are building out of source:
get_filename_component(ABS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(ABS_BINARY_DIR "${CMAKE_BINARY_DIR}" ABSOLUTE)
set(OUT_OF_SOURCE_BUILD TRUE)
if(ABS_SOURCE_DIR STREQUAL ABS_BINARY_DIR)
    set(OUT_OF_SOURCE_BUILD FALSE)
    message(WARNING "You are performing an in-source build. This is discouraged. For an explanation and the advantages of out-of-source builds, please refer to http://www.cmake.org/Wiki/CMake_FAQ#What_is_an_.22out-of-source.22_build.3F")
endif()

# Remove files which could require manual work.
# We will remove this after some time to have a clean build system without file modifications in the source tree again. (?)
set(OOSB_FILES "${PROJECT_SOURCE_DIR}/rtdata/ART.desktop" "${PROJECT_SOURCE_DIR}/rtgui/version.h" "${PROJECT_SOURCE_DIR}/rtgui/config.h" "${PROJECT_SOURCE_DIR}/AboutThisBuild.txt")
if(OUT_OF_SOURCE_BUILD)
    foreach(f ${OOSB_FILES})
        file(REMOVE "${f}")
    endforeach()
endif()

# Check for generated files in the source tree which should not be there when doing an out-of-source build.
# Without checking for this it might happen that old versions are used for the compilation:
if(OUT_OF_SOURCE_BUILD)
    foreach(f ${OOSB_FILES})
        if(EXISTS "${f}")
            message(SEND_ERROR "Generated \"${f}\" found inside the source tree. Please remove it as it is a relic of the old build system and prevents valid compilation now.")
        endif()
    endforeach()
endif()

### Start generating AboutThisBuild.txt
# Set the platform bit-depth:
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PROC_BIT_DEPTH 32 bits)
else()
    set(PROC_BIT_DEPTH 64 bits)
endif()

# Get compiler name and version.
# Only CMake > 2.8.7 knows CMAKE_*_COMPILER_VERSION
if(CMAKE_VERSION VERSION_GREATER 2.8.7)
    get_filename_component(COMPILER_INFO ${CMAKE_C_COMPILER} NAME_WE)
    set(COMPILER_INFO "${COMPILER_INFO} ${CMAKE_C_COMPILER_VERSION}")
else()
    execute_process(COMMAND gcc -dumpversion OUTPUT_VARIABLE GCC_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(COMPILER_INFO "gcc ${GCC_VERSION}")
endif()


# Get C++ and linker flags for rtengine (the GUI's C++ flags may have fewer flags):
set(CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${UPPER_CMAKE_BUILD_TYPE}} ${RTENGINE_CXX_FLAGS}")
set(LFLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS_${UPPER_CMAKE_BUILD_TYPE}}")

set(ABOUT_COMMAND_WITH_ARGS ${CMAKE_COMMAND}
    -DPROJECT_SOURCE_DIR:STRING=${PROJECT_SOURCE_DIR}
    -DCACHE_NAME_SUFFIX:STRING=${CACHE_NAME_SUFFIX}
    -DPROC_LABEL:STRING=${PROC_LABEL}
    -DPROC_BIT_DEPTH:STRING=${PROC_BIT_DEPTH}
    -DBUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DGTKMM_VERSION:STRING=${GTKMM_VERSION}
    -DOPTION_OMP:STRING=${OPTION_OMP}
    -DLENSFUN_VERSION:STRING=${LENSFUN_VERSION}
    -DEXIV2_VERSION:STRING=${EXIV2_VERSION}
    -DLCMS_VERSION_INFO:STRING=${ART_LCMS_VERSION_INFO}
    -DART_MIMALLOC_VERSION_INFO:STRING=${ART_MIMALLOC_VERSION_INFO}
    )

if(WIN32)
    list(APPEND ABOUT_COMMAND_WITH_ARGS -DSYSTEM:STRING=Windows
        -DCXX_FLAGS:STRING="${CXX_FLAGS}"
        -DLFLAGS:STRING="${LFLAGS}"
        -DCOMPILER_INFO:STRING="${COMPILER_INFO}"
        -DCMAKE_INSTALL_PREFIX:STRING="${CMAKE_INSTALL_PREFIX}"
        -DBIT_DEPTH:STRING="${CMAKE_SIZEOF_VOID_P}")
else()
    list(APPEND ABOUT_COMMAND_WITH_ARGS -DSYSTEM:STRING="${CMAKE_SYSTEM_NAME}"
        -DCXX_FLAGS:STRING=${CXX_FLAGS}
        -DLFLAGS:STRING=${LFLAGS}
        -DCOMPILER_INFO:STRING=${COMPILER_INFO} 
        -DBIT_DEPTH:STRING=${CMAKE_SIZEOF_VOID_P})       
endif()

if(ENABLE_LIBRAW AND LIBRAW_FOUND)
    list(APPEND ABOUT_COMMAND_WITH_ARGS
        -DART_LIBRAW_VERSION_INFO:STRING="${LIBRAW_VERSION}")
else()
    list(APPEND ABOUT_COMMAND_WITH_ARGS
        -DART_LIBRAW_VERSION_INFO:STRING="N/A")
endif()

if(ENABLE_OCIO AND OCIO_FOUND)
    list(APPEND ABOUT_COMMAND_WITH_ARGS
        -DART_OCIO_VERSION_INFO:STRING="${OCIO_VERSION}")
else()
    list(APPEND ABOUT_COMMAND_WITH_ARGS
        -DART_OCIO_VERSION_INFO:STRING="N/A")
endif()

if(ENABLE_CTL AND CTL_FOUND)
    list(APPEND ABOUT_COMMAND_WITH_ARGS
        -DART_CTL_VERSION_INFO:STRING=${ART_CTL_VERSION_INFO})
else()
    list(APPEND ABOUT_COMMAND_WITH_ARGS
        -DART_CTL_VERSION_INFO:STRING="N/A")
endif()

list(APPEND ABOUT_COMMAND_WITH_ARGS "-DBUILDINFO_OS:STRING=${CMAKE_HOST_SYSTEM_NAME}" -P "${PROJECT_SOURCE_DIR}/UpdateInfo.cmake")

add_custom_target(UpdateInfo ALL
    COMMAND ${ABOUT_COMMAND_WITH_ARGS}
    COMMENT "Creating AboutThisBuild.txt and other version-dependent files"
    )

### End generating AboutThisBuild.txt

install(FILES AUTHORS.txt DESTINATION "${CREDITSDIR}")
install(FILES LICENSE.txt DESTINATION "${LICENCEDIR}")
install(FILES "${CMAKE_BINARY_DIR}/AboutThisBuild.txt" DESTINATION "${CREDITSDIR}")
install(FILES RELEASE_NOTES.txt DESTINATION "${CREDITSDIR}" OPTIONAL)

# The standard location for man pages in Linux is /usr/share/man
# Use "manpath" to see the search paths for man pages on your system.
if(BUILD_BUNDLE)
    if(APPLE)
        install(FILES "${PROJECT_SOURCE_DIR}/doc/manpage/ART.1"
                DESTINATION "${DATADIR}/man/man1")
    else()
        install(FILES "${PROJECT_SOURCE_DIR}/doc/manpage/ART.1"
                DESTINATION "${DATADIR}/share/man/man1")
    endif()
else()
    install(FILES "${PROJECT_SOURCE_DIR}/doc/manpage/ART.1" DESTINATION "${CMAKE_INSTALL_PREFIX}/share/man/man1")
endif()

if(WIN32)
    install(DIRECTORY "licenses" DESTINATION "${LICENCEDIR}")
endif()

if(WIN32)
    include(CheckCXXSymbolExists)
    check_cxx_symbol_exists(_UCRT "ctime" ART_USES_UCRT)
    if (ART_USES_UCRT)
        add_definitions(-DART_WIN32_UCRT)
    endif()
endif()

if(APPLE)
    include(CheckSymbolExists)
    set(CMAKE_REQUIRED_INCLUDES ${GTK_INCLUDE_DIRS})
    message(STATUS "Checking whether GDK supports specifying custom color spaces")
    check_symbol_exists(GDK_QUARTZ_WINDOW_SUPPORTS_COLORSPACE "gdk/gdkquartz.h" HAVE_GDK_QUARTZ_COLORSPACE)
    if(HAVE_GDK_QUARTZ_COLORSPACE)
        message(STATUS "YES, GDK supports custom color spaces")
        add_definitions(-DART_MACOS_GDK_QUARTZ_COLORSPACE)
    else()
        message(STATUS "NO, GDK will force everything to sRGB")
    endif()
endif()

add_subdirectory(rtengine)
add_subdirectory(rtgui)
add_subdirectory(rtdata)
