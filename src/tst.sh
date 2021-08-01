#! /bin/sh

if test -d ../tests;
then
    cd ../tests

    \rm -f *.diff
    \rm -f *.tmp
    \rm -f *.out*

    for file in test2text testtxt testalto testcorrect testcorbig \
		testhocr testidf testpage testabby testlangcat teststats \
		testpm testclean testwordtranslate
    do bash ./testone.sh $file
    done
else
    exit 1
fi
