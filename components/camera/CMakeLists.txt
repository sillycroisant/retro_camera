idf_component_register(
    SRCS "camera.c"

    INCLUDE_DIRS "."

    REQUIRES 
        esp32-camera
        esp_driver_gpio
        freertos
        events
        mode
        storage
)