#pragma once

#include <functional>
#include <string>

namespace openhi2txt {

struct TraceSink {
    std::function<void(const std::string&)> write;

    void line(const std::string& text) const {
        if (write) write(text);
    }
};

} // namespace openhi2txt
