#!/bin/bash -e

conan install . -b missing -if i/cmake -of of -pr:b=default -s compiler.cppstd=20 -s build_type=Debug
conan install . -b missing -if i/cmake -of of -pr:b=default -s compiler.cppstd=20 -s build_type=Release

