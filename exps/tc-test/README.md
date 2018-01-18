## Guide for running the iperf3 and sockperf microbenchmarks

### Configuration

None other than general ../env/README.md setup

### Running the experiment


#### Running a single configuration by hand:

1. Start sinks by hand
```./tc_test_start_progs.py --dir sink --configf configs/test.yaml```

2. Start srcs by hand
```./tc_test_start_progs.py --dir src --configf configs/test.yaml```
