#define VERSION_1 121
#define VERSION_2 0
#define VERSION_3 0
#define VERSION_4 14
/*

Адрес        Длина      Назначение региона
------------------------------------------------------------------
0x08000000    38 KB     Код загрузчика
0x08009800   130 KB     OTA-образ с версией и КС в конце
0x0802A000   130 KB     Код основной программы + в конце длина/CRC16/версия
0x0804A800     2 KB     OTA-футер (длина/CRC/версия образа целевого устройства/стартовый адрес программы)
0x0804B000   208 KB     OTA-образ (буфер прошивки целевого устройства)
0x0807F000     2 KB     Серийный номер (сектор)
0x0807F800     2 KB     Настройки (сектор)
0x08080000   —          конец flash (512 KB)

0x20000000   ~144 KB    RAM (рабочая область загрузчика/приложения)
0x20023FFC     4 B      BOOT_MAGIC_ADDR — флаг "войти в загрузчик / в приложение"


121.0.0.14
OTA firmware fetch no longer hits the app's own :3000 directly — nginx now
carves out a plain-HTTP exception for /firmware/ requests on port 80
instead, and :3000 is closed to the outside entirely (see
host/nginx/timberline-web.conf, server.js, and buildOtaUrl()'s own comments)
121.0.0.13
Debug build for self-OTA testing — identical to .12, version bump only,
to verify an over-the-air update actually applies under the new memory map
121.0.0.12
New memory map
121.0.0.11
SLCAN Bridge mode fixed
121.0.0.10
Modem Tool USB control support added
121.0.0.9
Bootloader find filter bug fixed
bootloader table lazy load added + hardcoded table of known bootloaders
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
