# link: built like masm and asm6x and the compilers they serve - C++14, -Wall -Wextra
# -Werror -pedantic, objects outside the checkout, the program where BINDIR says. RIDE's
# workspace.mk calls this with BINDIR=bin and OBJDIR=bin/obj/link so that link.exe lands
# beside the editor, where settings.json names it as the linker for x86_64-windows in place
# of Microsoft's.
ifeq ($(origin CXX),default)
  ifneq ($(shell command -v clang++ 2>/dev/null),)
    CXX := clang++
  else
    CXX := g++
  endif
endif
CXXFLAGS = -std=c++14 -O2 -g -Wall -Wextra -Werror -pedantic -pthread
SRCS     = $(filter src/%.cpp,$(wildcard src/*.cpp))
OBJDIR  ?= ../build/LINK/obj
OBJS     = $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRCS))
BINDIR  ?= build
TARGET   = $(BINDIR)/link.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d)

test: $(TARGET)
	LINK=$(TARGET) sh tests/run.sh

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all test clean
