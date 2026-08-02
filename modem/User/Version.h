#define VERSION_1 121
#define VERSION_2 0
#define VERSION_3 0
#define VERSION_4 7
/*
0x08000000  192КБ  Загрузчик модема (+ собственный OTA-буфер модема — пока не используем)
0x08030000  152КБ  Код приложения модема (бюджет IROM1, реально занято ~79КБ)
0x08056000    2КБ  Запас/отступ
0x08056800    2КБ  OTA-мета (версия+CRC застейдженной прошивки MBC-2)
0x08057000  160КБ  OTA-буфер (сам образ прошивки MBC-2)
0x0807F000    2КБ  Серийный номер модема
0x0807F800    2КБ  Настройки модема (Config)


121.0.0.6
Internet reconnect bug fixed
Telemetry interval now is flexible
Modem status quick send
121.0.0.5
connection link string added
Strings are send by timer
Some modem connection bugs fixed
121.0.0.4
Hard reset added(10 sec button press)
"Only 2G" mode suppord added
4 RX buffers for strings
SMS reading bug fix
121.0.0.3
Last Will for online/offline status
underfroor and engine pumps statuses and temperatures added to telemetry
121.0.0.2
MQTT support added
121.0.0.1
German support added
121.0.0.0
First Beta version
*/
