/*
 * ESP32-C6-Touch-LCD-1.47 手势识别 Demo
 * 功能：识别单击、双击、滑动（上下左右）手势 + 方向检测（垂直/水平）
 * 
 * 依赖库：
 * - GFX_Library_for_Arduino (v1.5.9) - 在线安装
 * - esp_lcd_touch_axs5106l - 离线安装
 * - FastIMU (v1.2.8) - 在线安装
 */

#include <Arduino_GFX_Library.h>
#include "esp_lcd_touch_axs5106l.h"
#include <Wire.h>
#include "FastIMU.h"

// 屏幕配置
#define GFX_BL 23  // 背光引脚
#define ROTATION 0 // 屏幕旋转

// I2C 配置
#define I2C_SDA 18 // ESP_SDA
#define I2C_SCL 19 // ESP_SCL
#define TP_INT 21  // 触摸中断引脚
#define TP_RST 20  // 触摸复位引脚

// 触摸屏 I2C 地址
#define TOUCH_I2C_ADDRESS 0x38

// 手势识别参数
#define CLICK_THRESHOLD_MS 300     // 单击时间阈值（毫秒）
#define DOUBLE_CLICK_GAP_MS 400    // 双击间隔阈值（毫秒）
#define SWIPE_MIN_DISTANCE 50      // 滑动最小距离（像素）
#define SWIPE_MAX_TIME_MS 500      // 滑动最大时间（毫秒）

// 自定义颜色（如果库中没有定义）
#define RGB565_GRAY 0x8410  // 灰色

// IMU 配置
#define IMU_ADDRESS 0x6B  // QMI8658 I2C 地址
QMI8658 IMU;              // IMU 对象

// IMU 数据
calData calib = { 0 };    // 校准数据
AccelData accelData;      // 加速度计数据

// 方向枚举
enum Orientation {
  ORIENTATION_VERTICAL,
  ORIENTATION_HORIZONTAL,
  ORIENTATION_UNKNOWN
};

// 屏幕驱动配置
Arduino_DataBus *bus = new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 1 /* SCK */, 2 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 22 /* RST */, 0 /* rotation */, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /*col_offset1*/, 0 /*row_offset1*/,
  34 /*col_offset2*/, 0 /*row_offset2*/);

// 触摸屏驱动 - 使用 BSP 接口
bool touch_initialized = false;

// 触摸状态结构体
struct TouchState {
  bool touched;
  uint16_t x;
  uint16_t y;
  unsigned long touchStartTime;
  uint16_t startX;
  uint16_t startY;
  unsigned long lastClickTime;
  bool waitingForDoubleClick;
};

TouchState touchState = {false, 0, 0, 0, 0, 0, 0, false};

// 手势类型枚举
enum GestureType {
  GESTURE_NONE,
  GESTURE_CLICK,
  GESTURE_DOUBLE_CLICK,
  GESTURE_SWIPE_UP,
  GESTURE_SWIPE_DOWN,
  GESTURE_SWIPE_LEFT,
  GESTURE_SWIPE_RIGHT
};

// LCD寄存器初始化（从原始示例代码）
void lcd_reg_init(void) {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,

    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 
    0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,

    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,

    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,

    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,
    
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,
    
    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
  bus->batchOperation(init_operations, sizeof(init_operations));
}

// 初始化触摸屏
void initTouch() {
  // 初始化 I2C 和触摸屏 - 使用 BSP 接口
  Wire.begin(I2C_SDA, I2C_SCL);
  bsp_touch_init(&Wire, TP_RST, TP_INT, ROTATION, 172, 320);
  touch_initialized = true;
  
  Serial.println("Touch screen initialized with BSP driver");
}

// 初始化 IMU
void initIMU() {
  int err = IMU.init(calib, IMU_ADDRESS);
  if (err != 0) {
    Serial.print("IMU init failed, error: ");
    Serial.println(err);
  } else {
    Serial.println("IMU initialized successfully");
  }
}

