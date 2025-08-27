/*
 * ESP32 MPU6050 陀螺仪蓝牙鼠标控制器
 * 通过倾斜ESP32控制电脑光标，结合TTP224触摸按键实现完整鼠标功能
 * 
 * 功能说明：
 * - 倾斜控制：前后左右倾斜控制光标移动
 * - 触摸按键：实现点击、双击、滚轮等功能
 * - 手势识别：特定动作触发特殊功能
 * 
 * 硬件连接：
 * MPU6050: VCC-3.3V, GND-GND, SCL-GPIO22, SDA-GPIO21
 * TTP224: 输出1-GPIO13, 输出2-GPIO12, 输出3-GPIO14, 输出4-GPIO27
 */

#include <Wire.h>
#include <BleMouse.h>

// ===== 硬件引脚定义 =====
// MPU6050 I2C引脚（ESP32默认）
#define MPU_SDA 21
#define MPU_SCL 22

// TTP224触摸按键引脚
#define TOUCH_PIN_1 13  // 左键
#define TOUCH_PIN_2 12  // 右键
#define TOUCH_PIN_3 14  // 中键
#define TOUCH_PIN_4 27  // 功能键

// ===== MPU6050寄存器地址 =====
#define MPU6050_ADDR 0x68
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B
#define GYRO_XOUT_H 0x43
#define TEMP_OUT_H 0x41

// ===== 控制参数 =====
// 鼠标控制
#define MOUSE_SENSITIVITY 2.0    // 鼠标灵敏度（1.0-5.0）
#define MOUSE_SMOOTH 0.85        // 平滑系数（0.7-0.95）
#define DEAD_ZONE 3.0           // 死区角度（度）
#define MAX_SPEED 15            // 最大移动速度

// 滚轮控制
#define SCROLL_ANGLE 20         // 触发滚轮的倾斜角度
#define SCROLL_SPEED 3          // 滚轮速度

// 手势检测
#define SHAKE_THRESHOLD 300     // 摇晃检测阈值
#define TILT_LOCK_ANGLE 60     // 锁定角度

// 时间参数
#define DEBOUNCE_TIME 50
#define DOUBLE_CLICK_TIME 500
#define LONG_PRESS_TIME 1000
#define CALIBRATION_TIME 3000   // 校准时间

// ===== 全局对象 =====
BleMouse bleMouse("ESP32 陀螺鼠标", "Espressif", 100);

// ===== 数据结构 =====
struct MPU6050Data {
  int16_t accelX, accelY, accelZ;
  int16_t gyroX, gyroY, gyroZ;
  int16_t temperature;
  float angleX, angleY, angleZ;  // 融合后的角度
  float gyroAngleX, gyroAngleY, gyroAngleZ;  // 陀螺仪积分角度
};

struct MouseControl {
  float smoothX, smoothY;        // 平滑后的移动值
  int moveX, moveY;              // 最终移动值
  bool scrollMode;               // 滚轮模式
  bool precisionMode;            // 精确模式
  bool gestureMode;              // 手势模式
};

struct TouchKey {
  int pin;
  bool currentState;
  bool lastState;
  unsigned long pressTime;
  unsigned long releaseTime;
  bool longPressTriggered;
  int clickCount;
};

// ===== 全局变量 =====
MPU6050Data mpu;
MouseControl mouse = {0, 0, 0, 0, false, false, false};
TouchKey touchKeys[4];

// 校准偏移
int16_t accelXOffset = 0, accelYOffset = 0, accelZOffset = 0;
int16_t gyroXOffset = 0, gyroYOffset = 0, gyroZOffset = 0;

// 控制状态
bool isCalibrating = true;
unsigned long calibrationStartTime = 0;
unsigned long lastTime = 0;
float deltaTime = 0;

