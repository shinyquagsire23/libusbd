#!/bin/zsh
make clean && make
cd examples/keyboard/ ; make clean && make && cp ./example_keyboard ../../ && cd ../../ && ./example_keyboard