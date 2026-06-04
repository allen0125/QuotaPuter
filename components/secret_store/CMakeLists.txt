idf_component_register(
    SRCS "secret_store.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_partition)
