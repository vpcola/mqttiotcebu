deps_config := \
	/Users/Vergil/esp/esp-idf/components/app_trace/Kconfig \
	/Users/Vergil/esp/esp-idf/components/aws_iot/Kconfig \
	/Users/Vergil/esp/esp-idf/components/bt/Kconfig \
	/Users/Vergil/esp/esp-idf/components/esp32/Kconfig \
	/Users/Vergil/esp/esp-idf/components/ethernet/Kconfig \
	/Users/Vergil/esp/esp-idf/components/fatfs/Kconfig \
	/Users/Vergil/esp/esp-idf/components/freertos/Kconfig \
	/Users/Vergil/esp/esp-idf/components/log/Kconfig \
	/Users/Vergil/esp/esp-idf/components/lwip/Kconfig \
	/Users/Vergil/esp/esp-idf/components/mbedtls/Kconfig \
	/Users/Vergil/esp/esp-idf/components/openssl/Kconfig \
	/Users/Vergil/esp/esp-idf/components/spi_flash/Kconfig \
	/Users/Vergil/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/Users/Vergil/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/Users/Vergil/esp/Projects/iotcebu/main/Kconfig.projbuild \
	/Users/Vergil/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/Users/Vergil/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
