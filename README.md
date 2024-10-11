# tinySQL

Writing an SQLite clone from scratch in C. This project was heavily inspired from [cstack](https://github.com/cstack)'s amazing [guide](https://cstack.github.io/db_tutorial/) on writing an SQL Database in C, and aims to build on top of it by extending its functionality to make it as close to SQLite as possble.

> This project is unfinished and in active development. Pull requests, suggestions and any sort of help in general is always welcome :)

## Architecture

![sqlite architecture](https://cstack.github.io/db_tutorial/assets/images/arch2.gif)

_sqlite architecture (https://www.sqlite.org/arch.html)_

A query goes through a chain of components in order to retrieve or modify data. The _front-end_ consists of the:

1. tokenizer
2. parser
3. code generator

The input to the front-end is a SQL query. the output is sqlite virtual machine bytecode (essentially a compiled program that can operate on the database).

The _back-end_ consists of the:

1. virtual machine
2. B-tree
3. pager
4. os interface

## Installing

1. macOS: Install CMake using Homebrew
2. Linux (Ubuntu): Follow [this](https://vpsie.com/knowledge-base/how-to-install-cmake-on-ubuntu-20-04/) guide. Make sure to download the latest version of CMake.

## Try it for yourself

### Build

Run `make`, this will build the binary, then run `./tinysql mydb.db` to start.

### Test

- Run all tests at once by running

```bash
python -m unittest discover
```

- To run a specific test (e.g.):

```bash
python -m unittest test.test_insert
```

- To run a specific test case (e.g.):

```bash
python -m unittest test.test_insert.TestInsert.test_persistence
```
