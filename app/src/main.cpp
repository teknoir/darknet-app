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
#include "mqtt/async_client.h"
#include "mqtt/topic.h"
#include "base64.h"
#include "json.hpp"

#include <fstream>
#include "darknet.h"
#include "yolo_v2_class.hpp"
extern "C" {
#include "stb_image.h"
}
#undef GPU // avoid conflict with sl::MEM::GPU

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

void json_result(std::vector<bbox_t> const result_vec, std::vector<std::string> const obj_names, int frame_id = -1) {
    if (frame_id >= 0) std::cout << " Frame: " << frame_id << std::endl;
    for (auto &i : result_vec) {
        if (obj_names.size() > i.obj_id) std::cout << obj_names[i.obj_id] << " - ";
        std::cout << "obj_id = " << i.obj_id << ",  x = " << i.x << ", y = " << i.y
            << ", w = " << i.w << ", h = " << i.h
            << std::setprecision(3) << ", prob = " << i.prob << std::endl;
    }
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

std::string getOrDefault(std::string name, std::string value)
{
    if(const char* env_p = std::getenv(name.c_str()))
        return env_p;
    else
        return value;
}

const int MAX_BUFFERED_MSGS = 1024;	// Amount of off-line buffering
const std::string PERSIST_DIR { "data-persist" };

std::function<void(int)> shutdown_handler;
void signalHandler( int signum ) {
    std::cout << "Interrupt signal (" << signum << ") received.\n" << std::flush;
    shutdown_handler(signum);
}

int main(int argc, char* argv[])
{
	// The broker/server address
	const std::string SERVER_ADDRESS("tcp://"+getOrDefault("HMQ_SERVICE_HOST", "hmq.kube-system")+":"+getOrDefault("HMQ_SERVICE_PORT", "1883"));
	// The topic name for output 0
	const std::string MQTT_OUT_0(getOrDefault("MQTT_OUT_0", "goodbye/world"));
	// The topic name for input 0
	const std::string MQTT_IN_0(getOrDefault("MQTT_IN_0", "welcome/world"));
	// The QoS to use for publishing and subscribing
	const int QOS = 1;
	// Tell the broker we don't want our own messages sent back to us.
	const bool NO_LOCAL = true;

    // Darknet configs
	const std::string  NAMES_FILE(getOrDefault("NAMES_FILE", "/darknet/coco.names"));
    const std::string  CFG_FILE(getOrDefault("CFG_FILE", "/darknet/yolov3.cfg"));
    const std::string  WEIGHTS_FILE(getOrDefault("WEIGHTS_FILE", "/darknet/yolov3.weights"));
    Detector detector(CFG_FILE, WEIGHTS_FILE);
    auto obj_names = objects_names_from_file(NAMES_FILE);

    mqtt::connect_options connOpts;
	connOpts.set_keep_alive_interval(20);
	connOpts.set_clean_start(true);
	connOpts.set_automatic_reconnect(true);
	connOpts.set_mqtt_version(MQTTVERSION_3_1_1);

	mqtt::async_client cli(SERVER_ADDRESS, ""); //, MAX_BUFFERED_MSGS, PERSIST_DIR);

    // register signal SIGINT and signal handler and graceful disconnect
    signal(SIGINT, signalHandler);
    shutdown_handler = [&](int signum) {
        // Disconnect
        try {
            std::cout << "Disconnecting from the MQTT broker..." << std::flush;
            cli.disconnect()->wait();
            std::cout << "OK" << std::endl;
        }
        catch (const mqtt::exception& exc) {
            std::cerr << exc.what() << std::endl;
            return 1;
        }

        exit(signum);
    };

    // Set topics: in and out
	mqtt::topic topic_in { cli, MQTT_IN_0, QOS };
	mqtt::topic topic_out { cli, MQTT_OUT_0, QOS };

    std::function<void(mqtt::const_message_ptr)> message_handler = [&](mqtt::const_message_ptr msg) {
        std::cout << "Image received!\n" << std::flush;
        try {
            //std::cout << "Regexp before: " << msg->get_payload_str() << "\n" << std::flush;
            //std::regex e("^data:image/.+;base64,(.+)");  // All regexp crash due to recursion on (.*) on long lines like this
            //std::string encodedImageData = std::regex_replace(msg->get_payload_str(), e, "$2");
            std::string encodedImageData = msg->get_payload_str();
            std::string delimiter = ";base64,";
            encodedImageData.erase(0, encodedImageData.find(delimiter) + delimiter.length()); // Remove MIME header
            std::vector<BYTE> decodedImageData = base64_decode(encodedImageData);
            image_t img = proc_image(&decodedImageData.front(), decodedImageData.size());
            std::vector<bbox_t> result_vec = detector.detect(img);
            detector.free_image(img);
            show_console_result(result_vec, obj_names);

            //auto j = json::parse(msg->get_payload_str());
            //topic_out->publish(j.dump());
        }
        catch (std::exception &e) {
            std::cerr << "exception: " << e.what() << "\n";
        }
        catch (...) {
            std::cerr << "unknown exception \n";
        }
    };

	// Set the callback for incoming messages
	cli.set_message_callback([topic_in, topic_out, message_handler](mqtt::const_message_ptr msg) mutable {
	    std::cout << "Message received on topic: " << msg->get_topic() << "\n" << std::flush;
	    if (msg->get_topic() == topic_in.get_name()) {
	        message_handler(msg);
	    }
	    //    handleImageMsg(msg, ptrDetector, obj_names, &topic_out);
	});

	// Start the connection.
	try {
		std::cout << "Connecting to the MQTT broker at '" << SERVER_ADDRESS << "'..." << std::flush;
		auto tok = cli.connect(connOpts);
		tok->wait();

		// Subscribe to the topic using "no local" so that
		// we don't get own messages sent back to us

		std::cout << "Ok\nJoining the topics..." << std::flush;
		auto subOpts = mqtt::subscribe_options(NO_LOCAL);
		topic_in.subscribe(subOpts)->wait();
		topic_out.subscribe(subOpts)->wait();
		std::cout << "Ok" << std::endl;
	}
	catch (const mqtt::exception& exc) {
		std::cerr << "\nERROR: Unable to connect. "
			<< exc.what() << std::endl;
		return 1;
	}

	// Just block till user tells us to quit with a SIGINT.
    std::string usrMsg;
	while (std::getline(std::cin, usrMsg)) {
		usrMsg = "{\"message\": \"" + usrMsg + "\"}";
		topic_out.publish(usrMsg);
	}

 	return 0;
}
