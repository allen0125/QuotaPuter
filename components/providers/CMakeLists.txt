idf_component_register(
    SRCS "provider_util.c" "minimax.c" "providers.c"
    INCLUDE_DIRS "include"
    REQUIRES provider_core http_client secret_store json)
