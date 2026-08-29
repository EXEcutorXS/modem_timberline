#define VERSION_1 121
#define VERSION_2 0
#define VERSION_3 0
#define VERSION_4 8
/*

Адрес        Длина      Назначение региона
------------------------------------------------------------------
0x08000000    38 KB     Код загрузчика
0x08009800     2 KB     OTA-футер (длина/CRC/версия образа модема)
0x0800A000   128 KB     OTA-образ (буфер новой прошивки модема)
0x0802A000     2 KB     Футер приложения (длина/CRC16/версия)
0x0802A800   128 KB     Код основной программы
0x0804A800     2 KB     OTA-футер (длина/CRC/версия образа целевого устройства/стартовый адрес программы)
0x0804B000   208 KB     OTA-образ (буфер прошивки целевого устройства)
0x0807F000     2 KB     Серийный номер (сектор)
0x0807F800     2 KB     Настройки (сектор)
0x08080000   —          конец flash (512 KB)

0x20000000   ~144 KB    RAM (рабочая область загрузчика/приложения)
0x20023FFC     4 B      BOOT_MAGIC_ADDR — флаг "войти в загрузчик / в приложение"


121.0.0.8
Adapted for 123.0.4.13 bootloader
Type/adr filter now works only for commands
MBC-2 detection PGN now is 21 instead of 19
New memory map
Self OTA suppurt added
New map for zone control(topic count minimised)
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