// ===== 初始化函数 =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   ESP32 陀螺仪蓝牙鼠标控制器 v2.0     ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // 初始化I2C
  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setClock(400000);  // 400kHz I2C速度
  
  // 初始化MPU6050
  if (!initMPU6050()) {
    Serial.println("❌ MPU6050初始化失败！");
    while (1) delay(10);
  }
  Serial.println("✅ MPU6050初始化成功");
  
  // 初始化触摸按键
  initTouchKeys();
  Serial.println("✅ 触摸按键初始化成功");
  
  // 初始化蓝牙鼠标
  bleMouse.begin();
  Serial.println("✅ 蓝牙鼠标启动，等待连接...");
  
  // 开始校准
  Serial.println("\n📐 开始校准，请保持设备静止...");
  calibrationStartTime = millis();
  lastTime = millis();
}

// ===== 主循环 =====
void loop() {
  // 计算时间差
  unsigned long currentTime = millis();
  deltaTime = (currentTime - lastTime) / 1000.0;
  lastTime = currentTime;
  
  // 校准阶段
  if (isCalibrating) {
    performCalibration();
    return;
  }
  
  // 读取传感器数据
  readMPU6050();
  calculateAngles();
  
  // 处理触摸输入
  processTouchKeys();
  
  // 蓝牙连接时处理鼠标控制
  if (bleMouse.isConnected()) {
    processMouseControl();
    detectGestures();
  }
  
  // 调试输出（降低频率）
  static unsigned long lastDebugTime = 0;
  if (currentTime - lastDebugTime > 100) {
    lastDebugTime = currentTime;
    printDebugInfo();
  }
  
  delay(10);  // 100Hz更新率
}

// ===== MPU6050相关函数 =====
bool initMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(PWR_MGMT_1);
  Wire.write(0x00);  // 唤醒MPU6050
  byte error = Wire.endTransmission();
  
  if (error != 0) {
    return false;
  }
  
  // 配置陀螺仪范围 (±250°/s)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1B);
  Wire.write(0x00);
  Wire.endTransmission();
  
  // 配置加速度计范围 (±2g)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1C);
  Wire.write(0x00);
  Wire.endTransmission();
  
  // 配置低通滤波器
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1A);
  Wire.write(0x05);  // 10Hz带宽
  Wire.endTransmission();
  
  return true;
}

void readMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(ACCEL_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true);
  
  // 读取加速度
  mpu.accelX = (Wire.read() << 8 | Wire.read()) - accelXOffset;
  mpu.accelY = (Wire.read() << 8 | Wire.read()) - accelYOffset;
  mpu.accelZ = (Wire.read() << 8 | Wire.read()) - accelZOffset;
  
  // 读取温度
  mpu.temperature = Wire.read() << 8 | Wire.read();
  
  // 读取陀螺仪
  mpu.gyroX = (Wire.read() << 8 | Wire.read()) - gyroXOffset;
  mpu.gyroY = (Wire.read() << 8 | Wire.read()) - gyroYOffset;
  mpu.gyroZ = (Wire.read() << 8 | Wire.read()) - gyroZOffset;
}

void calculateAngles() {
  // 陀螺仪角速度（度/秒）
  float gyroRateX = mpu.gyroX / 131.0;
  float gyroRateY = mpu.gyroY / 131.0;
  float gyroRateZ = mpu.gyroZ / 131.0;
  
  // 陀螺仪积分
  mpu.gyroAngleX += gyroRateX * deltaTime;
  mpu.gyroAngleY += gyroRateY * deltaTime;
  mpu.gyroAngleZ += gyroRateZ * deltaTime;
  
  // 加速度计角度
  float accelAngleX = atan2(mpu.accelY, sqrt(pow(mpu.accelX, 2) + pow(mpu.accelZ, 2))) * 180 / PI;
  float accelAngleY = atan2(-mpu.accelX, sqrt(pow(mpu.accelY, 2) + pow(mpu.accelZ, 2))) * 180 / PI;
  
  // 互补滤波融合
  float alpha = 0.96;  // 陀螺仪权重
  mpu.angleX = alpha * (mpu.angleX + gyroRateX * deltaTime) + (1 - alpha) * accelAngleX;
  mpu.angleY = alpha * (mpu.angleY + gyroRateY * deltaTime) + (1 - alpha) * accelAngleY;
  mpu.angleZ = mpu.gyroAngleZ;  // Z轴只用陀螺仪
}

