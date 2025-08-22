#include <driver/i2s.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// WiFi配置
const char* ssid = "ZTE-th99Ry";      // 改为你的WiFi名称
const char* password = "ZTE-th99Ry";   // 改为你的WiFi密码

// I2S配置
#define I2S_WS 25
#define I2S_SD 32
#define I2S_SCK 33
#define I2S_PORT I2S_NUM_0
#define I2S_SAMPLE_RATE   (16000)
#define I2S_SAMPLE_BITS   (32)
#define bufferLen 1024

int32_t sBuffer[bufferLen];
int16_t audioBuffer[bufferLen];

// Web服务器和WebSocket
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// HTML网页（内嵌音频播放器）
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32音频流</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100vh;
            margin: 0;
            padding: 20px;
            box-sizing: border-box;
        }
        .container {
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 40px;
            text-align: center;
            max-width: 400px;
            width: 100%;
        }
        h1 {
            margin: 0 0 30px 0;
            font-size: 28px;
        }
        .status {
            padding: 15px;
            border-radius: 10px;
            margin: 20px 0;
            background: rgba(255, 255, 255, 0.2);
        }
        button {
            background: white;
            color: #667eea;
            border: none;
            padding: 15px 30px;
            border-radius: 10px;
            font-size: 18px;
            font-weight: bold;
            margin: 10px;
            cursor: pointer;
            transition: all 0.3s;
        }
        button:hover {
            transform: scale(1.05);
            box-shadow: 0 5px 20px rgba(0,0,0,0.3);
        }
        button:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        .visualizer {
            width: 100%;
            height: 100px;
            background: rgba(0, 0, 0, 0.3);
            border-radius: 10px;
            margin: 20px 0;
        }
        .volume {
            margin: 20px 0;
        }
        input[type="range"] {
            width: 100%;
            -webkit-appearance: none;
            appearance: none;
            height: 5px;
            background: rgba(255, 255, 255, 0.3);
            border-radius: 5px;
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 20px;
            height: 20px;
            background: white;
            border-radius: 50%;
            cursor: pointer;
        }
        .stats {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-top: 20px;
            font-size: 14px;
        }
        .stat {
            background: rgba(255, 255, 255, 0.1);
            padding: 10px;
            border-radius: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎤 ESP32音频流</h1>
        <div class="status" id="status">未连接</div>
        <canvas class="visualizer" id="visualizer"></canvas>
        <button id="connectBtn" onclick="toggleConnection()">连接音频</button>
        <button id="recordBtn" onclick="toggleRecording()" disabled>开始录音</button>
        <div class="volume">
            <label>音量: <span id="volumeValue">50</span>%</label>
            <input type="range" id="volume" min="0" max="100" value="50" oninput="changeVolume(this.value)">
        </div>
        <div class="stats">
            <div class="stat">采样率: <span id="sampleRate">16000</span> Hz</div>
            <div class="stat">延迟: <span id="latency">0</span> ms</div>
            <div class="stat">缓冲区: <span id="bufferSize">0</span> KB</div>
            <div class="stat">状态: <span id="wsState">断开</span></div>
        </div>
    </div>

    <script>
        let ws = null;
        let audioContext = null;
        let source = null;
        let gainNode = null;
        let isConnected = false;
        let isRecording = false;
        let audioQueue = [];
        let nextTime = 0;
        
        // 可视化
        const canvas = document.getElementById('visualizer');
        const canvasCtx = canvas.getContext('2d');
        canvas.width = canvas.offsetWidth;
        canvas.height = canvas.offsetHeight;
        
        function initAudio() {
            audioContext = new (window.AudioContext || window.webkitAudioContext)({
                sampleRate: 16000,
                latencyHint: 'interactive'
            });
            
            gainNode = audioContext.createGain();
            gainNode.connect(audioContext.destination);
            gainNode.gain.value = 0.5;
            
            // iOS需要用户交互才能播放音频
            if (audioContext.state === 'suspended') {
                audioContext.resume();
            }
        }
        
        function toggleConnection() {
            if (!isConnected) {
                connect();
            } else {
                disconnect();
            }
        }
        
        function connect() {
            if (!audioContext) {
                initAudio();
            }
            
            const wsUrl = 'ws://' + window.location.hostname + ':81/';
            ws = new WebSocket(wsUrl);
            ws.binaryType = 'arraybuffer';
            
            ws.onopen = function() {
                isConnected = true;
                document.getElementById('status').textContent = '已连接';
                document.getElementById('status').style.background = 'rgba(0, 255, 0, 0.3)';
                document.getElementById('connectBtn').textContent = '断开连接';
                document.getElementById('recordBtn').disabled = false;
                document.getElementById('wsState').textContent = '已连接';
            };
            
            ws.onmessage = function(event) {
                if (event.data instanceof ArrayBuffer && isRecording) {
                    playAudioData(event.data);
                    updateStats(event.data);
                }
            };
            
            ws.onerror = function(error) {
                console.error('WebSocket错误:', error);
                document.getElementById('status').textContent = '连接错误';
                document.getElementById('status').style.background = 'rgba(255, 0, 0, 0.3)';
            };
            
            ws.onclose = function() {
                isConnected = false;
                isRecording = false;
                document.getElementById('status').textContent = '未连接';
                document.getElementById('status').style.background = 'rgba(255, 255, 255, 0.2)';
                document.getElementById('connectBtn').textContent = '连接音频';
                document.getElementById('recordBtn').disabled = true;
                document.getElementById('recordBtn').textContent = '开始录音';
                document.getElementById('wsState').textContent = '断开';
            };
        }
        
        function disconnect() {
            if (ws) {
                ws.close();
            }
        }
        
        function toggleRecording() {
            isRecording = !isRecording;
            document.getElementById('recordBtn').textContent = isRecording ? '停止录音' : '开始录音';
            
            if (isRecording) {
                nextTime = audioContext.currentTime;
                ws.send('START');
            } else {
                ws.send('STOP');
            }
        }
        
        function playAudioData(arrayBuffer) {
            // 将Int16数组转换为Float32
            const int16Array = new Int16Array(arrayBuffer);
            const float32Array = new Float32Array(int16Array.length);
            
            for (let i = 0; i < int16Array.length; i++) {
                float32Array[i] = int16Array[i] / 32768.0;
            }
            
            // 创建音频缓冲区
            const audioBuffer = audioContext.createBuffer(1, float32Array.length, 16000);
            audioBuffer.getChannelData(0).set(float32Array);
            
            // 播放音频
            const source = audioContext.createBufferSource();
            source.buffer = audioBuffer;
            source.connect(gainNode);
            
            if (nextTime < audioContext.currentTime) {
                nextTime = audioContext.currentTime;
            }
            source.start(nextTime);
            nextTime += audioBuffer.duration;
            
            // 可视化
            visualize(float32Array);
        }
        
        function visualize(dataArray) {
            canvasCtx.fillStyle = 'rgba(0, 0, 0, 0.2)';
            canvasCtx.fillRect(0, 0, canvas.width, canvas.height);
            
            canvasCtx.lineWidth = 2;
            canvasCtx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
            canvasCtx.beginPath();
            
            const sliceWidth = canvas.width / dataArray.length;
            let x = 0;
            
            for (let i = 0; i < dataArray.length; i += 10) {
                const v = dataArray[i];
                const y = (v + 1) / 2 * canvas.height;
                
                if (i === 0) {
                    canvasCtx.moveTo(x, y);
                } else {
                    canvasCtx.lineTo(x, y);
                }
                x += sliceWidth * 10;
            }
            
            canvasCtx.stroke();
        }
        
        function changeVolume(value) {
            document.getElementById('volumeValue').textContent = value;
            if (gainNode) {
                gainNode.gain.value = value / 100;
            }
        }
        
        function updateStats(data) {
            const latency = ((nextTime - audioContext.currentTime) * 1000).toFixed(0);
            const bufferSize = (data.byteLength / 1024).toFixed(1);
            
            document.getElementById('latency').textContent = Math.max(0, latency);
            document.getElementById('bufferSize').textContent = bufferSize;
        }
        
        // 初始化可视化背景
        function initVisualizer() {
            canvasCtx.fillStyle = 'rgba(0, 0, 0, 0.3)';
            canvasCtx.fillRect(0, 0, canvas.width, canvas.height);
        }
        
        initVisualizer();
    </script>
</body>
</html>
)rawliteral";

