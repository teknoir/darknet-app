#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>
#include <csignal>
#include <functional>
#include <regex>
#include <fstream>
#include <chrono>
#include <unistd.h>
#include "mqtt/async_client.h"
#include "mqtt/topic.h"
#include "base64.h"
#include "json.hpp"
using json = nlohmann::json;

#include "darknet.h"
#include "yolo_v2_class.hpp"
extern "C" {
#include "stb_image.h"
}
#undef GPU // avoid conflict with sl::MEM::GPU

std::string getOrDefault(std::string name, std::string value)
{
    if(const char* env_p = std::getenv(name.c_str()))
        return env_p;
    else
        return value;
}
// The client name on the broker
const std::string CLIENT_ID("darknet");
// The broker/server address
const std::string SERVER_ADDRESS("tcp://"+getOrDefault("MQTT_SERVICE_HOST", "mqtt.kube-system")+":"+getOrDefault("MQTT_SERVICE_PORT", "1883"));
// The topic name for output 0
const std::string MQTT_OUT_0(getOrDefault("MQTT_OUT_0", "darknet/out"));
// The topic name for input 0
const std::string MQTT_IN_0(getOrDefault("MQTT_IN_0", "darknet/in"));
// The QoS to use for publishing and subscribing
const int QOS = 1;
// Timeout
const auto TIMEOUT = std::chrono::seconds(10);

// Darknet configs
const std::string  NAMES_FILE(getOrDefault("NAMES_FILE", "/darknet/coco.names"));
const std::string  CFG_FILE(getOrDefault("CFG_FILE", "/darknet/yolov3.cfg"));
const std::string  WEIGHTS_FILE(getOrDefault("WEIGHTS_FILE", "/darknet/yolov3.weights"));

static image proc_image_stb(unsigned char* buffer, int len, int channels)
{
    int w, h, c;
    //unsigned char *data = stbi_load(filename, &w, &h, &c, channels);
    unsigned char *data = stbi_load_from_memory(buffer, len, &w, &h, &c, channels);
    if (!data)
        throw std::runtime_error("file not found");
    if (channels) c = channels;
    int i, j, k;
    image im = make_image(w, h, c);
    for (k = 0; k < c; ++k) {
        for (j = 0; j < h; ++j) {
            for (i = 0; i < w; ++i) {
                int dst_index = i + w*j + w*h*k;
                int src_index = k + c*i + c*w*j;
                im.data[dst_index] = (float)data[src_index] / 255.;
            }
        }
    }
    free(data);
    return im;
}

image_t proc_image(unsigned char* buffer, int size)
{
    image im = proc_image_stb(buffer, size, 3);

    image_t img;
    img.c = im.c;
    img.data = im.data;
    img.h = im.h;
    img.w = im.w;

    return img;
}

void show_console_result(std::vector<bbox_t> const result_vec, std::vector<std::string> const obj_names, int frame_id = -1) {
    if (frame_id >= 0) std::cout << " Frame: " << frame_id << std::endl << std::flush;
    for (auto &i : result_vec) {
        if (obj_names.size() > i.obj_id) std::cout << obj_names[i.obj_id] << " - ";
        std::cout << "obj_id = " << i.obj_id << ",  x = " << i.x << ", y = " << i.y
            << ", w = " << i.w << ", h = " << i.h
            << std::setprecision(3) << ", prob = " << i.prob << std::endl << std::flush;
    }
}

json json_result(std::vector<bbox_t> const result_vec, std::vector<std::string> const obj_names, image_t img, int frame_id = -1) {
    json j;
    if (frame_id >= 0) j["frameId"] = frame_id;
    j["objects"] = {};
    int c = 0;
    for (auto &i : result_vec) {
        if (obj_names.size() > i.obj_id) j["objects"][c]["className"] = obj_names[i.obj_id];
        j["objects"][c]["objId"] = i.obj_id;
        j["objects"][c]["score"] = i.prob;
        j["objects"][c]["bbox"] = { (float)i.x/(float)img.w, (float)i.y/(float)img.h, (float)i.w/(float)img.w, (float)i.h/(float)img.h };
        c++;
    }
    return j;
}

