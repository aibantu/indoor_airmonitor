// 四行数据显示：日期/CO2/温度/湿度，屏幕方向上下颠倒
#include <Arduino.h>
#include <ST7789_AVR.h>
#include "display_helper.h"
#include <SPI.h>
// DHT sensor
#include <DHT.h>
#include <esp_system.h>

// 本版本仅使用 16 字节帧的简单累加校验，不再需要 CRC16 查表

// 引脚定义（依据实际连线，如需调整请改这里）
#define PIN_DC   16
#define PIN_RST  5
#define PIN_CS   17

ST7789_AVR tft(PIN_DC, PIN_RST, PIN_CS);

// DHT 配置（选取不与屏幕/ SPI 冲突的引脚）
#define DHTPIN 2     // 使用 GPIO2，确保未被屏幕占用
#define DHTTYPE DHT22 // DHT22 (AM2302)
DHT dht(DHTPIN, DHTTYPE);

// CO2 传感器串口（被动输出 16 字节帧，每秒一次）
// 固定接线: 传感器 TX -> ESP32-S3 GPIO38 (RX)；GND 共地；不需要 MCU TX
// 说明: 使用 UART0 IO 矩阵映射 RX 到 38；若某板载定义不支持可改为 UART1 并保持 begin 参数一致
#define CO2_UART_RX 38
#define CO2_UART_TX -1  // 未使用
HardwareSerial co2Serial(0);
// CO2 串口帧缓冲
static uint8_t co2Buf[64];
static size_t co2BufLen = 0; // 接收缓冲当前长度
static uint32_t co2FrameCount = 0; // 已解析帧计数
static uint8_t lastFrame[16];
static uint32_t lastFrameMillis = 0; // 最近帧到达时间
static uint32_t lastByteMillis = 0;   // 最近任意字节到达时间
static uint32_t rxRetryCount = 0;     // 自动提示计数

// 临时串口自检开关：设置为 1 将运行最小串口自检程序（用于定位 Serial 无输出问题）
#define SERIAL_TEST 0

// 字体尺寸与行距 (第一行更大 + 居中显示)
uint8_t gFirstLineSize = 3; // 日期行字号
uint8_t gOtherLineSize = 3; // 其他行字号

// 行位置与数值区域起始x缓存
int16_t yDateTop = 6, yLineSep = 22, yCo2Top = 32, yTempTop = 48, yHumTop = 64;
int16_t xCo2Digits = 50, xTempDigits = 50, xHumDigits = 50; // 数字起始X（仅数字部分）
int16_t xCo2Unit = 0; // 单位起始X，display_helper 中会在 initLayout 设置
int16_t co2DigitRegionW = 60, tempDigitRegionW = 60, humDigitRegionW = 60; // 预留宽度（像素）
uint8_t firstH = 8, otherH = 8;

// 上一帧数据缓存，用于比较变化
String prevDate = "";
uint32_t prevCo2 = 0xFFFFFFFF;
int prevTempScaled = 0x7FFFFFFF; // 温度*10
int prevHumScaled  = 0x7FFFFFFF; // 湿度*10
int prevCo2Chars = 0; // 上一次数字字符数
int prevTempChars = 0;
int prevHumChars  = 0;
bool layoutInited = false;
// 前一次已显示的纯数字字符串缓存（仅数字和小数点，不含单位/度符号）
String prevCo2Str = "";
String prevTempStr = "";
String prevHumStr = "";

// 状态数据（模拟）
static uint32_t co2ppm = 450;      // CO2 ppm
static float temperatureC = 25.3f; // 温度
static float humidityPct = 48.5f;  // 湿度
static uint32_t lastUpdateMs = 0;  // 刷新时间

// 持久化 boot 计数用于检测是否频繁重启（例如打开串口导致复位）
RTC_DATA_ATTR static uint32_t bootCount = 0;

