// Display helpers: layout initialization and partial updates
#ifndef DISPLAY_HELPER_H
#define DISPLAY_HELPER_H

#include <Arduino.h>
#include <ST7789_AVR.h>

// Extern globals from main.cpp
extern ST7789_AVR tft;
extern uint8_t gFirstLineSize;
extern uint8_t gOtherLineSize;
extern int16_t yDateTop, yLineSep, yCo2Top, yTempTop, yHumTop;
extern int16_t xCo2Digits, xTempDigits, xHumDigits;
extern int16_t xCo2Unit; // 单位起始 X，用于只重绘数字区域
extern int16_t co2DigitRegionW, tempDigitRegionW, humDigitRegionW;
extern uint8_t firstH, otherH;
extern String prevDate;
extern uint32_t prevCo2;
extern int prevTempScaled, prevHumScaled;
extern int prevCo2Chars, prevTempChars, prevHumChars;
extern bool layoutInited;
extern String prevCo2Str, prevTempStr, prevHumStr;

static inline void initLayout(const String &date) {
  uint16_t screenH = tft.height();
  firstH = 8 * gFirstLineSize;
  otherH = 8 * gOtherLineSize;
  uint16_t fixedLines = firstH + 3 * otherH + 2;
  uint16_t segments = 5;
  int16_t remain = (int16_t)screenH - fixedLines;
  int16_t seg = remain / segments; if(seg<0) seg=0;

  tft.fillRect(0,0,tft.width(),screenH,BLACK);
  // 日期
  tft.setTextSize(gFirstLineSize); tft.setTextColor(WHITE);
  uint16_t datePixelW = date.length()*6*gFirstLineSize;
  int16_t dateX = (int16_t)(tft.width()-datePixelW)/2; if(dateX<0) dateX=0;
  yDateTop = seg; tft.setCursor(dateX,yDateTop); tft.print(date); prevDate = date;
  // 分隔线
  yLineSep = yDateTop + firstH + seg/2; if(yLineSep+1>=screenH) yLineSep=screenH-3;
  tft.drawFastHLine(0,yLineSep,tft.width(),GREY); tft.drawFastHLine(0,yLineSep+1,tft.width(),GREY);

  // CO2 标签 + 数字 + 单位
  tft.setTextSize(gOtherLineSize); tft.setTextColor(YELLOW);
  yCo2Top = yLineSep + 2 + seg; tft.setCursor(6,yCo2Top); tft.print("CO2 : ");
  xCo2Digits = tft.getCursorX();
  // 预先计算字符宽与单位宽度，固定单位在屏幕右侧位置，数字在单位左侧区域右对齐
  int charW = 6 * gOtherLineSize;
  String co2Str = String(prevCo2==0xFFFFFFFF?450:prevCo2);
  String unitStr = " ppm";
  int unitW = unitStr.length() * charW;
  int rightMargin = 6; // 距右边留白像素数
  xCo2Unit = tft.width() - rightMargin - unitW;
  if (xCo2Unit < xCo2Digits + charW) {
    // 保护性处理，避免单位位置小于数字起始位置
    xCo2Unit = xCo2Digits + charW;
  }
  // 将数字区域宽度设置为单位起始位置与数字起始位置的差值
  co2DigitRegionW = xCo2Unit - xCo2Digits;
  // 右对齐数字并绘制初始值
  int digitsW = co2Str.length() * charW;
  int digitsX = xCo2Unit - digitsW;
  if (digitsX < xCo2Digits) digitsX = xCo2Digits;
  tft.setCursor(digitsX, yCo2Top); tft.print(co2Str);
  prevCo2 = co2Str.toInt(); prevCo2Chars = co2Str.length(); prevCo2Str = co2Str;
  // 绘制固定单位
  tft.setCursor(xCo2Unit, yCo2Top); tft.print(unitStr);

  // 温度
  tft.setTextColor(CYAN);
  yTempTop = yCo2Top + otherH + seg; tft.setCursor(6,yTempTop); tft.print("Temp: ");
  xTempDigits = tft.getCursorX();
  String tempStr = String(25.3f,1);
  tft.print(tempStr); prevTempScaled = (int)(25.3f*10+0.5f); prevTempChars = tempStr.length(); prevTempStr = tempStr;
  int charWTemp = 6 * gOtherLineSize;
  int16_t degX = xTempDigits + prevTempChars * charWTemp + charWTemp;
  for(int dy=0; dy<6; dy++){ for(int dx=0; dx<6; dx++){ int rx=dx-3, ry=dy-3; int r2=rx*rx+ry*ry; if(r2>=3 && r2<=6) tft.drawPixel(degX+dx,yTempTop+dy,CYAN);} }
  tft.setCursor(degX+8,yTempTop); tft.print("C");
  tempDigitRegionW = 5 * 6 * gOtherLineSize;

  // 湿度
  tft.setTextColor(MAGENTA);
  yHumTop = yTempTop + otherH + seg; tft.setCursor(6,yHumTop); tft.print("Humi: ");
  xHumDigits = tft.getCursorX();
  String humStr = String(48.5f,1);
  tft.print(humStr); prevHumScaled = (int)(48.5f*10+0.5f); prevHumChars = humStr.length(); prevHumStr = humStr;
  tft.print(" %");
  humDigitRegionW = 5 * 6 * gOtherLineSize;

  layoutInited = true;
}

