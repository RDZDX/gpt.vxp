cmake_minimum_required (VERSION 3.8)

set(TARGET_NAME "main")

set(SOURCES "main.cpp" "main.h" "gpt_ui.cpp" "gpt_ui.h" "bearssl_impl.c" "chatgpt.h" "cJSON.h" "Console_io.cpp" "libco.cpp" "thread.cpp" "CircleBuf.cpp" "Console_io.h" "libco.h" "thread.h" "CircleBuf.h" "chatgpt.cpp" "cJSON.c" "https.cpp")

add_exec_vxp(${TARGET_NAME}
    SRCS ${SOURCES}
    RESOURSES resourses
    APP_NAME "${APP_NAME}"
    APPID "${APPID}"
    BACKGROUND ${BACKGROUND}
    API "${API}"
    RAM "${RAM}"
)

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(${TARGET_NAME} mreapi)
