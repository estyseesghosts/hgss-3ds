cd /work/pokeheartgold
( time make -j10 ) > /work/build-hg2.log 2>&1
echo "exit=$?" >> /work/build-hg2.log
