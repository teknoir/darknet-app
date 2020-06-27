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
	const std::string SERVER_ADDRESS("tcp://"+getOrDefault("MQTT_SERVICE_HOST", "mqtt.kube-system")+":"+getOrDefault("MQTT_SERVICE_PORT", "1883"));
	// The topic name for output 0
	const std::string MQTT_OUT_0(getOrDefault("MQTT_OUT_0", "darknet/out"));
	// The topic name for input 0
	const std::string MQTT_IN_0(getOrDefault("MQTT_IN_0", "darknet/in"));
	// The QoS to use for publishing and subscribing
	const int QOS = 1;
	// Tell the broker we don't want our own messages sent back to us.
	//const bool NO_LOCAL = true;

	const auto TIMEOUT = std::chrono::seconds(10);

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

	mqtt::async_client cli(SERVER_ADDRESS, "darknet"); //, MAX_BUFFERED_MSGS, PERSIST_DIR);

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

	// Start the connection.
	try {
		std::cout << "Connecting to the MQTT broker at '" << SERVER_ADDRESS << "'..." << std::flush;
		cli.connect(connOpts)->wait();

		// Subscribe to the topic using "no local" so that
		// we don't get own messages sent back to us

		std::cout << "Ok\nJoining the topics..." << std::flush;
		//auto subOpts = mqtt::subscribe_options(NO_LOCAL);
		//topic_in.subscribe(subOpts)->wait();
		//topic_out.subscribe(subOpts)->wait();
		cli.start_consuming();
        cli.subscribe(MQTT_IN_0, QOS)->wait();
		std::cout << "Ok" << std::endl;

        // Consume messages

		while (true) {
			auto msg = cli.consume_message();
			if (!msg) break;
			if (MQTT_IN_0 == topic_in.get_name()) {
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

                    auto start = std::chrono::high_resolution_clock::now();
                    std::vector<bbox_t> result_vec = detector.detect(img);
                    auto stop = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
                    detector.free_image(img);

                    show_console_result(result_vec, obj_names);
                    auto j = json_result(result_vec, obj_names, img);
                    j["image"] = msg->get_payload_str();
                    j["inference_time"] = duration.count()/1000.0;

                    //topic_out.publish(j.dump());

                    mqtt::message_ptr pubmsg = mqtt::make_message(MQTT_OUT_0, j.dump());
                    pubmsg->set_qos(QOS);
                    cli.publish(pubmsg)->wait_for(TIMEOUT);
                }
                catch (std::exception &e) {
                    std::cerr << "exception: " << e.what() << "\n";
                }
                catch (...) {
                    std::cerr << "unknown exception \n";
                }
            }
		}

		// Disconnect
		shutdown_handler(0);
	}
	catch (const mqtt::exception& exc) {
		std::cerr << "\nERROR: Unable to connect. "
			<< exc.what() << std::endl;
		return 1;
	}

 	return 0;
}
