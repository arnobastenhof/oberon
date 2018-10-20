# Oberon Compiler

## Introduction

The current codebase ports a subset of Wirth's reference implementation for
the Oberon07 language to C (i.e., concerning the Oberon compiler, not the OS).
Some notable omissions and deviations are:
* No generation of object- and symbol files. Instead, the compiler combines
  with a RISC-0 emulator into a single executable, with an in-memory array
  serving as their interface. Compare this to the implementation of PL/0 found
  in prior editions of Algorithms + Data Structures = Programs.
* By extension, separate compilation and imports are unsupported. For I/O, the
  built-in functions are extended with primitives `Read`, `Write` and
  `WriteLn`. The first one takes an `INTEGER` or `CHAR` variable as argument,
  while the latter two may take a `STRING` argument as well. Finally,
  `WriteLn `'s argument is optional, outputting only a newline if absent. See
  `test/io.mod` for some examples.
* No support for floating-point numbers. In particular, the built-in functions
  operating thereon specified in the Oberon language report have been removed.
* No support for unsigned arithmetic. I.e., the built-in functions `ADC`, `SBC`
  and `UML` have been removed, with the RISC-0 emulator similarly ignoring the
  Carry bit.
* In general, type descriptors have not been implemented. As a result,
  dynamic memory allocation (via `NEW`) and case statements are not supported.
* Despite the absence of `NEW`, `POINTER`s can still be used, albeit only
  through recource to the unsafe `SYSTEM.ADR` and `SYSTEM.VAL` functions. See
  `test/pointer.mod` for an example.
Future versions may remedy some of these issues (or not). Note this project
mainly served self-educational purposes.

## Requirements

* GCC or Clang (C99-compatible)
* GNU Make
* [Optional] Valgrind (Remove from Makefile if not present on your system)

## Installation

Clone or download the sources. E.g,
```
wget https://github.com/arnobastenhof/oberon/archive/master.zip
unzip master.zip
```
To compile, type the following command from the project root,
```
make build
```
This creates an executable `oc` in the directory `build`. To run the test
suite, type
```
make test
```
The above two commands are combined into
```
make all
```

## Module overview

Largely following Wirth's reference implementation, the codebase is divided
into the following modules.

* Pool (`pool.h`, `pool.c`) exposes an arena-based memory allocator, for the
  most part constituting a simplified variant of the one used in LCC as
  described in Fraser and Hanson's "A retargetable C compiler". Rather than
  singalling an allocation failure through a special return value like
  `NULL`, instead a rudimentary implementation of exceptions is used (see
  `except.h`) to return control to a handler that may, e.g., clean up resources
  prior to exiting. This considerably simplifies application code. Note this is
  the only module that has no counterpart in Wirth's code, as Oberon features
  a garbage collector.
* ORS (`ors.h`, `ors.c`) contains the scanner.
* ORB (`orb.h`, `orb.c`) defines the symbol table.
* ORG (`org.h`, `org.c`) contains the code generator for Wirth's RISC-0
  architecture, described in his book "Compiler Construction".
* ORP (`orp.h`, `orp.c`) contains the parser.
* RISC (`risc.h`, `risc.c`) contains a RISC-0 emulator.

Finally, unit tests are implemented using a modest extension of Jera Design's
Minunit test framework (see `minunit.h` and `minunit.c`).

## Some notes on code style

* Oberon makes the export of global variables more safe by making them
  read-only for other modules, and the reference implementation for its
  compiler certainly makes use of this. The closest analogy for C would
  probably be to expose Java-style getters instead as part of a header, while
  keeping the variables themselves private to the implementation (i.e.,
  declared `static`). Though normally I would feel hestitant to use global
  variables with external linkage in C, I have nonetheless chosen to stay close
  to the source materials in this particular case.
* In his book 'C interfaces and implementations,' Hanson advocates the use
  of opaque pointers in header files to hide the implementation details of
  one's data structures. Though certainly the better programming practice, 
  especially when programming in the large, I have not followed this advice
  mainly to allow for stack allocation whenever possible (where you need to
  know a struct's size at compile time), as well as to stay more close to the
  source materials for this particular case.
* One aspect where I deviated from the source materials is in the ordering
  of function definitions. Oberon, like C, prohibits forward declarations for
  its procedure calls, but, unlike C, does not support prototypes. A typical
  Oberon source file will thus typically read very much like a linear
  narrative, with the definition of support functions preceding their use.
  For the current port, I have instead chosen to follow what I perceive as
  more standard practice in C by: (a) prototyping all static functions,
  (b) first defining all functions with external linkage, and (c) only then
  providing definitions for the static functions. (As an aside, when it comes
  to mutual recursion, Oberon works around its prohibition of forward
  references, albeit at a small runtime cost, by declaring procedure pointer
  variables and setting them in the module initializer.)
* Method names have been written in `UpperCamelCase`, type names and enum
  constants in `lowerCamelCase`, variable names in `lower_snake_case`, and
  `#define`d names in `UPPER_SNAKE_CASE`. In addition, `typedef`d names and
  `union`- and `struct` tags have been postfixed with `_t`, `_u` and `_s`,
  resp., while global variables similarly bear a `g_` prefix. Finally, enum
  constants begin with `k` (e.g., `kCondU`).
* Methods declared `extern` bear a prefix identifying their module. E.g.,
  `ORS_Init`.
* Statement blocks following keywords that signal conditional- and loop
  constructs are delimited by braces, with the sole exception of empty blocks,
  where I resort to a single properly indented `;`.

## Further reading

The primary influence for this work came from the following sources:
* The 2017 revision of Wirth's Compiler Construction, available at
  https://www.inf.ethz.ch/personal/wirth/CompilerConstruction/index.html
* The 2013 revision of Project Oberon (including the reference implementation
  for the Oberon07 compiler), available at
  https://www.inf.ethz.ch/personal/wirth/ProjectOberon/index.html
* Programming in Oberon, available at
  https://www.inf.ethz.ch/personal/wirth/Oberon/PIO.pdf

In addition, we have taken some inspiration from the following works, most
notably for memory management:
* Fraser, C.W., & Hanson, D.R. (1995). A retargetable C compiler: design and
  implementation. Addison-Wesley Longman Publishing Co., Inc.
* Hanson, D.R. (1996). C interfaces and implementations: techniques for
  creating reusable software. Addison-Wesley Longman Publishing Co., Inc.
