benchmarks=( "blackscholes"  "streamcluster" "swaptions" "splash2x.cholesky" "splash2x.fft"  "splash2x.ocean_cp" "splash2x.radix" "splash2x.water_nsquared" "splash2x.water_spatial" "splash2x.lu_cb" "splash2x.lu_ncb")

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
	cp "${configs_folder}/gcc_O0_$1.bldconf" "./config/gcc.bldconf"
	echo "Building -O0.."
	build_benchmarks "."

	echo "Running -O0.."
	for benchmark in ${benchmarks[@]};
   	do
        echo "Running ${benchmark}.."

  	    resfile="./results/${benchmark}.results"
 	      if [ -f "$resfile" ]; then
	            rm $resfile
	      fi
        for i in {0..10};
        do
	          output=$($parsecmgmt -a run -p $benchmark -i simlarge)
            crash=$(echo "${output}" | grep -i "dumped" || true);
            if [ ! -z "$crash" ];then
               echo "${benchmark} crashed!"
               exit 1;
            fi
 	          echo "$output" | grep "real" | sed -r 's/real.*([0-9]+m.*)s/\1/' >> $resfile
        done
 	done
	echo "Cleaning -O0.."
	clean_benchmarks "."
}

print_usage() {
    echo "bench.sh [parsec_benchmark_folder] [command]"
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
