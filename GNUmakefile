# GNUmakefile - Bootstrap wrapper for building tmux from a git checkout.
#
# GNU make looks for GNUmakefile before Makefile, so on a fresh clone
# (where the autotools-generated Makefile does not yet exist) this file
# is found instead.  It checks for required build tools, runs autogen.sh
# and configure, then re-invokes make so the generated Makefile takes over.
#
# All build artifacts are placed in obj/ (VPATH build) and the final
# binary is copied to bin/ so the source tree stays clean.
#
# Once the build system has been bootstrapped, every target is delegated
# to the generated Makefile in obj/ with zero overhead beyond the
# file-existence check.
#
# Variables:
#   BUILDDIR         - object / build directory (default: obj)
#   BINDIR           - output binary directory  (default: bin)
#   CONFIGURE_FLAGS  - extra flags passed to ./configure
#                      (default: --prefix=$HOME/.local)

BUILDDIR        ?= obj
BINDIR          ?= bin

# --------------------------------------------------------------------------
# Fast path: if the build directory already has a Makefile, delegate every
# target to it and keep bin/ up to date.
# --------------------------------------------------------------------------
ifneq ($(wildcard $(BUILDDIR)/Makefile),)

.PHONY: all install clean distclean

all:
	@$(MAKE) --no-print-directory -C $(BUILDDIR) all
	@mkdir -p $(BINDIR)
	@cp -f $(BUILDDIR)/tmux $(BINDIR)/tmux
	@echo "Binary: $(BINDIR)/tmux"

install:
	@$(MAKE) --no-print-directory -C $(BUILDDIR) install

clean:
	@$(MAKE) --no-print-directory -C $(BUILDDIR) clean
	@rm -rf $(BINDIR)

distclean:
	@rm -rf $(BUILDDIR) $(BINDIR)

%:
	@$(MAKE) --no-print-directory -C $(BUILDDIR) $@

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

# Build CPPFLAGS/LDFLAGS/PKG_CONFIG_PATH from Homebrew package prefixes
# at configure time (after brew install has run).  Passing CPPFLAGS and
# LDFLAGS directly is the most reliable way to make configure's
# AC_CHECK_HEADER and AC_SEARCH_LIBS find keg-only packages.
BREW_PACKAGES   = libevent ncurses utf8proc
CONFIGURE_ENV   = \
	PKG_CONFIG_PATH="$$(for p in $(BREW_PACKAGES); do d=$$(brew --prefix $$p 2>/dev/null) && printf '%s:' "$$d/lib/pkgconfig"; done)$$PKG_CONFIG_PATH" \
	CPPFLAGS="$$(for p in $(BREW_PACKAGES); do d=$$(brew --prefix $$p 2>/dev/null) && printf '%s ' "-I$$d/include"; done)$$CPPFLAGS" \
	LDFLAGS="$$(for p in $(BREW_PACKAGES); do d=$$(brew --prefix $$p 2>/dev/null) && printf '%s ' "-L$$d/lib"; done)$$LDFLAGS"
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
	brew list libevent  >/dev/null 2>&1 || MISSING="$$MISSING libevent"; \
	brew list utf8proc  >/dev/null 2>&1 || MISSING="$$MISSING utf8proc"; \
	if [ -n "$$MISSING" ]; then \
		echo "==> Installing missing Homebrew packages:$$MISSING"; \
		brew install$$MISSING || exit 1; \
	fi
endef

else ifeq ($(UNAME_S),Linux)
# -- Linux -----------------------------------------------------------------
CONFIGURE_ENV =

define CHECK_DEPS
	@MISSING=""; MISSING_LIBS=""; \
	command -v autoconf   >/dev/null 2>&1 || MISSING="$$MISSING autoconf"; \
	command -v aclocal    >/dev/null 2>&1 || MISSING="$$MISSING automake"; \
	command -v pkg-config >/dev/null 2>&1 || MISSING="$$MISSING pkg-config"; \
	command -v yacc >/dev/null 2>&1 || command -v bison >/dev/null 2>&1 || \
		MISSING="$$MISSING bison"; \
	if command -v pkg-config >/dev/null 2>&1; then \
		pkg-config --exists libevent_core 2>/dev/null || \
		pkg-config --exists libevent 2>/dev/null || \
			MISSING_LIBS="$$MISSING_LIBS libevent-dev(deb)/libevent-devel(rpm)"; \
		pkg-config --exists ncurses 2>/dev/null || \
		pkg-config --exists ncursesw 2>/dev/null || \
		pkg-config --exists tinfo 2>/dev/null || \
			MISSING_LIBS="$$MISSING_LIBS libncurses-dev(deb)/ncurses-devel(rpm)"; \
	fi; \
	if [ -n "$$MISSING" ] || [ -n "$$MISSING_LIBS" ]; then \
		[ -n "$$MISSING" ] && \
			echo "Error: missing tools:$$MISSING"; \
		[ -n "$$MISSING_LIBS" ] && \
			echo "Error: missing libraries:$$MISSING_LIBS"; \
		echo "  Debian/Ubuntu:  sudo apt install$$MISSING libevent-dev libncurses-dev"; \
		echo "  Fedora/RHEL:    sudo dnf install$$MISSING libevent-devel ncurses-devel"; \
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

.PHONY: all install bootstrap check-deps clean distclean maintainer-clean

all install: bootstrap
	@$(MAKE) --no-print-directory $@

bootstrap: check-deps
	@if [ ! -f configure ]; then \
		echo "==> Running autogen.sh ..."; \
		sh autogen.sh || exit 1; \
	fi
	@if [ -f config.status ]; then \
		echo "==> Cleaning stale in-tree configure artifacts ..."; \
		rm -f config.status config.log; \
		if [ -f Makefile ] && grep -q 'generated by automake' Makefile 2>/dev/null; then \
			rm -f Makefile; \
		fi; \
	fi
	@mkdir -p $(BUILDDIR)
	@echo "==> Running ./configure $(CONFIGURE_FLAGS) (in $(BUILDDIR)/) ..."
	@cd $(BUILDDIR) && $(CONFIGURE_ENV) ../configure $(CONFIGURE_FLAGS) || exit 1
	@echo "==> Bootstrap complete.  Build artifacts will be in $(BUILDDIR)/."

check-deps:
	$(CHECK_DEPS)

clean distclean maintainer-clean:
	@if [ -f config.status ] || [ -f config.log ]; then \
		echo "==> Removing stale in-tree configure artifacts ..."; \
		rm -f config.status config.log; \
		if [ -f Makefile ] && grep -q 'generated by automake' Makefile 2>/dev/null; then \
			rm -f Makefile; \
		fi; \
	else \
		echo "Nothing to clean (build system not yet bootstrapped)."; \
	fi

endif
