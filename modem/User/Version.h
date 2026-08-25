#define VERSION_1 121
#define VERSION_2 0
#define VERSION_3 0
#define VERSION_4 7
/*
0x08000000  192КБ  Загрузчик модема (+ собственный OTA-буфер модема — пока не используем)
0x08030000  104КБ  Код приложения модема (бюджет IROM1, реально занято ~79КБ)
0x0804A000    2КБ  Запас/отступ
0x0804A800    2КБ  OTA-мета (версия+CRC застейдженной прошивки целевого устройства)
0x0804B000  208КБ  OTA-буфер (сам образ прошивки: МВС-2 до 128КБ или ПУ28-Timberline ~206КБ)
0x0807F000    2КБ  Серийный номер модема
0x0807F800    2КБ  Настройки модема (Config)

NOTE: бюджет IROM1 ранее был указан как 152КБ и в этом файле, и в комментарии
flash.h, но настройки проекта (modemDragonfly.uvprojx / .sct) реально не
были уменьшены с 318КБ — из-за этого перепрошивка через Keil могла затирать
OTA-буфер и серийный номер. Исправлено вместе с расширением буфера под
ПУ28-Timberline (см. Library/Flash/flash.h). Перед следующей прошивкой
через Keil проверить фактический размер кода (Code+RO+RW) — должен
укладываться в 104КБ.


121.0.0.7
Firmware storing and updating added
USB settings change added, 4g only sim support added
UCS2 for receiving SMS support added
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
