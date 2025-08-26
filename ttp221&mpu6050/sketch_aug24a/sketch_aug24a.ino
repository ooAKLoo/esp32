// ESP32 TTP224 电容式4键触摸感应 - 精简版
// 支持单击、双击、滑动手势识别

// 引脚定义 - TTP224的4个输出连接到ESP32
#define TOUCH_PIN_1 13
#define TOUCH_PIN_2 12
#define TOUCH_PIN_3 14
#define TOUCH_PIN_4 27

// 时间参数（毫秒）
#define DEBOUNCE_TIME 50        // 消抖时间
#define LONG_PRESS_TIME 1000    // 长按阈值
#define DOUBLE_CLICK_TIME 500   // 双击检测时间窗口
#define SWIPE_TIMEOUT 500       // 滑动超时时间
#define SWIPE_MIN_KEYS 3        // 最少触发键数才算滑动

// 触摸键状态结构
struct TouchKey {
  int pin;
  bool currentState;
  bool lastState;
  unsigned long pressTime;
  unsigned long lastClickTime;
  int clickCount;
  bool longPressTriggered;  // 长按已触发标志
};

// 滑动检测结构
struct SwipeGesture {
  int sequence[4];
  int sequenceIndex;
  unsigned long startTime;
  bool isActive;
};

// 全局变量
TouchKey touchKeys[4] = {
  {TOUCH_PIN_1, false, false, 0, 0, 0, false},
  {TOUCH_PIN_2, false, false, 0, 0, 0, false},
  {TOUCH_PIN_3, false, false, 0, 0, 0, false},
  {TOUCH_PIN_4, false, false, 0, 0, 0, false}
};

SwipeGesture swipe = {{-1, -1, -1, -1}, 0, 0, false};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32 TTP224 触摸感应系统 - 精简版");
  Serial.println("功能：单击、双击、长按、滑动手势");
  
  // 初始化触摸引脚
  for (int i = 0; i < 4; i++) {
    pinMode(touchKeys[i].pin, INPUT);
  }
  
  Serial.println("准备就绪！\n");
}

void loop() {
  // 读取所有触摸键状态
  for (int i = 0; i < 4; i++) {
    readTouchKey(i);
  }
  
  // 检测长按
  checkLongPress();
  
  // 检测滑动手势
  detectSwipeGesture();
  
  // 检测双击超时
  checkDoubleClickTimeout();
  
  delay(10);
}

// 读取单个触摸键状态
void readTouchKey(int index) {
  TouchKey &key = touchKeys[index];
  
  // 读取当前状态
  key.currentState = digitalRead(key.pin);
  
  // 状态改变检测
  if (key.currentState != key.lastState) {
    unsigned long currentTime = millis();
    
    // 消抖处理
    if (currentTime - key.pressTime > DEBOUNCE_TIME) {
      
      if (key.currentState == HIGH) {
        // 按下事件
        key.pressTime = currentTime;
        key.longPressTriggered = false;  // 重置长按标志
        onKeyPressed(index + 1);
        
      } else {
        // 释放事件 - 只有不是长按时才检测点击
        if (!key.longPressTriggered) {
          handleClick(index + 1, currentTime);
        }
      }
    }
    
    key.lastState = key.currentState;
  }
}

// 按键按下事件
void onKeyPressed(int keyNumber) {
  // 添加到滑动序列
  addToSwipeSequence(keyNumber);
}

// 处理点击事件
void handleClick(int keyNumber, unsigned long currentTime) {
  TouchKey &key = touchKeys[keyNumber - 1];
  
  // 检查是否在双击时间窗口内
  if (currentTime - key.lastClickTime < DOUBLE_CLICK_TIME && key.clickCount == 1) {
    // 双击
    key.clickCount = 0;
    key.lastClickTime = 0;
    onDoubleClick(keyNumber);
  } else {
    // 可能的单击（需要等待确认不是双击）
    key.clickCount = 1;
    key.lastClickTime = currentTime;
  }
}

// 检测双击超时
void checkDoubleClickTimeout() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < 4; i++) {
    TouchKey &key = touchKeys[i];
    if (key.clickCount == 1 && currentTime - key.lastClickTime > DOUBLE_CLICK_TIME) {
      // 确认单击
      key.clickCount = 0;
      key.lastClickTime = 0;
      onSingleClick(i + 1);
    }
  }
}

// 单击事件
void onSingleClick(int keyNumber) {
  Serial.print("👆 按键 ");
  Serial.print(keyNumber);
  Serial.println(" 单击");
}

