# ESP32S3 驱动 ST7789 屏幕硬件连接与代码适配说明

---

## 1. 硬件连接（引脚配置）

ESP32S3 的 SPI 引脚可自由分配，推荐如下（以常见 1.3/1.54/1.69/1.9/2.0/2.8 英寸 ST7789 屏为例）：

| LCD引脚 | 名称 | ESP32S3引脚建议 | 说明 |
|---------|------|----------------|------|
| 1       | GND  | GND            | 地   |
| 2       | VCC  | 3.3V           | 仅能用3.3V供电 |
| 3       | SCL  | 18             | SPI SCK（可自定义）|
| 4       | SDA  | 15             | SPI MOSI（可自定义）|
| 5       | RES  | 5              | 复位（任意GPIO）|
| 6       | DC   | 16             | 数据/命令（任意GPIO）|
| 7       | CS   | 17             | 片选（任意GPIO，若无CS则接GND）|
| 8       | BLK  | 3.3V           | 背光（可接3.3V或PWM）|

> 你可根据实际板卡和项目需要更换引脚，只需在代码中对应修改。

---

## 2. 代码修改与兼容性设置

### 2.1 头文件宏定义

- **开启兼容模式**：在 `ST7789_AVR.h` 文件顶部取消注释 `#define COMPATIBILITY_MODE`，确保如下：
  ```cpp
  #define COMPATIBILITY_MODE
  ```
- **CS_ALWAYS_LOW**：如你的屏幕没有CS引脚，取消注释 `#define CS_ALWAYS_LOW`。

### 2.2 示例代码（适配ESP32S3）

```cpp
#include <ST7789_AVR.h>

// 根据你的实际连线修改以下引脚号
#define PIN_DC   16
#define PIN_RST  5
#define PIN_CS   17 // 若无CS引脚则填-1，并在头文件定义CS_ALWAYS_LOW

ST7789_AVR tft(PIN_DC, PIN_RST, PIN_CS);

void setup() {
  // ESP32S3需手动指定SPI引脚
  SPI.begin(18, -1, 23, PIN_CS); // SCK, MISO(不用), MOSI, SS
  tft.begin();
  tft.fillScreen(BLACK);
  tft.setRotation(2);
  tft.setTextColor(WHITE);
  tft.setCursor(10, 10);
  tft.print("Hello ST7789!");
}

void loop() {
  // 你的显示逻辑
}
```

### 2.3 其他注意事项

- **库依赖**：需安装 Adafruit GFX Library。
- **SPI速度**：ESP32S3可支持更高SPI频率，默认兼容模式下已足够快。
- **PROGMEM/pgm_read_byte**：ESP32/ESP32S3 支持 PROGMEM，但访问方式与 AVR 不同。ST7789_AVR 兼容模式下已自动适配。

---

## 3. 代码分析与修改点说明

- 只需在 `ST7789_AVR.h` 取消 `#define COMPATIBILITY_MODE` 的注释。
- 示例代码中 SPI.begin() 需手动指定 SCK/MOSI/CS 引脚。
- 其余 API（如 fillScreen、drawPixel、drawImage、setRotation 等）与 AVR 用法完全一致，无需修改。
- 若需更高性能，可考虑直接使用 ESP32 专用的 ST7789 库（如 TFT_eSPI），但本库在兼容模式下也能正常驱动。

---

## 4. 完整实现示例

假设你用 1.3/1.54/1.69/1.9/2.0/2.8 英寸 ST7789 屏，连线如上，代码如下：

```cpp
#include <ST7789_AVR.h>
#include <SPI.h>

// 修改为你的实际引脚
#define PIN_DC   16
#define PIN_RST  5
#define PIN_CS   17

ST7789_AVR tft(PIN_DC, PIN_RST, PIN_CS);

void setup() {
  // ESP32S3 SPI引脚初始化
  SPI.begin(18, -1, 23, PIN_CS); // SCK, MISO, MOSI, SS
  tft.begin();
  tft.fillScreen(BLACK);
  tft.setRotation(2);
  tft.setTextColor(WHITE);
  tft.setCursor(10, 10);
  tft.print("Hello ST7789!");
  delay(1000);

  // 画图演示
  tft.fillRect(20, 20, 100, 50, RED);
  tft.drawFastVLine(60, 0, 240, GREEN);
  tft.drawFastHLine(0, 120, 240, BLUE);
  tft.invertDisplay(true);
  delay(500);
  tft.invertDisplay(false);
}

void loop() {
  // 可添加更多显示逻辑
}
```

---

## 5. 总结

- 只需开启 `COMPATIBILITY_MODE`，并在 `SPI.begin()` 指定引脚即可。
- 其余 API 与 AVR 用法一致，所有显示功能均可用。
- 推荐使用 3.3V 供电，避免损坏屏幕。

如需更复杂的显示功能（如图片、字体、滚动等），可参考库自带 `examples` 目录，代码无需特殊修改即可在 ESP32S3 上运行。
