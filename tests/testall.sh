# /bin/sh

\rm -f *.diff
\rm -f *.tmp
\rm -f *.out*

if [ "$my_bin" = "" ];
then
    export my_bin=/home/sloot/usr/local/bin
fi

for file in testfolia2text testfoliatxt testfoliaalto testcollect
do ./testone.sh $file
done
