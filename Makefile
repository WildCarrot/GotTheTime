all: build serve
	echo "Done!"

build:
	./waf build

serve:
	python -m SimpleHTTPServer 8000
