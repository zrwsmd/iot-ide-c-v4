.PHONY: all clean cross sdk

# 本机编译（ARM64 上位机直接运行）
all:
	@mkdir -p build
	@cd build && cmake .. && make -j$$(nproc)
	@echo "✅ Build done: build/iot_ide"

# 交叉编译（x86 开发机编译，产物传到 ARM64）
cross:
	@mkdir -p build-cross
	@cd build-cross && cmake .. -DCMAKE_TOOLCHAIN_FILE=../aarch64-toolchain.cmake && make -j$$(nproc)
	@echo "✅ Cross-build done: build-cross/iot_ide"

# 下载并解压 SDK（只需执行一次）
sdk:
	@chmod +x scripts/download_sdk.sh && ./scripts/download_sdk.sh

clean:
	@rm -rf build build-cross
	@echo "✅ Cleaned"