std::vector<std::string> objects_names_from_file(std::string const filename) {
    std::ifstream file(filename);
    std::vector<std::string> file_lines;
    if (!file.is_open()) return file_lines;
    for(std::string line; getline(file, line);) file_lines.push_back(line);
    std::cout << "object names loaded \n";
    return file_lines;
}

/////////////////////////////////////////////////////////////////////////////
// for convenience
using json = nlohmann::json;

const int MAX_BUFFERED_MSGS = 1024;	// Amount of off-line buffering
const std::string PERSIST_DIR { "data-persist" };

std::function<void(int)> shutdown_handler;
void signalHandler( int signum ) {
    std::cout << "Interrupt signal (" << signum << ") received.\n" << std::flush;
    shutdown_handler(signum);
}

class action_listener : public virtual mqtt::iaction_listener
{
	std::string name_;

	void on_failure(const mqtt::token& tok) override {
		std::cout << name_ << " failure";
		if (tok.get_message_id() != 0)
			std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
		std::cout << std::endl;
	}

	void on_success(const mqtt::token& tok) override {
		std::cout << name_ << " success";
		if (tok.get_message_id() != 0)
			std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
		auto top = tok.get_topics();
		if (top && !top->empty())
			std::cout << "\ttoken topic: '" << (*top)[0] << "', ..." << std::endl;
		std::cout << std::endl;
	}

public:
	action_listener(const std::string& name) : name_(name) {}
};

class callback : public virtual mqtt::callback,
					public virtual mqtt::iaction_listener

{
	// Counter for the number of connection retries
	int nretry_;
	// The MQTT client
	mqtt::async_client& cli_;
	// Options to use if we need to reconnect
	mqtt::connect_options& connOpts_;
	// An action listener to display the result of actions.
	action_listener subListener_;

    Detector* detector_;
    std::vector<std::string>& objNames_;

	// This deomonstrates manually reconnecting to the broker by calling
	// connect() again. This is a possibility for an application that keeps
	// a copy of it's original connect_options, or if the app wants to
	// reconnect with different options.
	// Another way this can be done manually, if using the same options, is
	// to just call the async_client::reconnect() method.
	void reconnect() {
		std::this_thread::sleep_for(std::chrono::milliseconds(2500));
		try {
			cli_.connect(connOpts_, nullptr, *this);
		}
		catch (const mqtt::exception& exc) {
			std::cerr << "Error: " << exc.what() << std::endl;
			exit(1);
		}
	}

	// Re-connection failure
	void on_failure(const mqtt::token& tok) override {
		std::cout << "Connection attempt failed" << std::endl;
		if (++nretry_ > 10)
			exit(1);
		reconnect();
	}

	// (Re)connection success
	// Either this or connected() can be used for callbacks.
	void on_success(const mqtt::token& tok) override {}

	// (Re)connection success
	void connected(const std::string& cause) override {
		std::cout << "\nConnection success" << std::endl;
		std::cout << "\nSubscribing to topic '" << MQTT_IN_0 << "'\n"
			<< "\tfor client " << CLIENT_ID
			<< " using QoS" << QOS << "\n" << std::endl;

		cli_.subscribe(MQTT_IN_0, QOS, nullptr, subListener_);
	}

	// Callback for when the connection is lost.
	// This will initiate the attempt to manually reconnect.
	void connection_lost(const std::string& cause) override {
		std::cout << "\nConnection lost" << std::endl;
		if (!cause.empty())
			std::cout << "\tcause: " << cause << std::endl;

		std::cout << "Reconnecting..." << std::endl;
		nretry_ = 0;
		reconnect();
	}

	// Callback for when a message arrives.
	void message_arrived(mqtt::const_message_ptr msg) override {
		std::cout << "Message arrived" << std::endl;
		std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
		//std::cout << "\tpayload: '" << msg->to_string() << "'\n" << std::endl;
		try {
            //std::cout << "Regexp before: " << msg->get_payload_str() << "\n" << std::flush;
            //std::regex e("^data:image/.+;base64,(.+)");  // All regexp crash due to recursion on (.*) on long lines like this
            //std::string encodedImageData = std::regex_replace(msg->get_payload_str(), e, "$2");
            std::string encodedImageData = msg->to_string();
            std::string delimiter = ";base64,";
            encodedImageData.erase(0, encodedImageData.find(delimiter) + delimiter.length()); // Remove MIME header
            std::vector<BYTE> decodedImageData = base64_decode(encodedImageData);
            std::cout << "Image decoded..." << std::endl;
            image_t img = proc_image(&decodedImageData.front(), decodedImageData.size());
            std::cout << "Image processed..." << std::endl;
            auto start = std::chrono::high_resolution_clock::now();
            std::vector<bbox_t> result_vec = detector_->detect(img);
            std::cout << "Image detected..." << std::endl;
            auto stop = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
            detector_->free_image(img);

            show_console_result(result_vec, objNames_);
            auto j = json_result(result_vec, objNames_, img);
            j["image"] = msg->get_payload_str();
            j["inference_time"] = duration.count()/1000.0;

            //topic_out.publish(j.dump());

            mqtt::message_ptr pubmsg = mqtt::make_message(MQTT_OUT_0, j.dump());
            pubmsg->set_qos(QOS);
            cli_.publish(pubmsg)->wait_for(TIMEOUT);
        }
        catch (std::exception &e) {
            std::cerr << "exception: " << e.what() << "\n";
        }
        catch (...) {
            std::cerr << "unknown exception \n";
        }
	}

	void delivery_complete(mqtt::delivery_token_ptr token) override {}

public:
	callback(mqtt::async_client& cli, mqtt::connect_options& connOpts, Detector* detector, std::vector<std::string>& objNames)
				: nretry_(0), cli_(cli), connOpts_(connOpts), subListener_("Subscription"), detector_(detector), objNames_(objNames) {}
};

