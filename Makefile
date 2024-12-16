vzip: paral.c
	gcc paral.c -lz -Wall -o vzip

vzips: serial.c
	gcc paral.c -lz -Wall -o vzip

test:
	rm -f video.vzip
	./vzip frames
	./check.sh

clean:
	rm -f vzip video.vzip

