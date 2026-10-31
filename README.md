# Rx Compiler

This repository contains a C++ Rx compiler front end with parsing and semantic
checking. LLVM IR and RV32IM code generation are not implemented yet.

The compiler accepts one Rx source path and exits with 0 when it accepts the
program or 1 when it rejects the program. Diagnostics are written to stderr.
