# Rev1

1. USB + power
  * Glue 2.1x5.5mm barrel jack to underside left
  * Route +5 from barrel to TP2
  * Route GND from barrel to I2S GND
2. Composite, https://github.com/bradgrantham/rocinante/issues/1
  * Cut trace from composite pin 1 (shield / GND) to C20
  * Route from composite pin 2 (signal) to C20
  * Add GND from composite pin 1 to VGA shield GND
3. USER1 conflicts with SPI4_CK/PE2 (which can't be disabled through MX - can it work if disabled by hand?)
  * Cut USER1 trace to PE2
  * Route to TIM3_CH2 / PB5 (MOUSE CLK)