// 获取设备方向
Orientation getOrientation() {
  IMU.update();
  IMU.getAccel(&accelData);
  
  // 判断设备方向（基于重力方向）
  // 垂直：Z轴接近 -1g 或 +1g
  // 水平：X轴或Y轴接近 -1g 或 +1g
  
  float absX = fabs(accelData.accelX);
  float absY = fabs(accelData.accelY);
  float absZ = fabs(accelData.accelZ);
  
  // 找出最大值，它指示重力方向
  if (absZ > absX && absZ > absY && absZ > 0.7) {
    // Z轴主导 - 设备垂直
    return ORIENTATION_VERTICAL;
  } else if ((absX > absZ || absY > absZ) && (absX > 0.7 || absY > 0.7)) {
    // X轴或Y轴主导 - 设备水平
    return ORIENTATION_HORIZONTAL;
  }
  
  return ORIENTATION_UNKNOWN;
}

// 读取触摸坐标
bool readTouch(uint16_t &x, uint16_t &y) {
  if (!touch_initialized) {
    return false;
  }
  
  touch_data_t touch_data;
  bsp_touch_read();
  bool touched = bsp_touch_get_coordinates(&touch_data);
  
  if (touched && touch_data.touch_num > 0) {
    x = touch_data.coords[0].x;
    y = touch_data.coords[0].y;
    return true;
  }
  
  return false;
}

// 识别手势
GestureType recognizeGesture() {
  uint16_t currentX, currentY;
  bool currentlyTouched = readTouch(currentX, currentY);
  unsigned long currentTime = millis();
  GestureType gesture = GESTURE_NONE;
  
  // 检测触摸开始
  if (currentlyTouched && !touchState.touched) {
    touchState.touched = true;
    touchState.touchStartTime = currentTime;
    touchState.startX = currentX;
    touchState.startY = currentY;
    touchState.x = currentX;
    touchState.y = currentY;
  }
  // 检测触摸结束
  else if (!currentlyTouched && touchState.touched) {
    touchState.touched = false;
    unsigned long touchDuration = currentTime - touchState.touchStartTime;
    
    // 计算移动距离
    int16_t deltaX = touchState.x - touchState.startX;
    int16_t deltaY = touchState.y - touchState.startY;
    uint16_t distance = sqrt(deltaX * deltaX + deltaY * deltaY);
    
    // 判断手势类型
    if (distance < 20 && touchDuration < CLICK_THRESHOLD_MS) {
      // 可能是单击或双击
      if (touchState.waitingForDoubleClick && 
          (currentTime - touchState.lastClickTime) < DOUBLE_CLICK_GAP_MS) {
        // 双击
        gesture = GESTURE_DOUBLE_CLICK;
        touchState.waitingForDoubleClick = false;
      } else {
        // 标记为潜在的单击
        touchState.waitingForDoubleClick = true;
        touchState.lastClickTime = currentTime;
      }
    } else if (distance >= SWIPE_MIN_DISTANCE && touchDuration < SWIPE_MAX_TIME_MS) {
      // 滑动手势
      if (abs(deltaX) > abs(deltaY)) {
        // 水平滑动
        gesture = (deltaX > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
      } else {
        // 垂直滑动
        gesture = (deltaY > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
      }
      touchState.waitingForDoubleClick = false;
    }
  }
  // 更新触摸位置
  else if (currentlyTouched && touchState.touched) {
    touchState.x = currentX;
    touchState.y = currentY;
  }
  
  // 检查单击超时
  if (!touchState.touched && touchState.waitingForDoubleClick && 
      (currentTime - touchState.lastClickTime) >= DOUBLE_CLICK_GAP_MS) {
    gesture = GESTURE_CLICK;
    touchState.waitingForDoubleClick = false;
  }
  
  return gesture;
}

// 显示手势信息
void displayGesture(GestureType gesture) {
  static GestureType lastGesture = GESTURE_NONE;
  static unsigned long gestureTime = 0;
  
  // 获取并显示设备方向
  Orientation orientation = getOrientation();
  
  // 显示方向状态（始终显示）
  gfx->fillRect(10, 240, 152, 30, RGB565_BLACK);
  gfx->setCursor(10, 250);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_ORANGE);
  gfx->print("Orient: ");
  
  switch(orientation) {
    case ORIENTATION_VERTICAL:
      gfx->println("Vertical");
      Serial.println("Orientation: Vertical");
      break;
    case ORIENTATION_HORIZONTAL:
      gfx->println("Horizontal");
      Serial.println("Orientation: Horizontal");
      break;
    default:
      gfx->println("Unknown");
      Serial.println("Orientation: Unknown");
      break;
  }
  
  // 显示加速度计数据（调试用）
  gfx->setCursor(10, 265);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_GRAY);
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "X:%.1f Y:%.1f Z:%.1f", 
           accelData.accelX, accelData.accelY, accelData.accelZ);
  gfx->print(buffer);
  
  if (gesture != GESTURE_NONE) {
    lastGesture = gesture;
    gestureTime = millis();
    
    // 清屏（手势区域）
    gfx->fillRect(10, 100, 152, 120, RGB565_BLACK);
    
    // 设置文本属性
    gfx->setCursor(10, 120);
    gfx->setTextSize(2);
    
    // 显示手势类型
    switch(gesture) {
      case GESTURE_CLICK:
        gfx->setTextColor(RGB565_GREEN);
        gfx->println("Click!");
        Serial.println("Gesture: Click");
        break;
        
      case GESTURE_DOUBLE_CLICK:
        gfx->setTextColor(RGB565_YELLOW);
        gfx->println("Double\nClick!");
        Serial.println("Gesture: Double Click");
        break;
        
      case GESTURE_SWIPE_UP:
        gfx->setTextColor(RGB565_CYAN);
        gfx->println("Swipe\nUp!");
        Serial.println("Gesture: Swipe Up");
        break;
        
      case GESTURE_SWIPE_DOWN:
        gfx->setTextColor(RGB565_CYAN);
        gfx->println("Swipe\nDown!");
        Serial.println("Gesture: Swipe Down");
        break;
        
      case GESTURE_SWIPE_LEFT:
        gfx->setTextColor(RGB565_MAGENTA);
        gfx->println("Swipe\nLeft!");
        Serial.println("Gesture: Swipe Left");
        break;
        
      case GESTURE_SWIPE_RIGHT:
        gfx->setTextColor(RGB565_MAGENTA);
        gfx->println("Swipe\nRight!");
        Serial.println("Gesture: Swipe Right");
        break;
    }
    
    // 显示触摸位置
    if (gesture == GESTURE_CLICK || gesture == GESTURE_DOUBLE_CLICK) {
      gfx->setCursor(10, 170);
      gfx->setTextSize(1);
      gfx->setTextColor(RGB565_WHITE);
      gfx->printf("Pos: %d,%d", touchState.startX, touchState.startY);
    }
  }
  
  // 3秒后清除显示
  if (lastGesture != GESTURE_NONE && (millis() - gestureTime) > 3000) {
    gfx->fillRect(10, 100, 152, 120, RGB565_BLACK);
    lastGesture = GESTURE_NONE;
  }
}

