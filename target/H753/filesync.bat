@echo off
echo Only run this batch file if you want to overwrite files
echo with the latest from the local ok repo.
echo.
set /P yesno=Do you want to continue? (y/n) 
if /i "%yesno%"=="y" goto continue
echo Exiting...
exit
:continue

if exist ..\..\..\ok (
  copy ..\..\src\mole\mole.c       .\Core\App\mole\mole.c /y
  copy ..\..\src\mole\mole.h       .\Core\App\mole\mole.h /y
  copy ..\..\src\mole\moleconfig.h .\Core\App\mole\moleconfig.h /y
  copy ..\..\src\mole\blake2s.c    .\Core\App\mole\blake2s.c /y
  copy ..\..\src\mole\blake2s.h    .\Core\App\mole\blake2s.h /y
  copy ..\..\src\mole\xchacha.c    .\Core\App\mole\xchacha.c /y
  copy ..\..\src\mole\xchacha.h    .\Core\App\mole\xchacha.h /y
  copy ..\..\src\bci\bci.c         .\Core\App\bci\bci.c /y
  copy ..\..\src\bci\bci.h         .\Core\App\bci\bci.h /y
  copy ..\..\src\bci\bciHW.c       .\Core\App\bci\bciHW.c /y
  copy ..\..\src\bci\bciHW.h       .\Core\App\bci\bciHW.h /y
  copy ..\..\src\bci\vm.c          .\Core\App\bci\vm.c /y
  copy ..\..\src\bci\vm.h          .\Core\App\bci\vm.h /y
) else (
  echo Missing ok folder, please clone the repo
)