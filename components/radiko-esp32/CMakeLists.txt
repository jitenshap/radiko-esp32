idf_component_register(SRCS "radiko.c"
                    INCLUDE_DIRS .
                    REQUIRES esp_http_client mbedtls expat log nvs_flash esp-tls
                    EMBED_TXTFILES radiko_jp_root_cert.pem)
