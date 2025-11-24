// 四行数据显示：日期/CO2/温度/湿度，屏幕方向上下颠倒
#include <Arduino.h>
#include <ST7789_AVR.h>
#include "display_helper.h"  // 使用新的display_helper.h
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
#define CO2_UART_RX 38
#define CO2_UART_TX -1  // 未使用
HardwareSerial co2Serial(0);

// CO2 串口帧缓冲
static uint8_t co2Buf[64];
static size_t co2BufLen = 0;
static uint32_t co2FrameCount = 0;
static uint8_t lastFrame[16];
static uint32_t lastFrameMillis = 0;
static uint32_t lastByteMillis = 0;
static uint32_t rxRetryCount = 0;

// 临时串口自检开关
// #define SERIAL_TEST 0

// 字体尺寸（与display_helper.h保持一致）
uint8_t gFirstLineSize = 3;
uint8_t gOtherLineSize = 3;

// 全局变量定义（在display_helper.h中extern声明）
bool layoutInited = false;
DisplayState displayState;
int16_t yDate, yLine, yCo2, yTemp, yHum, xUnit;

// 状态数据
static uint32_t co2ppm = 450;
static float temperatureC = 25.3f;
static float humidityPct = 48.5f;
static uint32_t lastUpdateMs = 0;

// 持久化 boot 计数
RTC_DATA_ATTR static uint32_t bootCount = 0;

// 将 __DATE__ 解析为 YYYY-MM-DD
String formatDate() {
  const char *dateStr = __DATE__;
  char monStr[4];
  int day, year;
  sscanf(dateStr, "%3s %d %d", monStr, &day, &year);
  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int monthNum = (strstr(months, monStr) - months) / 3 + 1;
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, monthNum, day);
  return String(buf);
}

// 仅解析 16 字节帧：格式
// BYTE0=0x42, BYTE1=0x4D, BYTE2..BYTE14=数据内容, BYTE15= (BYTE0+...+BYTE14) & 0xFF
// CO2 浓度 = BYTE6 * 256 + BYTE7
void processCo2Buffer() {
  while (co2BufLen >= 16) {
    if (!(co2Buf[0] == 0x42 && co2Buf[1] == 0x4D)) {
      memmove(co2Buf, co2Buf + 1, co2BufLen - 1);
      co2BufLen -= 1;
      continue;
    }
    
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
    
    if (co2BufLen > 16) memmove(co2Buf, co2Buf + 16, co2BufLen - 16);
    co2BufLen -= 16;
  }
}

void setup() {
  bootCount++;
  Serial.begin(9600);
  delay(200);
  Serial.print("Serial started @9600, bootCount="); Serial.println(bootCount);
  
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.print("Reset reason: "); Serial.println((int)rr);

  #ifdef SERIAL_TEST
  Serial.println("SERIAL_TEST is enabled.");
  #endif

  // SPI初始化
  SPI.begin(18, -1, 15, PIN_CS);

  tft.init(172, 320);
  tft.setRotation(3);  // 上下颠倒
  Serial.println("TFT initialized");
  Serial.print("Boot millis= "); Serial.println(millis());
  
  // 使用新的显示初始化函数
  String currentDate = formatDate();
  initDisplayLayout(currentDate);
  
  delay(3500);

  // 初始化传感器
  dht.begin();
  co2Serial.begin(9600, SERIAL_8N1, CO2_UART_RX, CO2_UART_TX);
  
  Serial.print("CO2 UART(0) fixed RX="); Serial.print(CO2_UART_RX);
  Serial.print(" TX="); Serial.print(CO2_UART_TX);
  Serial.println(" @9600 passive frames");
}

void loop() {
  // 处理CO2串口数据
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
  
  // 每秒更新显示
  if (now - lastUpdateMs > 1000) {
    lastUpdateMs = now;
    
    Serial.print("CO2 value: "); Serial.println(co2ppm);

    // 读取DHT传感器数据
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    Serial.print("DHT read -> t="); Serial.print(t);
    Serial.print(" h="); Serial.print(h);
    if (isnan(t) || isnan(h)) Serial.println(" (NaN)"); else Serial.println();

    if (!isnan(t)) temperatureC = t;
    if (!isnan(h)) humidityPct = h;

    Serial.print("Using values -> Temp="); Serial.print(temperatureC);
    Serial.print(" Hum="); Serial.println(humidityPct);

    // 使用新的显示更新函数（自动处理位级更新）
    String currentDate = formatDate();
    updateDisplay(currentDate, co2ppm, temperatureC, humidityPct);

  }
}