# Object File Linker

A C++ implementation of a linker that combines multiple object files into a single executable binary.

## Overview
The `CLinker` class processes object files with a custom binary format and links them together by resolving function dependencies and generating the final executable code.

## Key Features
- **File Processing**: Reads binary object files with custom format containing headers, exported functions, imported functions, and code sections
- **Symbol Resolution**: Resolves function dependencies between object files and detects duplicate or undefined symbols
- **Dead Code Elimination**: Uses breadth-first search starting from an entry point to include only functions that are actually needed
- **Address Patching**: Updates function call addresses in the final executable to point to the correct locations
- **Error Handling**: Comprehensive error detection for missing files, duplicate symbols, and undefined references

## Object File Format
Each object file contains:
- Header with counts of exported/imported functions and code size
- List of exported functions (name + offset)
- List of imported functions (name + reference locations)
- Binary code section

## Usage
```cpp
CLinker linker;
linker.addFile("file1.o")
      .addFile("file2.o")
      .linkOutput("executable", "main");
```

## Error Conditions
- File reading failures
- Duplicate symbol definitions
- Undefined symbol references
- Missing entry point function

The linker ensures only necessary functions are included in the final executable, optimizing the output size while maintaining all required dependencies.
