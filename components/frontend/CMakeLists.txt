idf_component_register(

    SRCS
        "frontend.c"

    INCLUDE_DIRS
        "."

    EMBED_TXTFILES
        "assets/index.html"

    REQUIRES
        storage
)