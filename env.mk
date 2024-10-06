npm-root != npm root
npm-root-dir != dirname ${npm-root}
npm-lib := @tty-pt/qhash @tty-pt/libit
LDFLAGS += -lit -lqhash -ldb
