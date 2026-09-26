idf_component_register(
    SRCS "vfd_renderer.c" "profiles/boe_vfm041ssbr1_s1.c"
    INCLUDE_DIRS "include" "profiles"
    REQUIRES esp_common
    PRIV_REQUIRES pt6312)
