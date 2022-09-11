// SPDX-License-Identifier: MIT

#include "MCP3x6x.h"

#include <wiring_private.h>

MCP3x6x::MCP3x6x(const uint16_t MCP3x6x_DEVICE_TYPE, const uint8_t pinCS, SPIClass *theSPI,
                 const uint8_t pinMOSI, const uint8_t pinMISO, const uint8_t pinCLK) {
  switch (MCP3x6x_DEVICE_TYPE) {
    case MCP3461_DEVICE_TYPE:
      _resolution_max = 16;
      _channels_max   = 2;
      break;
    case MCP3462_DEVICE_TYPE:
      _resolution_max = 16;
      _channels_max   = 4;
      break;
    case MCP3464_DEVICE_TYPE:
      _resolution_max = 16;
      _channels_max   = 8;
      break;
    case MCP3561_DEVICE_TYPE:
      _resolution_max = 24;
      _channels_max   = 2;
      break;
    case MCP3562_DEVICE_TYPE:
      _resolution_max = 24;
      _channels_max   = 4;
      break;
    case MCP3564_DEVICE_TYPE:
      _resolution_max = 24;
      _channels_max   = 8;
      break;
    default:
#warning "undefined MCP3x6x_DEVICE_TYPE"
      break;
  }

  //  settings.id = MCP3x6x_DEVICE_TYPE;

  _spi       = theSPI;
  _pinMISO   = pinMISO;
  _pinMOSI   = pinMOSI;
  _pinCLK    = pinCLK;
  _pinCS     = pinCS;

  _resolution = _resolution_max;
  _channel_mask |= 0xff << _channels_max;  // todo use this one
};

MCP3x6x::MCP3x6x(const uint8_t pinIRQ, const uint8_t pinMCLK, const uint16_t MCP3x6x_DEVICE_TYPE,
                 const uint8_t pinCS, SPIClass *theSPI, const uint8_t pinMOSI,
                 const uint8_t pinMISO, const uint8_t pinCLK)
    : MCP3x6x(MCP3x6x_DEVICE_TYPE, pinCS, theSPI, pinMOSI, pinMISO, pinCLK) {
  _pinIRQ  = pinIRQ;
  _pinMCLK = pinMCLK;
}

MCP3x6x::status_t MCP3x6x::_transfer(uint8_t *data, uint8_t addr, size_t size) {
  _spi->beginTransaction(SPISettings(MCP3x6x_SPI_SPEED, MCP3x6x_SPI_ORDER, MCP3x6x_SPI_MODE));
  digitalWrite(_pinCS, LOW);
  _status.raw = _spi->transfer(addr);
  _spi->transfer(data, size);
  digitalWrite(_pinCS, HIGH);
  _spi->endTransaction();
  return _status;
}

MCP3x6x::status_t MCP3x6x::_fastcmd(uint8_t cmd) {
  _spi->beginTransaction(SPISettings(MCP3x6x_SPI_SPEED, MCP3x6x_SPI_ORDER, MCP3x6x_SPI_MODE));
  digitalWrite(_pinCS, LOW);
  _status.raw = _spi->transfer(cmd);
  digitalWrite(_pinCS, HIGH);
  _spi->endTransaction();
  return _status;
}

bool MCP3x6x::begin() {
  pinMode(_pinCS, OUTPUT);
  digitalWrite(_pinCS, HIGH);

  _spi->begin();
#if ARDUINO_ARCH_SAMD
  // todo figure out how to get dynamicaly sercom index
  pinPeripheral(_pinMISO, PIO_SERCOM);
  pinPeripheral(_pinMOSI, PIO_SERCOM);
  pinPeripheral(_pinCLK, PIO_SERCOM);
#endif

  _status = reset();

  setClockSelection(clk_sel::internal);

  if (_pinIRQ != 0 || _pinMCLK != 0) {
    // scan mode
    //    setScanChannel(); //todo
    setDataFormat(data_format::id_sgnext_data);
    setConvMode(conv_mode::continuous);
    _status = conversion();
  } else {
    // mux mode
    setDataFormat(data_format::sgn_data);
    setReference();
    _status = standby();
  }

  return true;
}

MCP3x6x::status_t MCP3x6x::read(Adcdata *data) {
  uint8_t buffer[4];

  if (settings.config3.data_format == data_format::sgn_data) {
    _status = _transfer(buffer, MCP3x6x_CMD_SREAD | MCP3x6x_ADR_ADCDATA, 3);
  } else {
    _status = _transfer(buffer, MCP3x6x_CMD_SREAD | MCP3x6x_ADR_ADCDATA, 4);
  }

  // reversing byte order
  for (size_t i = 0, e = sizeof(buffer); i < e / 2; i++, e--) {
    uint8_t temp  = buffer[i];
    buffer[i]     = buffer[e - 1];
    buffer[e - 1] = temp;
  }

#if MCP3x6x_DEBUG
  Serial.print("buffer: 0x");
  Serial.print(buffer[3], HEX);
  Serial.print(buffer[2], HEX);
  Serial.print(buffer[1], HEX);
  Serial.println(buffer[0], HEX);
#endif

  data->channelid = _getChannel((uint32_t &)buffer);
  data->value     = _getValue((uint32_t &)buffer);

  return _status;
}