// 将 __DATE__ 解析为 YYYY-MM-DD（__DATE__ 格式: "Nov 17 2025"）
String formatDate() {
  const char *dateStr = __DATE__; // 例如 "Nov 17 2025"
  char monStr[4];
  int day, year;
  sscanf(dateStr, "%3s %d %d", monStr, &day, &year);
  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int monthNum = (strstr(months, monStr) - months) / 3 + 1;
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, monthNum, day);
  return String(buf);
}

// initLayout implemented in include/display_helper.h

// updateValues implemented in include/display_helper.h

// updateDateIfNeeded implemented in include/display_helper.h

// 仅解析 16 字节帧：格式
// BYTE0=0x42, BYTE1=0x4D, BYTE2..BYTE14=数据内容, BYTE15= (BYTE0+...+BYTE14) & 0xFF
// CO2 浓度 = BYTE6*256 + BYTE7
void processCo2Buffer() {
  // 不断尝试解析首部为 0x42 0x4D 的 16 字节帧
  while (co2BufLen >= 16) {
    // 寻找头部（若首字节不是0x42或第二字节不是0x4D，丢弃一个字节继续）
    if (!(co2Buf[0] == 0x42 && co2Buf[1] == 0x4D)) {
      // 失配移除首字节
      memmove(co2Buf, co2Buf + 1, co2BufLen - 1);
      co2BufLen -= 1;
      continue;
    }
    // 计算校验和（BYTE0..BYTE14）
    uint16_t sum = 0;
    for (int i = 0; i <= 14; ++i) sum += co2Buf[i];
    uint8_t expected = (uint8_t)(sum & 0xFF);
    uint8_t recvChk = co2Buf[15];
    bool chkOk = (expected == recvChk);
    uint16_t co2val = ((uint16_t)co2Buf[6] << 8) | co2Buf[7];
    co2ppm = co2val;
    memcpy(lastFrame, co2Buf, 16);
    lastFrameMillis = millis();
    co2FrameCount++;
    Serial.print("CO2 frame: ");
    for (int i = 0; i < 16; ++i) {
      if (co2Buf[i] < 16) Serial.print('0');
      Serial.print(co2Buf[i], HEX); Serial.print(' ');
    }
    Serial.print(" -> CO2="); Serial.print(co2ppm);
    if (!chkOk) {
      Serial.print(" (checksum mismatch exp="); Serial.print(expected, HEX);
      Serial.print(" got="); Serial.print(recvChk, HEX); Serial.print(")");
    }
    Serial.println();
    // 移除已解析帧
    if (co2BufLen > 16) memmove(co2Buf, co2Buf + 16, co2BufLen - 16);
    co2BufLen -= 16;

    // 不在此处直接绘制屏幕，display 更新由 updateValues() 统一处理
  }
}

void setup() {
  // 先初始化串口用于早期调试输出（有些板子需要更早初始化以便主机能枚举）
  bootCount++;
  Serial.begin(9600);
  delay(200);
  Serial.print("Serial started @9600, bootCount="); Serial.println(bootCount);
  // 打印复位原因，帮助定位是否由于打开串口导致复位/进入下载模式
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.print("Reset reason: "); Serial.println((int)rr);

  #ifdef SERIAL_TEST
  Serial.println("SERIAL_TEST is enabled.");
  #endif

  // SPI初始化（ESP32S3 默认 VSPI/FSPI 视具体板卡，这里手动指定）
  SPI.begin(18, -1, 15, PIN_CS); // SCK=18, MISO未用(-1), MOSI=15, CS=PIN_CS

  tft.init(172, 320);      // 自定义逻辑尺寸
  tft.setRotation(3);      // 与之前方向上下颠倒 (1 -> 3)
  Serial.println("TFT initialized");
  Serial.print("Boot millis= "); Serial.println(millis());
  // 先初始化显示，这样在可能的重启或长延时期间，不会长时间黑屏
  initLayout(formatDate());
  // 给外设初始化时间（例如 DHT、传感器及屏幕稳定）
  delay(3500); // >=3秒

  // 初始化 DHT 传感器
  dht.begin();

  // 初始化 CO2 串口（9600 8N1），仅 RX 使用 38
  co2Serial.begin(9600, SERIAL_8N1, CO2_UART_RX, CO2_UART_TX);
  Serial.print("CO2 UART(0) fixed RX="); Serial.print(CO2_UART_RX);
  Serial.print(" TX="); Serial.print(CO2_UART_TX);
  Serial.println(" @9600 passive frames");

  // 若需要板载 LED 指示，请在此处设置对应引脚
}

