#include "http_server.hpp"
#include "hpack.hpp"
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <deque>
#include <map>

struct endpoint_settings
{
	uint32_t header_table_size = 4096;
	bool enable_push = true;
	uint32_t max_concurrent_streams = (uint32_t)-1;
	int32_t initial_window_size = 65535;
	uint32_t max_frame_size = 16384;
	uint32_t max_header_list_size = (uint32_t)-1;
};

enum class frame_type : char
{
	data = 0,
	headers = 1,
	priority = 2,
	rst_stream = 3,
	settings = 4,
	push_promise = 5,
	ping = 6,
	goaway = 7,
	window_update = 8,
	continuation = 9,
};

enum frame_flags : char
{
	ack = 0x01,
	end_stream = 0x01,
	end_headers = 0x04,
	padded = 0x08,
	priority = 0x20,
};

enum settings_ids : char
{
	header_table_size = 1,
	enable_push = 2,
	max_concurrent_streams = 3,
	initial_window_size = 4,
	max_frame_size = 5,
	max_header_list_size = 6,
};

enum class error_code : uint32_t
{
	no_error = 0,
	protocol_error = 1,
	internal_error = 2,
	flow_control_error = 3,
	settings_timeout = 4,
	stream_closed = 5,
	frame_size_error = 6,
	refused_stream = 7,
	cancel = 8,
	compression_error = 9,
	connect_error = 10,
	enhance_your_calm = 11,
	inadequate_security = 12,
	http_1_1_required = 13,
};

template <typename T>
void store_be(char * buf, T value, size_t len = sizeof(T))
{
	while (len)
	{
		buf[--len] = (char)value;
		value >>= 8;
	}
}

template <typename T>
T load_be(char const * buf, size_t len = sizeof(T))
{
	T r = 0;
	while (len)
	{
		r = (r << 8) | uint8_t(*buf++);
		--len;
	}
	return r;
}

template <typename T>
void load_be(T & v, char const * buf, size_t len = sizeof(T))
{
	v = load_be<T>(buf, len);
}

template <typename F>
struct on_exit_t
{
	explicit on_exit_t(F && f)
		: f_(std::forward<F>(f)), armed_(true)
	{
	}

	on_exit_t(on_exit_t && o)
		: f_(std::move(o.f_)), armed_(o.armed_)
	{
		o.armed_ = false;
	}

	~on_exit_t()
	{
		if (armed_)
			f_();
	}

	F f_;
	bool armed_;
};

template <typename F>
auto on_exit(F && f)
{
	return on_exit_t<std::remove_const_t<std::remove_reference_t<F>>>(std::forward<F>(f));
}

struct http2_stream
{
	std::string header_block;
	bool open_from_client = true;

	void process_headers()
	{
	}
};

struct http2_frame
{
	uint32_t payload_size;
	frame_type type;
	frame_flags flags;
	uint32_t stream_id;
};

