idf_component_register(
    SRCS "main.c"
    
    INCLUDE_DIRS "."
    
    REQUIRES
        camera
        storage
        webserver
        network
        frontend
        events
        input
        mode
)
