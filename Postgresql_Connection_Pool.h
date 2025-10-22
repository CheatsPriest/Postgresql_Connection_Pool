// Postgresql_Connection_Pool.h : включаемый файл для стандартных системных включаемых файлов
// или включаемые файлы для конкретного проекта.

#pragma once

#include <Windows.h>

#include <memory>
#include <iostream>
#include <pqxx/pqxx>
#include <string>
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


template<bool countRequestsAndEdicts = false, bool enableThreadsHealthCare=true>
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

	std::condition_variable doctor_cv;

	std::mutex commitMutex;

	std::vector<std::thread> pool;
	std::vector<std::thread> doctors;

	std::vector<std::atomic_flag> health_flags;

	std::queue<std::pair<size_t, std::string>> tasks;
	std::unordered_map<size_t, Task> results;

	const size_t poolSize;

public:

	ConnectionPool(std::string auth, size_t numWorkers = 4) : authString(auth), poolSize(numWorkers){
		
		
		if constexpr (enableThreadsHealthCare) {
			
			std::vector<std::atomic_flag> buf(numWorkers);
			health_flags = std::move(buf);
			doctors.reserve(1);
			doctors.emplace_back(&ConnectionPool::health_care, this);
		}

		std::atomic_thread_fence(std::memory_order_seq_cst);
		pool.reserve(numWorkers);
		for (size_t i = 0; i < numWorkers; i++) {
			
			pool.emplace_back(&ConnectionPool::process, this, i);
			
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
		doctor_cv.notify_all();
		queue_cv.notify_all();
		informer_cv.notify_all();
		for (auto& el : pool) {
			if(el.joinable()) el.join();
		}
		if constexpr (enableThreadsHealthCare) {
			for (auto& el : doctors) {
				if (el.joinable()) el.join();
			}
		}
	}

private:

	void process(size_t threadId) {
		pqxx::connection connectionObject(authString);


		while (!quite) {

			std::unique_lock<std::mutex> locker(queue_mtx);
			queue_cv.wait(locker, [this, threadId]()->bool {
				if constexpr (enableThreadsHealthCare) health_flags[threadId].clear(std::memory_order_relaxed);
				return !tasks.empty() or quite; 
				});

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

	void health_care() {
		std::mutex lock;

		while (!quite) {
			std::unique_lock<std::mutex> lock_ptr(lock);
			doctor_cv.wait_for(lock_ptr, std::chrono::seconds(60), [this]()->bool {return quite.load(); });
			if (quite)return;

			for (auto& el : health_flags) {
				el.test_and_set();
			}
			queue_cv.notify_all();//Будим всех чтобы они мне проставили false

			doctor_cv.wait_for(lock_ptr, std::chrono::seconds(40), [this]()->bool {return quite.load(); });
			if (quite)return;
		
			for (size_t i = 0; i < health_flags.size(); ++i) {
				if (health_flags[i].test_and_set()) {
					if (pool[i].joinable()) {
						HANDLE thread_handle = pool[i].native_handle();
						TerminateThread(thread_handle, 0);
						pool[i].detach();  
						pool[i] = std::thread(&ConnectionPool::process, this, i);
					}
					else {
						pool[i] = std::thread(&ConnectionPool::process, this, i);
					}
					health_flags[i].clear(std::memory_order_relaxed);
				}
			}

		}

	}

};


// TODO: установите здесь ссылки на дополнительные заголовки, требующиеся для программы.
