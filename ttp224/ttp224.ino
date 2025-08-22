// ESP32 TTP224 电容式4键触摸感应 - 点击与滑动检测
// 支持单击、长按、滑动手势识别

// 引脚定义 - TTP224的4个输出连接到ESP32
#define TOUCH_PIN_1 13  // 触摸键1
#define TOUCH_PIN_2 12  // 触摸键2
#define TOUCH_PIN_3 14  // 触摸键3
#define TOUCH_PIN_4 27  // 触摸键4

// 时间参数（毫秒）
#define DEBOUNCE_TIME 50        // 消抖时间
#define LONG_PRESS_TIME 1000   // 长按阈值
#define SWIPE_TIMEOUT 500      // 滑动超时时间
#define SWIPE_MIN_KEYS 3       // 最少触发键数才算滑动

// 触摸键状态结构
struct TouchKey {
  int pin;
  bool currentState;
  bool lastState;
  unsigned long pressTime;
  unsigned long releaseTime;
  bool triggered;
};

// 滑动检测结构
struct SwipeGesture {
  int sequence[4];        // 记录触摸顺序
  int sequenceIndex;      // 当前序列位置
  unsigned long startTime; // 滑动开始时间
  bool isActive;          // 是否正在检测滑动
};

// 全局变量
TouchKey touchKeys[4] = {
  {TOUCH_PIN_1, false, false, 0, 0, false},
  {TOUCH_PIN_2, false, false, 0, 0, false},
  {TOUCH_PIN_3, false, false, 0, 0, false},
  {TOUCH_PIN_4, false, false, 0, 0, false}
};

SwipeGesture swipe = {{-1, -1, -1, -1}, 0, 0, false};

// 统计变量
int clickCount[4] = {0, 0, 0, 0};
int swipeLeftToRight = 0;
int swipeRightToLeft = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("ESP32 TTP224 触摸感应系统");
  Serial.println("========================================");
  Serial.println("功能：");
  Serial.println("1. 单击检测");
  Serial.println("2. 长按检测");
  Serial.println("3. 滑动手势 (1→4 或 4→1)");
  Serial.println("========================================\n");
  
  // 初始化触摸引脚
  for (int i = 0; i < 4; i++) {
    pinMode(touchKeys[i].pin, INPUT);
    Serial.print("触摸键 ");
    Serial.print(i + 1);
    Serial.print(" 初始化完成 (GPIO");
    Serial.print(touchKeys[i].pin);
    Serial.println(")");
  }
  
  Serial.println("\n准备就绪！请触摸按键...\n");
}

void loop() {
  // 读取所有触摸键状态
  for (int i = 0; i < 4; i++) {
    readTouchKey(i);
  }
  
  // 检测滑动手势
  detectSwipeGesture();
  
  // 定期输出统计信息（可选）
  static unsigned long lastStatsTime = 0;
  if (millis() - lastStatsTime > 10000) { // 每10秒输出一次
    printStatistics();
    lastStatsTime = millis();
  }
  
  delay(10); // 主循环延时
}

// 读取单个触摸键状态
void readTouchKey(int index) {
  TouchKey &key = touchKeys[index];
  
  // 读取当前状态（TTP224输出高电平表示触摸）
  key.currentState = digitalRead(key.pin);
  
  // 状态改变检测
  if (key.currentState != key.lastState) {
    unsigned long currentTime = millis();
    
    // 消抖处理
    if (currentTime - key.pressTime > DEBOUNCE_TIME) {
      
      if (key.currentState == HIGH) {
        // 按下事件
        key.pressTime = currentTime;
        key.triggered = false;
        onKeyPressed(index + 1);
        
      } else {
        // 释放事件
        key.releaseTime = currentTime;
        unsigned long pressDuration = key.releaseTime - key.pressTime;
        
        if (!key.triggered) {
          if (pressDuration < LONG_PRESS_TIME) {
            onKeyClicked(index + 1);
          } else {
            onKeyLongPressed(index + 1);
          }
        }
        
        onKeyReleased(index + 1);
      }
    }
    
    key.lastState = key.currentState;
  }
  
  // 持续按压检测（用于长按）
  if (key.currentState == HIGH && !key.triggered) {
    if (millis() - key.pressTime >= LONG_PRESS_TIME) {
      onKeyLongPressed(index + 1);
      key.triggered = true;
    }
  }
}

// 按键按下事件
void onKeyPressed(int keyNumber) {
  Serial.print("🔽 按键 ");
  Serial.print(keyNumber);
  Serial.println(" 按下");
  
  // 添加到滑动序列
  addToSwipeSequence(keyNumber);
}

