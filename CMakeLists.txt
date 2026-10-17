cmake_minimum_required(VERSION 3.16)

# BabyTalk lives next to the project as a plain clone (scripts/fetch_deps.sh).
set(BABYTALK_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/babytalk")
set(EXTRA_COMPONENT_DIRS "${BABYTALK_DIR}/components")

# sanoTTS is fetched at a pinned commit and massaged for this firmware.
# prepare_sanotts.py is the portable version of BabyTalk's prepare.sh.
execute_process(
    COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/scripts/prepare_sanotts.py"
            "${CMAKE_CURRENT_LIST_DIR}/third_party/sanoTTS"
            "${CMAKE_CURRENT_LIST_DIR}/third_party/sanotts_prepared"
    RESULT_VARIABLE SANOTTS_RC)
if(SANOTTS_RC EQUAL 0)
    set(SANOTTS_SRC "${CMAKE_CURRENT_LIST_DIR}/third_party/sanotts_prepared")
elseif(SANOTTS_RC EQUAL 1)
    message(STATUS "mojitalk: no sanoTTS checkout, building without speech output")
else()
    message(FATAL_ERROR "mojitalk: sanoTTS preparation failed")
endif()

# The runtime's 104 KB of static scratch in PSRAM; the 84 KB synthesis arena
# stays in internal RAM, shared with STT.
set(SANOTTS_BSS_PSRAM 1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ziggy)
