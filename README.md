[![Makefile Examples CI](https://github.com/MohamedElashri/jaggedcpp/actions/workflows/build.yml/badge.svg)](https://github.com/MohamedElashri/jaggedcpp/actions/workflows/build.yml)

# Jagged Array C++ Library

## Description

This project aims to provide a C++ library for handling jagged arrays, similar to the functionality offered by the Awkward Array library in Python. The library supports various operations, including element access, appending, flattening, reshaping, padding, and clipping. It also provides several reduction methods and statistical functions like mean, variance, and standard deviation.


This is still in early development and while the goal is to implement as much as possible from awkward array, there are still a lot of things to do. I mainly work on these as I need them in a project so if you need something that is not implemented yet, feel free to open an issue or a pull request. I focus mainly on methods that I need for my personal projects, but I am open to suggestions.


No documentation is available yet, but I am working on it. For now, you can check out the examples in the `examples` folder to get an idea of how to use the library.
## Features

- Element-wise access and modification
- Support for various jagged array operations
- Reduction methods (`all`, `any`, `sum`, `prod`, `max`, `min`)
- Statistical methods (`moment`, `mean`, `var`, `std`)

## Prerequisites

- C++ Compiler with support for C++17 (e.g., `g++`)

## Compilation

This project uses a Makefile to automate the compilation process. To compile all example files, navigate to the project's root directory and execute:

```bash
make all
```

This will compile all example files located in the `examples` folder and store the compiled executables in `examples/obj`.

To compile a specific example, run:

```bash
make <example_name_without_extension>
```

To clean up and remove all compiled files, execute:

```bash
make clean
```



## License

This project is licensed under the MIT License. See the [LICENSE.md] file for details.

## Acknowledgments

This project was inspired by the python [Awkward Array library](https://awkward-array.org/doc/main/index.html) .
