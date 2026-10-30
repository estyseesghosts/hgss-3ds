# ---------------------------------------------------------------------------
# Phase 1: SDK scaffold only. No game code is compiled yet (that is Phase 2).
#
#   make test      Python tool tests, plus the vendored port unit tests once
#                  they build against the patched HGSS SDK (see PORT_TESTS)
#   make clean
#
# Run `python3 tools/bootstrap.py` first (fetches and patches the decomp).
# ---------------------------------------------------------------------------

PORT := 3ds_port
TARGET ?= host

.PHONY: test tool-tests port-tests clean check-bootstrap

check-bootstrap:
	@test -f build/upstream/lib/NitroSDK/include/nitro.h || \
	    { echo "Run first: python3 tools/bootstrap.py"; exit 1; }

test: tool-tests port-tests

tool-tests:
	python3 -I -m unittest discover -s tests

# Not wired up yet: the vendored 3ds_port/nitro Makefile lists Platinum-layout
# sources, so it cannot build the HGSS lib/ until the SDK patch rebase and an
# HGSS source list exist (docs/progress/phase-1.md).
port-tests: check-bootstrap
	@echo "port-tests: NOT IMPLEMENTED YET (Phase 1 in progress)"; exit 1

clean:
	rm -rf build
