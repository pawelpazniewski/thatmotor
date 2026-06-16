idf_component_register(
    SRCS "src/throttle_chain.c" "src/servo_chain.c" "src/ramp.c" "src/chain_math.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES safety_clamp settings
)
