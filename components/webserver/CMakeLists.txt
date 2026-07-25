idf_component_register(
    SRCS    
        "handlers/handlers.c"
        "handlers/root.c" 
        "handlers/api_status.c"
        "handlers/api_capture.c"
        "handlers/api_gallery.c"
        "handlers/static_files.c"

        "webserver.c"

    INCLUDE_DIRS "."

    REQUIRES 
        "esp_http_server"
        "storage"
)