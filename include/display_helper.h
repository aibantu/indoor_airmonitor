// 优化版显示助手 - 位级更新，日期低频率更新
#ifndef DISPLAY_OPTIMIZED_H
#define DISPLAY_OPTIMIZED_H

#include <Arduino.h>
#include <ST7789_AVR.h>

extern ST7789_AVR tft;
extern uint8_t gFirstLineSize;
extern uint8_t gOtherLineSize;
extern bool layoutInited;

// 显示状态记录
struct DisplayState {
  String date = "";
  uint32_t co2 = 0;
  float temp = 0;
  float hum = 0;
  String co2Str = "----";
  String tempStr = "--.-";
  String humStr = "--.-";
  unsigned long lastDateUpdate = 0; // 上次日期更新时间
};


extern DisplayState displayState;
extern int16_t yDate, yLine, yCo2, yTemp, yHum, xUnit;

// 初始化布局（只绘制静态内容）
static inline void initDisplayLayout(const String& date) {
  if (layoutInited) return;
  
  uint16_t w = tft.width(), h = tft.height();
  tft.fillScreen(BLACK);
  // tft.fillRect(0,0,w,h,GREEN);
  
  // 计算布局
  uint8_t titleH = 8 * gFirstLineSize;
  uint8_t dataH = 8 * gOtherLineSize;
  int16_t spacing = (h - (titleH + 3 * dataH + 2)) / 5;
  
  yDate = spacing;
  yLine = yDate + titleH + spacing/2;
  yCo2 = yLine + 2 + spacing;
  yTemp = yCo2 + dataH + spacing;
  yHum = yTemp + dataH + spacing;
  xUnit = w - 7 * 6 * gOtherLineSize; // 单位固定位置,6个字符
  
  // 绘制静态标签和单位（只绘制一次）
  tft.setTextSize(gFirstLineSize);
  tft.setTextColor(WHITE);
  int16_t dateX = (w - date.length() * 6 * gFirstLineSize) / 2;
  tft.setCursor(dateX < 0 ? 0 : dateX, yDate);
  tft.print(date);
  displayState.date = date;
  
  // 分割线
  tft.drawFastHLine(0, yLine, w, GREY);
  tft.drawFastHLine(0, yLine+1, w, GREY);
  
  // 数据行标签和单位（静态，只绘制一次）
  tft.setTextSize(gOtherLineSize);
  
  // CO2行
  tft.setCursor(6, yCo2); 
  tft.setTextColor(YELLOW); 
  tft.print("CO2 : ");
  int16_t co2InitX = xUnit - 4 * 6 * gOtherLineSize - 6 * 2; // 初始显示位置
  tft.setCursor(co2InitX, yCo2);
  tft.print("----"); // 初始显示"----"
  tft.setCursor(xUnit, yCo2); 
  tft.print("ppm");
  
  
  // 温度行
  tft.setCursor(6, yTemp); 
  tft.setTextColor(CYAN); 
  tft.print("Temp: ");
  tft.setCursor(xUnit, yTemp);
  int16_t tempInitX = xUnit - 4 * 6 * gOtherLineSize - 6 * 2; // 初始显示位置
  tft.setCursor(tempInitX, yTemp);
  tft.print("--.-"); // 初始显示"--.-"
  // 温度度符号
  for (int dy = 0; dy < 6; dy++) {
    for (int dx = 0; dx < 6; dx++) {
      int rx = dx-3, ry = dy-3;
      if (rx*rx + ry*ry >= 3 && rx*rx + ry*ry <= 6) {
        tft.drawPixel(xUnit+dx, yTemp+dy, CYAN);
      }
    }
  }
  tft.setCursor(xUnit+8, yTemp); 
  tft.print("C");
  
  // 湿度行
  tft.setCursor(6, yHum); 
  tft.setTextColor(MAGENTA); 
  tft.print("Humi: ");
  int16_t humInitX = xUnit - 4 * 6 * gOtherLineSize - 6 * 2; // 初始显示位置
  tft.setCursor(humInitX, yHum);
  tft.print("--.-"); // 初始显示"--.-"
  tft.setCursor(xUnit, yHum); 
  tft.print("%");
  
  layoutInited = true;
}

// 更新单个字符（位级更新）
static inline void updateChar(int16_t x, int16_t y, uint16_t color, char oldChar, char newChar, uint8_t textSize = 1) {
  if (oldChar != newChar) {
    int charW = 6 * textSize;
    int charH = 8 * textSize;
    tft.fillRect(x, y, charW, charH, BLACK);
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(newChar);
  }
}

// 更新数值显示（位级更新）
static inline void updateValue(int16_t x, int16_t y, uint16_t color, const String& oldStr, const String& newStr, uint8_t textSize) {
  int charW = 6 * textSize;
  int charH = 8 * textSize;
  // int maxChars = 6; // 最大字符数
  
  if (oldStr.length() != newStr.length()) {
    // 长度变化，整块更新
    int maxChars = max(oldStr.length(), newStr.length())  * charW;
    tft.fillRect(xUnit - maxChars - 6 * 2, y, maxChars, charH, BLACK);
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(newStr);
  } else {
    // 长度相同，逐字符更新
    for (int i = 0; i < newStr.length(); i++) {
      if (oldStr[i] != newStr[i]) {
        updateChar(x + i * charW, y, color, oldStr[i], newStr[i], textSize);
      }
    }
  }
}

// 主更新函数 - 日期每1小时更新一次，其他数值位级更新
static inline void updateDisplay(const String& date, uint32_t co2, float temp, float hum) {
  if (!layoutInited) initDisplayLayout(date);
  
  int charW = 6 * gOtherLineSize;
  unsigned long currentTime = millis();
  
  // 1. 更新日期（每1小时检查一次，位级更新）
  if (date != displayState.date && (currentTime - displayState.lastDateUpdate >= 3600000)) {
    int16_t dateX = (tft.width() - date.length() * 6 * gFirstLineSize) / 2;
    int16_t oldDateX = (tft.width() - displayState.date.length() * 6 * gFirstLineSize) / 2;    
    updateValue(dateX, yDate, WHITE, displayState.date, date, gFirstLineSize);
    
    displayState.date = date;
    displayState.lastDateUpdate = currentTime;
  }
  
  // 2. 更新CO2（数值右对齐，位级更新）
  if (co2 != displayState.co2 ) {
    String newCo2 = String(co2);
    int digitsW = newCo2.length() * charW;
    int co2X = xUnit - digitsW - 6 * 2;
    
    updateValue(co2X, yCo2, YELLOW, displayState.co2Str, newCo2, gOtherLineSize);
    displayState.co2 = co2;
    displayState.co2Str = newCo2;
  }
  
  // 3. 更新温度（位级更新）
  if (temp != displayState.temp) {
    String newTemp = String(temp, 1);
    int digitsW = newTemp.length() * charW;
    int tempX = xUnit - digitsW - 6 * 2;
    
    updateValue(tempX, yTemp, CYAN, displayState.tempStr, newTemp, gOtherLineSize);
    displayState.temp = temp;
    displayState.tempStr = newTemp;
  }
  
  // 4. 更新湿度（位级更新）
  if (hum != displayState.hum) {
    String newHum = String(hum, 1);
    int digitsW = newHum.length() * charW;
    int humX = xUnit - digitsW - 6 * 2;
    
    updateValue(humX, yHum, MAGENTA, displayState.humStr, newHum, gOtherLineSize);
    displayState.hum = hum;
    displayState.humStr = newHum;
  }
}

#endif // DISPLAY_OPTIMIZED_H