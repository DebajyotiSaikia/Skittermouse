#include "test_framework.h"

#include "core/log.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// The file logger: disabled until init() gets a path, then appends timestamped lines.
void run_log_tests() {
    // Disabled: init("") -> not enabled, write() is a no-op.
    sm::log::init("");
    SM_CHECK(!sm::log::enabled());
    sm::log::write("should not appear anywhere");

    // Enabled: writes timestamped lines containing the messages.
    const std::string path = "sm_log_cov_test.txt";
    std::remove(path.c_str());
    sm::log::init(path);
    SM_CHECK(sm::log::enabled());
    sm::log::write("hello-log-42");
    sm::log::write("second-line");

    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    const std::string content = ss.str();
    SM_CHECK(content.find("hello-log-42") != std::string::npos);
    SM_CHECK(content.find("second-line") != std::string::npos);
    SM_CHECK(std::count(content.begin(), content.end(), '\n') >= 2); // two lines appended

    sm::log::init(""); // reset global state so other tests aren't affected
    SM_CHECK(!sm::log::enabled());
    std::remove(path.c_str());
}
