@echo off
REM ============================================================
REM  FDR Converter - Build del ejecutable standalone (.exe)
REM  Requiere Python 3.9+ y pip instalados.
REM ============================================================

echo === Instalando dependencias ===
pip install -r requirements.txt

echo.
echo === Generando ejecutable con PyInstaller ===
pyinstaller ^
    --onefile ^
    --windowed ^
    --name "FDR_Converter" ^
    --icon NONE ^
    --add-data ".;." ^
    fdr_converter.py

echo.
echo === Listo! ===
echo El ejecutable se encuentra en: dist\FDR_Converter.exe
echo Copia ese archivo al pendrive del hangar (sin necesitar Python instalado).
pause
