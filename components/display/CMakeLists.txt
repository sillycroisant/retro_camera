idf_component_register(
    SRCS "display.c"

    INCLUDE_DIRS "."
    
    REQUIRES 
        esp_lcd
        driver
        freertos
        storage
        esp32-camera
)