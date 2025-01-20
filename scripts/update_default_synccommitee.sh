#! /bin/bash
xxd -n default_synccommittee -i ../../in4/js/state | sed 's/^unsigned/const unsigned/' | clang-format > ../src/verifier/default_synccommittee.h