void i2s_install() {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
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

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 WiFi音频流服务器");
  Serial.println("=======================");
  
  // 初始化I2S
  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);
  Serial.println("✓ I2S初始化完成");
  
  // 连接WiFi
  Serial.print("连接WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✓ WiFi连接成功");
  Serial.print("IP地址: ");
  Serial.println(WiFi.localIP());
  
  // 设置Web服务器
  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });
  server.begin();
  Serial.println("✓ Web服务器启动 (端口80)");
  
  // 启动WebSocket服务器
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("✓ WebSocket服务器启动 (端口81)");
  
  Serial.println("\n使用方法:");
  Serial.println("1. iPhone连接同一WiFi网络");
  Serial.print("2. Safari浏览器打开: http://");
  Serial.println(WiFi.localIP());
  Serial.println("3. 点击'连接音频'然后'开始录音'");
  Serial.println("\n准备就绪!\n");
}

bool streaming = false;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_TEXT:
      String msg = String((char*)payload);
      if (msg == "START") {
        streaming = true;
        Serial.println("开始音频流传输");
      } else if (msg == "STOP") {
        streaming = false;
        Serial.println("停止音频流传输");
      }
      break;
  }
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  if (streaming && webSocket.connectedClients() > 0) {
    size_t bytesIn = 0;
    esp_err_t result = i2s_read(I2S_PORT, &sBuffer, bufferLen * sizeof(int32_t), &bytesIn, (100 / portTICK_PERIOD_MS));
    
    if (result == ESP_OK && bytesIn > 0) {
      int samples = bytesIn / sizeof(int32_t);
      
      // 转换32位数据为16位
      for (int i = 0; i < samples; i++) {
        // INMP441数据处理
        int32_t sample = sBuffer[i] >> 14;
        audioBuffer[i] = (int16_t)sample;
      }
      
      // 通过WebSocket发送音频数据
      webSocket.broadcastBIN((uint8_t*)audioBuffer, samples * sizeof(int16_t));
    }
  }
  
  // 显示状态
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    Serial.print("客户端数: ");
    Serial.print(webSocket.connectedClients());
    Serial.print(" | 流状态: ");
    Serial.println(streaming ? "传输中" : "待机");
  }
}