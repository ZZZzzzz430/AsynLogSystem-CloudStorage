MYSQL_FLAGS := $(shell pkg-config --cflags mysqlclient)
MYSQL_LIBS := $(shell pkg-config --libs mysqlclient)
AUTH_SOURCES := database/MySqlConnection.cpp database/UserRepository.cpp database/SessionRepository.cpp auth/AuthService.cpp auth/Crypto.cpp
COMMON_LIBS := -lpthread -lstdc++fs -ljsoncpp -lbundle -levent $(MYSQL_LIBS) -lcrypto
COMMON_FLAGS := -std=c++17 $(MYSQL_FLAGS)
HEADERS := $(wildcard *.hpp database/*.hpp auth/*.hpp ../../log_system/logs_code/*.hpp)
test:Test.cpp base64.cpp $(AUTH_SOURCES) $(HEADERS)
	g++ -o $@ Test.cpp base64.cpp $(AUTH_SOURCES) $(COMMON_FLAGS) $(COMMON_LIBS)
gdb_test:Test.cpp base64.cpp $(AUTH_SOURCES) $(HEADERS) base64.h bundle.h
	g++ -g3 -O0 -fno-omit-frame-pointer -o $@ Test.cpp base64.cpp $(AUTH_SOURCES) $(COMMON_FLAGS) $(COMMON_LIBS)
MYSQL_SCAFFOLD_BUILD_DIR ?= ../../build/mysql-scaffold
MYSQL_SCAFFOLD_CMAKE_ARGS ?=
mysql-scaffold:
	cmake -S database -B "$(MYSQL_SCAFFOLD_BUILD_DIR)" $(MYSQL_SCAFFOLD_CMAKE_ARGS)
	cmake --build "$(MYSQL_SCAFFOLD_BUILD_DIR)"
.PHONY:clean mysql-scaffold
clean:
	rm -rf test gdb_test ./deep_storage ./low_storage ./logfile storage.data
