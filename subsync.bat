rem clean up any .o files left by make failure
rem (or not) del /s /q *.o
rem change submodules to the latest and greatest, discarding local changes
git submodule update --init --recursive
git submodule update --remote --recursive
git add --all
git commit -m "Changed submodule head"
git push
