.PHONY: sim firmware flash clean help

help:
	@echo "Targets:"
	@echo "  make sim       Build and run the macOS SDL simulator"
	@echo "  make firmware  Build the Teensy 4.1 .hex"
	@echo "  make flash     Build firmware and upload to the Teensy"
	@echo "  make clean     Remove all PlatformIO build artifacts"

sim:
	./scripts/run-sim.sh

firmware:
	./scripts/build-firmware.sh

flash:
	./scripts/flash.sh

clean:
	rm -rf .pio
