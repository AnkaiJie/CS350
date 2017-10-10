ASSN=$1
CMD=$2
CMD+=";q"
RUNS=$3
fname="ajieTestOutput.txt"

> $fname

for i in $(seq 1 $RUNS);
do
	if [[ $(($i % 100)) == 0 ]]; then
		echo "Run $i on $CMD"
	fi
	sys161 kernel-ASST$ASSN "$CMD" 2>&1 | tee -a $fname > /dev/null
done

cat $fname | egrep "fail|FAIL"
