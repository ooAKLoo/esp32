#include <Wire.h>

// MPU6050寄存器地址
#define MPU6050_ADDR         0x68
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_TEMP_OUT_H   0x41
#define MPU6050_GYRO_XOUT_H  0x43
#define MPU6050_WHO_AM_I     0x75

// 全局变量
int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;
float angleX = 0, angleY = 0, angleZ = 0;
float gyroXoffset = 0, gyroYoffset = 0, gyroZoffset = 0;
unsigned long previousTime = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("ESP32 MPU6050 陀螺仪测试程序");
  Serial.println("========================================");
  
  // 初始化I2C
  Wire.begin(21, 22);  // SDA=21, SCL=22
  Wire.setClock(400000); // 400kHz
  
  // 唤醒MPU6050
  Serial.println("正在唤醒MPU6050...");
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_PWR_MGMT_1);
  Wire.write(0);  // 唤醒MPU6050
  byte error = Wire.endTransmission(true);
  
  if (error == 0) {
    Serial.println("✓ MPU6050唤醒成功！");
  } else {
    Serial.print("✗ MPU6050唤醒失败，错误代码: ");
    Serial.println(error);
    return;
  }
  
  delay(100);
  
  // 读取WHO_AM_I寄存器验证连接
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_WHO_AM_I);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 1, true);
  byte whoami = Wire.read();
  Serial.print("WHO_AM_I寄存器值: 0x");
  Serial.println(whoami, HEX);
  
  if (whoami == 0x68 || whoami == 0x98 || whoami == 0x7C) {
    Serial.println("✓ MPU6050身份验证成功！");
  } else {
    Serial.println("⚠️  WHO_AM_I值异常，但继续运行...");
  }
  
  // 配置陀螺仪 (±250°/s)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_GYRO_CONFIG);
  Wire.write(0x00);
  Wire.endTransmission(true);
  
  // 配置加速度计 (±2g)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_ACCEL_CONFIG);
  Wire.write(0x00);
  Wire.endTransmission(true);
  
  delay(100);
  
  // 校准陀螺仪
  Serial.println("\n正在校准陀螺仪（请保持静止）...");
  calibrateGyro();
  Serial.println("✓ 校准完成！\n");
  
  previousTime = millis();
}

void calibrateGyro() {
  long sumX = 0, sumY = 0, sumZ = 0;
  int samples = 500;
  
  for (int i = 0; i < samples; i++) {
    readMPU6050();
    sumX += GyX;
    sumY += GyY;
    sumZ += GyZ;
    delay(2);
  }
  
  gyroXoffset = sumX / samples / 131.0;
  gyroYoffset = sumY / samples / 131.0;
  gyroZoffset = sumZ / samples / 131.0;
  
  Serial.print("陀螺仪偏移值 - X: ");
  Serial.print(gyroXoffset);
  Serial.print(" Y: ");
  Serial.print(gyroYoffset);
  Serial.print(" Z: ");
  Serial.println(gyroZoffset);
}

void readMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_ACCEL_XOUT_H);
  Wire.endTransmission(false);
  
  Wire.requestFrom(MPU6050_ADDR, 14, true);
  
  // 读取加速度数据
  AcX = Wire.read() << 8 | Wire.read();
  AcY = Wire.read() << 8 | Wire.read();
  AcZ = Wire.read() << 8 | Wire.read();
  
  // 读取温度数据
  Tmp = Wire.read() << 8 | Wire.read();
  
  // 读取陀螺仪数据
  GyX = Wire.read() << 8 | Wire.read();
  GyY = Wire.read() << 8 | Wire.read();
  GyZ = Wire.read() << 8 | Wire.read();
}

void loop() {
  readMPU6050();
  
  unsigned long currentTime = millis();
  float dt = (currentTime - previousTime) / 1000.0;
  previousTime = currentTime;
  
  // 转换为物理单位
  float accelX = AcX / 16384.0;  // g
  float accelY = AcY / 16384.0;
  float accelZ = AcZ / 16384.0;
  
  float gyroX = (GyX / 131.0) - gyroXoffset;  // °/s
  float gyroY = (GyY / 131.0) - gyroYoffset;
  float gyroZ = (GyZ / 131.0) - gyroZoffset;
  
  float temp = Tmp / 340.0 + 36.53;  // °C
  
  // 计算倾角
  float accelAngleX = atan2(accelY, sqrt(accelX * accelX + accelZ * accelZ)) * 180 / PI;
  float accelAngleY = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180 / PI;
  
  // 陀螺仪积分
  angleX += gyroX * dt;
  angleY += gyroY * dt;
  angleZ += gyroZ * dt;
  
  // 互补滤波
  angleX = 0.96 * angleX + 0.04 * accelAngleX;
  angleY = 0.96 * angleY + 0.04 * accelAngleY;
  
  // 输出数据
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  
  // 原始数据
  Serial.print("原始数据 | 加速度: ");
  Serial.print(AcX); Serial.print(", ");
  Serial.print(AcY); Serial.print(", ");
  Serial.print(AcZ); Serial.print(" | 陀螺仪: ");
  Serial.print(GyX); Serial.print(", ");
  Serial.print(GyY); Serial.print(", ");
  Serial.println(GyZ);
  
  // 物理值
  Serial.print("陀螺仪 (°/s) | X: ");
  Serial.print(gyroX, 1);
  Serial.print(" | Y: ");
  Serial.print(gyroY, 1);
  Serial.print(" | Z: ");
  Serial.println(gyroZ, 1);
  
  Serial.print("加速度 (g)   | X: ");
  Serial.print(accelX, 2);
  Serial.print(" | Y: ");
  Serial.print(accelY, 2);
  Serial.print(" | Z: ");
  Serial.println(accelZ, 2);
  
  Serial.print("角度 (°)     | Roll: ");
  Serial.print(angleX, 1);
  Serial.print(" | Pitch: ");
  Serial.print(angleY, 1);
  Serial.print(" | Yaw: ");
  Serial.println(angleZ, 1);
  
  Serial.print("温度: ");
  Serial.print(temp, 1);
  Serial.println(" °C");
  
  // 倾斜检测
  if (abs(angleX) > 45) {
    Serial.println("⚠️  X轴倾斜过大！");
  }
  if (abs(angleY) > 45) {
    Serial.println("⚠️  Y轴倾斜过大！");
  }
  
  // 运动检测
  float totalGyro = abs(gyroX) + abs(gyroY) + abs(gyroZ);
  if (totalGyro > 100) {
    Serial.println("🔄 检测到快速运动！");
  } else if (totalGyro > 30) {
    Serial.println("➡️  检测到运动");
  }
  
  delay(100);  // 10Hz更新率
}