# m5097dabk -- unique string to find this makefile without sourcecontrol...

# allows automatical multicore build sessions!
JOBS?=$(shell getconf _NPROCESSORS_ONLN)

# we wanna use absolute path' where possible
SRCDIR=$(shell pwd)

# carefull -- we ask the c++ compiler, not the c-compiler!
# additionally the environment variable CXX is asked, so not neccessarily the native compiler!
ifndef ARCH
ARCH?=$(shell ${CXX} -dumpmachine)
else
override CMAKE_FLAGS+=-DCMAKE_TOOLCHAIN_FILE=../../CMake-Modules/${ARCH}.cmake
endif

# by default, we use a compiler dependent build and install directory.
INSTALLDIR?=$(shell readlink -m ~/DFKI.install/$(ARCH))
BUILDDIR?=$(shell readlink -m ./build/$(ARCH))

# SILENCE!
override MAKEFLAGS+=--no-print-directory

# configure cmake:
override CMAKE_FLAGS+=-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_INSTALL_PREFIX=$(INSTALLDIR)
# this test-flags are more project specific:
override CMAKE_DEBUG_FLAGS+=-DNDLCOM_ENABLE_TESTING=ON

## working area ###

all: compile

info:
	@echo "srcdir (here):"
	@echo $(SRCDIR)
	@echo "installdir:"
	@echo $(INSTALLDIR)
	@echo "builddir:"
	@echo $(BUILDDIR)
	@echo "arch:"
	@echo $(ARCH)
	@echo "cmake_flags:"
	@echo $(CMAKE_FLAGS)

# so we have "build" as a shorthand for creating a new build environment
build: $(BUILDDIR)/Makefile

$(BUILDDIR)/Makefile: Makefile
	mkdir -p $(BUILDDIR);\
	sh -c "cd $(BUILDDIR); cmake $(SRCDIR) $(CMAKE_FLAGS)"

link_dependency_graph: build
	mkdir -p $(BUILDDIR)
	sh -c "cd $(BUILDDIR); cmake $(SRCDIR) --graphviz=link_dependency_graph.dot"
	dot $(BUILDDIR)/link_dependency_graph.dot -Tpng > $(SRCDIR)/link_dependency_graph.png

compile: build
	${MAKE} -j$(JOBS) -C $(BUILDDIR)

doc: build
	${MAKE} -j$(JOBS) -C $(BUILDDIR) doc

test: build
	sh -c "cd $(BUILDDIR); cmake $(SRCDIR) $(CMAKE_FLAGS) $(CMAKE_DEBUG_FLAGS)"
	${MAKE} -j$(JOBS) -C $(BUILDDIR) all test

install: compile
	${MAKE} -j$(JOBS) -C $(BUILDDIR) install

clean:
	-${MAKE} -C $(BUILDDIR) clean

distclean: clean
	-rm -rf $(BUILDDIR)
	@rm -f link_dependency_graph.png

.PHONY: compile
