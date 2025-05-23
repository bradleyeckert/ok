if exist ..\..\..\mole (
  copy ..\..\..\mole\src\mole.c mole.c /y
  copy ..\..\..\mole\src\mole.h mole.h /y
  copy ..\..\..\mole\src\moleconfig.h moleconfig.h /y
  copy ..\..\..\mole\src\blake2s\src\blake2s.c blake2s.c /y
  copy ..\..\..\mole\src\blake2s\src\blake2s.h blake2s.h /y
  copy ..\..\..\mole\src\xchacha\src\xchacha.c xchacha.c /y
  copy ..\..\..\mole\src\xchacha\src\xchacha.h xchacha.h /y
) else (
  echo Missing mole folder, please clone the repo
)