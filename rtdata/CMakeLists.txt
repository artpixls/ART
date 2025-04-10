file(GLOB LANGUAGEFILES "languages/*")
file(GLOB SOUNDFILES "sounds/*")
file(GLOB INPUTICCFILES "iccprofiles/input/*")
file(GLOB OUTPUTICCFILES "iccprofiles/output/*")
file(GLOB DCPFILES "dcpprofiles/*")
file(GLOB FONTS "fonts/*")

set(PROFILESDIR "profiles")
set(THEMEDIR "themes")

set(CTLSCRIPTSDIR "ctlscripts")
set(LUTSDIR "luts")


file(GLOB IMG_SVG LIST_DIRECTORIES false "images/svg/*")
file(GLOB IMG_PNG LIST_DIRECTORIES false "images/png/*")
file(GLOB IMG_ICO LIST_DIRECTORIES false "images/*")

if(WIN32)
    set(OPTIONSFILE "options/options.win")
elseif(APPLE)
    set(OPTIONSFILE "options/options.osx")
else()
    set(OPTIONSFILE "options/options.lin")
endif()

if(WIN32)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/WindowsInnoSetup.iss" DESTINATION ${BINDIR})
endif()

if(APPLE AND NOT APPLE_NEW_BUNDLE)
    set(ICONSDIR "${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/Resources/share/icons")
    set(DATADIR "${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}/Resources/share")
endif()

if(UNIX)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ART.desktop.in" "${CMAKE_CURRENT_BINARY_DIR}/ART.desktop")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ART.desktop" DESTINATION ${DESKTOPDIR})
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/us.pixls.art.ART.metainfo.xml" DESTINATION "${DATADIR}/metainfo")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/images/png/ART-logo-16.png" DESTINATION "${ICONSDIR}/hicolor/16x16/apps" RENAME ART.png)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/images/png/ART-logo-24.png" DESTINATION "${ICONSDIR}/hicolor/24x24/apps" RENAME ART.png)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/images/png/ART-logo-48.png" DESTINATION "${ICONSDIR}/hicolor/48x48/apps" RENAME ART.png)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/images/png/ART-logo-128.png" DESTINATION "${ICONSDIR}/hicolor/128x128/apps" RENAME ART.png)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/images/png/ART-logo-256.png" DESTINATION "${ICONSDIR}/hicolor/256x256/apps" RENAME ART.png)
endif()

install(FILES ${LANGUAGEFILES} DESTINATION "${DATADIR}/languages")
install(FILES ${SOUNDFILES} DESTINATION "${DATADIR}/sounds")
install(FILES ${INPUTICCFILES} DESTINATION "${DATADIR}/iccprofiles/input")
install(FILES ${OUTPUTICCFILES} DESTINATION "${DATADIR}/iccprofiles/output")
install(FILES ${DCPFILES} DESTINATION "${DATADIR}/dcpprofiles")
install(FILES "cammatrices.json" DESTINATION "${DATADIR}")
install(FILES "dcraw.json" DESTINATION "${DATADIR}")
install(FILES "rt.json" DESTINATION "${DATADIR}")
install(FILES "wbpresets.json" DESTINATION "${DATADIR}")
install(FILES ${OPTIONSFILE} DESTINATION "${DATADIR}" PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ RENAME options)
if(WIN32)
    install(FILES ${FONTS} DESTINATION "${DATADIR}/fonts")
endif()
install(FILES "dynamicprofile.cfg" DESTINATION "${DATADIR}")


install(DIRECTORY ${PROFILESDIR} DESTINATION "${DATADIR}" FILES_MATCHING PATTERN "*.arp")
install(DIRECTORY ${THEMEDIR} DESTINATION "${DATADIR}")

install(FILES ${IMG_SVG} DESTINATION "${DATADIR}/images")
install(FILES ${IMG_PNG} DESTINATION "${DATADIR}/images")
install(FILES ${IMG_ICO} DESTINATION "${DATADIR}/images")

install(DIRECTORY ${CTLSCRIPTSDIR} DESTINATION "${DATADIR}" FILES_MATCHING PATTERN "*.ctl")
install(DIRECTORY ${LUTSDIR} DESTINATION "${DATADIR}" FILES_MATCHING REGEX ".*[^~]$")

if(APPLE AND NOT APPLE_NEW_BUNDLE)
    # CMake escapes first item quote character. Do not remove 'DUMMY_VARIABLE='
    set(MACOSX_BUNDLE_COMMAND DUMMY_VARIABLE=
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        GTK_PREFIX="${GTK_PREFIX}")
    if(PROC_BIT_DEPTH MATCHES 32)
        list(APPEND MACOSX_BUNDLE_COMMAND PROC_BIT_DEPTH=32)
    else()
        list(APPEND MACOSX_BUNDLE_COMMAND PROC_BIT_DEPTH=64)
    endif()
    list(APPEND MACOSX_BUNDLE_COMMAND sh "${PROJECT_SOURCE_DIR}/tools/osx/macosx_bundle.sh")

    add_custom_target(macosx_bundle
        COMMAND ${MACOSX_BUNDLE_COMMAND}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Creating macOS bundle")
endif()
