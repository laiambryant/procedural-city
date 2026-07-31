SHELL := /bin/bash

PLATFORM ?= linux
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

SCONS := scons platform=$(PLATFORM) -j$(JOBS)
FORMAT_GLOB := src/**/*.cpp src/**/*.h

.PHONY: all debug release clean submodules format format-check help

all: debug

debug: submodules
	$(SCONS) target=template_debug

release: submodules
	$(SCONS) target=template_release

submodules:
	git submodule update --init --recursive

clean:
	$(SCONS) target=template_debug -c
	$(SCONS) target=template_release -c

format:
	shopt -s globstar; clang-format -i $(FORMAT_GLOB)

format-check:
	shopt -s globstar; clang-format --dry-run --Werror $(FORMAT_GLOB)

help:
	@echo "Targets:"
	@echo "  debug         Build template_debug (default)"
	@echo "  release       Build template_release"
	@echo "  submodules    Fetch/update godot-cpp submodule"
	@echo "  clean         Remove build outputs for both targets"
	@echo "  format        Apply clang-format to src/"
	@echo "  format-check  Check formatting (as run in CI)"
	@echo ""
	@echo "Variables: PLATFORM=$(PLATFORM) JOBS=$(JOBS)"
