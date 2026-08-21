.PHONY: all linux macos windows run size clean

all: linux

linux:
	cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
	cmake --build build/linux

windows:
	cmake -S . -B build/windows -G Ninja \
		-DCMAKE_BUILD_TYPE=MinSizeRel \
		-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
	cmake --build build/windows

macos:
	cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
	cmake --build build/macos

run: linux
	./build/linux/c2t

size:
	@find build -maxdepth 2 -type f \( -name c2t -o -name 'c2t.exe' \) \
		-exec du -h {} +

clean:
	cmake -E remove_directory build/linux
	cmake -E remove_directory build/windows
	cmake -E remove_directory build/macos

embedded:
	python3 tools/embed_config.py build/linux/c2t \
	--config customer.env \
	--output dist/c2t-customer \
	--force
