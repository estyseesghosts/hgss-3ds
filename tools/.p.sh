cd /work/pokeplatinum-3ds
unset CC CXX
gcc --version | head -1
( python3 tools/bootstrap.py --clean && time make test ) > /work/plat-test.log 2>&1
echo "exit=$?" >> /work/plat-test.log
