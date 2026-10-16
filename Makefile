IMAGE ?= stm32-build

.PHONY: image build flash monitor clean

image:
	docker build -t $(IMAGE) docker/

build: image
	docker run --rm -v $(PWD):/workspace -w /workspace \
		--user $$(id -u):$$(id -g) $(IMAGE) \
		make -C firmware -f build.mk all

flash: build
	wokwi-cli --timeout 15000 --serial-log-file docs/screenshots/last-run-serial.log --expect-text "SELFTEST END total=5 passed=5 failed=0"

monitor:
	wokwi-cli --timeout 0

clean:
	rm -rf firmware/build

.PHONY: test
test:
	@tests/run.sh

.PHONY: check-deps check
check-deps:
	@scripts/check_deps.sh

check: check-deps test

.PHONY: capture
capture:
	scripts/capture.sh $(NAME)

.PHONY: coverage
coverage:
	@tests/coverage.sh
