# cmakefile executed within a makefile target

# If we find ReleaseInfo.cmake we use the info from there and don't need Git to be installed
find_file(REL_INFO_FILE ReleaseInfo.cmake PATHS "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}" NO_DEFAULT_PATH)
if(REL_INFO_FILE STREQUAL REL_INFO_FILE-NOTFOUND)
    if(EXISTS "${PROJECT_SOURCE_DIR}/.git")
        # this is a git repo
        
        # we look for the git command in this paths by order of preference
        if(WIN32)
            find_program(GIT_CMD git.exe HINTS ENV Path PATH_SUFFIXES ../)
        elseif(APPLE)
            find_program(GIT_CMD git PATHS "/opt/local/bin" "/usr/local/bin" "/usr/bin")
            find_program(GIT_CMD git)
            set(SHELL "/bin/bash")
        else() # Linux
            find_program(GIT_CMD git)
            set(SHELL "/bin/bash")
        endif()

        # Fail if Git is not installed
        if(GIT_CMD STREQUAL GIT_CMD-NOTFOUND)
            message(WARNING "git command not found! -- info will be bogus")
        else()
            message(STATUS "git command found: ${GIT_CMD}")
            # Get version description.
            # Depending on whether you checked out a branch (dev) or a tag (release),
            # "git describe" will return "5.0-gtk2-2-g12345678" or "5.0-gtk2", respectively.
            execute_process(COMMAND ${GIT_CMD} describe --tags --always --exact-match OUTPUT_VARIABLE GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

            # Get branch name.
            # Will return empty if you checked out a commit or tag. Empty string handled later.
            execute_process(COMMAND ${GIT_CMD} symbolic-ref --short -q HEAD OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

            # Get commit hash.
            execute_process(COMMAND ${GIT_CMD} rev-parse --short --verify HEAD OUTPUT_VARIABLE GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

            # Get commit date, YYYY-MM-DD.
            execute_process(COMMAND ${GIT_CMD} show -s --format=%cd --date=format:%Y-%m-%d OUTPUT_VARIABLE GIT_COMMIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

            # Get number of commits since tagging. This is what "GIT_DESCRIBE" uses.
            # Works when checking out branch, tag or commit.
            # Get a list of all tags in repo:
            execute_process(COMMAND ${GIT_CMD} tag --merged HEAD OUTPUT_VARIABLE GIT_TAG WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
            # Replace newlines with semicolons so that it can be split:
            string(REPLACE "\n" ";" GIT_TAG_LIST "${GIT_TAG}")
        endif()
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/.hg")
        # a hg-git repo (i.e. Alberto's one :-)

        # we look for the hg command in this paths by order of preference
        if(WIN32)
            find_program(HG_CMD hg.exe HINTS ENV Path PATH_SUFFIXES ../)
        elseif(APPLE)
            find_program(HG_CMD hg PATHS "/opt/local/bin" "/usr/local/bin" "/usr/bin")
            find_program(HG_CMD hg)
            set(SHELL "/bin/bash")
        else() # Linux
            find_program(HG_CMD hg)
            set(SHELL "/bin/bash")
        endif()

        # Fail if Mercurial is not installed
        if(HG_CMD STREQUAL HG_CMD-NOTFOUND)
            message(WARNING "hg command not found! -- info will be bogus")
        else()
            message(STATUS "hg command found: ${HG_CMD}")
        
            # we emulate the behaviour of git with Mercurial
            execute_process(COMMAND ${HG_CMD} log -r .
                #--template "{latesttag('re:.*v?[0-9.]+(rc)?[0-9]+$') % '{sub('^.*/.*:', '', tag)}{ifeq(distance, 0, '', '-')}{ifeq(distance, 0, '', count(revset('ancestors(\".\") and descendants(last(tag(r\"re:^v?[0-9]+[.][0-9.]+(rc[0-9]+)?$\"), 1))'))-1)}{ifeq(distance, 0, '', '-g')}{ifeq(distance, 0, '', short(gitnode))}'}"
                --template "{latesttag('re:.*v?[0-9.]+(rc)?[0-9]+$') % '{ifeq(distance, 0, sub('^.*/.*:', '', tag), '')}'}"
                OUTPUT_VARIABLE GIT_DESCRIBE
                OUTPUT_STRIP_TRAILING_WHITESPACE
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

            execute_process(COMMAND ${HG_CMD} log -r . --template "{activebookmark}"
                OUTPUT_VARIABLE GIT_BRANCH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

            execute_process(COMMAND ${HG_CMD} log -r . --template "{if(gitnode, short(gitnode), short(node))}{if(gitnode, '', '+')}"
                OUTPUT_VARIABLE GIT_COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

            execute_process(COMMAND ${HG_CMD} log -r . --template "{date|shortdate}"
                OUTPUT_VARIABLE GIT_COMMIT_DATE
                OUTPUT_STRIP_TRAILING_WHITESPACE
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        endif()
    else()
        message(WARNING "not inside a repository -- info will be bogus!")
    endif()

    # If user checked-out something which is not a branch, use the description as branch name.
    if(NOT GIT_COMMIT)
        set(GIT_COMMIT "unknown")
    endif()
    if(NOT GIT_DESCRIBE)
        set(GIT_DESCRIBE "${GIT_COMMIT}")
    endif()
    if(NOT GIT_BRANCH)
        set(GIT_BRANCH "${GIT_DESCRIBE}")
    endif()

    string(TIMESTAMP BUILDINFO_DATE UTC)

    message(STATUS "Git checkout information:")
    message(STATUS "    Commit description: ${GIT_DESCRIBE}")
    message(STATUS "    Branch: ${GIT_BRANCH}")
    message(STATUS "    Commit: ${GIT_COMMIT}")
    message(STATUS "    Commit date: ${GIT_COMMIT_DATE}")
    message(STATUS "Build information:")
    message(STATUS "    Build OS: ${BUILDINFO_OS}")
    message(STATUS "    Build date: ${BUILDINFO_DATE}")

    if(NOT DEFINED CACHE_NAME_SUFFIX)
        set(CACHE_NAME_SUFFIX "${GIT_DESCRIBE}")
        message(STATUS "CACHE_NAME_SUFFIX was not defined, it is now \"${CACHE_NAME_SUFFIX}\"")
    else()
        message(STATUS "CACHE_NAME_SUFFIX is \"${CACHE_NAME_SUFFIX}\"")
    endif()

    file(WRITE
        "${CMAKE_BINARY_DIR}/ReleaseInfo.cmake.in"
        "set(GIT_DESCRIBE \"${GIT_DESCRIBE}\")
set(GIT_BRANCH \"${GIT_BRANCH}\")
set(GIT_COMMIT \"${GIT_COMMIT}\")
set(GIT_COMMIT_DATE \"${GIT_COMMIT_DATE}\")
")
else()
    include(${REL_INFO_FILE})
endif()

if(WIN32)
    if(BIT_DEPTH EQUAL 4)
        set(BUILD_BIT_DEPTH 32)
        set(ARCHITECTURE_ALLOWED "x86 x64")
        set(INSTALL_MODE "")
    elseif(BIT_DEPTH EQUAL 8)
        set(BUILD_BIT_DEPTH 64)
        set(ARCHITECTURE_ALLOWED "x64 arm64")
        set(INSTALL_MODE "x64 arm64")
    endif(BIT_DEPTH EQUAL 4)
    # set part of the output archive name
    set(SYSTEM_NAME "WinVista")

    configure_file("${PROJECT_SOURCE_DIR}/tools/win/InnoSetup/WindowsInnoSetup.iss.in" "${CMAKE_BINARY_DIR}/rtdata/WindowsInnoSetup.iss")
endif(WIN32)

# build version.h from template
configure_file("${PROJECT_SOURCE_DIR}/rtgui/version.h.in" "${CMAKE_BINARY_DIR}/rtgui/version.h")
# build AboutThisBuild.txt from template
configure_file("${PROJECT_SOURCE_DIR}/AboutThisBuild.txt.in" "${CMAKE_BINARY_DIR}/AboutThisBuild.txt")

if(APPLE)
    configure_file("${PROJECT_SOURCE_DIR}/tools/osx/Info.plist.in" "${CMAKE_BINARY_DIR}/Info.plist")    
endif()
