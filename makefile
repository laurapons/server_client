START_TIME :=$(shell date +%s)

########### DIRECTORIES DEFINITION ###########
BUILDDIR := bin
SRCDIR := src
# output binary
BIN := $(BUILDDIR)/test
# generated object files directory
OBJDIR := $(BUILDDIR)/.o
# generated dependency files directory
DEPDIR := $(BUILDDIR)/.d
# external libraries directory
EXTERNAL_LIBDIR := lib/

########### FILES DEFINITION ###########
# source files
SRCS = $(shell find $(SRCDIR)/ -name "*.cpp")
# object files, auto generated from source files
OBJS := $(patsubst %,$(OBJDIR)/%.o,$(basename $(SRCS)))
# dependency files, auto generated from source files
DEPS := $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS)))

# compilers (at least gcc and clang) don't create the subdirectories automatically
$(shell mkdir -p $(dir $(OBJS)) >/dev/null)
$(shell mkdir -p $(dir $(DEPS)) >/dev/null)

########## GUI COMPILATION DETAILS ##########
GUIDIR := $(PWD)/../QT_GUI_TEST
GUI_BIN := $(GUIDIR)/bin
GUI_PRO := $(GUIDIR)/src/qt_app_test.pro
GUI_MAKEFILE := $(GUI_BIN)/Makefile

########## EXT_APP COMPILATION DETAILS ##########
EXT_APP := test/EXT_APP/

########### COMPILING & LINKING OPTIONS ###########
CC_PREFIX :=#ccache ## After doing some tests, it's actually quicker without ccache
# C++ compiler
CXX := g++
# linker
LD := g++
# C++ flags
CXXFLAGS := -std=c++20 -I$(SRCDIR) -I$(EXTERNAL_LIBDIR) -fcoroutines
# C/C++ flags
CPPFLAGS := -g -Wall -Wextra -pedantic
# linker flags
LDFLAGS :=
# linker flags: libraries to link (e.g. -lfoo)
#  -lboost_thread -lboost_system -lboost_thread -lboost_chrono -lboost_context -DBOOST_COROUTINES_NO_DEPRECATION_WARNING
LDLIBS := -pthread -lrt -lboost_thread -lboost_coroutine -lboost_program_options
# flags required for dependency generation; passed to compilers
DEPFLAGS = -MT $@ -MD -MP -MF $(DEPDIR)/$*.d
# compile C++ source files
COMPILE.cpp = $(CC_PREFIX) $(CXX) $(DEPFLAGS) $(CXXFLAGS) $(CPPFLAGS) -c -o $@
# link object files to binary
LINK.o = $(LD) -o $@ $^ $(LDFLAGS) $(LDLIBS)
# Number of CPU processors to parallelize compilation
NPROCS:=$(shell grep -c ^processor /proc/cpuinfo)

### MAKE OPTIONS TO COMPILE EXTERNAL PROJECTS ###
QMAKE := qmake
QMAKE_OPTIONS := -spec linux-g++ CONFIG+=debug # qmake options to compile the GUI in debug mode

########### MAKE COMMANDS DEFINITION ###########
.PHONY: clean test qt_test ext_app clean_test clean_qt_test clean_ext_app # Declare targets to be available to be called by other targets

all: test qt_test ext_app
	@echo "Build took $$(($$(date +%s)-$(START_TIME))) seconds"
test:
	@make $(BIN) -j$(NPROCS)
qt_test:
	$(QMAKE) -makefile -o $(GUI_MAKEFILE) $(QMAKE_OPTIONS) $(GUI_PRO)
	@make -C $(GUI_BIN) -j$(NPROCS)
ext_app:
	@make -C $(EXT_APP) -j$(NPROCS)

clean: clean_test clean_qt_test clean_ext_app
clean_test:
	$(RM) -r $(OBJDIR) $(DEPDIR) $(BIN)
clean_qt_test:
	$(RM) -r $(GUI_BIN)
clean_ext_app:
	@make -C $(EXT_APP) clean

########### COMMANDS DEFINITION ###########
$(BIN): $(OBJS)
	$(LINK.o)
	@echo "Build took $$(($$(date +%s)-$(START_TIME))) seconds"

$(OBJDIR)/%.o: %.cpp
$(OBJDIR)/%.o: %.cpp $(DEPDIR)/%.d
	@echo compile $(@F)
	$(COMPILE.cpp) $<

.PRECIOUS: $(DEPDIR)/%.d
$(DEPDIR)/%.d: ;

-include $(DEPS)
