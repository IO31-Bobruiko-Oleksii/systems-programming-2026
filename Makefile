IMAGE ?= stm32-build

.PHONY: image build flash monitor clean

image:
	docker build -t $(IMAGE) docker/

build: image
	docker run --rm -v $(PWD):/workspace -w /workspace \
		--user $$(id -u):$$(id -g) $(IMAGE) \
		make -C firmware all

flash: build
	wokwi-cli --timeout 15000 --serial-log-file docs/screenshots/last-run-serial.log --expect-text "TIM3 PWM"

monitor:
	wokwi-cli --timeout 0

clean:
	rm -rf firmware/build
