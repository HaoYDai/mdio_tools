#!/bin/bash

HOST=aarch64-linux-gnu
CC=aarch64-linux-gnu-gcc

compile() {

	if [[ ${2} == "tool" ]]; then # tool-chain
	PREFIX=
	else # output
	PREFIX=
	fi

	cd ${1}
	autoreconf -i
	./configure --host=${HOST} ${CC} --prefix=${PREFIX}
	make && make install
}

compile ./libmnl tool
compile ./libmdionetlink tool



cd mdionetlink
make




