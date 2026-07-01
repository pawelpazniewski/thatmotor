# Spot-lock blackbox: pure core (record codec + ring index + session sampler)
# plus the flash HAL (esp_partition on the `spotlog` partition) and the
# background recorder task. The pure sources are also host-tested standalone via
# test/host; here they enter the IDF build alongside the HAL/task adapters.
idf_component_register(
    SRCS "src/blackbox_record.c"
         "src/blackbox_ring.c"
         "src/blackbox_sampler.c"
         "src/blackbox_resume.c"
         "src/blackbox.c"
         "src/blackbox_recorder.c"
         "src/blackbox_csv.c"
         "src/params_cmd.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_partition spi_flash control_loop settings state_machine freertos esp_timer
)
