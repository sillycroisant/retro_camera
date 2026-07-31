idf_component_register(
    SRCS
        "input.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        driver
        esp_driver_gpio
        events
        mode
        freertos
)