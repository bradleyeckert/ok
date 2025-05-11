del /s /q *.o
git submodule update --init --recursive
git submodule update --remote --recursive
git add --all
git commit -m "Changed submodule head"
git push
