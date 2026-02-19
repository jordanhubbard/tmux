# GNUmakefile - Bootstrap wrapper for building tmux from a git checkout.
#
# GNU make looks for GNUmakefile before Makefile, so on a fresh clone
# (where the autotools-generated Makefile does not yet exist) this file
# is found instead.  It checks for required build tools, runs autogen.sh
# and configure, then re-invokes make so the generated Makefile takes over.
#
# Once the build system has been bootstrapped, every target is delegated
# to the generated Makefile with zero overhead beyond the file-existence
# check.
#
# Variables:
#   CONFIGURE_FLAGS  - extra flags passed to ./configure
#                      (default: --prefix=$HOME/.local)

# --------------------------------------------------------------------------
# Fast path: if the autotools-generated Makefile already exists, delegate
# every target to it and get out of the way.
# --------------------------------------------------------------------------
ifneq ($(wildcard Makefile),)

all:
	@$(MAKE) -f Makefile all

%:
	@$(MAKE) -f Makefile $@

.PHONY: all

else
# --------------------------------------------------------------------------
# Bootstrap path: no generated Makefile yet.
# --------------------------------------------------------------------------

UNAME_S         := $(shell uname -s)
CONFIGURE_FLAGS ?= --prefix=$(HOME)/.local

# --------------------------------------------------------------------------
# Per-OS configuration.
# --------------------------------------------------------------------------

ifeq ($(UNAME_S),Darwin)
# -- macOS (Homebrew) ------------------------------------------------------

HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null)

# Set PKG_CONFIG_PATH at configure time (after brew install has run)
# and enable utf8proc (macOS wcwidth(3) has poor Unicode support).
# Also pass LIBUTF8PROC_CFLAGS/LIBS directly since some Homebrew
# installations don't expose a libutf8proc.pc file.
CONFIGURE_ENV = \
	PKG_CONFIG_PATH="$$(brew --prefix libevent 2>/dev/null)/lib/pkgconfig:$$(brew --prefix ncurses 2>/dev/null)/lib/pkgconfig:$$(brew --prefix utf8proc 2>/dev/null)/lib/pkgconfig:$$PKG_CONFIG_PATH" \
	LIBUTF8PROC_CFLAGS="-I$$(brew --prefix utf8proc 2>/dev/null)/include" \
	LIBUTF8PROC_LIBS="-L$$(brew --prefix utf8proc 2>/dev/null)/lib -lutf8proc"
CONFIGURE_FLAGS += --enable-utf8proc

define CHECK_DEPS
	@command -v brew >/dev/null 2>&1 || \
		{ echo "Error: Homebrew is required on macOS.  Install from https://brew.sh"; exit 1; }
	@MISSING=""; \
	command -v autoconf   >/dev/null 2>&1 || MISSING="$$MISSING autoconf"; \
	command -v aclocal    >/dev/null 2>&1 || MISSING="$$MISSING automake"; \
	command -v pkg-config >/dev/null 2>&1 || MISSING="$$MISSING pkg-config"; \
	command -v bison >/dev/null 2>&1 || command -v yacc >/dev/null 2>&1 || \
		MISSING="$$MISSING bison"; \
	brew --prefix libevent  >/dev/null 2>&1 || MISSING="$$MISSING libevent"; \
	brew --prefix utf8proc  >/dev/null 2>&1 || MISSING="$$MISSING utf8proc"; \
	if [ -n "$$MISSING" ]; then \
		echo "==> Installing missing Homebrew packages:$$MISSING"; \
		brew install$$MISSING || exit 1; \
	fi
endef

else ifeq ($(UNAME_S),Linux)
# -- Linux -----------------------------------------------------------------
CONFIGURE_ENV =

