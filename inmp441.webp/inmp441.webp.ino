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

// HTML网页（增强版，支持录音回放）
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32音频录制与回放</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 30px;
            text-align: center;
            max-width: 500px;
            width: 100%;
        }
        
        h1 {
            margin-bottom: 25px;
            font-size: 26px;
        }
        
        .status-box {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-bottom: 20px;
        }
        
        .status {
            padding: 12px;
            border-radius: 10px;
            background: rgba(255, 255, 255, 0.2);
            font-size: 14px;
        }
        
        .visualizer {
            width: 100%;
            height: 100px;
            background: rgba(0, 0, 0, 0.3);
            border-radius: 10px;
            margin: 20px 0;
        }
        
        .controls {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            justify-content: center;
            margin: 20px 0;
        }
        
        button {
            background: white;
            color: #667eea;
            border: none;
            padding: 12px 20px;
            border-radius: 10px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s;
            flex: 1;
            min-width: 120px;
        }
        
        button:hover:not(:disabled) {
            transform: scale(1.05);
            box-shadow: 0 5px 20px rgba(0,0,0,0.3);
        }
        
        button:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        
        button.recording {
            background: #ff4444;
            color: white;
            animation: pulse 1.5s infinite;
        }
        
        button.playing {
            background: #44ff44;
            color: #333;
        }
        
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.7; }
            100% { opacity: 1; }
        }
        
        .volume-control {
            margin: 20px 0;
        }
        
        .volume-label {
            display: flex;
            justify-content: space-between;
            margin-bottom: 5px;
            font-size: 14px;
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
        
        .recordings-list {
            margin-top: 20px;
            max-height: 200px;
            overflow-y: auto;
        }
        
        .recording-item {
            background: rgba(255, 255, 255, 0.1);
            padding: 10px;
            border-radius: 8px;
            margin-bottom: 10px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .recording-info {
            text-align: left;
            flex: 1;
        }
        
        .recording-name {
            font-weight: bold;
            font-size: 14px;
        }
        
        .recording-meta {
            font-size: 12px;
            opacity: 0.8;
        }
        
        .recording-actions {
            display: flex;
            gap: 5px;
        }
        
        .mini-btn {
            padding: 5px 10px;
            font-size: 12px;
            min-width: auto;
        }
        
        .info-panel {
            margin-top: 20px;
            padding: 15px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 10px;
            font-size: 12px;
            text-align: left;
        }
        
        .timer {
            font-size: 24px;
            font-weight: bold;
            margin: 10px 0;
            font-family: 'Courier New', monospace;
        }
        
        audio {
            width: 100%;
            margin-top: 10px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎤 ESP32 音频录制系统</h1>
        
        <div class="status-box">
            <div class="status" id="connectionStatus">未连接</div>
            <div class="status" id="recordStatus">就绪</div>
        </div>
        
        <div class="timer" id="timer">00:00</div>
        
        <canvas class="visualizer" id="visualizer"></canvas>
        
        <div class="controls">
            <button id="connectBtn" onclick="toggleConnection()">连接设备</button>
            <button id="recordBtn" onclick="toggleRecording()" disabled>开始录音</button>
            <button id="playBtn" onclick="playLastRecording()" disabled>播放录音</button>
            <button id="downloadBtn" onclick="downloadRecording()" disabled>下载录音</button>
        </div>
        
        <div class="volume-control">
            <div class="volume-label">
                <span>监听音量</span>
                <span id="volumeValue">50%</span>
            </div>
            <input type="range" id="volume" min="0" max="100" value="50" oninput="changeVolume(this.value)">
        </div>
        
        <div class="recordings-list" id="recordingsList"></div>
        
        <audio id="audioPlayer" controls style="display:none;"></audio>
        
        <div class="info-panel">
            <div>采样率: 16000 Hz | 单声道</div>
            <div>延迟: <span id="latency">0</span> ms</div>
            <div>已录制: <span id="recordedSize">0</span> KB</div>
            <div>录音数: <span id="recordingCount">0</span></div>
        </div>
    </div>

    <script>
        let ws = null;
        let audioContext = null;
        let gainNode = null;
        let isConnected = false;
        let isRecording = false;
        let isPlaying = false;
        let recordedChunks = [];
        let recordings = [];
        let startTime = null;
        let timerInterval = null;
        let nextTime = 0;
        
        // 可视化
        const canvas = document.getElementById('visualizer');
        const canvasCtx = canvas.getContext('2d');
        canvas.width = canvas.offsetWidth * 2;
        canvas.height = canvas.offsetHeight * 2;
        canvas.style.width = canvas.offsetWidth + 'px';
        canvas.style.height = canvas.offsetHeight + 'px';
        
        function initAudio() {
            audioContext = new (window.AudioContext || window.webkitAudioContext)({
                sampleRate: 16000,
                latencyHint: 'interactive'
            });
            
            gainNode = audioContext.createGain();
            gainNode.connect(audioContext.destination);
            gainNode.gain.value = 0.5;
            
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
                document.getElementById('connectionStatus').textContent = '已连接';
                document.getElementById('connectionStatus').style.background = 'rgba(0, 255, 0, 0.3)';
                document.getElementById('connectBtn').textContent = '断开连接';
                document.getElementById('recordBtn').disabled = false;
            };
            
            ws.onmessage = function(event) {
                if (event.data instanceof ArrayBuffer && isRecording) {
                    // 保存录音数据
                    recordedChunks.push(event.data);
                    
                    // 实时播放（监听）
                    playAudioData(event.data);
                    
                    // 更新统计
                    updateStats();
                }
            };
            
            ws.onerror = function(error) {
                console.error('WebSocket错误:', error);
                document.getElementById('connectionStatus').textContent = '连接错误';
                document.getElementById('connectionStatus').style.background = 'rgba(255, 0, 0, 0.3)';
            };
            
            ws.onclose = function() {
                isConnected = false;
                isRecording = false;
                document.getElementById('connectionStatus').textContent = '未连接';
                document.getElementById('connectionStatus').style.background = 'rgba(255, 255, 255, 0.2)';
                document.getElementById('connectBtn').textContent = '连接设备';
                document.getElementById('recordBtn').disabled = true;
                if (isRecording) {
                    stopRecording();
                }
            };
        }
        
        function disconnect() {
            if (ws) {
                ws.close();
            }
        }
        
        function toggleRecording() {
            if (!isRecording) {
                startRecording();
            } else {
                stopRecording();
            }
        }
        
        function startRecording() {
            isRecording = true;
            recordedChunks = [];
            startTime = Date.now();
            nextTime = audioContext.currentTime;
            
            document.getElementById('recordBtn').textContent = '停止录音';
            document.getElementById('recordBtn').classList.add('recording');
            document.getElementById('recordStatus').textContent = '录音中...';
            document.getElementById('recordStatus').style.background = 'rgba(255, 0, 0, 0.3)';
            
            // 启动计时器
            timerInterval = setInterval(updateTimer, 100);
            
            ws.send('START');
        }
        
        function stopRecording() {
            isRecording = false;
            
            document.getElementById('recordBtn').textContent = '开始录音';
            document.getElementById('recordBtn').classList.remove('recording');
            document.getElementById('recordStatus').textContent = '就绪';
            document.getElementById('recordStatus').style.background = 'rgba(255, 255, 255, 0.2)';
            
            // 停止计时器
            clearInterval(timerInterval);
            
            ws.send('STOP');
            
            // 保存录音
            if (recordedChunks.length > 0) {
                saveRecording();
            }
        }
        
        function saveRecording() {
            // 合并所有音频块
            const totalLength = recordedChunks.reduce((acc, chunk) => acc + chunk.byteLength, 0);
            const mergedArray = new Int16Array(totalLength / 2);
            let offset = 0;
            
            for (const chunk of recordedChunks) {
                const int16Array = new Int16Array(chunk);
                mergedArray.set(int16Array, offset);
                offset += int16Array.length;
            }
            
            // 创建WAV文件
            const wavBlob = createWavFile(mergedArray);
            const duration = (Date.now() - startTime) / 1000;
            
            // 保存到录音列表
            const recording = {
                id: Date.now(),
                name: `录音 ${recordings.length + 1}`,
                date: new Date().toLocaleString('zh-CN'),
                duration: duration.toFixed(1),
                size: (wavBlob.size / 1024).toFixed(1),
                blob: wavBlob,
                url: URL.createObjectURL(wavBlob)
            };
            
            recordings.unshift(recording);
            updateRecordingsList();
            
            // 启用播放和下载按钮
            document.getElementById('playBtn').disabled = false;
            document.getElementById('downloadBtn').disabled = false;
            
            // 自动播放最新录音
            setTimeout(() => {
                playRecording(recording);
            }, 500);
        }
        
        function createWavFile(samples) {
            const buffer = new ArrayBuffer(44 + samples.length * 2);
            const view = new DataView(buffer);
            
            // WAV文件头
            const writeString = (offset, string) => {
                for (let i = 0; i < string.length; i++) {
                    view.setUint8(offset + i, string.charCodeAt(i));
                }
            };
            
            writeString(0, 'RIFF');
            view.setUint32(4, 36 + samples.length * 2, true);
            writeString(8, 'WAVE');
            writeString(12, 'fmt ');
            view.setUint32(16, 16, true);
            view.setUint16(20, 1, true);
            view.setUint16(22, 1, true); // 单声道
            view.setUint32(24, 16000, true); // 采样率
            view.setUint32(28, 16000 * 2, true);
            view.setUint16(32, 2, true);
            view.setUint16(34, 16, true);
            writeString(36, 'data');
            view.setUint32(40, samples.length * 2, true);
            
            // 写入音频数据
            let offset = 44;
            for (let i = 0; i < samples.length; i++) {
                view.setInt16(offset, samples[i], true);
                offset += 2;
            }
            
            return new Blob([buffer], { type: 'audio/wav' });
        }
        
        function playAudioData(arrayBuffer) {
            if (!isRecording) return;
            
            const int16Array = new Int16Array(arrayBuffer);
            const float32Array = new Float32Array(int16Array.length);
            
            for (let i = 0; i < int16Array.length; i++) {
                float32Array[i] = int16Array[i] / 32768.0;
            }
            
            const audioBuffer = audioContext.createBuffer(1, float32Array.length, 16000);
            audioBuffer.getChannelData(0).set(float32Array);
            
            const source = audioContext.createBufferSource();
            source.buffer = audioBuffer;
            source.connect(gainNode);
            
            if (nextTime < audioContext.currentTime) {
                nextTime = audioContext.currentTime;
            }
            source.start(nextTime);
            nextTime += audioBuffer.duration;
            
            visualize(float32Array);
        }
        
        function playRecording(recording) {
            const audio = document.getElementById('audioPlayer');
            audio.src = recording.url;
            audio.style.display = 'block';
            audio.play();
            
            document.getElementById('playBtn').textContent = '停止播放';
            document.getElementById('playBtn').classList.add('playing');
            
            audio.onended = function() {
                document.getElementById('playBtn').textContent = '播放录音';
                document.getElementById('playBtn').classList.remove('playing');
            };
        }
        
        function playLastRecording() {
            if (recordings.length > 0) {
                const audio = document.getElementById('audioPlayer');
                if (audio.paused) {
                    playRecording(recordings[0]);
                } else {
                    audio.pause();
                    document.getElementById('playBtn').textContent = '播放录音';
                    document.getElementById('playBtn').classList.remove('playing');
                }
            }
        }
        
        function downloadRecording() {
            if (recordings.length > 0) {
                const recording = recordings[0];
                const a = document.createElement('a');
                a.href = recording.url;
                a.download = `ESP32_录音_${recording.id}.wav`;
                a.click();
            }
        }
        
        function deleteRecording(id) {
            const index = recordings.findIndex(r => r.id === id);
            if (index !== -1) {
                URL.revokeObjectURL(recordings[index].url);
                recordings.splice(index, 1);
                updateRecordingsList();
                
                if (recordings.length === 0) {
                    document.getElementById('playBtn').disabled = true;
                    document.getElementById('downloadBtn').disabled = true;
                }
            }
        }
        
        function updateRecordingsList() {
            const list = document.getElementById('recordingsList');
            list.innerHTML = '';
            
            recordings.forEach(recording => {
                const item = document.createElement('div');
                item.className = 'recording-item';
                item.innerHTML = `
                    <div class="recording-info">
                        <div class="recording-name">${recording.name}</div>
                        <div class="recording-meta">${recording.duration}秒 | ${recording.size}KB | ${recording.date}</div>
                    </div>
                    <div class="recording-actions">
                        <button class="mini-btn" onclick="playRecording(recordings.find(r => r.id === ${recording.id}))">播放</button>
                        <button class="mini-btn" onclick="deleteRecording(${recording.id})">删除</button>
                    </div>
                `;
                list.appendChild(item);
            });
            
            document.getElementById('recordingCount').textContent = recordings.length;
        }
        
        function visualize(dataArray) {
            canvasCtx.fillStyle = 'rgba(0, 0, 0, 0.1)';
            canvasCtx.fillRect(0, 0, canvas.width, canvas.height);
            
            canvasCtx.lineWidth = 2;
            canvasCtx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
            canvasCtx.beginPath();
            
            const sliceWidth = canvas.width / dataArray.length * 10;
            let x = 0;
            
            for (let i = 0; i < dataArray.length; i += 10) {
                const v = dataArray[i];
                const y = (v + 1) / 2 * canvas.height;
                
                if (i === 0) {
                    canvasCtx.moveTo(x, y);
                } else {
                    canvasCtx.lineTo(x, y);
                }
                x += sliceWidth;
            }
            
            canvasCtx.stroke();
        }
        
        function changeVolume(value) {
            document.getElementById('volumeValue').textContent = value + '%';
            if (gainNode) {
                gainNode.gain.value = value / 100;
            }
        }
        
        function updateTimer() {
            if (isRecording && startTime) {
                const elapsed = Math.floor((Date.now() - startTime) / 1000);
                const minutes = Math.floor(elapsed / 60);
                const seconds = elapsed % 60;
                document.getElementById('timer').textContent = 
                    `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
            } else {
                document.getElementById('timer').textContent = '00:00';
            }
        }
        
        function updateStats() {
            const totalSize = recordedChunks.reduce((acc, chunk) => acc + chunk.byteLength, 0);
            document.getElementById('recordedSize').textContent = (totalSize / 1024).toFixed(1);
            
            if (audioContext && nextTime) {
                const latency = ((nextTime - audioContext.currentTime) * 1000).toFixed(0);
                document.getElementById('latency').textContent = Math.max(0, latency);
            }
        }
        
        // 初始化
        function init() {
            canvasCtx.fillStyle = 'rgba(0, 0, 0, 0.3)';
            canvasCtx.fillRect(0, 0, canvas.width, canvas.height);
        }
        
        init();
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
  Serial.println("\nESP32 音频录制与回放系统");
  Serial.println("========================");
  
  // 初始化I2S
  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);
  Serial.println("✓ I2S初始化完成");
  
  // 连接WiFi
  Serial.print("连接WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n✗ WiFi连接失败!");
    Serial.println("请检查WiFi配置");
    return;
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
  
  Serial.println("\n=====================================");
  Serial.println("  使用说明:");
  Serial.println("  1. iPhone连接同一WiFi网络");
  Serial.print("  2. Safari打开: http://");
  Serial.println(WiFi.localIP());
  Serial.println("  3. 点击'连接设备'");
  Serial.println("  4. 点击'开始录音'录制声音");
  Serial.println("  5. 点击'停止录音'结束");
  Serial.println("  6. 自动播放录音，也可手动播放");
  Serial.println("  7. 可下载录音为WAV文件");
  Serial.println("=====================================\n");
}

bool streaming = false;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] 断开连接\n", num);
      streaming = false;
      break;
      
    case WStype_CONNECTED:
      Serial.printf("[%u] 客户端连接\n", num);
      break;
      
    case WStype_TEXT:
      String msg = String((char*)payload);
      if (msg == "START") {
        streaming = true;
        Serial.println("开始录音流传输");
      } else if (msg == "STOP") {
        streaming = false;
        Serial.println("停止录音流传输");
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
      
      // 显示音量指示
      static unsigned long lastVolumeDisplay = 0;
      if (millis() - lastVolumeDisplay > 200) {
        lastVolumeDisplay = millis();
        
        float rms = 0;
        for (int i = 0; i < samples; i++) {
          rms += audioBuffer[i] * audioBuffer[i];
        }
        rms = sqrt(rms / samples);
        
        int level = (int)(rms / 500);
        if (level > 30) level = 30;
        
        Serial.print("录音中 [");
        for (int i = 0; i < level; i++) {
          Serial.print("█");
        }
        for (int i = level; i < 30; i++) {
          Serial.print(" ");
        }
        Serial.println("]");
      }
    }
  }
  
  // 显示状态
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    if (!streaming) {
      Serial.print("状态: 待机 | 客户端: ");
      Serial.print(webSocket.connectedClients());
      Serial.print(" | WiFi: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    }
  }
}