// Fast ST7789 IPS 240x240 SPI display library
// (c) 2019-24 by Pawel A. Hernik

#include "ST7789_AVR.h"
#include <SPI.h>

/*
Changes:
- fixed drawPixel window
- fixed rgbWheel
- fixed setRotation
- fixed fillRect for w*h>64k
- added support for 240x280 and 170x320
- added clipping for negative x,y
*/

#define ST7789_NOP     0x00
#define ST7789_SWRESET 0x01
#define ST7789_SLPIN   0x10
#define ST7789_SLPOUT  0x11
#define ST7789_PTLON   0x12
#define ST7789_NORON   0x13
#define ST7789_INVOFF  0x20
#define ST7789_INVON   0x21
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON  0x29
#define ST7789_IDMOFF  0x38
#define ST7789_IDMON   0x39
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_RAMRD   0x2E
#define ST7789_COLMOD  0x3A
#define ST7789_MADCTL  0x36
#define ST7789_PTLAR    0x30
#define ST7789_VSCRDEF  0x33
#define ST7789_VSCRSADD 0x37
#define ST7789_WRDISBV  0x51
#define ST7789_WRCTRLD  0x53
#define ST7789_WRCACE   0x55
#define ST7789_WRCABCMB 0x5e
#define ST7789_POWSAVE    0xbc
#define ST7789_DLPOFFSAVE 0xbd

#define ST7789_MADCTL_MY  0x80
#define ST7789_MADCTL_MX  0x40
#define ST7789_MADCTL_MV  0x20
#define ST7789_MADCTL_ML  0x10
#define ST7789_MADCTL_RGB 0x00

#define ST7789_240x240_XSTART 0
#define ST7789_240x240_YSTART 0

#define ST_CMD_DELAY   0x80

static const uint8_t PROGMEM init_240x240[] = {
		9,
		ST7789_SWRESET,   ST_CMD_DELAY,
			150,
		ST7789_SLPOUT ,   ST_CMD_DELAY,
			255,
		ST7789_COLMOD , 1+ST_CMD_DELAY,
			0x55,
			10,
		ST7789_MADCTL , 1,
			0x00,
		ST7789_CASET  , 4,
			0x00, ST7789_240x240_XSTART,
			(ST7789_TFTWIDTH+ST7789_240x240_XSTART) >> 8,
			(ST7789_TFTWIDTH+ST7789_240x240_XSTART) & 0xFF,
		ST7789_RASET  , 4,
			0x00, ST7789_240x240_YSTART,
			(ST7789_TFTHEIGHT+ST7789_240x240_YSTART) >> 8,
			(ST7789_TFTHEIGHT+ST7789_240x240_YSTART) & 0xFF,
		ST7789_INVON ,   ST_CMD_DELAY,
			10,
		ST7789_NORON  ,   ST_CMD_DELAY,
			10,
		ST7789_DISPON ,   ST_CMD_DELAY,
			20
};

#ifdef COMPATIBILITY_MODE
static SPISettings spiSettings;
#define SPI_START  SPI.beginTransaction(spiSettings)
#define SPI_END    SPI.endTransaction()
#else
#define SPI_START
#define SPI_END
#endif

#ifdef COMPATIBILITY_MODE
#define DC_DATA     digitalWrite(dcPin, HIGH)
#define DC_COMMAND  digitalWrite(dcPin, LOW)
#define CS_IDLE     digitalWrite(csPin, HIGH)
#define CS_ACTIVE   digitalWrite(csPin, LOW)
#else
#define DC_DATA    *dcPort |= dcMask
#define DC_COMMAND *dcPort &= ~dcMask
#define CS_IDLE    *csPort |= csMask
#define CS_ACTIVE  *csPort &= ~csMask
#endif

#ifdef CS_ALWAYS_LOW
#define CS_IDLE
#define CS_ACTIVE
#endif

inline void ST7789_AVR::writeSPI(uint8_t c) {
#ifdef COMPATIBILITY_MODE
	SPI.transfer(c);
#else
	SPDR = c;
	asm volatile("rjmp .+0\n");
	asm volatile("rjmp .+0\n");
	asm volatile("rjmp .+0\n");
	asm volatile("rjmp .+0\n");
#endif
}

inline void ST7789_AVR::writeMulti(uint16_t color, uint16_t num) {
#ifdef COMPATIBILITY_MODE
	while(num--) { SPI.transfer(color>>8); SPI.transfer(color); }
#else
	// AVR optimized path omitted for ESP32
#endif
}

inline void ST7789_AVR::copyMulti(uint8_t *img, uint16_t num) {
#ifdef COMPATIBILITY_MODE
	while(num--) { SPI.transfer(*(img+1)); SPI.transfer(*img); img+=2; }
#else
	// AVR optimized path omitted
#endif
}

void ST7789_AVR::writeCmd(uint8_t c) {
	DC_COMMAND; CS_ACTIVE; SPI_START; writeSPI(c); CS_IDLE; SPI_END;
}

void ST7789_AVR::writeData(uint8_t d8) {
	DC_DATA; CS_ACTIVE; SPI_START; writeSPI(d8); CS_IDLE; SPI_END;
}