void performCalibration() {
  static int samples = 0;
  static long sumAX = 0, sumAY = 0, sumAZ = 0;
  static long sumGX = 0, sumGY = 0, sumGZ = 0;
  
  if (millis() - calibrationStartTime < CALIBRATION_TIME) {
    // 累积样本
    readMPU6050();
    sumAX += mpu.accelX;
    sumAY += mpu.accelY;
    sumAZ += mpu.accelZ - 16384;  // 1g = 16384
    sumGX += mpu.gyroX;
    sumGY += mpu.gyroY;
    sumGZ += mpu.gyroZ;
    samples++;
    
    // 显示进度
    if (samples % 50 == 0) {
      int progress = (millis() - calibrationStartTime) * 100 / CALIBRATION_TIME;
      Serial.print("校准进度: ");
      Serial.print(progress);
      Serial.println("%");
    }
  } else {
    // 计算偏移
    accelXOffset = sumAX / samples;
    accelYOffset = sumAY / samples;
    accelZOffset = sumAZ / samples;
    gyroXOffset = sumGX / samples;
    gyroYOffset = sumGY / samples;
    gyroZOffset = sumGZ / samples;
    
    Serial.println("✅ 校准完成！");
    Serial.println("可以开始使用了！\n");
    
    isCalibrating = false;
  }
}

// ===== 鼠标控制函数 =====
void processMouseControl() {
  // 检查特殊模式
  if (mouse.scrollMode) {
    processScrollMode();
    return;
  }
  
  // 计算鼠标移动
  float moveX = 0, moveY = 0;
  
  // 使用倾斜角度控制光标
  if (abs(mpu.angleY) > DEAD_ZONE) {
    moveX = mpu.angleY * MOUSE_SENSITIVITY;
    
    // 应用非线性曲线（更自然的控制）
    moveX = moveX * abs(moveX) / 20;
    
    // 限制最大速度
    moveX = constrain(moveX, -MAX_SPEED, MAX_SPEED);
  }
  
  if (abs(mpu.angleX) > DEAD_ZONE) {
    moveY = -mpu.angleX * MOUSE_SENSITIVITY;
    
    // 应用非线性曲线
    moveY = moveY * abs(moveY) / 20;
    
    // 限制最大速度
    moveY = constrain(moveY, -MAX_SPEED, MAX_SPEED);
  }
  
  // 精确模式（按住按键4时）
  if (mouse.precisionMode) {
    moveX *= 0.3;
    moveY *= 0.3;
  }
  
  // 平滑处理
  mouse.smoothX = mouse.smoothX * MOUSE_SMOOTH + moveX * (1 - MOUSE_SMOOTH);
  mouse.smoothY = mouse.smoothY * MOUSE_SMOOTH + moveY * (1 - MOUSE_SMOOTH);
  
  // 转换为整数
  mouse.moveX = round(mouse.smoothX);
  mouse.moveY = round(mouse.smoothY);
  
  // 发送鼠标移动
  if (mouse.moveX != 0 || mouse.moveY != 0) {
    bleMouse.move(mouse.moveX, mouse.moveY, 0);
  }
}

void processScrollMode() {
  // 使用Y轴倾斜控制滚轮
  if (mpu.angleX > SCROLL_ANGLE) {
    bleMouse.move(0, 0, -SCROLL_SPEED);  // 向下滚动
    delay(100);
  } else if (mpu.angleX < -SCROLL_ANGLE) {
    bleMouse.move(0, 0, SCROLL_SPEED);   // 向上滚动
    delay(100);
  }
}

