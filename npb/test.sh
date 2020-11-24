#!/bin/bash

cmd1="python3.7 -m npb.run_npb --suite-list test_suite.def --threads=1,2,4,6,8 --iterations=3"
cmd2="python3.7 -m npb.run_npb --suite-list test_suite.def --threads=1,2,4,6,8,10,12,14,16 --iterations=3"

if [ $1 = "preview" ]; then
	cmd1+=" --preview"
	cmd2+=" --preview"
	shift
fi

echo "Testing..."

echo "Give pass:"
read -s PW

export PATH=~/toolchain/bin/:$PATH
export LD_LIBRARY_PATH=$(llvm-config --libdir)
export RESULT_DIR=${NPB_SCRIPT_DIR}/results/test

cp "${NPB_SCRIPT_DIR}"/config/make.def "${NPB_DIR}"/config
cp "${NPB_SCRIPT_DIR}"/config/test_suite.def "${NPB_DIR}"/config/suite.def

if [ ! -d "$RESULT_DIR" ]; then
echo
mkdir "$RESULT_DIR"
fi
rm -f "$RESULT_DIR"/*

echo $PW | sudo -S -E bash -c "$cmd1"
echo $PW | sudo -S -E bash -c "$cmd2"

for filename in "$RESULT_DIR"/*; do
	[ -e "$filename" ] || exit
	if grep -q "UNSUCCESSFUL" "$filename"; then
		echo "Not Verified!!!"
		exit
	fi
done
echo "Verified!!!"
