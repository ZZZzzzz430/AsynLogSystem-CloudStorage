test:Test.cpp base64.cpp
	g++ -o $@ $^ -std=c++17 -lpthread -lstdc++fs -ljsoncpp -lbundle -levent 
gdb_test:Test.cpp base64.cpp $(wildcard *.hpp ../../log_system/logs_code/*.hpp ../../log_system/logs_code/backlog/*.hpp) base64.h bundle.h
	g++ -g3 -O0 -fno-omit-frame-pointer -o $@ Test.cpp base64.cpp -std=c++17 -lpthread -lstdc++fs -ljsoncpp -lbundle -levent
.PHONY:clean
clean:
	rm -rf test gdb_test ./deep_storage ./low_storage ./logfile storage.data
