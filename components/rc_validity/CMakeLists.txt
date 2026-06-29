idf_component_register(
    SRCS "src/rc_validity.c" "src/switch_debounce.c" "src/click_counter.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_capture
)
