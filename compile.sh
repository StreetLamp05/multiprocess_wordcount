#!/bin/bash


FILE=$1
echo "Searching for" $FILE
if find . -name $1 | grep -q .; then
  echo "file found"
else
  echo "file not found"
  exit 0
fi
echo "----- compiling-------"
LENGTH=${#FILE}
echo $LENGTH

gcc -Wall -Wextra -Wconversion -Wsign-conversion -pedantic-errors $1 -o ${FILE%.*}".out"