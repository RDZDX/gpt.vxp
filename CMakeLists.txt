cmake_minimum_required (VERSION 3.10)

project ("gpt.vxp")

set(APP_NAME "gpt.vxp")
set(DEVELOPER_NAME "gvl610 & Ximik_Boda")
set(RAM "1500") #Ram requirement (in kb)
set(IMSI "1234567890" CACHE STRING "Imsi of first SIM-card")
set(API "TCP Sensor BitStream Contact MMS SMSMng File SysStorage SysFile") #Api for app, 
set(APPID "-1")
set(CERTID "1" CACHE STRING "")
set(CERT "none" CACHE STRING "")

option(BACKGROUND "App can work background" OFF)

set(MRE_SDK "$ENV{MRE_SDK}" CACHE STRING "MRE SDK dir")
set(TinyMRESDK "$ENV{TinyMRESDK}" CACHE STRING "TinyMRESDK dir")

if(BACKGROUND)
    add_definitions(-DSUPPORT_BG)
endif()

include(cmake/MreUtils.cmake)

add_subdirectory(resourses)
#add_subdirectory(core)
add_subdirectory(mreapi)
#add_subdirectory(vxp)
add_subdirectory(run)

add_subdirectory(main)

#Full list: Audio Camera Call TCP File HTTP Sensor SIM card Record SMS(person) SMS(SP) BitStream Contact LBS MMS ProMng SMSMng Video XML Sec SysStorage Payment BT PUSH UDP SysFile
