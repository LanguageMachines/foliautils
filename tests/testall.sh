# /bin/sh

\rm -f *.diff
\rm -f *.tmp
\rm -f *.out*

if [ "$my_bin" = "" ];
then
    export my_bin=/home/sloot/usr/local/bin
fi

for file in test2text testtxt testalto testcollect testcorrect \
	    testhocr testidf testpage testlangcat teststats
do ./testone.sh $file
done
