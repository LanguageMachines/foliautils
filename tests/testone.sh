# /bin/sh -x

if [ "$my_bin" = "" ];
then
    export my_bin=/home/sloot/usr/local/bin
fi

OK="\033[1;32m OK  \033[0m"
FAIL="\033[1;31m  FAILED  \033[0m"
KNOWNFAIL="\033[1;33m  KNOWN FAILURES  \033[0m"

echo $my_bin

for file in $1
do if test -x $file
   then
       \rm -f $file.diff
       \rm -f $file.err
       \rm -f $file.out
       echo -n "testing $file "
       ./$file > $file.err 2>&1
       diff -w $file.out $file.ok >& $file.diff
       if [ $? -ne 0 ];
       then
           diff -w $file.diff $file.diff.known >& /dev/null
      	   if [ $? -ne 0 ];
	   then
	       echo -e $FAIL;
	       echo "differences logged in $file.diff";
	   else
	       echo -e $KNOWNFAIL;
	       \rm -f $file.diff
	   fi
       else
	   echo -e $OK
	   rm $file.diff
       fi
   else
       echo "file $file not found"
   fi
done