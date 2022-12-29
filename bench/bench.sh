benchmarks=("blackscholes" "fluidanimate" "streamcluster" "swaptions" "splash2x.barnes" "splash2x.cholesky" "splash2x.fft" "splash2x.lu_cb" "splash2x.lu_ncb" "splash2x.ocean_cp" "splash2x.radiosity" "splash2x.radix" "splash2x.water_nsquared" "splash2x.water_spatial")

parsecmgmt="./bin/parsecmgmt"
data_process="process.py"

build_benchmarks() {
    cd $1

    for benchmark in ${benchmarks[@]};
    do	
	echo "Building ${benchmark}..."
        $parsecmgmt -a build -p $benchmark 1> /dev/null
    done
}

clean_benchmarks() {
    cd $1

    for benchmark in ${benchmarks[@]};
    do
        $parsecmgmt -a clean -p $benchmark
        $parsecmgmt -a uninstall -p $benchmark
    done
}

run_benchmarks() {
    configs_folder=$(pwd)/configs
    cd $1
    for i in {0..2};
    do
	cp "${configs_folder}/gcc_O${i}.bldconf" "./config/gcc.bldconf"
	echo "Building -O${i}..."
	build_benchmarks "."

	echo "Running -O${i}..."
	for benchmark in ${benchmarks[@]};
   	do
  	      resfile="./results/${benchmark}_O${i}.results"
 	      if [ -f "$resfile" ]; then
	            rm $resfile
	      fi

	      output=$($parsecmgmt -a run -p $benchmark -i simlarge)
 	      echo "$output" | grep "real" | sed -r 's/real.*([0-9]+m.*)s/\1/' >> $resfile
 	done
	echo "Cleaning -O${i}..."
	clean_benchmarks "."
    done
}

print_usage() {
    echo "placeholder"
}

if [ "$#" -ne 2 ]; then
    print_usage
    exit
fi

set -e


case $2 in

    "build")
        build_benchmarks $1
        ;;
    "clean")
        clean_benchmarks $1
        ;;
    "run")
        run_benchmarks $1
        ;;
    *)
        print_usage
        ;;
esac
