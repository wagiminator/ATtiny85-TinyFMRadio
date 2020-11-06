avrdude -p t85 -c usbasp -U efuse:w:0xFF:m -U hfuse:w:0xD7:m -U lfuse:w:0xE2:m -U flash:w:tinyFMtuner.ino.hex:i
