#!/bin/bash

filesdir=$1
searchstr=$2

# if missing param
if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Not all required parameters were provided."
    exit 1
fi

# if filesdir not a dir
if ! [ -d "$1" ]; then
    echo "\"${filesdir}\" is not a directory."
    exit 1
fi

cd ${filesdir}

# get X and Y and echo the next:
X=`find ${filesdir} -type f | wc -l`
Y=`grep -r ${searchstr} | wc -l`

echo The number of files are $X and the number of matching lines are $Y
