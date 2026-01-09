cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
set (PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set (WEB_ROOT "${PROJ_ROOT}/web_root")
set (BUNDLE_JS "${WEB_ROOT}/bundle.js")
set (PACKFS "${PROJ_ROOT}/main/pack_fs.c")
set (PACKJS "${PROJ_ROOT}/tool/pack.js")
set (URI "https://npm.reversehttp.com/preact,preact/hooks,htm/preact,preact-router")
list (APPEND DEP_SRCS
    ${BUNDLE_JS}
    "${WEB_ROOT}/main.js"
    "${WEB_ROOT}/components.js"
    "${WEB_ROOT}/index.html"
    "${WEB_ROOT}/tailwind.css"
    "${PROJ_ROOT}/certs/server_key.pem"
    "${PROJ_ROOT}/certs/server_cert.pem"
)
list (APPEND PACK_SRCS
    "web_root/bundle.js::gzip"
    "web_root/components.js::gzip"
    "web_root/history.min.js::gzip"
    "web_root/index.html::gzip"
    "web_root/main.js::gzip"
    "web_root/tailwind.css::gzip"
    "certs/*"
)
# Bundle JS libraries (preact, preact-router, ...) into a single file
add_custom_command(
    OUTPUT ${BUNDLE_JS}
    COMMAND curl -s ${URI} -o ${BUNDLE_JS}
    WORKING_DIRECTORY ${PROJ_ROOT}
    COMMENT "Bundle JS libraries (preact, preact-router, ...) into a single file"
)
# Generate packed filesystem for serving Web UI
add_custom_command(
    OUTPUT ${PACKFS}
    COMMAND node ${PACKJS} ${PACK_SRCS} > ${PACKFS}
    DEPENDS ${DEP_SRCS}
    WORKING_DIRECTORY ${PROJ_ROOT}
    COMMENT "Generate packed filesystem for serving Web UI"
)
add_custom_target(packfs
    DEPENDS ${PACKFS}
)