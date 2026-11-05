# Dependencies: JUCE 8 and the Ableton Link SDK.
# Point ELOOPER_JUCE_DIR / ELOOPER_LINK_DIR at local checkouts to skip downloading.
include(FetchContent)

set(ELOOPER_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
set(ELOOPER_LINK_DIR "" CACHE PATH "Path to a local Ableton Link checkout (optional)")

if(ELOOPER_JUCE_DIR)
    add_subdirectory(${ELOOPER_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                              GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

if(ELOOPER_LINK_DIR)
    set(_link_dir ${ELOOPER_LINK_DIR})
else()
    # Pinned commit; SOURCE_SUBDIR points nowhere so Link's own CMakeLists isn't added
    # (we only need AbletonLinkConfig.cmake).
    FetchContent_Declare(abletonlink GIT_REPOSITORY https://github.com/Ableton/link.git
                                     GIT_TAG 5ba907ae027a8c2ec8824463e37296dcefc5e9be
                                     GIT_SUBMODULES modules/asio-standalone
                                     SOURCE_SUBDIR _none_)
    FetchContent_MakeAvailable(abletonlink)
    set(_link_dir ${abletonlink_SOURCE_DIR})
endif()
include(${_link_dir}/AbletonLinkConfig.cmake)