int main(int argc, char* argv[])
{
    Detector detector(CFG_FILE, WEIGHTS_FILE);
    auto objNames = objects_names_from_file(NAMES_FILE);

    mqtt::connect_options connOpts;
	connOpts.set_keep_alive_interval(20);
	connOpts.set_clean_start(true);
	//connOpts.set_automatic_reconnect(true);
	connOpts.set_mqtt_version(MQTTVERSION_3_1_1);

	mqtt::async_client cli(SERVER_ADDRESS, CLIENT_ID); //, MAX_BUFFERED_MSGS, PERSIST_DIR);

    // Set topics: in and out
	//mqtt::topic topic_in { cli, MQTT_IN_0, QOS };
	//mqtt::topic topic_out { cli, MQTT_OUT_0, QOS };

    // register signal SIGINT and signal handler and graceful disconnect
    signal(SIGINT, signalHandler);
    shutdown_handler = [&](int signum) {
        // Disconnect
        try {
            std::cout << "Disconnecting from the MQTT broker..." << std::flush;
            cli.unsubscribe(MQTT_IN_0)->wait();
            //cli.unsubscribe(MQTT_OUT_0)->wait();
            cli.stop_consuming();
            cli.disconnect()->wait();
            std::cout << "OK" << std::endl;
        }
        catch (const mqtt::exception& exc) {
            std::cerr << exc.what() << std::endl;
            return 1;
        }

        exit(signum);
    };

    callback cb(cli, connOpts, &detector, objNames);
    cli.set_callback(cb);

	// Start the connection.
	try {
		std::cout << "Connecting to the MQTT broker at '" << SERVER_ADDRESS << "'..." << std::flush;
		//cli.connect(connOpts)->wait();
		cli.connect(connOpts, nullptr, cb);
	}
	catch (const mqtt::exception& exc) {
		std::cerr << "\nERROR: Unable to connect. "
			<< exc.what() << std::endl;
		return 1;
	}

	// Just block till user tells us to quit with a SIGINT.
    while(true){ usleep(250000); }

 	return 0;
}
