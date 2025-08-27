#include <driver/i2s.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>      // 添加mDNS支持
#include <ArduinoJson.h>  // 添加JSON支持

// WiFi配置存储
Preferences preferences;

// I2S配置
#define I2S_WS 25
#define I2S_SD 32
#define I2S_SCK 33
#define I2S_PORT I2S_NUM_0
#define I2S_SAMPLE_RATE (16000)
#define I2S_SAMPLE_BITS (32)
#define bufferLen 256

int32_t sBuffer[bufferLen];
int16_t audioBuffer[bufferLen];

// WiFi AP配置
const char* AP_SSID = "ESP32-Audio-Config";
const char* AP_PASS = "12345678";

// 设备信息
const char* DEVICE_NAME = "ESP32-Audio";
const char* SERVICE_NAME = "esp32-audio";

// Web服务器和DNS
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// 音频流服务器
WiFiServer audioServer(8888);  // 音频数据端口
WiFiClient audioClient;
bool streaming = false;
bool recording_active = false;

// 状态服务器（用于客户端发现设备）
WiFiServer statusServer(8889);  // 状态查询端口

// UDP广播
WiFiUDP udp;
const int UDP_PORT = 8890;
unsigned long lastBroadcast = 0;
const unsigned long BROADCAST_INTERVAL = 2000;  // 每2秒广播一次

// 运行模式
enum Mode {
  MODE_AP,
  MODE_STATION
};
Mode currentMode = MODE_AP;

// HTML配置页面（保持原样）
const char* configPage = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32音频设备配置</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 400px; margin: 50px auto; padding: 20px; background: #f0f0f0; }
        .container { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        input[type="text"], input[type="password"] { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }
        button { width: 100%; padding: 12px; margin: 10px 0; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }
        button:hover { background: #45a049; }
        .status { margin: 10px 0; padding: 10px; border-radius: 5px; text-align: center; }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
        .info { margin-top: 20px; padding: 15px; background: #e3f2fd; border-radius: 5px; font-size: 14px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32 音频设备</h1>
        <form action="/config" method="POST">
            <label>WiFi网络名称 (SSID):</label>
            <input type="text" name="ssid" required>
            
            <label>WiFi密码:</label>
            <input type="password" name="password">
            
            <button type="submit">连接WiFi</button>
        </form>
        
        <form action="/reset" method="POST">
            <button type="submit" style="background: #f44336;">重置设备</button>
        </form>
        
        <div class="info">
            <strong>使用说明：</strong><br>
            1. 输入您的WiFi网络信息<br>
            2. 点击"连接WiFi"<br>
            3. 设备将自动重启并连接到您的网络<br>
            4. 连接成功后，音频流端口：8888
        </div>
    </div>
</body>
</html>
)";

const char* successPage = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>配置成功</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; padding: 50px; }
        .success-box { max-width: 400px; margin: 0 auto; padding: 30px; background: #d4edda; border-radius: 10px; color: #155724; }
    </style>
</head>
<body>
    <div class="success-box">
        <h1>✓ 配置成功！</h1>
        <p>设备将在5秒后重启并连接到您的WiFi网络</p>
        <p>请记住设备IP地址用于音频流连接</p>
    </div>
</body>
</html>
)";

void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_set_pin(I2S_PORT, &pin_config);
}

// 处理根路径
void handleRoot() {
  server.send(200, "text/html", configPage);
}

// 处理录音开始命令
void handleStartRecording() {
  if (audioClient && audioClient.connected()) {
    recording_active = true;
    Serial.println("开始录音");
    server.send(200, "text/plain", "Recording started");
  } else {
    server.send(400, "text/plain", "No client connected");
  }
}

// 处理录音停止命令
void handleStopRecording() {
  recording_active = false;
  Serial.println("停止录音");
  server.send(200, "text/plain", "Recording stopped");
}

// 处理状态API
void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["device"] = DEVICE_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["streaming"] = streaming;
  doc["recording"] = recording_active;
  doc["connected"] = audioClient.connected();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// 处理WiFi配置
void handleConfig() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.length() > 0) {
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putBool("configured", true);

    Serial.println("保存WiFi配置:");
    Serial.println("SSID: " + ssid);

    server.send(200, "text/html", successPage);
    delay(5000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "SSID不能为空");
  }
}

// 处理重置
void handleReset() {
  preferences.clear();
  server.send(200, "text/html", "<html><body><h1>设备已重置，5秒后重启...</h1></body></html>");
  delay(5000);
  ESP.restart();
}

