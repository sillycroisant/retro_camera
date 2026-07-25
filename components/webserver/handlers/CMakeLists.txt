idf_component_register(

    SRCS
        "webserver.c"
        
        # "storage.c"

        "handlers/handlers.c"
        "handlers/root.c"
        "handlers/api_status.c"
        "handlers/api_capture.c"
        "handlers/api_gallery.c"
        "handlers/static_files.c"


    INCLUDE_DIRS
        "."
        "handlers"

    REQUIRES
        esp_http_server
        storage
)