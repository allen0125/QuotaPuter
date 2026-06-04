idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES m5unified provider_core secret_store cache http_client wifi_manager nvs_flash)
