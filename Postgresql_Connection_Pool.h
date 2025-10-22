// Postgresql_Connection_Pool.h : включаемый файл для стандартных системных включаемых файлов
// или включаемые файлы для конкретного проекта.

#pragma once

#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <chrono>
#include <type_traits>
#include <tuple>
#include <utility>
#include <format>
#include <condition_variable>


template<bool countRequestsAndEdicts = false>
class ConnectionPool
{
private:


	enum class TaskStatus {
		processing, completed
	};

	struct Task {
		size_t id;
		//std::string command;
		pqxx::result response;
		TaskStatus status = TaskStatus::processing;
		Task(size_t id_) : id(id_) {}
		Task() {

		}
		pqxx::result returnResponse()&& {
			return std::move(response);
		}
	};

	
	std::atomic<size_t> requests_processed{ 0 };
	std::atomic<size_t> edicts_processed{ 0 };
	std::atomic<size_t> active_connections{ 0 };

	std::string authString;

	std::atomic<bool> quite{ false };
	std::atomic<size_t> freeId{ 1 };

	std::mutex queue_mtx;
	std::condition_variable queue_cv;

	std::mutex informer_mtx;
	std::condition_variable informer_cv;
	std::mutex m_q;

	std::mutex commitMutex;

	std::vector<std::thread> pool;

	std::queue<std::pair<size_t, std::string>> tasks;
	std::unordered_map<size_t, Task> results;

	const size_t poolSize;

public:

	ConnectionPool(std::string auth, size_t numWorkers = 4) : authString(auth), poolSize(numWorkers){
		pool.reserve(numWorkers);
		for (size_t i = 0; i < numWorkers; i++) {
			pool.emplace_back(&ConnectionPool::process, this);
		}

	}
	template<typename ... Args>
	pqxx::result request(std::string_view command, Args&& ... arg) {

		std::string finalString = "";
		if constexpr (sizeof...(arg) > 0)finalString = std::vformat(command, std::make_format_args(arg...));
		else finalString = std::string(command);

		const size_t taskId = freeId++;

		
		std::unique_lock<std::mutex> locker_ptr(informer_mtx);
		auto [iter, inserted] = results.emplace(taskId, Task(taskId));
		auto& task = iter->second;  
		locker_ptr.unlock();
		
		{
			std::lock_guard<std::mutex> lock(queue_mtx);
			tasks.push({ taskId, finalString });
			queue_cv.notify_one();
		}
		
		
		std::unique_lock<std::mutex> locker(informer_mtx);
		informer_cv.wait(locker, [&task]()->bool {
			return task.status == TaskStatus::completed;
			});

		pqxx::result res = std::move(task.response);
		
		results.erase(iter);

		if constexpr (countRequestsAndEdicts) requests_processed++;

		return res;

	}

	

	

	//ASYNC UI
	//Returns task id
	template<typename ... Args>
	size_t request_async(std::string_view command, Args&& ... arg) {

		std::string finalString = "";
		if constexpr (sizeof...(arg) > 0)finalString = std::vformat(command, std::make_format_args(arg...));
		else finalString = std::string(command);

		const size_t taskId = freeId++;


		std::unique_lock<std::mutex> locker_ptr(informer_mtx);
		auto [iter, inserted] = results.emplace(taskId, Task(taskId));
		locker_ptr.unlock();

		{
			std::lock_guard<std::mutex> lock(queue_mtx);
			tasks.push({ taskId, finalString });
			queue_cv.notify_one();
		}

		return taskId;

	}
	//Get result by id
	pqxx::result get_result_async(size_t taskId) {

		std::unique_lock<std::mutex> locker(informer_mtx);
		auto iter = results.find(taskId);

		if (iter == results.end())throw std::runtime_error("There is no id in results that is equal to taskId.");

		informer_cv.wait(locker, [&iter]()->bool {
			return iter->second.status == TaskStatus::completed;
			});

		pqxx::result res = std::move(iter->second.response);

		results.erase(iter);

		if constexpr (countRequestsAndEdicts) requests_processed++;

		return res;
	}
	
	/*
	* Execute SQL command asynchronously (fire-and-forget)
	* Doesn't return any result, doesn't wait for completion
	*/
	template<typename ... Args>
	void edict(std::string_view command, Args&& ... arg) {
		std::string finalString = "";
		if constexpr (sizeof...(arg) > 0)finalString = std::vformat(command, std::make_format_args(arg...));
		else finalString = std::string(command);


		{
			std::lock_guard<std::mutex> lock(queue_mtx);
			tasks.push({ 0, finalString });
			queue_cv.notify_one();
		}

		if constexpr (countRequestsAndEdicts) edicts_processed++;

	}

	~ConnectionPool() {
		if constexpr (countRequestsAndEdicts) std::cout << "REQUESTS: " << requests_processed << std::endl;
		if constexpr (countRequestsAndEdicts) std::cout << "EDICTS: " << edicts_processed << std::endl;
		quite = true;
		queue_cv.notify_all();
		informer_cv.notify_all();
		for (int i = 0; i < pool.size(); i++) {
			pool[i].join();
		}
	}

private:

	void process() {
		pqxx::connection connectionObject(authString);


		while (!quite) {

			std::unique_lock<std::mutex> locker(queue_mtx);
			queue_cv.wait(locker, [this]()->bool {return !tasks.empty() or quite; });

			if (!tasks.empty() and !quite) {
				pqxx::work worker(connectionObject);

				std::pair<size_t, std::string> curTask = std::move(tasks.front());
				tasks.pop();
				locker.unlock();



				if (curTask.first != 0) {
					pqxx::result res = worker.exec(curTask.second);
					worker.commit();
					std::lock_guard<std::mutex> informer_guard(informer_mtx);
					results[curTask.first].response = std::move(res);
					results[curTask.first].status = TaskStatus::completed;
				}
				else {
					worker.exec(curTask.second);
					worker.commit();
				}
			}

			informer_cv.notify_all();

		}

	}



};


// TODO: установите здесь ссылки на дополнительные заголовки, требующиеся для программы.
