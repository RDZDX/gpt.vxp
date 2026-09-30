set(TARGET_NAME "main")

set(SOURCES main.c bearssl_impl.c https.cpp certificates.h thread.cpp thread.h libco.cpp)

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