// 按键释放事件
void onKeyReleased(int keyNumber) {
  // 可以在这里添加释放时的处理
}

// 单击事件
void onKeyClicked(int keyNumber) {
  Serial.print("👆 按键 ");
  Serial.print(keyNumber);
  Serial.println(" 单击");
  
  clickCount[keyNumber - 1]++;
  
  // 可以在这里添加单击的具体功能
  handleClick(keyNumber);
}

// 长按事件
void onKeyLongPressed(int keyNumber) {
  Serial.print("🔒 按键 ");
  Serial.print(keyNumber);
  Serial.println(" 长按");
  
  // 可以在这里添加长按的具体功能
  handleLongPress(keyNumber);
}

// 添加到滑动序列
void addToSwipeSequence(int keyNumber) {
  unsigned long currentTime = millis();
  
  // 检查是否开始新的滑动
  if (!swipe.isActive || currentTime - swipe.startTime > SWIPE_TIMEOUT) {
    // 重置滑动检测
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
    // 分析滑动方向
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
  
  // 判断滑动方向
  if (trend > 0) {
    // 从左到右滑动 (1→4)
    Serial.println("\n➡️  滑动手势检测：从左到右 (1→4)");
    swipeLeftToRight++;
    handleSwipe(1); // 1表示向右
    
  } else if (trend < 0) {
    // 从右到左滑动 (4→1)
    Serial.println("\n⬅️  滑动手势检测：从右到左 (4→1)");
    swipeRightToLeft++;
    handleSwipe(-1); // -1表示向左
    
  } else {
    // 无明确方向
    Serial.println("\n↔️  滑动手势检测：无明确方向");
  }
  
  // 输出滑动序列
  Serial.print("滑动序列：");
  for (int i = 0; i < swipe.sequenceIndex; i++) {
    Serial.print(swipe.sequence[i]);
    if (i < swipe.sequenceIndex - 1) Serial.print(" → ");
  }
  Serial.println("\n");
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

// 处理单击事件
void handleClick(int keyNumber) {
  // 在这里添加每个按键单击的具体功能
  switch(keyNumber) {
    case 1:
      Serial.println("   执行功能：选择项目1");
      break;
    case 2:
      Serial.println("   执行功能：选择项目2");
      break;
    case 3:
      Serial.println("   执行功能：选择项目3");
      break;
    case 4:
      Serial.println("   执行功能：选择项目4");
      break;
  }
}

// 处理长按事件
void handleLongPress(int keyNumber) {
  // 在这里添加每个按键长按的具体功能
  switch(keyNumber) {
    case 1:
      Serial.println("   执行功能：进入设置模式");
      break;
    case 2:
      Serial.println("   执行功能：确认操作");
      break;
    case 3:
      Serial.println("   执行功能：取消操作");
      break;
    case 4:
      Serial.println("   执行功能：返回上级");
      break;
  }
}

// 处理滑动手势
void handleSwipe(int direction) {
  if (direction > 0) {
    // 向右滑动
    Serial.println("   执行功能：下一页/增加音量/向右切换");
  } else {
    // 向左滑动
    Serial.println("   执行功能：上一页/减少音量/向左切换");
  }
}

// 输出统计信息
void printStatistics() {
  Serial.println("\n========================================");
  Serial.println("📊 触摸统计信息");
  Serial.println("========================================");
  
  Serial.println("单击次数：");
  for (int i = 0; i < 4; i++) {
    Serial.print("  按键");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(clickCount[i]);
    Serial.println(" 次");
  }
  
  Serial.println("\n滑动手势：");
  Serial.print("  向右滑动 (1→4): ");
  Serial.print(swipeLeftToRight);
  Serial.println(" 次");
  Serial.print("  向左滑动 (4→1): ");
  Serial.print(swipeRightToLeft);
  Serial.println(" 次");
  
  Serial.println("========================================\n");
}

// 高级功能：组合键检测（可选）
bool isMultiTouch() {
  int touchCount = 0;
  for (int i = 0; i < 4; i++) {
    if (touchKeys[i].currentState == HIGH) {
      touchCount++;
    }
  }
  return touchCount > 1;
}

// 获取当前触摸的所有键
void getCurrentTouchKeys(bool result[4]) {
  for (int i = 0; i < 4; i++) {
    result[i] = (touchKeys[i].currentState == HIGH);
  }
}

// 调试函数：输出所有触摸键状态
void debugPrintTouchStates() {
  Serial.print("触摸状态: [");
  for (int i = 0; i < 4; i++) {
    Serial.print(touchKeys[i].currentState ? "■" : "□");
    if (i < 3) Serial.print(" ");
  }
  Serial.println("]");
} 