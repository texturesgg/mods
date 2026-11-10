# Builds this mod against tgg-melee's SDK:
#   cmake -S . -B build -G Ninja -DTGG_SDK=$(tgg sdk path)
#   cmake --build build
# The mod lands in build/mods/<id>/.
cmake_minimum_required(VERSION 3.25)
project(tgg.replay-browser LANGUAGES C)

set(TGG_SDK "" CACHE PATH "tgg-melee's mod SDK (tgg sdk path)")
if(NOT TGG_SDK)
    message(FATAL_ERROR "Set TGG_SDK to tgg-melee's SDK: -DTGG_SDK=$(tgg sdk path)")
endif()
include("${TGG_SDK}/TggMod.cmake")
tgg_use_sdk("${TGG_SDK}")
tgg_add_mod(tgg.replay-browser)
