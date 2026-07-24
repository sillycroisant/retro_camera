idf_component_register(
    SRCS "handlers.c" "webserver.c"

    INCLUDE_DIRS "."

    REQUIRES 
            "esp_http_server"
)