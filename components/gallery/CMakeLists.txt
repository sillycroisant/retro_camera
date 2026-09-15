idf_component_register(
    SRCS "gallery.c"

    INCLUDE_DIRS "."
    
    REQUIRES 
        freertos
        events
        mode
        storage
        display
)