// 显示实时触摸点
void displayTouchPoint() {
  static uint16_t lastX = 0, lastY = 0;
  
  // 清除上一个点
  if (lastX > 0 || lastY > 0) {
    gfx->fillCircle(lastX, lastY, 3, RGB565_BLACK);
  }
  
  // 绘制当前触摸点
  if (touchState.touched) {
    gfx->fillCircle(touchState.x, touchState.y, 3, RGB565_RED);
    lastX = touchState.x;
    lastY = touchState.y;
  } else {
    lastX = 0;
    lastY = 0;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C6 Touch Gesture Demo Starting...");
  
  // 初始化显示屏
  if (!gfx->begin()) {
    Serial.println("GFX initialization failed!");
    while(1);
  }
  
  // 应用LCD寄存器配置
  lcd_reg_init();
  
  gfx->setRotation(ROTATION);
  gfx->fillScreen(RGB565_BLACK);
  
  // 开启背光
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  
  // 初始化触摸屏
  initTouch();
  
  // 初始化IMU
  initIMU();
  
  // 显示标题
  gfx->setCursor(10, 10);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->println("Gesture");
  gfx->println("Demo");
  
  // 显示说明
  gfx->setCursor(10, 60);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_GRAY);
  gfx->println("Try:");
  gfx->println("- Single Click");
  gfx->println("- Double Click");
  gfx->println("- Swipe U/D/L/R");
  
  Serial.println("Setup complete. Ready for gestures!");
}

void loop() {
  // 识别手势
  GestureType gesture = recognizeGesture();
  
  // 显示手势结果
  displayGesture(gesture);
  
  // 显示触摸点（可选）
  displayTouchPoint();
  
  // 小延时避免过度占用CPU
  delay(10);
}