void http2_server(istream & in, ostream & out, std::function<response(request &&)> const & fn)
{
	static char const client_preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

	std::map<uint32_t, http2_stream> streams;
	uint32_t next_client_stream = 1;

	hpack_decoder header_dec(4096);

	endpoint_settings next_server_settings;
	endpoint_settings client_settings;
	std::atomic<int> client_settings_in_flight = 0;

	auto send_frame = [&](frame_type type, char flags, uint32_t stream_id, std::string_view payload) {
		char header[9];
		store_be(header, payload.size(), 3);
		header[3] = static_cast<char>(type);
		header[4] = flags;
		store_be(&header[5], stream_id);

		out.write_all(header, sizeof header);
		out.write_all(payload.data(), payload.size());
	};

	std::exception_ptr stream0_writer_error;

	std::condition_variable send_ready;
	std::mutex send_mutex;
	bool send_done = false;
	size_t setting_acks = 0;
	std::deque<std::string> pings;

	auto stream0_write = [&]() {
		try
		{
			std::unique_lock<std::mutex> l(send_mutex);

			++client_settings_in_flight;
			send_frame(frame_type::settings, 0, 0, "");

			endpoint_settings server_settings;
			while (!send_done)
			{
				if (!pings.empty())
				{
					auto const & ping = pings.front();
					send_frame(frame_type::ping, frame_flags::ack, 0, ping);
					pings.pop_front();
					continue;
				}

				if (setting_acks)
				{
					server_settings = next_server_settings;
					--setting_acks;
					send_frame(frame_type::settings, frame_flags::ack, 0, "");
					continue;
				}

				send_ready.wait(l);
			}
		}
		catch (...)
		{
			stream0_writer_error = std::current_exception();
		}
	};

	std::thread stream0_writer(stream0_write);
	auto stream0_writer_canceller = on_exit([&] {
		{
			std::lock_guard<std::mutex> l(send_mutex);
			send_done = true;
			send_ready.notify_one();
		}

		stream0_writer.join();
	});

	char buf[16 * 1024];
	in.read_all(buf, sizeof client_preface - 1);

	if (memcmp(buf, client_preface, sizeof client_preface - 1) != 0)
		throw std::runtime_error("invalid client preface");

	auto connection_error = [&](error_code ec) {
		throw std::runtime_error("connection error");
	};

	for (;;)
	{
		if (stream0_writer_error)
			std::rethrow_exception(stream0_writer_error);

		char frame_header[9];
		char payload[65536];

		auto read_frame = [&](char * buf, size_t len) {
			in.read_all(frame_header, sizeof frame_header);
			http2_frame frame;
			frame.payload_size = load_be<uint32_t>(frame_header, 3);
			frame.type = static_cast<frame_type>(frame_header[3]);
			frame.flags = static_cast<frame_flags>(frame_header[4]);
			frame.stream_id = load_be<uint32_t>(&frame_header[5]);
			frame.stream_id &= 0x7fffffff;

			if (frame.payload_size > len)
				connection_error(error_code::frame_size_error);

			in.read_all(payload, frame.payload_size);
			return frame;
		};

		http2_frame frame = read_frame(payload, client_settings.max_frame_size);

		switch (frame.type)
		{
		case frame_type::headers:
			if (frame.stream_id == 0)
				connection_error(error_code::protocol_error);

			if ((frame.stream_id & 1) == 0)
				connection_error(error_code::protocol_error);

			if (frame.stream_id < next_client_stream)
				connection_error(error_code::protocol_error);

			{
				auto & stream = streams[frame.stream_id];
				next_client_stream = frame.stream_id + 2;

				char * pl = payload;
				if (frame.flags & frame_flags::padded)
				{
					if (frame.payload_size < 1)
						connection_error(error_code::protocol_error);

					uint8_t pad_length = (uint8_t)*pl++;
					--frame.payload_size;

					if (frame.payload_size < pad_length)
						connection_error(error_code::protocol_error);

					frame.payload_size -= pad_length;
				}

				if (frame.flags & frame_flags::priority)
				{
					if (frame.payload_size < 6)
						connection_error(error_code::protocol_error);

					pl += 6;
					frame.payload_size -= 6;
				}

				if (frame.flags & frame_flags::end_stream)
					stream.open_from_client = false;

				char * payload_end = pl + frame.payload_size;
				uint32_t stream_id = frame.stream_id;
				while ((frame.flags & frame_flags::end_headers) == 0)
				{
					size_t next_chunk = std::end(payload) - payload_end;
					if (next_chunk > client_settings.max_frame_size)
						next_chunk = client_settings.max_frame_size;
					frame = read_frame(payload_end, next_chunk);
					if (frame.type != frame_type::continuation || frame.stream_id != stream_id)
						connection_error(error_code::protocol_error);
					payload_end += frame.payload_size;
				}

				std::vector<header> headers;
				header_dec.decode(headers, { pl, payload_end });
			}
			break;
		case frame_type::continuation:
			{
				auto it = streams.find(frame.stream_id);
				if (it == streams.end())
					connection_error(error_code::protocol_error);
				auto && stream = it->second;
				stream.header_block.append(payload, frame.payload_size);

				if (frame.flags & frame_flags::end_headers)
					stream.process_headers();
			}
			break;
		case frame_type::ping:
			if (frame.stream_id != 0)
				connection_error(error_code::protocol_error);
			if (frame.payload_size != 8)
				connection_error(error_code::frame_size_error);

			if (frame.flags & frame_flags::ack)
			{
			}
			else
			{
				std::lock_guard<std::mutex> l(send_mutex);
				pings.push_back({ payload, frame.payload_size });
				send_ready.notify_one();
			}
			break;
		case frame_type::settings:
			if (frame.stream_id != 0)
				connection_error(error_code::protocol_error);

			if (frame.flags & frame_flags::ack)
			{
				if (frame.payload_size != 0)
					connection_error(error_code::frame_size_error);
				if (--client_settings_in_flight < 0)
					connection_error(error_code::protocol_error);
			}
			else
			{
				endpoint_settings new_server_settings = next_server_settings;

				size_t idx = 0;
				for (; idx < frame.payload_size; idx += 6)
				{
					auto id = static_cast<settings_ids>(load_be<uint16_t>(payload + idx));
					uint32_t value = load_be<uint32_t>(payload + idx + 2);

					switch (id)
					{
					case settings_ids::header_table_size:
						new_server_settings.header_table_size = value;
						break;
					case settings_ids::enable_push:
						if (value != 0 && value != 1)
							connection_error(error_code::protocol_error);
						new_server_settings.enable_push = value != 0;
						break;
					case settings_ids::max_concurrent_streams:
						new_server_settings.max_concurrent_streams = value;
						break;
					case settings_ids::initial_window_size:
						if (value > 0x7fffffff)
							connection_error(error_code::flow_control_error);
						new_server_settings.initial_window_size = value;
						break;
					case settings_ids::max_frame_size:
						if (value < 16384 || value >= (1 << 24))
							connection_error(error_code::protocol_error);
						new_server_settings.max_frame_size = value;
						break;
					case settings_ids::max_header_list_size:
						new_server_settings.max_header_list_size = value;
						break;
					}
				}

				if (idx != frame.payload_size)
					connection_error(error_code::frame_size_error);

				std::lock_guard<std::mutex> l(send_mutex);
				next_server_settings = new_server_settings;
				++setting_acks;
				send_ready.notify_one();
			}
			break;
		}
	}
}
