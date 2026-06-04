idf_component_register(
    SRCS "provisioner.c"
    INCLUDE_DIRS "include"
    REQUIRES provider_core providers secret_store cache wifi_manager json driver)
