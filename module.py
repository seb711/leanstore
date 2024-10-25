from osv.modules import api

default = api.run("/tpcc -target_gib=1 --ssd_path=/test --worker_threads=2 --pp_threads=1 --dram_gib=2 --csv_path=./log --free_pct=1 --contention_split --xmerge --print_tx_console --run_for_seconds=60 --isolation_level=si")