void MCP3x6x::ISR_handler() {
  read(&adcdata);
  result.raw[(uint8_t)adcdata.channelid] = adcdata.value;
#if MCP3x6x_DEBUG
  Serial.print("channel: ");
  Serial.println((uint8_t)adcdata.channelid);
  Serial.print("value: ");
  Serial.println(adcdata.value);
#endif
}

void MCP3x6x::lock(uint8_t key) {
  settings.lock.raw = key;
  write(settings.lock);
}

void MCP3x6x::unlock() {
  settings.lock.raw = _DEFAULT_LOCK;
  write(settings.lock);
}

void MCP3x6x::setDataFormat(data_format format) {
  settings.config3.data_format = format;

  switch (format) {
    case data_format::sgn_data:
    case data_format::sgn_data_zero:
      _resolution--;
      break;
    case data_format::sgnext_data:
    case data_format::id_sgnext_data:
      break;
    default:
      _resolution = -1;
      break;
  }

  _status = write(settings.config3);
}

void MCP3x6x::setConvMode(conv_mode mode) {
  settings.config3.conv_mode = mode;
  _status                    = write(settings.config3);
}

void MCP3x6x::setAdcMode(adc_mode mode) {
  settings.config0.adc = mode;
  _status              = write(settings.config0);
}

void MCP3x6x::setClockSelection(clk_sel clk) {
  settings.config0.clk = clk;
  _status              = write(settings.config0);
}

void MCP3x6x::setScanChannel(mux_t ch) {
  for (size_t i = 0; i < sizeof(_channelID); i++) {
    if (_channelID[i] == ch.raw) {
      bitSet(settings.scan.channel.raw, i);
      break;
    }
  }
  _status = write(settings.scan);
}

void MCP3x6x::unsetScanChannel(mux_t ch) {
  for (size_t i = 0; i < sizeof(_channelID); i++) {
    if (_channelID[i] == ch.raw) {
      bitClear(settings.scan.channel.raw, i);
      break;
    }
  }
  _status = write(settings.scan);
}

void MCP3x6x::setReference(float vref) {
  _reference = vref;
  if (vref == 0.0) {
    vref                      = 2.4;
    settings.config0.vref_sel = 1;
    write(settings.config0);
  }
}

float MCP3x6x::getReference() { return _reference; }

// returns signed ADC value from raw data
int32_t MCP3x6x::_getValue(uint32_t raw) {
  switch (settings.config3.data_format) {
    case (data_format::sgn_data):
      bitWrite(raw, 31, bitRead(raw, 24));
      return raw;
      break;
    case (data_format::sgn_data_zero):
      return raw >> 8;
      break;
    case (data_format::sgnext_data):
    case (data_format::id_sgnext_data):
      bitWrite(raw, 31, bitRead(raw, 25));
      return raw & 0x80FFFFFF;
      break;
  };
  return -1;
}

uint8_t MCP3x6x::_getChannel(uint32_t raw) {
  if (settings.config3.data_format == data_format::id_sgnext_data) {
    return ((raw >> 28) & 0x0F);
  } else {
    for (size_t i = 0; i < sizeof(_channelID); i++) {
      if (_channelID[i] == settings.mux.raw) {
        return i;
      }
    }
  }
  return -1;
}

int32_t MCP3x6x::analogRead(mux_t ch) {
  settings.mux                                  = ch;
  _status                                       = write(settings.mux);

  _status                                       = conversion();
  _status                                       = read(&adcdata);
  return result.raw[(uint8_t)adcdata.channelid] = adcdata.value;
}

int32_t MCP3x6x::analogReadDifferential(mux pinP, mux pinN) {
  settings.mux = ((uint8_t)pinP << 4) | (uint8_t)pinN;
  write(settings.mux);

  _status                                       = conversion();
  _status                                       = read(&adcdata);
  return result.raw[(uint8_t)adcdata.channelid] = adcdata.value;
}

void MCP3x6x::singleEndedMode() { _differential = false; }

void MCP3x6x::differentialMode() { _differential = true; }

bool MCP3x6x::isDifferential() { return _differential; }

uint32_t MCP3x6x::getMaxValue() { return pow(2, _resolution); }


bool MCP3x6x::isContinuous() { return _continuous; }

void MCP3x6x::startContinuousDifferential() {
  _continuous   = true;
  _differential = true;
}

void MCP3x6x::startSingleDifferential() {
  _continuous   = false;
  _differential = true;
}

void MCP3x6x::setResolution(size_t bits) {
  if (bits < _resolution_max) {
    _resolution = bits;
  }
}

bool MCP3x6x::isComplete() { return _status.dr; }
