idf_component_register(
    SRCS "http_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server wifi_manager
    EMBED_TXTFILES
        "web/index.html"
        "web/style.css"
        "web/app.js"
)
