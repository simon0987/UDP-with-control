all: sender receiver

sender: sender.cpp
	g++ -o $@ $^
receiver: receiver.cpp
	g++ -o $@ $^
clean:
	rm -rf sender receiver