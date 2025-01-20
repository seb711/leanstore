.PHONY: module
BUILD_DIR = build

module: install-dependencies cmake-configure

# Install required dependencies using apt-get
.PHONY: install-dependencies
install-dependencies:
	sudo apt-get update
	sudo apt-get install -y cmake libtbb-dev libaio-dev libsnappy-dev zlib1g-dev \
		libbz2-dev liblz4-dev libzstd-dev librocksdb-dev liblmdb-dev \
		libwiredtiger-dev liburing-dev

# Configure the project with CMake, specifying GCC 12 as the compiler, linking against libtbb, and adding -fPIC for shared lib
.PHONY: cmake-configure
cmake-configure:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release -DLEANSTORE_INCLUDE_OSV=0\
		-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
		-DCMAKE_C_FLAGS="-fPIC" \
		-DCMAKE_CXX_FLAGS="-fPIC" .. && make -j

# Clean the build directory
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)