void ST7789_AVR::writeData16(uint16_t d16) {
	DC_DATA; CS_ACTIVE; SPI_START; writeMulti(d16,1); CS_IDLE; SPI_END;
}

ST7789_AVR::ST7789_AVR(int8_t dc, int8_t rst, int8_t cs) : Adafruit_GFX(ST7789_TFTWIDTH, ST7789_TFTHEIGHT) {
	csPin=cs; dcPin=dc; rstPin=rst;
}

void ST7789_AVR::init(uint16_t wd, uint16_t ht) {
	commonST7789Init(NULL);
	if(wd==240 && ht==280) { xstart=0; ystart=20; xend=0; yend=20; }
	else if(wd==240 && ht==240) { xstart=0; ystart=80; xend=0; yend=0; }
	else if(wd==170 && ht==320) { xstart=35; ystart=0; xend=35; yend=0; }
	else if(wd==172 && ht==320) { xstart=34; ystart=0; xend=34; yend=0; }
	else { xstart=0; ystart=0; xend=0; yend=0; }
	xoffs=yoffs=0; _width=_widthIni=wd; _height=_heightIni=ht;
	displayInit(init_240x240); setRotation(2);
}

void ST7789_AVR::displayInit(const uint8_t *addr) {
	uint8_t numCommands,numArgs; uint16_t ms; numCommands=pgm_read_byte(addr++);
	while(numCommands--) {
		writeCmd(pgm_read_byte(addr++)); numArgs=pgm_read_byte(addr++); ms=numArgs & ST_CMD_DELAY; numArgs &= ~ST_CMD_DELAY;
		while(numArgs--) writeData(pgm_read_byte(addr++)); if(ms){ ms=pgm_read_byte(addr++); if(ms==255) ms=500; delay(ms);} }
}

void ST7789_AVR::commonST7789Init(const uint8_t *cmdList) {
	pinMode(dcPin,OUTPUT);
#ifndef CS_ALWAYS_LOW
	pinMode(csPin,OUTPUT);
#endif
	SPI.begin();
#ifdef COMPATIBILITY_MODE
	spiSettings = SPISettings(16000000, MSBFIRST, SPI_MODE3);
#endif
	if(csPin>=0) { pinMode(csPin,OUTPUT); digitalWrite(csPin,LOW); }
	if(rstPin!=-1){ pinMode(rstPin,OUTPUT); digitalWrite(rstPin,HIGH); delay(50); digitalWrite(rstPin,LOW); delay(50); digitalWrite(rstPin,HIGH); delay(50); }
	if(cmdList) displayInit(cmdList);
}

void ST7789_AVR::setRotation(uint8_t m) {
	rotation = m & 3; switch(rotation){ case 0: m=ST7789_MADCTL_MX|ST7789_MADCTL_MY|ST7789_MADCTL_RGB; xoffs=xstart; yoffs=ystart; _width=_widthIni; _height=_heightIni; break; case 1: m=ST7789_MADCTL_MY|ST7789_MADCTL_MV|ST7789_MADCTL_RGB; xoffs=ystart; yoffs=xstart; _height=_widthIni; _width=_heightIni; break; case 2: m=ST7789_MADCTL_RGB; xoffs=xend; yoffs=yend; _width=_widthIni; _height=_heightIni; break; case 3: m=ST7789_MADCTL_MX|ST7789_MADCTL_MV|ST7789_MADCTL_RGB; xoffs=yend; yoffs=xend; _height=_widthIni; _width=_heightIni; break; }
	writeCmd(ST7789_MADCTL); writeData(m);
}

void ST7789_AVR::setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye) {
	xs+=xoffs; xe+=xoffs; ys+=yoffs; ye+=yoffs; CS_ACTIVE; SPI_START; DC_COMMAND; writeSPI(ST7789_CASET); DC_DATA; writeSPI(xs>>8); writeSPI(xs); writeSPI(xe>>8); writeSPI(xe); DC_COMMAND; writeSPI(ST7789_RASET); DC_DATA; writeSPI(ys>>8); writeSPI(ys); writeSPI(ye>>8); writeSPI(ye); DC_COMMAND; writeSPI(ST7789_RAMWR); DC_DATA; }

void ST7789_AVR::pushColor(uint16_t color) { SPI_START; CS_ACTIVE; writeSPI(color>>8); writeSPI(color); CS_IDLE; SPI_END; }

void ST7789_AVR::drawPixel(int16_t x,int16_t y,uint16_t color){ if(x<0||x>=_width||y<0||y>=_height) return; setAddrWindow(x,y,x,y); writeSPI(color>>8); writeSPI(color); CS_IDLE; SPI_END; }

void ST7789_AVR::drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t color){ if(x>=_width||y>=_height||h<=0) return; if(y+h>_height) h=_height-y; if(y<0){h+=y; y=0;} if(h<=0) return; setAddrWindow(x,y,x,y+h-1); writeMulti(color,h); CS_IDLE; SPI_END; }

