# /bin/sh

\rm -f *.diff
\rm -f *.tmp
\rm -f *.out*

if [ "$my_bin" = "" ];
then
    export my_bin=/home/sloot/usr/local/bin
fi

for file in test2text testtxt testalto testhocr testidf testpage testabby \
		      testpm testclean testlangcat testhyph testmerge \
		      testcorrect testcorbig teststats testwordtranslate
do bash ./testone.sh $file
done
