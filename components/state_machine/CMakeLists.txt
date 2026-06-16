idf_component_register(
    SRCS "src/state_machine.c" "src/esc_calibration.c"
    INCLUDE_DIRS "include"
    REQUIRES signal_chain
)
