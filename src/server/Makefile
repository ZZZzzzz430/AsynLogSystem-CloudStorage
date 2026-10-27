test:Test.cpp base64.cpp
	g++ -o $@ $^ -std=c++17 -lpthread -lstdc++fs -ljsoncpp -lbundle -levent 
gdb_test:Test.cpp base64.cpp $(wildcard *.hpp ../../log_system/logs_code/*.hpp ../../log_system/logs_code/backlog/*.hpp) base64.h bundle.h
	g++ -g3 -O0 -fno-omit-frame-pointer -o $@ Test.cpp base64.cpp -std=c++17 -lpthread -lstdc++fs -ljsoncpp -lbundle -levent
# 数据库是可选的独立挖空练习；不改变上面 test/gdb_test 的默认源码和依赖。
# TODO DB06：等连接/仓储/认证验收完成后，再把库接到真正的服务目标。
MYSQL_SCAFFOLD_BUILD_DIR ?= ../../build/mysql-scaffold
MYSQL_SCAFFOLD_CMAKE_ARGS ?=
mysql-scaffold:
	cmake -S database -B "$(MYSQL_SCAFFOLD_BUILD_DIR)" $(MYSQL_SCAFFOLD_CMAKE_ARGS)
	cmake --build "$(MYSQL_SCAFFOLD_BUILD_DIR)"
.PHONY:clean mysql-scaffold
clean:
	rm -rf test gdb_test ./deep_storage ./low_storage ./logfile storage.data
