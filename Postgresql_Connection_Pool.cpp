// Postgresql_Connection_Pool.cpp: определяет точку входа для приложения.
//

#include "Postgresql_Connection_Pool.h"

using namespace std;

int main()
{
	


	ConnectionPool<true> pool("host=localhost port=5432 dbname=#### user=postgres password=####");
	size_t id1 = pool.request_async("select * from servers limit 10");
	size_t id2 = pool.request_async("select * from servers limit {}", 3);
	
	auto res1 = pool.get_result_async(id1);
	auto res2 = pool.get_result_async(id2);

	for (const auto& row : res1) {
		for (const auto& field : row) {
			std::cout << field.name() << ": " << field.c_str() << "\t";
		}
		std::cout << std::endl;
	}

	for (const auto& row : res2) {
		for (const auto& field : row) {
			std::cout << field.name() << ": " << field.c_str() << "\t";
		}
		std::cout << std::endl;
	}

	return 0;
}
