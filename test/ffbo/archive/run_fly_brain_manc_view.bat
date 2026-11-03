@echo off
rem Digital Fruit Fly - full pathway launcher (double-click to run)
rem   FAFB brain (139k neurons) -> MANC cord (23.7k) -> FlyBody -> fantasy render
rem Requires four DLL dirs on PATH: Qt5 / xhqm / mujoco / glew
set PATH=C:\Qt\5.15.2\msvc2019_64\bin;D:\WorkSpace\xhqm_v2\binary;D:\WorkSpace\_research\digital-fruit-fly\depend\mujoco;D:\WorkSpace\xhqm_v2\depend\glew\bin;%PATH%

set RESEARCH=D:\WorkSpace\_research\digital-fruit-fly
set FAFB=%RESEARCH%\data\flywire\v783
set MANC=%RESEARCH%\data\flywire\manc
set FLY_XML=%RESEARCH%\code\flygym\src\flygym\assets\model\flybody\fruitfly.xml
set VIS_TYPES=%FAFB%\visual_neuron_types.csv
set VIS_COORD=%FAFB%\coordinates.csv

rem Odor source geom (odor_source) already exists in fruitfly.xml; --odor-move toggles motion
rem Optional args are forwarded, e.g. --snapshot out.png  or  --min-syn 5
"D:\WorkSpace\xhqm_v2\xhqm\test\ffbo\build\fly_brain_manc_view.exe" ^
  "%FAFB%\neurons.csv" "%FAFB%\connections.csv" "%FAFB%\cell_types.csv" "%FAFB%\classification.csv" ^
  "%MANC%\neurons.csv" "%MANC%\connections_princeton.csv" "%FLY_XML%" ^
  "%VIS_TYPES%" "%VIS_COORD%" %*

if errorlevel 1 pause
