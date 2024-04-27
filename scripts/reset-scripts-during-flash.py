# simple script to handle first flash of nuggeto
import esptool

#  # Patch to force esptool to use no_reset option. This is necessary because
#  # arduino-cli does not provide an option to forward this flag. This may become
#  # unncessary when esp32s2 is better supported.
#  RUN sed -i '/.*args = parser.parse_args(argv)/a\ \ \ \ args.after="no_reset"' $(grep -r parser.parse_args /root/.arduino15/packages/esp32/tools/esptool_py/ -l)
#  
#  COPY RubberNugget ./RubberNugget
#  COPY fatfs ./fatfs
#  COPY scripts ./scripts
#  RUN ./scripts/build_web_ui.sh
#  ARG FATFS_OUTPUT_FILE
#  ARG FATFS_FROM_DIR
#  RUN mkdir build
#  RUN OUTPUT_FILE=${FATFS_OUTPUT_FILE} DIR=${FATFS_FROM_DIR} ./fatfs/generate_fs.sh && mv ./fatfs/${FATFS_OUTPUT_FILE} ./build
#  RUN ./arduino-cli compile -b esp32:esp32:esp32s2 RubberNugget \
#      --build-property build.partitions=noota_3gffat \
#      --build-property build.cdc_on_boot=1 \
#      --output-dir=./build

# Makefile
#  build: submodules
#  	docker build . --file Dockerfile --build-arg FATFS_OUTPUT_FILE=$(FATFS) --build-arg FATFS_FROM_DIR=default --build-arg ARDUINO_CLI_VERSION=$(ARDUINO_CLI_VERSION) --tag rubber-nugget
#  
#  flash: check-port build
#  	docker create --name $(CONTAINER_NAME) --device=$(PORT) -t rubber-nugget:latest
#  	docker start $(CONTAINER_NAME)
#  	docker exec $(CONTAINER_NAME) bash -c \
#  		'./arduino-cli upload -b esp32:esp32:esp32s2 --port $(PORT) RubberNugget/ && sleep 2'
#  ifeq ($(RESET_SCRIPTS_DURING_FLASH), true)
#  	docker exec $(CONTAINER_NAME) bash -c \
#  		'python3 -m esptool --after no_reset write_flash 0x111000 build/$(FATFS)'
#  endif
#  	docker rm --force $(CONTAINER_NAME)
#  
#  generate_bin: build
#  	docker create --name $(CONTAINER_NAME) -t rubber-nugget:latest
#  	docker start $(CONTAINER_NAME)
#  	docker cp $(CONTAINER_NAME):/app/build .
#  	docker rm --force $(CONTAINER_NAME)
#  	cp usb_nugget_bin_template usb_nugget.bin
#  	dd of=usb_nugget.bin if=build/RubberNugget.ino.bin seek=65536 bs=1 conv=notrunc
#  	dd of=usb_nugget.bin if=build/$(FATFS) seek=1118208 bs=1 conv=notrunc count=3076096
