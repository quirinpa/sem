npm-root != npm root
npm-root-dir != dirname ${npm-root}
npm-lib := @tty-pt/qdb @tty-pt/libit
LDFLAGS += -lit -lqdb -ldb