void ST7789_AVR::drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t color){ if(x>=_width||y>=_height||w<=0) return; if(x+w>_width) w=_width-x; if(x<0){w+=x; x=0;} if(w<=0) return; setAddrWindow(x,y,x+w-1,y); writeMulti(color,w); CS_IDLE; SPI_END; }

void ST7789_AVR::fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t color){ if(x>=_width||y>=_height||w<=0||h<=0) return; if(x+w>_width) w=_width-x; if(y+h>_height) h=_height-y; if(x<0){w+=x; x=0;} if(w<=0) return; if(y<0){h+=y; y=0;} if(h<=0) return; setAddrWindow(x,y,x+w-1,y+h-1); if((long)w*h>0x10000) writeMulti(color,0); writeMulti(color,w*h); CS_IDLE; SPI_END; }

void ST7789_AVR::fillScreen(uint16_t color){ fillRect(0,0,_width,_height,color); }

void ST7789_AVR::drawImage(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t *img16){ if(w<=0||h<=0) return; setAddrWindow(x,y,x+w-1,y+h-1); copyMulti((uint8_t*)img16,w*h); CS_IDLE; SPI_END; }

void ST7789_AVR::drawImageF(int16_t x,int16_t y,int16_t w,int16_t h,const uint16_t *img16){ if(x>=_width||y>=_height||w<=0||h<=0) return; setAddrWindow(x,y,x+w-1,y+h-1); uint32_t num=(uint32_t)w*h; uint16_t num16=num>>3; uint8_t *img=(uint8_t*)img16; while(num16--){ for(uint8_t i=0;i<8;i++){ writeSPI(pgm_read_byte(img+1)); writeSPI(pgm_read_byte(img)); img+=2; } } uint8_t num8=num & 0x7; while(num8--){ writeSPI(pgm_read_byte(img+1)); writeSPI(pgm_read_byte(img)); img+=2; } CS_IDLE; SPI_END; }

uint16_t ST7789_AVR::Color565(uint8_t r,uint8_t g,uint8_t b){ return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

void ST7789_AVR::invertDisplay(boolean mode){ writeCmd(!mode ? ST7789_INVON : ST7789_INVOFF); }
void ST7789_AVR::partialDisplay(boolean mode){ writeCmd(mode ? ST7789_PTLON : ST7789_NORON); }
void ST7789_AVR::sleepDisplay(boolean mode){ writeCmd(mode ? ST7789_SLPIN : ST7789_SLPOUT); delay(5); }
void ST7789_AVR::enableDisplay(boolean mode){ writeCmd(mode ? ST7789_DISPON : ST7789_DISPOFF); }
void ST7789_AVR::idleDisplay(boolean mode){ writeCmd(mode ? ST7789_IDMON : ST7789_IDMOFF); }
void ST7789_AVR::resetDisplay(){ writeCmd(ST7789_SWRESET); delay(5); }
void ST7789_AVR::setScrollArea(uint16_t tfa,uint16_t bfa){ uint16_t vsa=320-tfa-bfa; writeCmd(ST7789_VSCRDEF); writeData16(tfa); writeData16(vsa); writeData16(bfa); }
void ST7789_AVR::setScroll(uint16_t vsp){ writeCmd(ST7789_VSCRSADD); writeData16(vsp); }
void ST7789_AVR::setPartArea(uint16_t sr,uint16_t er){ writeCmd(ST7789_PTLAR); writeData16(sr); writeData16(er); }
void ST7789_AVR::setBrightness(uint8_t br){ int val=0x04; writeCmd(ST7789_WRCTRLD); writeData(val); writeCmd(ST7789_WRDISBV); writeData(br); }
void ST7789_AVR::powerSave(uint8_t mode){ if(mode==0){ writeCmd(ST7789_POWSAVE); writeData(0xec|3); writeCmd(ST7789_DLPOFFSAVE); writeData(0xff); return; } int is=(mode&1)?0:1; int ns=(mode&2)?0:2; writeCmd(ST7789_POWSAVE); writeData(0xec|ns|is); if(mode&4){ writeCmd(ST7789_DLPOFFSAVE); writeData(0xfe); } }
void ST7789_AVR::rgbWheel(int idx,uint8_t *_r,uint8_t *_g,uint8_t *_b){ idx &= 0x1ff; if(idx < 85){ *_r=255; *_g=idx*3; *_b=0; return; } else if(idx < 85*2){ idx -= 85*1; *_r=255-idx*3; *_g=255; *_b=0; return; } else if(idx < 85*3){ idx -= 85*2; *_r=0; *_g=255; *_b=idx*3; return; } else if(idx < 85*4){ idx -= 85*3; *_r=0; *_g=255-idx*3; *_b=255; return; } else if(idx < 85*5){ idx -= 85*4; *_r=idx*3; *_g=0; *_b=255; return; } else { idx -= 85*5; if(idx>85) idx=85; *_r=255; *_g=0; *_b=255-idx*3; return; } }
uint16_t ST7789_AVR::rgbWheel(int idx){ uint8_t r,g,b; rgbWheel(idx,&r,&g,&b); return RGBto565(r,g,b); }