define CHECK_DEPS
	@MISSING=""; \
	command -v autoconf   >/dev/null 2>&1 || MISSING="$$MISSING autoconf"; \
	command -v aclocal    >/dev/null 2>&1 || MISSING="$$MISSING automake"; \
	command -v pkg-config >/dev/null 2>&1 || MISSING="$$MISSING pkg-config"; \
	command -v yacc >/dev/null 2>&1 || command -v bison >/dev/null 2>&1 || \
		MISSING="$$MISSING bison"; \
	if [ -n "$$MISSING" ]; then \
		echo "Error: missing dependencies:$$MISSING"; \
		echo "  Debian/Ubuntu:  sudo apt install$$MISSING"; \
		echo "  Fedora/RHEL:    sudo dnf install$$MISSING"; \
		echo "You also need libevent-dev (or libevent-devel) and"; \
		echo "libncurses-dev (or ncurses-devel)."; \
		exit 1; \
	fi
endef

else ifeq ($(UNAME_S),FreeBSD)
# -- FreeBSD ---------------------------------------------------------------
CONFIGURE_ENV =

define CHECK_DEPS
	@MISSING=""; \
	command -v autoconf   >/dev/null 2>&1 || MISSING="$$MISSING autoconf"; \
	command -v aclocal    >/dev/null 2>&1 || MISSING="$$MISSING automake"; \
	command -v pkg-config >/dev/null 2>&1 || MISSING="$$MISSING pkgconf"; \
	command -v yacc >/dev/null 2>&1 || command -v bison >/dev/null 2>&1 || \
		MISSING="$$MISSING bison"; \
	if [ -n "$$MISSING" ]; then \
		echo "Error: missing dependencies:$$MISSING"; \
		echo "Install with:  pkg install$$MISSING libevent"; \
		exit 1; \
	fi
endef

else ifeq ($(UNAME_S),OpenBSD)
# -- OpenBSD ---------------------------------------------------------------
CONFIGURE_ENV =

define CHECK_DEPS
	@MISSING=""; \
	command -v autoconf-2.69   >/dev/null 2>&1 || MISSING="$$MISSING autoconf-2.69"; \
	command -v automake-1.15   >/dev/null 2>&1 || MISSING="$$MISSING automake-1.15"; \
	command -v pkg-config >/dev/null 2>&1 || MISSING="$$MISSING pkg-config"; \
	command -v yacc >/dev/null 2>&1 || command -v bison >/dev/null 2>&1 || \
		MISSING="$$MISSING bison"; \
	if [ -n "$$MISSING" ]; then \
		echo "Error: missing dependencies:$$MISSING"; \
		echo "Install with:  pkg_add$$MISSING libevent"; \
		exit 1; \
	fi
endef

else
# -- Unknown / fallback ----------------------------------------------------
CONFIGURE_ENV =

define CHECK_DEPS
	@MISSING=""; \
	command -v autoconf   >/dev/null 2>&1 || MISSING="$$MISSING autoconf"; \
	command -v aclocal    >/dev/null 2>&1 || MISSING="$$MISSING automake"; \
	command -v pkg-config >/dev/null 2>&1 || MISSING="$$MISSING pkg-config"; \
	command -v yacc >/dev/null 2>&1 || command -v bison >/dev/null 2>&1 || \
		MISSING="$$MISSING bison/yacc"; \
	if [ -n "$$MISSING" ]; then \
		echo "Error: missing dependencies:$$MISSING"; \
		echo "Please install them with your system package manager."; \
		echo "You also need libevent and ncurses development libraries."; \
		exit 1; \
	fi
endef
endif

# --------------------------------------------------------------------------
# Bootstrap targets.
# --------------------------------------------------------------------------

.PHONY: all bootstrap check-deps clean distclean maintainer-clean

all: bootstrap
	@$(MAKE) $(MAKECMDGOALS)

bootstrap: check-deps
	@if [ ! -f configure ]; then \
		echo "==> Running autogen.sh ..."; \
		sh autogen.sh || exit 1; \
	fi
	@echo "==> Running ./configure $(CONFIGURE_FLAGS) ..."
	@$(CONFIGURE_ENV) ./configure $(CONFIGURE_FLAGS) || exit 1
	@echo "==> Bootstrap complete."

check-deps:
	$(CHECK_DEPS)

clean distclean maintainer-clean:
	@echo "Nothing to clean (build system not yet bootstrapped)."

%: all
	@:

endif
