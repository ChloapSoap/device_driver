# device_driver

This is a device driver which acts as an interface for read/write commands to/from
physical memory.

You must download the tarball to test this.  Most files are visible here, but a few workload
files for input aren't included for sake of brevity.

Once you've downloaded the tarball, you can test the driver and cache with the following commands:

$ make clean && make

$ ./block_sim -v -c <cache_size> workload/cmpsc311-sum19-assign4-workload.txt
