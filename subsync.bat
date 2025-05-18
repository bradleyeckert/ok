rem clean up any .o files left by make failure
del /s /q *.o
rem change submodules to the latest and greatest, discarding local changes
git submodule update --force --recursive
git submodule update --remote --recursive
git add --all
git commit -m "Changed submodule head"
git push