void detectGestures() {
  // 检测摇晃手势
  float totalGyro = sqrt(pow(mpu.gyroX, 2) + pow(mpu.gyroY, 2) + pow(mpu.gyroZ, 2));
  
  static unsigned long lastShakeTime = 0;
  static int shakeCount = 0;
  
  if (totalGyro > SHAKE_THRESHOLD * 131) {  // 转换为原始值
    if (millis() - lastShakeTime < 1000) {
      shakeCount++;
      if (shakeCount >= 3) {
        Serial.println("🎯 检测到摇晃手势！");
        // 执行特殊动作，如回到屏幕中心
        centerCursor();
        shakeCount = 0;
      }
    } else {
      shakeCount = 1;
    }
    lastShakeTime = millis();
  }
  
  // 检测倾斜锁定
  if (abs(mpu.angleX) > TILT_LOCK_ANGLE || abs(mpu.angleY) > TILT_LOCK_ANGLE) {
    static unsigned long tiltStartTime = 0;
    if (tiltStartTime == 0) {
      tiltStartTime = millis();
    } else if (millis() - tiltStartTime > 2000) {
      Serial.println("🔒 触发倾斜锁定！");
      mouse.gestureMode = !mouse.gestureMode;
      tiltStartTime = 0;
    }
  }
}

void centerCursor() {
  // 模拟移动到屏幕中心（需要多次小幅移动）
  for (int i = 0; i < 20; i++) {
    bleMouse.move(-100, -100, 0);
    delay(10);
  }
  for (int i = 0; i < 10; i++) {
    bleMouse.move(50, 50, 0);
    delay(10);
  }
}

// ===== 触摸按键函数 =====
void initTouchKeys() {
  touchKeys[0] = {TOUCH_PIN_1, false, false, 0, 0, false, 0};  // 左键
  touchKeys[1] = {TOUCH_PIN_2, false, false, 0, 0, false, 0};  // 右键
  touchKeys[2] = {TOUCH_PIN_3, false, false, 0, 0, false, 0};  // 中键
  touchKeys[3] = {TOUCH_PIN_4, false, false, 0, 0, false, 0};  // 功能键
  
  for (int i = 0; i < 4; i++) {
    pinMode(touchKeys[i].pin, INPUT);
  }
}

void processTouchKeys() {
  for (int i = 0; i < 4; i++) {
    TouchKey &key = touchKeys[i];
    key.currentState = digitalRead(key.pin);
    
    // 状态改变检测
    if (key.currentState != key.lastState) {
      unsigned long currentTime = millis();
      
      if (key.currentState == HIGH) {
        // 按下
        key.pressTime = currentTime;
        key.longPressTriggered = false;
        onKeyPressed(i);
        
      } else {
        // 释放
        key.releaseTime = currentTime;
        if (!key.longPressTriggered) {
          onKeyReleased(i);
        }
      }
      
      key.lastState = key.currentState;
    }
    
    // 长按检测
    if (key.currentState == HIGH && !key.longPressTriggered) {
      if (millis() - key.pressTime > LONG_PRESS_TIME) {
        key.longPressTriggered = true;
        onKeyLongPress(i);
      }
    }
  }
}

void onKeyPressed(int keyIndex) {
  if (!bleMouse.isConnected()) return;
  
  switch (keyIndex) {
    case 0:  // 按键1 - 左键按下
      bleMouse.press(MOUSE_LEFT);
      Serial.println("🖱️ 左键按下");
      break;
      
    case 1:  // 按键2 - 右键按下
      bleMouse.press(MOUSE_RIGHT);
      Serial.println("🖱️ 右键按下");
      break;
      
    case 2:  // 按键3 - 中键/切换滚轮模式
      mouse.scrollMode = !mouse.scrollMode;
      Serial.println(mouse.scrollMode ? "📜 滚轮模式" : "🖱️ 光标模式");
      break;
      
    case 3:  // 按键4 - 精确模式
      mouse.precisionMode = true;
      Serial.println("🎯 精确模式开启");
      break;
  }
}

