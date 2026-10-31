# ---------------------------------------------------------------------------
# Phase 1: SDK scaffold only. No game code is compiled yet (that is Phase 2).
#
#   make test        Python tool tests + the SDK_PORT_3DS host suites in tests/sdk (synthetic data only;
#                    needs `python3 tools/bootstrap.py` first). This is what CI runs.
#   make sdk-survey  syntax-check lib/NitroSDK and lib/NitroSystem with GCC under SDK_PORT_3DS (progress count)
#   make port-tests  the vendored 3ds_port unit tests against the HGSS SDK. NOT IMPLEMENTED: always fails until
#                    the wiring in docs/port-wiring.md exists. Deliberately not part of `make test`.
#   make layout      MWCC-vs-GCC save-layout probe: compares GCC against the committed MWCC numbers; needs the
#                    decomp's generated .naix headers (run the decomp's make once), MWCC only with --update-mwcc
#   make clean
# ---------------------------------------------------------------------------

PORT := 3ds_port
SDK_TESTS := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard tests/sdk/*/run.sh)))))

.PHONY: test tool-tests sdk-tests sdk-survey port-tests layout clean check-bootstrap

check-bootstrap:
	@test -f build/upstream/lib/NitroSDK/include/nitro.h || \
	    { echo "Run first: python3 tools/bootstrap.py"; exit 1; }

test: tool-tests sdk-tests

tool-tests:
	python3 -I -m unittest discover -s tests

sdk-tests: check-bootstrap
	@set -e; for t in $(SDK_TESTS); do echo "== tests/sdk/$$t"; bash tests/sdk/$$t/run.sh build/upstream; done

sdk-survey: check-bootstrap
	python3 tools/sdk_compile_survey.py -j $$(nproc) --m32

port-tests: check-bootstrap
	@echo "port-tests: NOT IMPLEMENTED (see docs/port-wiring.md)"; exit 1

layout: check-bootstrap
	bash tests/layout_probe/run.sh

clean:
	rm -rf build
