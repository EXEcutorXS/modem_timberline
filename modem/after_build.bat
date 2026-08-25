@echo off
rem Chains the two After-Build steps that used to fight over Keil's single
rem UserProg2 slot: OTA info generation (pre-existing) and the flash/RAM
rem usage report (see tools\flash_report.py). Keil's AfterMake only offers
rem UserProg1/UserProg2 — UserProg1 already runs fromelf --bin, so both of
rem these have to be chained through one script in UserProg2 instead of
rem each getting their own slot.
call "%~dp0__Get_OTA_Info.exe"
python "%~dp0tools\flash_report.py"
