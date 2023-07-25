.PHONY: release pull

BUILD_TPCH=1
BUILD_ODBC=1

export

DUCKDB_DIRECTORY=
ifndef DUCKDB_DIR
	DUCKDB_DIRECTORY=./duckdb
else
	DUCKDB_DIRECTORY=${DUCKDB_DIR}
endif

pull:
	git submodule init
	git submodule update --recursive --remote

release:
# Have to actually cd here because the makefile assumes it's called from within duckdb
	cd ${DUCKDB_DIRECTORY} && $(MAKE) -C . release
	mkdir -p ./build/release && \
	cd build/release && \
	cmake -DCMAKE_BUILD_TYPE=Release ../.. && \
	cmake --build . --config Release