// 双击事件
void onDoubleClick(int keyNumber) {
  Serial.print("👆👆 按键 ");
  Serial.print(keyNumber);
  Serial.println(" 双击");
}

// 长按事件
void onLongPress(int keyNumber) {
  Serial.print("🔒 按键 ");
  Serial.print(keyNumber);
  Serial.println(" 长按");
}

// 检测长按
void checkLongPress() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < 4; i++) {
    TouchKey &key = touchKeys[i];
    if (key.currentState == HIGH && !key.longPressTriggered) {
      if (currentTime - key.pressTime >= LONG_PRESS_TIME) {
        key.longPressTriggered = true;
        // 清除可能的点击计数，防止长按后还触发点击
        key.clickCount = 0;
        key.lastClickTime = 0;
        onLongPress(i + 1);
      }
    }
  }
}

// 添加到滑动序列
void addToSwipeSequence(int keyNumber) {
  unsigned long currentTime = millis();
  
  // 检查是否开始新的滑动
  if (!swipe.isActive || currentTime - swipe.startTime > SWIPE_TIMEOUT) {
    resetSwipeGesture();
    swipe.isActive = true;
    swipe.startTime = currentTime;
  }
  
  // 避免重复添加相同的键
  if (swipe.sequenceIndex == 0 || swipe.sequence[swipe.sequenceIndex - 1] != keyNumber) {
    if (swipe.sequenceIndex < 4) {
      swipe.sequence[swipe.sequenceIndex] = keyNumber;
      swipe.sequenceIndex++;
    }
  }
}

// 检测滑动手势
void detectSwipeGesture() {
  if (!swipe.isActive) return;
  
  unsigned long currentTime = millis();
  
  // 检查超时
  if (currentTime - swipe.startTime > SWIPE_TIMEOUT) {
    if (swipe.sequenceIndex >= SWIPE_MIN_KEYS) {
      analyzeSwipeDirection();
    }
    resetSwipeGesture();
  }
}

// 分析滑动方向
void analyzeSwipeDirection() {
  // 计算滑动趋势
  int trend = 0;
  for (int i = 1; i < swipe.sequenceIndex; i++) {
    trend += (swipe.sequence[i] - swipe.sequence[i-1]);
  }
  
  // 输出滑动序列
  Serial.print("滑动序列：");
  for (int i = 0; i < swipe.sequenceIndex; i++) {
    Serial.print(swipe.sequence[i]);
    if (i < swipe.sequenceIndex - 1) Serial.print("→");
  }
  Serial.print(" ");
  
  // 判断滑动方向
  if (trend > 0) {
    Serial.println("➡️ 向右滑动");
  } else if (trend < 0) {
    Serial.println("⬅️ 向左滑动");
  } else {
    Serial.println("↔️ 无明确方向");
  }
}

// 重置滑动手势
void resetSwipeGesture() {
  for (int i = 0; i < 4; i++) {
    swipe.sequence[i] = -1;
  }
  swipe.sequenceIndex = 0;
  swipe.isActive = false;
  swipe.startTime = 0;
}

/*
 * ESP32 蓝牙鼠标触摸控制器
 * 使用TTP224电容触摸模块作为输入，通过蓝牙模拟鼠标设备
 * 
 * 功能映射：
 * - 向上滑动（4→3→2→1）：鼠标滚轮向上
 * - 向下滑动（1→2→3→4）：鼠标滚轮向下
 * - 单击按键1：鼠标左键
 * - 单击按键2：鼠标右键
 * - 单击按键3：鼠标中键
 * - 单击按键4：后退键
 * - 长按按键1：按住左键（拖拽）
 * - 双击：双击左键
 * 
 * 需要安装库：ESP32-BLE-Mouse
 * 在Arduino IDE中：工具 -> 管理库 -> 搜索 "ESP32 BLE Mouse" 并安装
 */

/*
 * ESP32 原生BLE蓝牙鼠标触摸控制器
 * 使用ESP32原生BLE库，无需安装额外库
 * 使用TTP224电容触摸模块作为输入，通过蓝牙模拟鼠标设备
 * 
 * 功能映射：
 * - 向上滑动（4→3→2→1）：鼠标滚轮向上
 * - 向下滑动（1→2→3→4）：鼠标滚轮向下
 * - 单击按键1：鼠标左键
 * - 单击按键2：鼠标右键
 * - 单击按键3：鼠标中键
 * - 单击按键4：特殊功能键
 * - 长按按键1：按住左键（拖拽）
 * - 双击：双击左键
 */