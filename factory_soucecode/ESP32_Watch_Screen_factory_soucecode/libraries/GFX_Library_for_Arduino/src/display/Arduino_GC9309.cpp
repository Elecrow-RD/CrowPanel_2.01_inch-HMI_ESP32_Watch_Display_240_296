#include "Arduino_GC9309.h"
#include "SPI.h"

Arduino_GC9309::Arduino_GC9309(
    Arduino_DataBus *bus, int8_t rst, uint8_t r,
    bool ips, int16_t w, int16_t h,
    uint8_t col_offset1, uint8_t row_offset1, uint8_t col_offset2, uint8_t row_offset2)
    : Arduino_TFT(bus, rst, r, ips, w, h, col_offset1, row_offset1, col_offset2, row_offset2)
{
}

bool Arduino_GC9309::begin(int32_t speed)
{
  _override_datamode = SPI_MODE0; // always use SPI_MODE0

  return Arduino_TFT::begin(speed);
}

void Arduino_GC9309::writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h)
{
  if ((x != _currentX) || (w != _currentW) || (y != _currentY) || (h != _currentH))
  {
    _bus->writeC8D16D16(GC9309_CASET, x + _xStart, x + w - 1 + _xStart);
    _bus->writeC8D16D16(GC9309_RASET, y + _yStart, y + h - 1 + _yStart);

    _currentX = x;
    _currentY = y;
    _currentW = w;
    _currentH = h;
  }

  _bus->writeCommand(GC9309_RAMWR); // write to RAM
}

/**************************************************************************/
/*!
    @brief   Set origin of (0,0) and orientation of TFT display
    @param   m  The index for rotation, from 0-3 inclusive
*/
/**************************************************************************/
void Arduino_GC9309::setRotation(uint8_t r)
{
  Arduino_TFT::setRotation(r);
  switch (_rotation % 4)
  {
    case 1: // Landscape (Portrait + 90°)
      r = GC9309_MADCTL_MV | GC9309_MADCTL_RGB;
      break;
    case 2: // Inverter Portrait
      r = GC9309_MADCTL_MY | GC9309_MADCTL_RGB;
      break;
    case 3: // Inverted Landscape
      r = GC9309_MADCTL_MX | GC9309_MADCTL_MY | GC9309_MADCTL_MV | GC9309_MADCTL_RGB;
      break;
    default: // case 0: (Portrait)
      r = GC9309_MADCTL_MX | GC9309_MADCTL_RGB;
      break;
  }
  _bus->beginWrite();
  _bus->writeC8D8(GC9309_MADCTL, r);
  _bus->endWrite();
}

void Arduino_GC9309::invertDisplay(bool i)
{
  _bus->sendCommand(_ips ? (i ? GC9309_INVOFF : GC9309_INVON) : (i ? GC9309_INVON : GC9309_INVOFF));
}

void Arduino_GC9309::displayOn(void)
{
  _bus->sendCommand(GC9309_SLPOUT);
  delay(GC9309_SLPOUT_DELAY);
}

void Arduino_GC9309::displayOff(void)
{
  _bus->sendCommand(GC9309_SLPIN);
  delay(GC9309_SLPIN_DELAY);
}

void Arduino_GC9309::tftInit()
{
  if (_rst != GFX_NOT_DEFINED)
  {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH);
    delay(GC9309_RST_DELAY);
    digitalWrite(_rst, LOW);
    delay(GC9309_RST_DELAY);
    digitalWrite(_rst, HIGH);
    delay(GC9309_RST_DELAY);
  }
  else
  {
    // Software Rest
  }

  _bus->batchOperation(GC9309_init_operations, sizeof(GC9309_init_operations));

  invertDisplay(false);
}
