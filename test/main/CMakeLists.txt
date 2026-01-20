idf_component_register(
    SRCS "main.c" "test_wwvb_encoder.c" "test_dst_calc.c" 
         "../../main/wwvb_encoder.c" "../../main/dst_calc.c"
    INCLUDE_DIRS "." "../../main"
    REQUIRES unity esp_timer
)
