FILE(GLOB cfiles *.c)
FILE(GLOB mongoose_files ../mongoose/*.c)
FILE(GLOB driver_files ../driver/*.c)
idf_component_register(SRCS ${cfiles} ${mongoose_files} ${driver_files}
                       INCLUDE_DIRS "." "../mongoose" "../driver")