void loop() {
  // 每次循环都先处理 CO2 串口数据，保持实时性
  static size_t lastReportedCo2BufLen = 0;
  size_t beforeLen = co2BufLen;
  while (co2Serial.available()) {
    uint8_t b = (uint8_t)co2Serial.read();
    if (co2BufLen < sizeof(co2Buf)) co2Buf[co2BufLen++] = b;
    lastByteMillis = millis();
  }
  if (co2BufLen != beforeLen) {
    Serial.print("Passive CO2 bytes received: +"); Serial.print(co2BufLen - beforeLen);
    Serial.print(" total="); Serial.println(co2BufLen);
    size_t dumpN = co2BufLen < 16 ? co2BufLen : 16;
    Serial.print("Buf head: ");
    for (size_t z = 0; z < dumpN; z++) {
      if (co2Buf[z] < 16) Serial.print('0');
      Serial.print(co2Buf[z], HEX);
      Serial.print(' ');
    }
    Serial.println();
    lastReportedCo2BufLen = co2BufLen;
  }
  processCo2Buffer();

  uint32_t now = millis();
  static uint32_t lastHeartbeat = 0;
  if (now - lastHeartbeat > 2000) {
    lastHeartbeat = now;
    Serial.print("Heartbeat @"); Serial.println(now);
  }
  // DHT 与显示刷新周期：每 1 秒（传感器也每 1 秒输出）
  if (now - lastUpdateMs > 1000) {
    lastUpdateMs = now;
    // 解析已接收 CO2 数据（仅 16 字节帧）
    Serial.print("Before parse co2ppm="); Serial.println(co2ppm);
    processCo2Buffer();
    Serial.print("After parse co2ppm="); Serial.println(co2ppm);

    // 从 DHT 读取真实温湿度
    float h = dht.readHumidity();
    float t = dht.readTemperature(); // 默认摄氏

    // 调试输出：打印原始读取数据
    Serial.print("DHT read -> t="); Serial.print(t);
    Serial.print(" h="); Serial.print(h);
    if (isnan(t) || isnan(h)) Serial.println(" (NaN)"); else Serial.println();

    // 若读取失败则保持上次值
    if (!isnan(t)) {
      temperatureC = t;
    }
    if (!isnan(h)) {
      humidityPct = h;
    }

    Serial.print("Using values -> Temp="); Serial.print(temperatureC);
    Serial.print(" Hum="); Serial.println(humidityPct);

    String d = formatDate();
    updateDateIfNeeded(d);
    Serial.print("Display update CO2="); Serial.println(co2ppm);
    updateValues(co2ppm, temperatureC, humidityPct);

    // 无字节接收超时指示（>3 秒完全无字节）
    if (layoutInited && (millis() - lastByteMillis > 3000)) {
      tft.setTextSize(gOtherLineSize); tft.setTextColor(RED);
      // 在右侧显示，避免覆盖 CO2 区域
      int16_t rxX = tft.width() - 6 * gOtherLineSize * 8; // 预留8字符宽度
      tft.fillRect(rxX, yCo2Top, 6 * gOtherLineSize * 8, otherH, BLACK);
      tft.setCursor(rxX, yCo2Top);
      tft.print(" ");
    }
  }
}