idf_component_register(
    SRCS "vendor/sh2.c" "vendor/shtp.c" "vendor/sh2_SensorValue.c"
         "vendor/sh2_util.c"
    INCLUDE_DIRS "vendor"
)

# Vendored CEVA SH-2 driver (Apache-2.0, github.com/ceva-dsp/sh2). It is
# third-party code, so the project's warning policy must not apply to it.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
