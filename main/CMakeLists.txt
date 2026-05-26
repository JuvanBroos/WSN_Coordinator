idf_component_register(
    SRCS
    "app_main.c"
    "lora/lora_manager.c"
    "zigbee_task/zigbee_task.c"
    "serial/usb_serial.c"
    "espnow/espnow.c"
    INCLUDE_DIRS "." "lora" "zigbee_task" "serial" "espnow"
)
