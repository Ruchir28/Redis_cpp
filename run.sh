#!/bin/bash
filename=$1
echo $filename;
if [ "${#filename}" -eq 0 ]; then
    echo "Error: Filename is empty."
    exit 1
fi

filePath="$filename.cpp"
outputexecutablePath="./executables/$filename"
# compiling the file
clang++ -std=c++11 $filePath -o $outputexecutablePath
# executing the file 
./$outputexecutablePath