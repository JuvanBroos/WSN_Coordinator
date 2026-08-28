idf_component_register(
    SRCS
    "app_main.c"
    "lora/lora_interrupt.c"
    "lora/lora_gw_state_mgr.c"
    "zigbee_task/zigbee_task.c"
    "serial/usb_serial.c"
    "espnow/espnow.c"
    "sd_logger.c"
    "led/led.c"
    INCLUDE_DIRS "." "lora" "zigbee_task" "serial" "espnow" "led"
)
