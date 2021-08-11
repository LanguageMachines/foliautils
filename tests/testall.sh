# /bin/sh

\rm -f *.diff
\rm -f *.tmp
\rm -f *.out*

if [ "$my_bin" = "" ];
then
    export my_bin=/home/sloot/usr/local/bin
fi

err_cnt=0
for file in test2text testtxt testalto testcorrect testcorbig \
	    testhocr testidf testpage testabby testlangcat teststats \
	    testpm testclean testwordtranslate
do
    bash ./testone.sh $file
    if [ $? -ne 0 ]
    then
	err_cnt=$((err_cnt+1))
    fi
done
exit $err_cnt