void onKeyReleased(int keyIndex) {
  if (!bleMouse.isConnected()) return;
  
  switch (keyIndex) {
    case 0:  // 释放左键
      bleMouse.release(MOUSE_LEFT);
      Serial.println("🖱️ 左键释放");
      break;
      
    case 1:  // 释放右键
      bleMouse.release(MOUSE_RIGHT);
      Serial.println("🖱️ 右键释放");
      break;
      
    case 3:  // 关闭精确模式
      mouse.precisionMode = false;
      Serial.println("🎯 精确模式关闭");
      break;
  }
}

void onKeyLongPress(int keyIndex) {
  if (!bleMouse.isConnected()) return;
  
  switch (keyIndex) {
    case 0:  // 长按按键1 - 拖拽模式
      Serial.println("📌 拖拽模式");
      break;
      
    case 1:  // 长按按键2 - 特殊功能
      Serial.println("⚙️ 特殊功能");
      break;
      
    case 2:  // 长按按键3 - 重置校准
      Serial.println("🔄 重新校准");
      isCalibrating = true;
      calibrationStartTime = millis();
      break;
      
    case 3:  // 长按按键4 - 断开/重连蓝牙
      if (bleMouse.isConnected()) {
        Serial.println("📵 断开蓝牙连接");
        // 注：BleMouse库可能不支持主动断开
      }
      break;
  }
}

// ===== 调试输出 =====
void printDebugInfo() {
  static int counter = 0;
  counter++;
  
  // 每秒输出一次完整信息
  if (counter % 10 == 0) {
    Serial.println("┌─────────────────────────────────────┐");
    Serial.print("│ 角度 X:");
    Serial.print(mpu.angleX, 1);
    Serial.print("° Y:");
    Serial.print(mpu.angleY, 1);
    Serial.print("° Z:");
    Serial.print(mpu.angleZ, 1);
    Serial.println("°        │");
    
    Serial.print("│ 鼠标 X:");
    Serial.print(mouse.moveX);
    Serial.print(" Y:");
    Serial.print(mouse.moveY);
    
    if (mouse.scrollMode) {
      Serial.print(" [滚轮模式]");
    }
    if (mouse.precisionMode) {
      Serial.print(" [精确]");
    }
    Serial.println("        │");
    
    Serial.print("│ 蓝牙: ");
    Serial.print(bleMouse.isConnected() ? "已连接 ✅" : "未连接 ❌");
    
    float temp = mpu.temperature / 340.0 + 36.53;
    Serial.print(" 温度:");
    Serial.print(temp, 1);
    Serial.println("°C    │");
    
    Serial.println("└─────────────────────────────────────┘");
  }
}

/*
 * 使用说明：
 * 
 * 1. 安装必需库：
 *    - 在Arduino IDE中：工具 -> 管理库
 *    - 搜索并安装 "ESP32 BLE Mouse"
 * 
 * 2. 硬件连接：
 *    MPU6050:
 *    - VCC -> 3.3V
 *    - GND -> GND
 *    - SCL -> GPIO22
 *    - SDA -> GPIO21
 *    
 *    TTP224:
 *    - OUT1 -> GPIO13 (左键)
 *    - OUT2 -> GPIO12 (右键)
 *    - OUT3 -> GPIO14 (中键/滚轮切换)
 *    - OUT4 -> GPIO27 (精确模式)
 * 
 * 3. 操作方法：
 *    - 倾斜控制光标移动
 *    - 按键1：鼠标左键
 *    - 按键2：鼠标右键
 *    - 按键3：切换滚轮/光标模式
 *    - 按键4：按住进入精确模式
 *    - 摇晃3次：光标居中
 *    - 长按按键3：重新校准
 * 
 * 4. 蓝牙配对：
 *    - 在电脑蓝牙设置中搜索 "ESP32 陀螺鼠标"
 *    - 配对后自动识别为鼠标设备
 */