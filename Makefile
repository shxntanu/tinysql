db: src/main.c
	gcc src/*.c -o tinysql

run: tinysql
	./tinysql mydb.db

clean:
	rm -f db *.db

test: db
	python3 -m unittest test.test_insert

format: *.c
	clang-format -style=Google -i *.c
