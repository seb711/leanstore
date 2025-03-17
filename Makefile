.PHONY: module
BUILD_DIR = build

module: install-dependencies build-shared cmake-configure
LIBFAKEOSVDIR=$(OSV_BASE)/libfakeosv
LIB_SHARED = $(LIBFAKEOSVDIR)/libfakeosv.so

.PHONY: build-shared
build-shared:
	$(MAKE) -C $(LIBFAKEOSVDIR)

# Install required dependencies using apt-get
.PHONY: install-dependencies
install-dependencies:
	sudo apt-get update
	sudo apt-get install -y cmake libaio-dev libsnappy-dev zlib1g-dev \
		libbz2-dev liblz4-dev libzstd-dev librocksdb-dev liblmdb-dev \
		libwiredtiger-dev liburing-dev

# Configure the project with CMake, specifying GCC 12 as the compiler, linking against libtbb, and adding -fPIC for shared lib
.PHONY: cmake-configure
cmake-configure:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug -DLEANSTORE_INCLUDE_OSV=1\
		-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
		-DCMAKE_C_FLAGS="-fPIC" \
		-DCMAKE_CXX_FLAGS="-fPIC  -DMEAN_USE_JOBBING" -DLIBFAKEOSV_PATH=$(LIB_SHARED) .. && make -j

# Clean the build directory
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)