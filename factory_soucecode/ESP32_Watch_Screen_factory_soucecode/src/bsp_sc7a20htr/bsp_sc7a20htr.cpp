#include "bsp_sc7A20htr.h"

bool BSP_SC7A20::IsExist()
{
	uint8_t config;
	IIC_Read_Byte(WHO_AM_I_REG, &config, 1);
	if (config == CHIP_ID)
		return true;
	else
		return false;
}

void BSP_SC7A20::IIC_Write_Byte(uint8_t reg, uint8_t data)
{
	_i2cPort->beginTransmission(_address);
	_i2cPort->write(reg);
	_i2cPort->write(data);
	_i2cPort->endTransmission();
}

void BSP_SC7A20::IIC_Read_Byte(uint8_t reg, uint8_t *buf, int lenght)
{

	uint8_t i = 0;
	_i2cPort->beginTransmission(_address);
	reg |= 0x80; // turn auto-increment bit on
	_i2cPort->write(reg);
	_i2cPort->endTransmission(false);
	_i2cPort->requestFrom(_address, lenght);

	while (_i2cPort->available() && i < lenght)
	{
		*buf = _i2cPort->read();
		buf++;
		i++;
	}
}

int16_t BSP_SC7A20::_12bitComplement(uint8_t msb, uint8_t lsb)
{

	int16_t temp;
	temp = msb << 8 | lsb;
	temp = temp >> 4;
	temp = temp & 0x0fff;
	if (temp & 0x0800)
	{
		temp = temp & 0x07ff;
		temp = ~temp;
		temp = temp + 1;
		temp = temp & 0x07ff;
		temp = -temp;
	}
	return temp;
}

void BSP_SC7A20::measure(void)
{
	uint8_t buff[6];
	IIC_Read_Byte(0x28, buff, 6);
	accel_X = _12bitComplement(buff[1], buff[0]);
	accel_Y = _12bitComplement(buff[3], buff[2]);
	accel_Z = _12bitComplement(buff[5], buff[4]);
}

bool BSP_SC7A20::begin(uint8_t address, TwoWire *wirePort)
{
	_address = address;
	_i2cPort = wirePort;

	if (!IsExist())
		return false;
	
	IIC_Write_Byte(CTRL_REG1, 0x47);
	IIC_Write_Byte(CTRL_REG2,0x00);
	IIC_Write_Byte(CTRL_REG2,0x00);
	IIC_Write_Byte(CTRL_REG3,0x00);
	IIC_Write_Byte(CTRL_REG4,0x88);

	return true;
}