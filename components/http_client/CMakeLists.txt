idf_component_register(
    SRCS "http_client.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client provider_core
    PRIV_REQUIRES esp-tls mbedtls)
