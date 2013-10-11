all: build serve
	echo "Done!"

build: src/GotTheTime.c
	./waf build

serve:
	python -m SimpleHTTPServer 8000
