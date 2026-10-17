idf_component_register(SRCS "vendor/quirc.c" "vendor/identify.c" "vendor/decode.c" "vendor/version_db.c" "quirc_alloc.c"
    INCLUDE_DIRS "vendor" PRIV_REQUIRES heap)
set_source_files_properties("vendor/quirc.c" PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/quirc_alloc.h")
