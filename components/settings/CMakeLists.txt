idf_component_register(
    SRCS "src/settings_defaults.c" "src/settings_validate.c"
         "src/blob_codec.c" "src/commit_debounce.c" "src/nvs_provenance.c"
         "src/nvs_store.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES nvs_flash
)
