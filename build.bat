@echo off
call C:\Espressif\frameworks\esp-idf-v5.3\export.bat
cd /d z:\ESP32\osmmos_coontrol\esp32s3_MC\basic
idf.py build 2>&1
