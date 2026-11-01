# ---------------------------------------------------------------------------
# Phase 1 SDK scaffold plus the Phase 2 game build harness.
#
#   make test        Python tool tests + the SDK_PORT_3DS host suites in tests/sdk (synthetic data only;
#                    needs `python3 tools/bootstrap.py` first). This is what CI runs.
#   make content-check  fail if the git index contains game content, build artifacts or oversized files
#                    (tools/check_repo_content.py); also covered by a test under `make test`.
#   make sdk-survey  syntax-check lib/NitroSDK and lib/NitroSystem with GCC under SDK_PORT_3DS (progress count)
#   make port-tests  the vendored 3ds_port unit tests (pak, nitro, nitrosystem) against the
#                    HGSS SDK, via tools/hgss_nitro.py. Deliberately not part of `make test`.
#   make layout      MWCC-vs-GCC save-layout probe: compares GCC against the committed MWCC numbers; needs the
#                    decomp's generated .naix headers (run the decomp's make once), MWCC only with --update-mwcc
#   make game-pc     compile the HGSS game + data + port libraries with gcc-10 -m32 and link
#                    build/game-pc/pokeheartgold-pc (the Phase 2 harness; --keep-going reports every error)
#   make game-3ds    the same with devkitARM and the libctru/citro3d shell; links
#                    build/game-3ds/pokeheartgold-3ds.elf and packages
#                    build/game-3ds/pokeheartgold.3dsx with 3dsxtool (no SMDH yet). Both
#                    need `python3 tools/bootstrap.py` first.
#   make clean
# ---------------------------------------------------------------------------

PORT := 3ds_port
SDK_TESTS := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard tests/sdk/*/run.sh)))))

.PHONY: test tool-tests sdk-tests sdk-survey port-tests layout game-pc game-3ds content-check clean check-bootstrap

check-bootstrap:
	@test -f build/upstream/lib/NitroSDK/include/nitro.h || \
	    { echo "Run first: python3 tools/bootstrap.py"; exit 1; }

test: tool-tests sdk-tests

content-check:
	python3 tools/check_repo_content.py

tool-tests:
	python3 -I -m unittest discover -s tests

sdk-tests: check-bootstrap
	@set -e; for t in $(SDK_TESTS); do echo "== tests/sdk/$$t"; bash tests/sdk/$$t/run.sh build/upstream; done

sdk-survey: check-bootstrap
	python3 tools/sdk_compile_survey.py -j $$(nproc) --m32

PORT_TEST_OUT := $(CURDIR)/build/port-tests

port-tests: check-bootstrap
	python3 tools/hgss_nitro.py make --target host32 --out $(PORT_TEST_OUT) test

layout: check-bootstrap
	bash tests/layout_probe/run.sh

GAME_PC_OUT := $(CURDIR)/build/game-pc
GAME_3DS_OUT := $(CURDIR)/build/game-3ds

game-pc: check-bootstrap
	python3 tools/hgss_game.py --target host32 --keep-going --out $(GAME_PC_OUT)

game-3ds: check-bootstrap
	python3 tools/hgss_game.py --target 3ds --keep-going --out $(GAME_3DS_OUT)

clean:
	rm -rf build
