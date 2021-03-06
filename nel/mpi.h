#ifndef NEL_MPI_H_
#define NEL_MPI_H_

#include "network.h"
#include "simulator.h"

namespace nel {

using namespace core;

enum class message_type : uint64_t {
	ADD_AGENT = 0,
	ADD_AGENT_RESPONSE = 1,
	MOVE = 2,
	MOVE_RESPONSE = 3,
	GET_POSITION = 4,
	GET_POSITION_RESPONSE = 5,
	STEP_RESPONSE = 6
};

template<typename Stream>
inline bool read(message_type& type, Stream& in) {
	uint64_t v;
	if (!read(v, in)) return false;
	type = (message_type) v;
	return true;
}

template<typename Stream>
inline bool write(const message_type& type, Stream& out) {
	return write((uint64_t) type, out);
}

struct async_server {
	std::thread server_thread;
	socket_type server_socket;
	server_state state;
};

inline bool receive_add_agent(socket_type& connection, simulator& sim) {
	agent_state* new_agent = sim.add_agent();
	return write(message_type::ADD_AGENT_RESPONSE, connection)
		&& write((uint64_t) new_agent, connection);
}

inline bool receive_move(socket_type& connection, simulator& sim) {
	uint64_t agent_handle;
	direction dir;
	unsigned int num_steps;
	if (!read(agent_handle, connection) || !read(dir, connection) || !read(num_steps, connection))
		return false;
	bool result = sim.move(*((agent_state*) agent_handle), dir, num_steps);
	return write(message_type::MOVE_RESPONSE, connection)
		&& write(result, connection);
}

inline bool receive_get_position(socket_type& connection, simulator& sim) {
	uint64_t agent_handle;
	if (!read(agent_handle, connection))
		return false;
	position location = sim.get_position(*((agent_state*) agent_handle));
	return write(message_type::GET_POSITION_RESPONSE, connection)
		&& write(location, connection);
}

void server_process_message(socket_type& connection, simulator& sim) {
	message_type type;
	read(type, connection);
	switch (type) {
		case message_type::ADD_AGENT:
			receive_add_agent(connection, sim); return;
		case message_type::MOVE:
			receive_move(connection, sim); return;
		case message_type::GET_POSITION:
			receive_get_position(connection, sim); return;

		case message_type::ADD_AGENT_RESPONSE:
		case message_type::MOVE_RESPONSE:
		case message_type::GET_POSITION_RESPONSE:
		case message_type::STEP_RESPONSE:
			break;
	}
	fprintf(stderr, "server_process_message WARNING: Received message with unsupported type.\n");
}

bool init_server(
		async_server& new_server, simulator& sim, uint16_t server_port,
		unsigned int connection_queue_capacity, unsigned int worker_count)
{
	std::condition_variable cv; std::mutex lock;
	auto dispatch = [&]() {
		run_server(new_server.server_socket, server_port, connection_queue_capacity,
				worker_count, new_server.state, cv, lock, server_process_message, sim);
	};
	new_server.state = server_state::STARTING;
	new_server.server_thread = std::thread(dispatch);

	std::unique_lock<std::mutex> lck(lock);
	while (new_server.state == server_state::STARTING)
		cv.wait(lck);
	lck.unlock();
	if (new_server.state == server_state::STOPPING) {
		new_server.server_thread.join();
		return false;
	}
	return true;
}

inline bool init_server(simulator& sim, uint16_t server_port,
		unsigned int connection_queue_capacity, unsigned int worker_count)
{
	socket_type server_socket;
	server_state dummy = server_state::STARTING;
	std::condition_variable cv; std::mutex lock;
	return run_server(server_socket, server_port, connection_queue_capacity,
			worker_count, dummy, cv, lock, server_process_message, sim);
}

void stop_server(async_server& server) {
	server.state = server_state::STOPPING;
	write(0, server.server_socket);
	server.server_thread.join();
}


typedef void (*mpi_callback)();
typedef void (*mpi_add_agent_callback)(uint64_t);
typedef void (*mpi_move_callback)(bool);
typedef void (*mpi_get_position_callback)(const position&);

struct client_callbacks {
	mpi_add_agent_callback on_add_agent;
	mpi_move_callback on_move;
	mpi_get_position_callback on_get_position;
	mpi_callback on_step;
	mpi_callback on_lost_connection;
};

struct client {
	socket_type connection;
	std::thread response_listener;
	client_callbacks callbacks;
	bool client_running;
};

inline bool send_message(client& c, const void* data, unsigned int length) {
	return send(c.connection.handle, (const char*) data, length, 0) != 0;
}

bool send_add_agent(client& c) {
	message_type message = message_type::ADD_AGENT;
	return send_message(c, &message, sizeof(message));
}

bool send_move(client& c, uint64_t agent_handle, direction dir, unsigned int num_steps) {
	memory_stream out = memory_stream(sizeof(message_type) + sizeof(agent_handle) + sizeof(dir) + sizeof(num_steps));
	return write(message_type::MOVE, out)
		&& write(agent_handle, out)
		&& write(dir, out)
		&& write(num_steps, out)
		&& send_message(c, out.buffer, out.length);
}

bool send_get_position(client& c, uint64_t agent_handle) {
	memory_stream out = memory_stream(sizeof(message_type) + sizeof(uint64_t));
	return write(message_type::GET_POSITION, out)
		&& write(agent_handle, out)
		&& send_message(c, out.buffer, out.length);
}

inline bool receive_add_agent_response(client& c) {
	uint64_t agent_handle;
	if (!read(agent_handle, c.connection))
		return false;
	c.callbacks.on_add_agent(agent_handle);
	return true;
}

inline bool receive_move_response(client& c) {
	bool move_success;
	if (!read(move_success, c.connection))
		return false;
	c.callbacks.on_move(move_success);
	return true;
}

inline bool receive_get_position_response(client& c) {
	position location;
	if (!read(location, c.connection))
		return false;
	c.callbacks.on_get_position(location);
	return true;
}

inline bool receive_step_response(client& c) {
	c.callbacks.on_step();
	return true;
}

void run_response_listener(client& c) {
	while (c.client_running) {
		message_type type;
		bool success = read(type, c.connection);
		if (!c.client_running) {
			return; /* stop_client was called */
		} else if (!success) {
			c.callbacks.on_lost_connection();
			return;
		}
		switch (type) {
			case message_type::ADD_AGENT_RESPONSE:
				receive_add_agent_response(c); continue;
			case message_type::MOVE_RESPONSE:
				receive_move_response(c); continue;
			case message_type::GET_POSITION_RESPONSE:
				receive_get_position_response(c); continue;
			case message_type::STEP_RESPONSE:
				receive_step_response(c); continue;

			case message_type::ADD_AGENT:
			case message_type::MOVE:
			case message_type::GET_POSITION:
				break;
		}
		fprintf(stderr, "run_response_listener ERROR: Received invalid message type from server.\n");
	}
}

bool init_client(client& new_client, client_callbacks callbacks,
		const char* server_address, const char* server_port)
{
	auto process_connection = [&](socket_type& connection) {
		auto dispatch = [&]() {
			run_response_listener(new_client);
		};
		new_client.connection = connection;
		new_client.response_listener = std::thread(dispatch);
	};

	new_client.client_running = true;
	new_client.callbacks = callbacks;
	return run_client(server_address, server_port, process_connection);
}

void stop_client(client& c) {
	shutdown(c.connection.handle, 2);
	c.client_running = false;
	c.response_listener.join();
}

} /* namespace nel */

#endif /* NEL_MPI_H_ */