// 处理未找到
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// 设置AP模式
void setupAP() {
  Serial.println("启动AP模式...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP地址: ");
  Serial.println(IP);

  dnsServer.start(DNS_PORT, "*", IP);

  server.on("/", handleRoot);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web服务器已启动");
  Serial.printf("请连接WiFi: %s\n", AP_SSID);
  Serial.printf("密码: %s\n", AP_PASS);
  Serial.println("然后访问: http://192.168.4.1");
}

// 连接到WiFi
bool connectWiFi(String ssid, String password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.print("连接到WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi连接成功!");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi连接失败!");
    return false;
  }
}

// 设置mDNS服务
void setupMDNS() {
  if (MDNS.begin(SERVICE_NAME)) {
    Serial.println("mDNS服务已启动");
    // 添加服务
    MDNS.addService("esp32-audio", "tcp", 8888);
    MDNS.addService("http", "tcp", 80);

    // 添加TXT记录
    MDNS.addServiceTxt("esp32-audio", "tcp", "version", "1.0");
    MDNS.addServiceTxt("esp32-audio", "tcp", "device", DEVICE_NAME);
  } else {
    Serial.println("mDNS启动失败!");
  }
}

// 发送UDP广播
void sendUDPBroadcast() {
  if (millis() - lastBroadcast > BROADCAST_INTERVAL) {
    lastBroadcast = millis();

    StaticJsonDocument<256> doc;
    doc["type"] = "esp32-audio-device";
    doc["name"] = DEVICE_NAME;
    doc["ip"] = WiFi.localIP().toString();
    doc["port"] = 8888;
    doc["status_port"] = 8889;
    doc["streaming"] = streaming;

    String message;
    serializeJson(doc, message);

    IPAddress broadcastIP = WiFi.localIP();
    broadcastIP[3] = 255;  // 设置为广播地址

    udp.beginPacket(broadcastIP, UDP_PORT);
    udp.print(message);
    udp.endPacket();

    Serial.println("发送UDP广播: " + message);
  }
}

