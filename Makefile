# reset suffix list
.SUFFIXES:
.SUFFIXES: .c .h .o

# use spaces instead of tabs
.RECIPEPREFIX != ps

# paths
PATHS = src/
PATHB = build/
PATHO = build/obj/

# keep object files
.PRECIOUS: $(PATHO)%.o

# commands and flags
CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror
ALL_CFLAGS = -g -O3 -std=c99 -I$(PATHS) $(CFLAGS)

# file lists

BUILD_PATHS = $(PATHB) $(PATHO) $(PATHH)
OBJECTS = $(PATHO)ors.o $(PATHO)orb.o $(PATHO)orp.o $(PATHO)org.o \
          $(PATHO)pool.o $(PATHO)risc.o
TEST_OBJECTS := $(OBJECTS:.o=_test.o)
DEPS := $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

.PHONY: all build test clean

all : build test

build : $(PATHB)oc

test: $(PATHB)minunit
  valgrind $(PATHB)minunit

clean:
  -rm -rf $(BUILD_PATHS)

# build directories

$(PATHB):
  -mkdir $(PATHB)

$(PATHO): $(PATHB)
  -mkdir $(PATHO)

# object files

-include $(OBJECTS:.o=.d)

$(PATHO)%_test.o : $(PATHS)%.c $(PATHO)
  $(CC) $(ALL_CFLAGS) -c $< -DTEST -o $@ -MD -MP

$(PATHO)%.o : $(PATHS)%.c $(PATHO)
  $(CC) $(ALL_CFLAGS) -c $< -o $@ -MD -MP

# executable

$(PATHB)oc: $(PATHO)main.o $(OBJECTS)
  $(CC) -o $@ $^ -lm

$(PATHB)minunit: $(PATHO)minunit.o $(TEST_OBJECTS)
  $(CC) -o $@ $^ -lm
