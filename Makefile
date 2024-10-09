db: src/main.c
	gcc src/main.c -o tinysql

run: tinysql
	./tinysql mydb.db

clean:
	rm -f db *.db

test: db
	bundle exec rspec

format: *.c
	clang-format -style=Google -i *.c
