VERSION ?= $(shell sed -nE 's/^project.c2t VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)
TAG := v$(VERSION)

.PHONY: all linux macos windows run size clean embedded tag push release

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

tag:
	@echo "Creating and pushing tag $(TAG)..."
	git tag -a $(TAG) -m "Release $(TAG)"
	git push origin $(TAG)

push:
	git push origin main
	git push origin --tags

release:
	@if [ -z "$(V)" ]; then \
		echo "Error: Specify version V, e.g.: make release V=0.2.1"; \
		exit 1; \
	fi
	sed -i -E 's/project\(c2t VERSION [0-9]+\.[0-9]+\.[0-9]+/project(c2t VERSION $(V)/' CMakeLists.txt
	git commit -am "Bump version to $(V)"
	git tag -a v$(V) -m "Release v$(V)"
	git push origin main
	git push origin v$(V)