static inline void updateValues(uint32_t co2, float tempC, float humPct) {
  if(!layoutInited) return;
  int tempScaled = (int)(tempC*10+0.5f);
  int humScaled  = (int)(humPct*10+0.5f);
  int charW = 6 * gOtherLineSize;
  // 强制整行重绘 CO2（调试阶段，排除差分逻辑导致不更新）
  {
    String newStr = String(co2);
    int newLen = newStr.length();
    int charW = 6 * gOtherLineSize;
    // 仅清除数字区域（从数字起始到单位起始），避免擦掉静态单位。
    // 数字采用右对齐：数字起始 X = xCo2Unit - requiredDigitsW
    int requiredDigitsW = newLen * charW;
    int digitsX = xCo2Unit - requiredDigitsW;
    if (digitsX < xCo2Digits) digitsX = xCo2Digits;
    // 为避免残留，清除整个数字区域（保证覆盖之前更长的数字）
    tft.fillRect(xCo2Digits-1, yCo2Top, co2DigitRegionW+2, otherH, BLACK);
    tft.setTextSize(gOtherLineSize); tft.setTextColor(YELLOW); tft.setCursor(digitsX,yCo2Top);
    tft.print(newStr);
    prevCo2 = co2; prevCo2Chars = newLen; prevCo2Str = newStr;
    // 如果数字溢出分配区域，重绘单位以确保可见性
    if (requiredDigitsW > co2DigitRegionW) {
      tft.setTextSize(gOtherLineSize); tft.setTextColor(YELLOW); tft.setCursor(xCo2Unit, yCo2Top);
      tft.print(" ppm");
    }
  }
  if(tempScaled != prevTempScaled){
    String newStr = String(tempC,1);
    int newLen = newStr.length();
    if(newLen != prevTempChars){
      tft.fillRect(xTempDigits, yTempTop, tempDigitRegionW, otherH, BLACK);
      tft.setTextSize(gOtherLineSize); tft.setTextColor(CYAN); tft.setCursor(xTempDigits,yTempTop); tft.print(newStr);
    } else {
      for(int i=0;i<newLen;i++){
        if(newStr[i] != prevTempStr[i]){
          int16_t dx = xTempDigits + i*charW;
          tft.fillRect(dx, yTempTop, charW, otherH, BLACK);
          tft.setTextSize(gOtherLineSize); tft.setTextColor(CYAN); tft.setCursor(dx,yTempTop); tft.print(newStr[i]);
        }
      }
    }
    int16_t digitsEndX = xTempDigits + prevTempChars * charW;
    tft.fillRect(digitsEndX, yTempTop, charW*2, otherH, BLACK);
    int16_t newDegX = xTempDigits + newLen * charW + charW;
    for(int dy=0; dy<6; dy++){ for(int dx=0; dx<6; dx++){ int rx=dx-3, ry=dy-3; int r2=rx*rx+ry*ry; if(r2>=3 && r2<=6) tft.drawPixel(newDegX+dx,yTempTop+dy,CYAN);} }
    tft.setTextSize(gOtherLineSize); tft.setTextColor(CYAN); tft.setCursor(newDegX+8,yTempTop); tft.print("C");
    prevTempScaled = tempScaled; prevTempChars = newLen; prevTempStr = newStr;
  }
  if(humScaled != prevHumScaled){
    String newStr = String(humPct,1);
    int newLen = newStr.length();
    if(newLen != prevHumChars){
      tft.fillRect(xHumDigits, yHumTop, humDigitRegionW, otherH, BLACK);
      tft.setTextSize(gOtherLineSize); tft.setTextColor(MAGENTA); tft.setCursor(xHumDigits,yHumTop); tft.print(newStr);
    } else {
      for(int i=0;i<newLen;i++){
        if(newStr[i] != prevHumStr[i]){
          int16_t dx = xHumDigits + i*charW;
          tft.fillRect(dx, yHumTop, charW, otherH, BLACK);
          tft.setTextSize(gOtherLineSize); tft.setTextColor(MAGENTA); tft.setCursor(dx,yHumTop); tft.print(newStr[i]);
        }
      }
    }
    prevHumScaled = humScaled; prevHumChars = newLen; prevHumStr = newStr;
  }
}

static inline void updateDateIfNeeded(const String &date){
  if(!layoutInited) return;
  if(date != prevDate){
    uint16_t datePixelW = date.length()*6*gFirstLineSize;
    tft.fillRect(0,yDateTop,tft.width(),firstH,BLACK);
    int16_t dateX = (int16_t)(tft.width()-datePixelW)/2; if(dateX<0) dateX=0;
    tft.setTextSize(gFirstLineSize); tft.setTextColor(WHITE); tft.setCursor(dateX,yDateTop); tft.print(date);
    tft.drawFastHLine(0,yLineSep,tft.width(),GREY); tft.drawFastHLine(0,yLineSep+1,tft.width(),GREY);
    prevDate = date;
  }
}

#endif // DISPLAY_HELPER_H
