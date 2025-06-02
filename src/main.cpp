#include <Arduino.h>
#include "WiFi.h"
#include "ESPmDNS.h"
#include "esp_http_server.h"
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

const char* ssid = "ShuWlan-0X";
const char* password = "20010708";


void camera_init()
{
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
    if (psramFound())
    {
        config.frame_size = FRAMESIZE_UXGA; // (1600x1200)
        config.jpeg_quality = 10; // 0-63，越低质量越高
        config.fb_count = 2; // 帧缓冲区的数量 (使用 PSRAM 时可以为2或更多)
        config.fb_location = CAMERA_FB_IN_PSRAM; // 强制在PSRAM中分配帧缓冲区
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA; // (800x600)
        config.jpeg_quality = 12;
        config.fb_count = 1; // 没有 PSRAM 时，帧缓冲区在内部SRAM，只能为1
        config.fb_location = CAMERA_FB_IN_DRAM;
    }
    CHECK_ERROR(esp_camera_init(&config), "Camera init");
}

void wifi_init()
{
    Serial.print("[INFO]:WiFi init start");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.print("\r\n[INFO]:Camera Stream Ready! Go to: http://");
    Serial.println(WiFi.localIP());

    // (可选) 启动 mDNS 服务，方便通过 camera-stream.local 访问
    if (!MDNS.begin("camera-stream"))
    {
        // Set the hostname
        Serial.println("[ERROR]:Error setting up MDNS responder!");
        while (1)
        {
            delay(1000);
        }
    }
    Serial.println("[INFO]:MDNS responder started!");
    MDNS.addService("http", "tcp", 80);
}

httpd_handle_t stream_httpd = NULL;

// HTTP GET 请求处理函数，用于发送视频流
esp_err_t stream_handler(httpd_req_t* req)
{
    camera_fb_t* fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t* _jpg_buf = NULL;
    char* part_buf[64];

    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=--FRAME");
    if (res != ESP_OK)
    {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); // 允许跨域访问

    while (true)
    {
        fb = esp_camera_fb_get(); // 获取一帧图像
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->format != PIXFORMAT_JPEG)
            {
                // 确保是JPEG格式
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len); // 如果不是JPEG，尝试转换
                esp_camera_fb_return(fb); // 释放原始帧缓冲区
                fb = NULL;
                if (!jpeg_converted)
                {
                    Serial.println("JPEG compression failed");
                    res = ESP_FAIL;
                }
            }
            else
            {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char*)"\r\n--FRAME\r\n", strlen("\r\n--FRAME\r\n"));
        }
        if (res == ESP_OK)
        {
            // 发送JPEG头部
            sprintf((char*)part_buf, "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char*)part_buf, strlen((char*)part_buf));
        }
        if (res == ESP_OK)
        {
            // 发送JPEG数据
            res = httpd_resp_send_chunk(req, (const char*)_jpg_buf, _jpg_buf_len);
        }
        if (fb)
        {
            esp_camera_fb_return(fb); // 释放帧缓冲区
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf); // 如果是转换后的JPEG，需要释放
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            break; // 发送失败则退出循环
        }
        // 加一个小延时，避免过于频繁地发送导致客户端处理不过来或网络拥堵
        // delay(10); // 可根据实际效果调整
    }
    return res;
}

void startCameraServer()
{
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.server_port = 80; // HTTP 端口
    // httpd_config.ctrl_port = 32768; // 控制端口，通常不需要修改

    httpd_uri_t stream_uri = {
        .uri = "/stream", // 视频流的URL路径
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&stream_httpd, &httpd_config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}


void setup()
{
    Serial.begin(115200);
    camera_init();
    wifi_init();

    startCameraServer();
}

void loop()
{
    // Serial.println("Taking a photo...");
    // camera_fb_t* fb = esp_camera_fb_get(); // 获取帧缓冲区
    // if (!fb)
    // {
    //     Serial.println("Camera capture failed");
    //     delay(1000);
    //     return;
    // }
    //
    // Serial.printf("Photo captured! Size: %zu bytes, Width: %d, Height: %d, Format: %d\n",
    //               fb->len, fb->width, fb->height, fb->format);
    //
    // esp_camera_fb_return(fb); // 释放帧缓冲区，非常重要！
    //
    // Serial.println("Photo processed. Waiting for 5 seconds...");
    delay(1000);
}
