#include <Arduino.h>

// #include <FS.h>
// #include <SD_MMC.h>



//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
//#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
//#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3 // Has PSRAM
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3 // Has PSRAM
#include "camera_pins.h"

#include "esp_camera.h"

#include "esp_err.h"

#define CHECK_ERROR(func_call, error_message) \
    do { \
        esp_err_t err_code = (func_call); \
        if (err_code != ESP_OK) { \
            Serial.printf("[ERROR]:%s,0x%x (%s)\n", \
                          (error_message), \
                          err_code, \
                          esp_err_to_name(err_code)); \
            Serial.println("Restarting ESP..."); \
            ESP.restart(); \
            return; /* 注意：这将从当前函数返回 */ \
        } \
    } while (0)

void camera_init(){
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; // 摄像头时钟频率 (20MHz 常见, OV2640 支持 10-48MHz)
  config.pixel_format = PIXFORMAT_JPEG; // 输出格式 JPEG, YUV422, GRAYSCALE, RGB565

  // PSRAM 相关配置
  // ESP32-S3 有足够的内部 SRAM 处理较小的分辨率和 JPEG，但高质量图像需要 PSRAM
  // 对于高分辨率 (例如 UXGA 1600x1200), PIXFORMAT_JPEG 是必须的,并且可能需要更大的 fb_count
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA; // (1600x1200)
    config.jpeg_quality = 10;           // 0-63，越低质量越高
    config.fb_count = 2;                // 帧缓冲区的数量 (使用 PSRAM 时可以为2或更多)
    config.fb_location = CAMERA_FB_IN_PSRAM; // 强制在PSRAM中分配帧缓冲区
  } else {
    config.frame_size = FRAMESIZE_SVGA; // (800x600)
    config.jpeg_quality = 12;
    config.fb_count = 1;                // 没有 PSRAM 时，帧缓冲区在内部SRAM，只能为1
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  CHECK_ERROR(esp_camera_init(&config), "Camera init");

}



void setup() {
  Serial.begin(115200);
  camera_init();


}

void loop() {
  Serial.println("Taking a photo...");
  camera_fb_t *fb = esp_camera_fb_get(); // 获取帧缓冲区
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    return;
  }

  Serial.printf("Photo captured! Size: %zu bytes, Width: %d, Height: %d, Format: %d\n",
                fb->len, fb->width, fb->height, fb->format);

  esp_camera_fb_return(fb); // 释放帧缓冲区，非常重要！

  Serial.println("Photo processed. Waiting for 5 seconds...");
  delay(5000);
}