// 处理状态查询
void handleStatusClients() {
  WiFiClient statusClient = statusServer.available();
  if (statusClient) {
    Serial.println("状态查询客户端已连接");

    StaticJsonDocument<256> doc;
    doc["device"] = DEVICE_NAME;
    doc["ip"] = WiFi.localIP().toString();
    doc["audio_port"] = 8888;
    doc["streaming"] = streaming;
    doc["sample_rate"] = I2S_SAMPLE_RATE;
    doc["bits_per_sample"] = 16;

    String response;
    serializeJson(doc, response);

    statusClient.println("HTTP/1.1 200 OK");
    statusClient.println("Content-Type: application/json");
    statusClient.println("Connection: close");
    statusClient.println();
    statusClient.println(response);

    delay(10);
    statusClient.stop();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP32 音频流设备 v2.0");

  preferences.begin("wifi", false);

  bool configured = preferences.getBool("configured", false);

  if (configured) {
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");

    if (ssid.length() > 0) {
      Serial.println("找到保存的WiFi配置，尝试连接...");

      if (connectWiFi(ssid, password)) {
        currentMode = MODE_STATION;

        // 设置mDNS
        setupMDNS();

        // 初始化I2S
        Serial.println("初始化I2S...");
        i2s_install();
        i2s_setpin();
        i2s_start(I2S_PORT);

        // 启动音频服务器
        audioServer.begin();
        Serial.println("音频服务器启动在端口 8888");

        // 启动状态服务器
        statusServer.begin();
        Serial.println("状态服务器启动在端口 8889");

        // 初始化UDP
        udp.begin(UDP_PORT);
        Serial.println("UDP广播启动在端口 8890");

        // 设置Web状态页面
        server.on("/", []() {
          String html = "<html><body>";
          html += "<h1>ESP32音频设备</h1>";
          html += "<p>设备名称: " + String(DEVICE_NAME) + "</p>";
          html += "<p>IP地址: " + WiFi.localIP().toString() + "</p>";
          html += "<p>音频端口: 8888</p>";
          html += "<p>状态: " + String(streaming ? "已连接" : "空闲") + "</p>";
          html += "<p>录音: " + String(recording_active ? "录音中" : "未录音") + "</p>";
          html += "<p>客户端: " + String(audioClient.connected() ? "已连接" : "未连接") + "</p>";
          html += "</body></html>";
          server.send(200, "text/html", html);
        });
        server.on("/status", handleStatus);
        server.on("/start_recording", HTTP_POST, handleStartRecording);
        server.on("/stop_recording", HTTP_POST, handleStopRecording);
        server.begin();

        Serial.println("系统就绪!");
        Serial.println("=================================");
        Serial.println("设备信息:");
        Serial.println("IP地址: " + WiFi.localIP().toString());
        Serial.println("mDNS名称: " + String(SERVICE_NAME) + ".local");
        Serial.println("音频端口: 8888");
        Serial.println("状态端口: 8889");
        Serial.println("UDP广播端口: 8890");
        Serial.println("=================================");
      } else {
        Serial.println("连接失败，进入配置模式");
        preferences.clear();
        setupAP();
        currentMode = MODE_AP;
      }
    }
  } else {
    Serial.println("未找到WiFi配置，进入配置模式");
    setupAP();
    currentMode = MODE_AP;
  }
}

void loop() {
  if (currentMode == MODE_AP) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else if (currentMode == MODE_STATION) {
    // 处理Web请求
    server.handleClient();

    // mDNS不需要update（ESP32版本）
    // MDNS.update(); // 这行在ESP32中不需要

    // 发送UDP广播
    sendUDPBroadcast();

    // 处理状态查询
    handleStatusClients();

    // 处理音频客户端连接
    if (audioServer.hasClient()) {
      if (audioClient && audioClient.connected()) {
        WiFiClient rejectClient = audioServer.available();
        rejectClient.stop();
        Serial.println("拒绝新的音频客户端连接（已有连接）");
      } else {
        audioClient = audioServer.available();
        Serial.println("音频客户端已连接");
        Serial.print("客户端IP: ");
        Serial.println(audioClient.remoteIP());
        streaming = true;
      }
    }

    // 检查客户端断开
    if (audioClient && !audioClient.connected()) {
      Serial.println("音频客户端已断开");
      streaming = false;
      audioClient.stop();
    }

    // 发送音频数据（仅在录音激活时）
    // 在loop()函数中修改音频处理部分
    if (streaming && audioClient && audioClient.connected() && recording_active) {
      size_t bytesIn = 0;
      esp_err_t result = i2s_read(I2S_PORT, &sBuffer, bufferLen * sizeof(int32_t),
                                  &bytesIn, (100 / portTICK_PERIOD_MS));

      if (result == ESP_OK && bytesIn > 0) {
        int samples = bytesIn / sizeof(int32_t);

        // 详细调试：查看原始数据和不同转换方式的结果
        static int debugCount = 0;
        if (debugCount++ % 50 == 0) {  // 每50次打印一次
          Serial.println("\n===== I2S数据分析 =====");
          Serial.printf("原始32位数据 (前5个):\n");
          for (int i = 0; i < min(5, samples); i++) {
            Serial.printf("  [%d] HEX: 0x%08X, DEC: %d\n", i, sBuffer[i], sBuffer[i]);
          }

          Serial.println("\n不同位移的结果:");
          for (int i = 0; i < min(3, samples); i++) {
            Serial.printf("  样本[%d]:\n", i);
            Serial.printf("    >>8  = %d\n", (int16_t)(sBuffer[i] >> 8));
            Serial.printf("    >>12 = %d\n", (int16_t)(sBuffer[i] >> 12));
            Serial.printf("    >>14 = %d\n", (int16_t)(sBuffer[i] >> 14));
            Serial.printf("    >>16 = %d\n", (int16_t)(sBuffer[i] >> 16));
          }
          Serial.println("====================\n");
        }

        // 尝试不同的转换方式
        // 方案1: >>14 (WebSocket版本使用的)
        for (int i = 0; i < samples; i++) {
          int32_t sample = sBuffer[i] >> 14;
          audioBuffer[i] = (int16_t)sample;
        }

        // 计算并显示音频特征
        int16_t maxVal = 0;
        int16_t minVal = 0;
        int32_t sum = 0;

        for (int i = 0; i < samples; i++) {
          if (audioBuffer[i] > maxVal) maxVal = audioBuffer[i];
          if (audioBuffer[i] < minVal) minVal = audioBuffer[i];
          sum += audioBuffer[i];
        }

        float avg = (float)sum / samples;

        // 计算RMS
        float rms = 0;
        for (int i = 0; i < samples; i++) {
          float sample = (float)audioBuffer[i];
          rms += sample * sample;
        }
        rms = sqrt(rms / samples);

        // 发送数据包
        uint16_t packetSize = samples * sizeof(int16_t);
        audioClient.write((uint8_t*)&packetSize, sizeof(packetSize));
        audioClient.write((uint8_t*)audioBuffer, packetSize);

        // 输出统计信息
        static unsigned long lastDebug = 0;
        if (millis() - lastDebug > 1000) {
          lastDebug = millis();
          Serial.printf("音频统计 - RMS: %.2f, 最大: %d, 最小: %d, 平均: %.2f, 包: %d字节\n",
                        rms, maxVal, minVal, avg, packetSize);
        }
      }
    }
    // 检查WiFi连接状态
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 10000) {
      lastCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi连接丢失，尝试重连...");
        ESP.restart();
      }
    }
  }

  delay(1);
}